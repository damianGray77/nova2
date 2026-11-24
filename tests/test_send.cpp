// SEND, RECV, PING, arrivals and handlers (design.md 10.4, 11.2, 11.5, A.3,
// A.4; build-plan-gate3.md step 4), in small test worlds with hand-placed
// programs: a replicator SENDs itself to a neighbour's port 0 and dies, and
// the copy runs there as a prompt run, then makes programs of the sender's
// lineage; a RECV gets a message's bytes and its sender, the one waiting
// longest first; a SEND's caller waits the device's latency plus the size
// over the first link's bandwidth; a PING's time is its route's there and
// back; an arrival never runs at a place whose cores are all busy with
// threads; a handler fires once for a burst of messages; and a busy world
// on a map comes out the same on 1 and 4 host threads. And step 7's
// migrants (traffic_world.h) do what they're made to, on small grids; an
// arrival that does neither is counted by the first of its calls that
// failed, and by how it ended. And step 8's two rules: an idle run's SEND
// fails unless os.idle_send is on, and arrivals hold at most their share of
// a place's cores (region.arrival_share), so static keeps the rest.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/array.h"
#include "nova2/cpu.h"
#include "nova2/net.h"
#include "nova2/observe.h"
#include "nova2/os.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

// A program's bytes, an instruction at a time.
struct Code {
    u8 b[256] = {};
    u32 n = 0;
    Code& op(u8 o, u8 od) {
        b[n++] = encode(o, od);
        return *this;
    }
    Code& imm(u8 o, i32 v) {
        b[n++] = encode(o, kImm);
        b[n++] = static_cast<u8>(v);
        return *this;
    }
    Code& sys(u8 call) { return imm(kSys, call); }
    // Register r (not A) = v, through A.
    Code& set(u8 r, i32 v) { return imm(kMov, v).op(kSto, r); }
    // Register r = the address `at` bytes into the program's allocation,
    // from the instruction's own address.
    Code& addr(u8 r, u32 at) {
        const i32 off = static_cast<i32>(at) - static_cast<i32>(n);
        CHECK(off >= -128 && off <= 127);
        return op(kMov, kIp).imm(kAdd, off).op(kSto, r);
    }
    // C = A, then a wait for an interrupt that never comes: the test reads
    // the registers it saved.
    Code& done() { return op(kSto, kC).imm(kMov, 0).sys(kSleep); }
};

// The idle runs' replicator (test_net.cpp's): ALLOC 14, copy itself there,
// SPAWN it, again.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

const char* const kRing = "world.places = 6\nworld.map = ring\n";
// Links 16 bytes a tick; small memories; two cores a place; no noise and no
// corruption. A halted core starts again only for a change to its region or
// an arrival, and each start moves its spot 65 bytes on: over a test's few
// dozen starts, an arrival of 30 bytes is never cut short at the region's
// end, and the next starts don't land in its bytes.
const char* const kBase = "map.bandwidth.min = 16\nmap.bandwidth.max = 16\nmemory.size = 32768\nos.strict_share = 0\n"
                          "noise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\nnet.corruption = 0\n"
                          "hw.cores.min = 2\nhw.cores.max = 2\nregion.step = 65\nregion.retry = 1000000\n";

// A test world: its map, `base` (kBase unless given) and `more`; every reset region filled with
// PUSH A (an idle run has no stack, so it faults at once); watched by `obs`
// if given; its routes worked out by a first tick.
void make(World& w, const char* map, const char* more, Observer* obs = nullptr, u32 threads = 2,
          const char* base = kBase) {
    char text[2048];
    snprintf(text, sizeof text, "%s%s%s", map, base, more);
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(text, &pe));
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  send: world: %s\n", why.c_str());
        CHECK(false);
        return;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size());
    }
    if (obs)
        w.watch(obs);
    w.start_threads(threads);
    w.tick();
}

// A program placed by hand: its code at the start of a fresh, zeroed
// 256-byte allocation, its thread starting `entry` bytes in. The thread's
// record index; the allocation in *base.
u32 put(Place& p, const Code& c, u32 entry = 0, u32* base = nullptr) {
    const u32 at = p.mem.alloc(1, 256, 2, 0);
    CHECK(at != 0);
    memset(p.mem.bytes() + at, 0, 256);
    memcpy(p.mem.bytes() + at, c.b, c.n);
    const u32 id = p.os.place_by_hand(1, at, at + entry, 0);
    CHECK(id != 0);
    if (base)
        *base = at;
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    CHECK(false);
    return 0;
}

u64 uid_of(Place& p, u32 t) { return p.os.program(p.os.thread(t).program).uid; }
bool finished(Place& p, u32 t) { return p.os.thread(t).state == ThreadState::waiting_interrupt; }
Regs regs(Place& p, u32 t) {
    ExecContext x;
    p.os.load(t, x);
    return x.r;
}

// The waits a thread starts, seen after each tick: each time its wake tick
// changes while it sleeps, the tick it went to sleep at and for how long.
struct Sleeps {
    u64 last = ~0ull;
    u32 n = 0;
    u64 at[8] = {}, len[8] = {};
    void see(World& w, Place& p, u32 t) {
        const Thread& th = p.os.thread(t);
        if (th.state != ThreadState::sleeping || th.wake_tick == last)
            return;
        last = th.wake_tick;
        if (n < 8) {
            at[n] = w.tick_count() - 1;
            len[n] = th.wake_tick - at[n];
            ++n;
        }
    }
};

// The barriers an unhindered message of `size` bytes takes from s to d
// along the routes: each hop's size over its bandwidth, rounded up, plus its
// delay.
u64 route_time(World& w, u32 s, u32 d, u32 size) {
    const Map& m = w.map();
    u64 t = 0;
    for (u32 at = s; at != d;) {
        const u32 j = w.net().routes().next(at, d);
        if (j == ~0u)
            return ~0ull;
        const u32 k = m.link_at(at, j);
        t += (size + m.link(k).bandwidth - 1) / m.link(k).bandwidth + m.link(k).delay;
        at = m.other(k, at);
    }
    return t;
}

// A replicator at place 0 SENDs itself (kLax, then the sending code) to
// place 1's port 0, and EXITs. Its bytes run there as a prompt run, whose
// SPAWN is a colonist: its parent the sender, though the sender has died
// (the observer keeps a dead sender's record while what it sent may still
// arrive), of the sender's lineage. The colonist makes programs there.
void test_colonist() {
    World w;
    Observer obs;
    obs.init(ObserverSettings{});
    make(w, kRing, "", &obs);
    Place& p0 = w.place(0);
    Place& p1 = w.place(1);
    Code c;
    memcpy(c.b, kLax, 14);
    c.n = 14;
    const u32 start = c.n;
    c.addr(kC, 0).set(kD, 0).set(kB, 0).imm(kMov, 1).sys(kSend).sys(kExit);
    c.b[start + 5] = static_cast<u8>(c.n); // D = the whole program's length
    const u32 ts = put(p0, c, start);
    const u64 sender = uid_of(p0, ts);
    u64 colonist = 0, parent = 0, born = 0;
    bool sender_dead = false;
    u32 kept = 0;
    usize seen = 0;
    for (u32 t = 0; t < 60 && !colonist; ++t) {
        w.tick();
        kept = obs.dead_senders() > kept ? obs.dead_senders() : kept;
        for (; seen < obs.births.size(); ++seen) {
            const BirthFact& f = obs.births[seen];
            if (f.cause == BirthCause::arrival && f.place == 1 && !colonist) {
                colonist = f.uid;
                parent = f.parent_uid;
                born = f.tick;
                sender_dead = obs.program(sender) == nullptr;
            }
        }
    }
    CHECK(colonist != 0 && parent == sender && sender_dead && kept == 1);
    const ProgramRec* col = obs.program(colonist);
    CHECK(col && col->parent == sender && col->origin == sender && col->arrived && col->place == 1);
    CHECK(p1.region.prompt_runs == 1 && p1.region.arrival_runs == 1);
    CHECK(w.net().counts().sent == 1 && w.net().counts().delivered == 1 &&
          w.net().counts().delivered_bytes == c.n);
    // Then it makes programs there, of the sender's lineage (the colonist's
    // descendants, by the births' parents); the prompt run that made it
    // ended once its children took the cores. The dead sender's record is
    // kept while the bytes it landed are listed (a static run in them is of
    // its lineage, 13.2), and goes at the next sample after nothing of it is
    // left.
    for (u32 t = 0; t < 200; ++t)
        w.tick();
    Array<u64> family;
    family.push(colonist);
    for (usize i = 0; i < obs.births.size(); ++i) {
        const BirthFact& f = obs.births[i];
        if (f.place != 1 || f.uid == colonist)
            continue;
        for (usize k = 0; k < family.size(); ++k)
            if (family[k] == f.parent_uid) {
                family.push(f.uid);
                break;
            }
    }
    const u64 made = family.size() - 1;
    const LineageRec* L = obs.lineage(sender);
    CHECK(made > 0 && L && L->births >= made + 2 && obs.colony(sender, 1) != nullptr);
    CHECK(p1.region.arrival_made == 1 && p1.region.travellers == 0 && p1.region.arrival_neither == 0);
    bool listed = false;
    for (u32 i = 0; i < p1.region.landing_count(); ++i)
        listed = listed || p1.region.landing(i).sender == sender;
    CHECK(obs.dead_senders() == (listed ? 1u : 0u));
    printf("  send: a replicator SENT itself to place 1's port 0 and died; at tick %llu its copy, run there as a "
           "prompt run, made a colonist of its lineage, which made %llu programs more\n",
           static_cast<unsigned long long>(born), static_cast<unsigned long long>(made));
}

// RECV gets a message's bytes and its sender; the thread that has waited
// longest on the port gets the first message; a SEND's caller waits the
// device's latency plus the size over the first link's bandwidth, or the
// latency alone to its own place, where the message arrives after the
// latency.
void test_recv() {
    World w;
    make(w, kRing, "");
    Place& p0 = w.place(0);
    Place& p1 = w.place(1);
    u8 data[20], loop[6];
    for (u32 i = 0; i < 20; ++i)
        data[i] = static_cast<u8>(i * 7 + 1);
    for (u32 i = 0; i < 6; ++i)
        loop[i] = static_cast<u8>(200 + i);
    // Place 0's sender: 20 bytes to place 1's port 3, then the first 10.
    Code s;
    s.imm(kMov, 5).sys(kSleep).addr(kC, 100).set(kD, 20).set(kB, 3).imm(kMov, 1).sys(kSend);
    s.set(kD, 10).imm(kMov, 1).sys(kSend).done();
    u32 sbase = 0;
    const u32 ts = put(p0, s, 0, &sbase);
    memcpy(p0.mem.bytes() + sbase + 100, data, 20);
    // Place 1's receivers: X waits at once, Y two ticks later.
    Code x;
    x.addr(kB, 100).set(kC, 40).set(kD, -1).imm(kMov, 3).sys(kRecv).done();
    Code y;
    y.imm(kMov, 2).sys(kSleep).addr(kB, 100).set(kC, 40).set(kD, -1).imm(kMov, 3).sys(kRecv).done();
    u32 xbase = 0, ybase = 0;
    const u32 tx = put(p1, x, 0, &xbase);
    const u32 ty = put(p1, y, 0, &ybase);
    // Place 0 to itself: Z waits on port 4; S2 sends it 6 bytes.
    Code z;
    z.addr(kB, 100).set(kC, 40).set(kD, -1).imm(kMov, 4).sys(kRecv).done();
    Code s2;
    s2.imm(kMov, 3).sys(kSleep).addr(kC, 100).set(kD, 6).set(kB, 4).imm(kMov, 0).sys(kSend).done();
    u32 zbase = 0, s2base = 0;
    const u32 tz = put(p0, z, 0, &zbase);
    const u32 ts2 = put(p0, s2, 0, &s2base);
    memcpy(p0.mem.bytes() + s2base + 100, loop, 6);
    Sleeps ss, ss2;
    u64 z_woke = ~0ull;
    bool z_waited = false;
    for (u32 t = 0; t < 40; ++t) {
        w.tick();
        ss.see(w, p0, ts);
        ss2.see(w, p0, ts2);
        if (p0.os.thread(tz).state == ThreadState::waiting_recv)
            z_waited = true;
        else if (z_waited && z_woke == ~0ull)
            z_woke = w.tick_count() - 1;
    }
    CHECK(finished(p0, ts) && finished(p1, tx) && finished(p1, ty) && finished(p0, tz) && finished(p0, ts2));
    // The sleeps: 5; then 1 + 20/16 rounded up; then 1 + 10/16.
    CHECK(ss.n == 3 && ss.len[0] == 5 && ss.len[1] == 3 && ss.len[2] == 2);
    CHECK(ss2.n == 2 && ss2.len[0] == 3 && ss2.len[1] == 1);
    const Regs rx = regs(p1, tx), ry = regs(p1, ty), rz = regs(p0, tz);
    CHECK(rx.c == 20 && rx.b == 0 && memcmp(p1.mem.bytes() + xbase + 100, data, 20) == 0);
    CHECK(ry.c == 10 && ry.b == 0 && memcmp(p1.mem.bytes() + ybase + 100, data, 10) == 0);
    CHECK(p1.mem.bytes()[ybase + 110] == 0); // only what came
    CHECK(rz.c == 6 && rz.b == 0 && memcmp(p0.mem.bytes() + zbase + 100, loop, 6) == 0);
    CHECK(z_woke == ss2.at[1] + 1); // the device's latency
    CHECK(p1.os.recv_direct == 2 && p1.os.inbox_joined == 0 && p0.os.recv_direct == 1);
    CHECK(w.net().counts().sent == 3 && w.net().counts().looped == 1 && w.net().counts().delivered == 3);
    CHECK(regs(p0, ts).c == 1 && regs(p0, ts2).c == 1);
    printf("  send: RECVs got 20 and 10 bytes from place 0, the longest waiting first; SEND waited 3 and 2 "
           "ticks; to its own place, 1, arriving a tick later\n");
}

// PING: its time is the route's there and back (8 bytes each way); to its
// own place, twice the device's latency; past its timeout, -1. A neighbour
// SENSE waits the link's delay there and back, and reads the neighbour as
// the request lands.
void test_ping() {
    World w;
    make(w, kRing, "");
    Place& p0 = w.place(0);
    Code far, self, late;
    far.set(kB, 0).imm(kMov, 3).sys(kPing).done();
    self.set(kB, 0).imm(kMov, 0).sys(kPing).done();
    late.set(kB, 1).imm(kMov, 3).sys(kPing).done();
    const u32 tf = put(p0, far), tsf = put(p0, self), tl = put(p0, late);
    const Map& m = w.map();
    const u32 nb0 = m.other(m.link_at(0, 0), 0), nb1 = m.other(m.link_at(0, 1 % m.degree(0)), 0);
    const u64 d0 = m.link(m.link_at(0, 0)).delay;
    Code id, mem;
    id.set(kB, 1).imm(kMov, kNeighbourId).sys(kSense).done();
    mem.set(kB, 0).imm(kMov, kNeighbourFreeMemory).sys(kSense).done();
    const u32 tid = put(p0, id), tm = put(p0, mem);
    Sleeps sm;
    u32 free_at[64] = {};
    for (u32 t = 0; t < 60; ++t) {
        w.tick();
        sm.see(w, p0, tm);
        if (w.tick_count() - 1 < 64)
            free_at[w.tick_count() - 1] = w.place(nb0).mem.free_bytes();
    }
    CHECK(finished(p0, tf) && finished(p0, tsf) && finished(p0, tl) && finished(p0, tid) && finished(p0, tm));
    const u64 rtt = route_time(w, 0, 3, 8) + route_time(w, 3, 0, 8);
    CHECK(regs(p0, tf).c == rtt && regs(p0, tsf).c == 2 && regs(p0, tl).c == static_cast<u32>(-1));
    CHECK(w.net().counts().pings == 3 && w.net().counts().echoes == 3 && w.net().counts().looped == 1);
    CHECK(regs(p0, tid).c == nb1);
    CHECK(sm.n == 1 && sm.len[0] == 2 * d0);
    CHECK(sm.at[0] + d0 < 64 && regs(p0, tm).c == free_at[sm.at[0] + d0] && regs(p0, tm).c > 0);
    printf("  send: a PING across 3 hops took %llu ticks there and back, to its own place 2, and one past its "
           "timeout -1; a neighbour SENSE took %llu ticks\n",
           static_cast<unsigned long long>(rtt), static_cast<unsigned long long>(2 * d0));
}

// With every network device off, SEND fails (0), PING fails at once (-1),
// and a neighbour SENSE reads the neighbour's id but 0 for the rest.
void test_dark() {
    World w;
    make(w, kRing, "hw.network.threshold = 9000\nhw.network.maximum = 10000\n");
    Place& p0 = w.place(0);
    Code send, ping, id, mem;
    send.addr(kC, 100).set(kD, 20).set(kB, 3).imm(kMov, 1).sys(kSend).done();
    ping.set(kB, 0).imm(kMov, 1).sys(kPing).done();
    id.set(kB, 0).imm(kMov, kNeighbourId).sys(kSense).done();
    mem.set(kB, 0).imm(kMov, kNeighbourFreeMemory).sys(kSense).done();
    const u32 ts = put(p0, send), tp = put(p0, ping), ti = put(p0, id), tm = put(p0, mem);
    w.tick();
    CHECK(finished(p0, ts) && finished(p0, tp)); // at once
    for (u32 t = 0; t < 20; ++t)
        w.tick();
    const Map& m = w.map();
    CHECK(!p0.network_state.on && finished(p0, ti) && finished(p0, tm));
    CHECK(regs(p0, ts).c == 0 && regs(p0, tp).c == static_cast<u32>(-1));
    CHECK(regs(p0, ti).c == m.other(m.link_at(0, 0), 0) && regs(p0, tm).c == 0);
    CHECK(w.net().counts().sent == 0 && w.net().counts().pings == 0);
}

// SEND's other local failures: no bytes, bytes it can't read, and an exit
// queue with no room, counting what this tick's earlier SENDs keep there. A
// full inbox drops the newcomer; SENSE reads a port's inbox bytes (port 0's
// none: it isn't an inbox); a cold start loses the inboxes.
void test_full() {
    World w;
    make(w, kRing, "net.message_max = 100\nnet.inbox = 256\n");
    Place& p0 = w.place(0);
    Place& p1 = w.place(1);
    Place& p3 = w.place(3);
    Code none, unread, big;
    none.set(kD, 0).set(kB, 3).imm(kMov, 1).sys(kSend).done();
    unread.set(kC, 0).set(kD, 20).set(kB, 3).imm(kMov, 1).sys(kSend).done();
    big.set(kD, 100).set(kB, 3).imm(kMov, 4).sys(kSend).done(); // its own first 100 bytes
    const u32 tn = put(p0, none), tu = put(p0, unread);
    const u32 g1 = put(p3, big), g2 = put(p3, big); // the same tick, on its two cores
    w.tick();
    CHECK(finished(p0, tn) && finished(p0, tu) && regs(p0, tn).c == 0 && regs(p0, tu).c == 0);
    // Room for 8 ticks of 16 bytes: 128, so one of two 100-byte SENDs; it
    // waits 1 + 100/16 rounded up.
    CHECK(finished(p3, g1) != finished(p3, g2));
    for (u32 t = 0; t < 8; ++t)
        w.tick();
    CHECK(finished(p3, g1) && finished(p3, g2) && regs(p3, g1).c + regs(p3, g2).c == 1);
    // Three 100-byte messages to place 1's port 3, which nobody RECVs: an
    // inbox of 256 bytes takes two.
    u8 bytes[100];
    for (u32 i = 0; i < 100; ++i)
        bytes[i] = static_cast<u8>(i);
    for (u32 k = 0; k < 3; ++k) {
        CHECK(w.net().send(0, 1, 3, bytes, 100, 0, w.tick_count()) == SendFail::none);
        for (u32 t = 0; t < 12; ++t)
            w.tick();
    }
    CHECK(p1.os.inbox_joined == 2 && p1.os.inbox_dropped == 1 && p1.os.inbox_bytes(3) == 200);
    Code b3, b0;
    b3.set(kB, 3).imm(kMov, kPortBytes).sys(kSense).done();
    b0.set(kB, 0).imm(kMov, kPortBytes).sys(kSense).done();
    const u32 t3 = put(p1, b3), t0 = put(p1, b0);
    w.tick();
    CHECK(regs(p1, t3).c == 200 && regs(p1, t0).c == 0);
    p1.cold_start();
    CHECK(p1.os.inbox_bytes(3) == 0);
    TextBuf why;
    CHECK(p1.check(&why));
    w.tick();
    CHECK(p1.check(&why));
}

// An arrival never runs while every core has a thread: three threads
// spinning on two cores keep it waiting in port 0's queue; when they end,
// it runs.
void test_busy() {
    World w;
    make(w, kRing, "");
    Place& p2 = w.place(2);
    Code spin;
    spin.imm(kJmp, 0);
    u32 th[3];
    for (u32 i = 0; i < 3; ++i)
        th[i] = put(p2, spin);
    const u8 dud[4] = {encode(kPush, kA), encode(kPush, kA), encode(kPush, kA), encode(kPush, kA)};
    CHECK(w.net().send(0, 2, 0, dud, 4, 0, w.tick_count()) == SendFail::none);
    for (u32 t = 0; t < 50; ++t)
        w.tick();
    CHECK(w.net().counts().delivered == 1 && p2.region.queued() == 1 && p2.region.prompt_runs == 0);
    for (u32 i = 0; i < 3; ++i)
        p2.os.end_thread(th[i], DeathCause::exit);
    for (u32 t = 0; t < 5; ++t)
        w.tick();
    CHECK(p2.region.queued() == 0 && p2.region.prompt_runs == 1 && p2.region.arrival_neither == 1);
}

// What a prompt run of an arrival did, counted when it ends: it made a
// program, it only sent itself on (it waited on its core for the SEND,
// holding it), or neither.
void test_tally() {
    World w;
    make(w, kRing, "os.idle_send = true\n");
    Place& p1 = w.place(1);
    Place& p2 = w.place(2);
    const u8 dud[4] = {encode(kPush, kA), encode(kPush, kA), encode(kPush, kA), encode(kPush, kA)};
    // The traveller sends the 4 PUSH A bytes after it on to place 2, and
    // then faults.
    Code tv;
    tv.op(kMov, kC).imm(kAdd, 15).op(kSto, kC).set(kD, 4).set(kB, 0).imm(kMov, 2).sys(kSend);
    tv.op(kPush, kA);
    CHECK(tv.n == 15);
    memcpy(tv.b + tv.n, dud, 4);
    tv.n += 4;
    CHECK(w.net().send(0, 1, 0, dud, 4, 0, w.tick_count()) == SendFail::none);
    for (u32 t = 0; t < 20 && !p1.region.arrival_neither; ++t)
        w.tick();
    CHECK(p1.region.arrival_runs == 1 && p1.region.arrival_neither == 1);
    CHECK(w.net().send(0, 1, 0, tv.b, tv.n, 0, w.tick_count()) == SendFail::none);
    u64 sends = p1.os.idle_sends, sent_at = 0;
    u32 core = ~0u, held = 0;
    for (u32 t = 0; t < 30 && !p1.region.travellers; ++t) {
        w.tick();
        if (p1.os.idle_sends != sends) {
            sends = p1.os.idle_sends;
            sent_at = w.tick_count() - 1;
            for (u32 c = 0; c < p1.core_count(); ++c)
                if (p1.cores[c].idle_run && p1.cores[c].net_until)
                    core = c;
        } else if (core != ~0u && p1.cores[core].idle_run && p1.cores[core].net_until == sent_at + 2) {
            ++held; // waiting for its SEND: 1 + 4/16 rounded up
        }
    }
    CHECK(core != ~0u && held == 1 && p1.region.travellers == 1 && p1.region.arrival_runs == 2);
    // Its bytes stay in the region, where an idle run at that spot would
    // run them again (not as an arrival): PUSH A there again.
    memset(p1.mem.bytes() + p1.mem.region_base(), encode(kPush, kA), p1.mem.region_size());
    for (u32 t = 0; t < 20 && !p2.region.arrival_neither; ++t)
        w.tick();
    CHECK(p2.region.arrival_runs == 1 && p2.region.arrival_neither == 1);
    CHECK(w.net().send(0, 1, 0, kLax, 14, 0, w.tick_count()) == SendFail::none);
    for (u32 t = 0; t < 30 && !p1.region.arrival_made; ++t)
        w.tick();
    CHECK(p1.region.arrival_runs == 3 && p1.region.arrival_made == 1 && p1.region.travellers == 1 &&
          p1.region.arrival_neither == 1);
}

// By default an idle run may not SEND (9.2; build-plan-gate3.md step 8):
// the traveller's SEND fails, with 0 in A, nothing leaves, and it faults on
// the PUSH after it, an arrival that did neither. Its SEND is still counted
// as an idle run's (R4).
void test_idle_send_off() {
    World w;
    make(w, kRing, "");
    Place& p1 = w.place(1);
    Code tv;
    tv.op(kMov, kC).imm(kAdd, 15).op(kSto, kC).set(kD, 4).set(kB, 0).imm(kMov, 2).sys(kSend);
    tv.op(kPush, kA);
    CHECK(tv.n == 15);
    CHECK(w.net().send(0, 1, 0, tv.b, tv.n, 0, w.tick_count()) == SendFail::none);
    const u64 sends = p1.os.idle_sends, sent = w.net().counts().sent;
    for (u32 t = 0; t < 30 && !p1.region.arrival_neither; ++t)
        w.tick();
    CHECK(p1.region.arrival_runs == 1 && p1.region.arrival_neither == 1 && p1.region.travellers == 0);
    CHECK(p1.os.idle_sends == sends + 1 && w.net().counts().sent == sent);
    for (u32 t = 0; t < 30; ++t)
        w.tick();
    CHECK(w.net().counts().sent == sent && w.place(2).region.arrival_runs == 0);
}

// Arrivals share the cores with static (10.4; build-plan-gate3.md step 8):
// four cores a place, and four arrivals that spin to the cap. At the
// default share, a half, two hold cores while two wait, and static runs on
// the other two; with the share at 1, all four run at once, as before.
u32 most_held(const char* more, u32* waiting_while_full) {
    const char* const base4 = "map.bandwidth.min = 16\nmap.bandwidth.max = 16\nmemory.size = 32768\n"
                              "os.strict_share = 0\nnoise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\n"
                              "net.corruption = 0\nhw.cores.min = 4\nhw.cores.max = 4\nregion.step = 65\n"
                              "region.retry = 1000000\n";
    World w;
    make(w, kRing, more, nullptr, 2, base4);
    Place& p1 = w.place(1);
    CHECK(p1.core_count() == 4);
    Code spin;
    spin.imm(kJmp, 0);
    for (u32 i = 0; i < 4; ++i)
        CHECK(w.net().send(0, 1, 0, spin.b, spin.n, 0, w.tick_count()) == SendFail::none);
    u32 most = 0;
    *waiting_while_full = 0;
    for (u32 t = 0; t < 2000 && p1.region.arrival_runs < 4; ++t) {
        w.tick();
        u32 held = 0;
        for (u32 c = 0; c < p1.core_count(); ++c)
            held += p1.cores[c].idle_run && p1.cores[c].run_cause == BirthCause::arrival ? 1 : 0;
        most = held > most ? held : most;
        *waiting_while_full += held == most && p1.region.arrivals_waiting() ? 1 : 0;
    }
    CHECK(p1.region.arrival_runs == 4 && p1.region.arrivals_waiting() == 0);
    return most;
}

void test_arrival_share() {
    u32 waiting = 0;
    CHECK(most_held("", &waiting) == 2 && waiting > 0);
    CHECK(most_held("region.arrival_share = 1\n", &waiting) == 4);
}

// An arrival that keeps its core and does neither is counted by the first
// of its calls that failed, and by how it ended (asked after
// build-plan-gate3.md step 7). Six arrivals at place 1, each ending at a
// PUSH A (an idle run has no stack): one calls nothing; one ALLOCs 0 bytes
// (refused); one PROTECTs, then SPAWNs, its landing spot, which it doesn't
// hold (PROTECT counted, as the first); one only SPAWNs there; and one
// ALLOCs 48 bytes from the slowest device (its C is its landing spot) once
// that device is full, then once all are.
void test_failures() {
    World w;
    make(w, kRing, "");
    Place& p1 = w.place(1);
    const Region& g = p1.region;
    Code sleeper;
    sleeper.imm(kMov, 0).sys(kSleep);
    const u32 holder = p1.os.thread(put(p1, sleeper)).program; // it holds the memory filled below
    const u8 dud[4] = {encode(kPush, kA), encode(kPush, kA), encode(kPush, kA), encode(kPush, kA)};
    Code zero, protect, spawn, alloc;
    zero.imm(kMov, 0).sys(kAlloc).op(kPush, kA);
    protect.op(kMov, kC).sys(kProtect).op(kMov, kC).sys(kSpawn).op(kPush, kA);
    spawn.op(kMov, kC).sys(kSpawn).op(kPush, kA);
    alloc.set(kB, 0).imm(kMov, 48).sys(kAlloc).op(kPush, kA);
    const u8* bytes[6] = {dud, zero.b, protect.b, spawn.b, alloc.b, alloc.b};
    const u32 lens[6] = {4, zero.n, protect.n, spawn.n, alloc.n, alloc.n};
    for (u32 i = 0; i < 6; ++i) {
        if (i == 4) // the slowest device full, for a 48-byte ALLOC of data
            while (p1.mem.alloc(holder, 48, 0, -1)) {
            }
        if (i == 5) // and every device
            while (p1.mem.alloc(holder, 48, 0, 0)) {
            }
        CHECK(w.net().send(0, 1, 0, bytes[i], lens[i], 0, w.tick_count()) == SendFail::none);
        for (u32 t = 0; t < 20 && g.arrival_neither == i; ++t)
            w.tick();
        CHECK(g.arrival_runs == i + 1 && g.arrival_neither == i + 1);
    }
    CHECK(p1.mem.device_count() >= 2); // so another device had room at the fifth
    CHECK(g.arrival_preempted == 0 && g.arrival_overwritten == 0);
    CHECK(g.arrival_no_failure == 1 && g.arrival_alloc_refused == 1 && g.arrival_protect_failed == 1);
    CHECK(g.arrival_spawn_not_held == 1 && g.arrival_alloc_elsewhere == 1 && g.arrival_alloc_nowhere == 1);
    CHECK(g.arrival_free_failed == 0 && g.arrival_spawn_data == 0 && g.arrival_spawn_no_record == 0);
    CHECK(g.arrival_faulted == 6 && g.arrival_capped == 0 && g.arrival_cut == 0);
    TextBuf why;
    CHECK(p1.check(&why));
}

// A migrant asking for any device (worlds.migrant_device = any; asked after
// build-plan-gate3.md step 7): with place 1's slowest device full, the
// sensing migrant's arrival finds no room on the device it asks for (C, its
// landing spot), and the same asking for any device makes a program.
void test_any_device() {
    for (u32 k = 0; k < 3; ++k) {
        const HandCode c = migrant(static_cast<MigrantKind>(k), true);
        CHECK(!c.bad && c.n == migrant(static_cast<MigrantKind>(k)).n + 3);
    }
    World w;
    make(w, kRing, "");
    Place& p1 = w.place(1);
    const Region& g = p1.region;
    Code sleeper;
    sleeper.imm(kMov, 0).sys(kSleep);
    const u32 holder = p1.os.thread(put(p1, sleeper)).program;
    while (p1.mem.alloc(holder, 48, 0, -1)) {
    }
    CHECK(p1.mem.device_count() >= 2);
    const HandCode spot = migrant(MigrantKind::sense), any = migrant(MigrantKind::sense, true);
    CHECK(w.net().send(0, 1, 0, spot.b, spot.n, 0, w.tick_count()) == SendFail::none);
    for (u32 t = 0; t < 20 && g.arrival_neither == 0; ++t)
        w.tick();
    CHECK(g.arrival_runs == 1 && g.arrival_neither == 1 && g.arrival_alloc_elsewhere == 1);
    CHECK(w.net().send(0, 1, 0, any.b, any.n, 0, w.tick_count()) == SendFail::none);
    for (u32 t = 0; t < 20 && g.arrival_made == 0; ++t)
        w.tick();
    CHECK(g.arrival_runs == 2 && g.arrival_made == 1 && g.arrival_neither == 1);
    TextBuf why;
    CHECK(p1.check(&why));
}

// A data handler fires once for a burst: three messages to port 5 landing
// at one barrier (from both neighbours and the place itself) make one
// interrupt, whose handler RECVs all three. A second burst, a second.
void test_handler() {
    World w;
    make(w, kRing, "");
    Place& p1 = w.place(1);
    // The handler, at 0: [100] += 1; then RECV polls into [104] until
    // there's nothing, [101] += 1 for each; HRET.
    Code c;
    c.addr(kD, 100).op(kMov, kAtD).imm(kAdd, 1).op(kSto, kAtD);
    const u32 drain = c.n;
    c.addr(kB, 104).set(kC, 40).set(kD, 0).imm(kMov, 5).sys(kRecv);
    const u32 jez = c.n;
    c.imm(kJez, 0);
    c.addr(kD, 101).op(kMov, kAtD).imm(kAdd, 1).op(kSto, kAtD);
    c.imm(kJmp, static_cast<i32>(drain) - static_cast<i32>(c.n));
    c.b[jez + 1] = static_cast<u8>(c.n - jez);
    c.sys(kHret);
    // The thread: a data handler for port 5, then waits for interrupts.
    const u32 main = c.n;
    c.addr(kB, 0).set(kC, 5).imm(kMov, kOnData).sys(kSetHandler);
    const u32 wait = c.n;
    c.imm(kMov, 0).sys(kSleep);
    c.imm(kJmp, static_cast<i32>(wait) - static_cast<i32>(c.n));
    CHECK(c.n < 100);
    u32 base = 0;
    const u32 th = put(p1, c, main, &base);
    const u8* count = p1.mem.bytes() + base + 100;
    const u8 msg[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const u64 from0 = route_time(w, 0, 1, 8), from2 = route_time(w, 2, 1, 8);
    CHECK(from0 >= 1 && from2 >= 1);
    for (u32 burst = 1; burst <= 2; ++burst) {
        const u64 lands = w.tick_count() + 1 + (from0 > from2 ? from0 : from2);
        u64 joined = p1.os.inbox_joined, jump = 0;
        while (w.tick_count() <= lands) {
            const u64 now = w.tick_count();
            if (now + from0 == lands)
                CHECK(w.net().send(0, 1, 5, msg, 8, 0, now) == SendFail::none);
            if (now + from2 == lands)
                CHECK(w.net().send(2, 1, 5, msg, 8, 0, now) == SendFail::none);
            if (now + 1 == lands)
                CHECK(w.net().send(1, 1, 5, msg, 8, 0, now) == SendFail::none);
            w.tick();
            if (p1.os.inbox_joined != joined) {
                jump = p1.os.inbox_joined - joined;
                joined = p1.os.inbox_joined;
            }
        }
        CHECK(jump == 3); // all three at one barrier
        for (u32 t = 0; t < 5; ++t)
            w.tick();
        CHECK(count[0] == burst && count[1] == 3 * burst && p1.os.inbox_bytes(5) == 0);
    }
    CHECK(p1.os.thread(th).state == ThreadState::waiting_interrupt && p1.os.inbox_taken == 6);
    printf("  send: a data handler fired once for each burst of three messages, and took all three\n");
}

// A busy world on a map: at every place, a program SENDs the idle runs'
// replicator to place (tick mod places)'s port 0, SENSEs a neighbour and
// PINGs, again and again; arrivals run as prompt runs and colonise. It
// comes out the same on 1 and 4 host threads, and its checks hold.
struct Busy {
    u64 hash = 0;
    u64 v[16] = {};
    u32 bad = 0;
};

void busy(u32 threads, Busy* out) {
    World w;
    make(w, "world.places = 9\nworld.map = grid\n", "", nullptr, threads);
    for (u32 i = 0; i < w.place_count(); ++i) {
        Code c;
        memcpy(c.b, kLax, 14);
        c.n = 14;
        const u32 start = c.n;
        c.addr(kC, 0).set(kD, 14).set(kB, 0);
        const u32 loop = c.n;
        c.imm(kMov, kTick).sys(kSense).sys(kSend);
        c.imm(kMov, kNeighbourIdleCores).sys(kSense);
        c.imm(kMov, static_cast<i32>((i + 4) % 9)).sys(kPing);
        c.imm(kJmp, static_cast<i32>(loop) - static_cast<i32>(c.n));
        put(w.place(i), c, start);
    }
    TextBuf why;
    for (u32 t = 0; t < 400; ++t) {
        w.tick();
        if (t % 25 == 24)
            out->hash = out->hash * 1000003u ^ w.hash();
        if (threads == 1)
            for (u32 i = 0; i < w.place_count(); ++i)
                out->bad += w.place(i).check(&why) ? 0 : 1;
    }
    const NetCounts& n = w.net().counts();
    out->v[0] = n.sent;
    out->v[1] = n.delivered;
    out->v[2] = n.looped;
    out->v[3] = n.pings;
    out->v[4] = n.echoes;
    out->v[5] = n.full + n.stranded + n.lost + n.cut;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        out->v[6] += p.region.prompt_runs;
        out->v[7] += p.region.arrival_made;
        out->v[8] += p.region.travellers;
        out->v[9] += p.region.arrival_neither;
        out->v[10] += p.region.dropped;
        out->v[11] += p.os.idle_sends;
        out->v[12] += p.births;
        out->v[13] += p.deaths;
    }
}

void test_threads() {
    Busy one, four;
    busy(1, &one);
    busy(4, &four);
    CHECK(one.hash == four.hash && one.bad == 0);
    u32 differ = 0;
    for (u32 k = 0; k < 16; ++k)
        differ += one.v[k] != four.v[k] ? 1 : 0;
    CHECK(differ == 0);
    CHECK(one.v[0] > 50 && one.v[3] > 50 && one.v[6] > 0 && one.v[7] > 0);
    printf("  send: a busy 3 x 3 world (%llu SENDs, %llu PINGs, %llu prompt runs, %llu of which made programs, "
           "%llu births) came out the same on 1 and 4 threads\n",
           static_cast<unsigned long long>(one.v[0]), static_cast<unsigned long long>(one.v[3]),
           static_cast<unsigned long long>(one.v[6]), static_cast<unsigned long long>(one.v[7]),
           static_cast<unsigned long long>(one.v[12]));
}

// Gate 3's migrants (traffic_world.h; build-plan-gate3.md step 7), on a 4 x 4
// grid, half its places strict and its page sizes mixed:
//   - the sensing one, placed at place 0, reaches every place: each of the
//     others gets a colony of its lineage, strict places and lax;
//   - the blind one, placed at place 5, lands copies only at places 0 and 1
//     (the 0 and 1 its SENDs leave in A);
//   - the pushy one, placed at every place but the middle of a 3 x 3 grid,
//     whose cores are kept busy by spinning threads (one more than its
//     cores): arrivals reach it, and none ever runs there.
const char* const kMixed = "map.bandwidth.min = 16\nmap.bandwidth.max = 16\nmemory.size = 32768\n"
                           "os.strict_share = 0.5\nnoise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\n"
                           "net.corruption = 0\nhw.cores.min = 2\nhw.cores.max = 2\nregion.step = 65\n"
                           "region.retry = 1000000\n";

// Arrivals that reached place p's port 0: run, dropped for want of room, or
// still waiting.
u64 landed(const Place& p) { return p.region.arrival_runs + p.region.dropped + p.region.queued(); }

void test_migrants() {
    for (u32 k = 0; k < 3; ++k)
        CHECK(!migrant(static_cast<MigrantKind>(k)).bad);
    // The sensing migrant.
    Observer obs;
    ObserverSettings os;
    os.keep_facts = false;
    obs.init(os);
    World w;
    make(w, "world.places = 16\nworld.map = grid\n", "", &obs, 2, kMixed);
    const u64 origin = place_migrant(w.place(0), migrant(MigrantKind::sense));
    CHECK(origin != 0);
    u32 strict = 0;
    for (u32 i = 0; i < w.place_count(); ++i)
        strict += w.place(i).mem.strict() ? 1 : 0;
    CHECK(strict > 0 && strict < w.place_count());
    u32 reached = 0, reached_strict = 0;
    u64 last = 0;
    for (u32 t = 0; t < 3000 && reached + 1 < w.place_count(); ++t) {
        w.tick();
        reached = reached_strict = 0;
        for (u32 i = 1; i < w.place_count(); ++i)
            if (obs.colony(origin, i)) {
                ++reached;
                reached_strict += w.place(i).mem.strict() ? 1 : 0;
            }
        last = w.tick_count();
    }
    CHECK(reached + 1 == w.place_count() && reached_strict > 0);
    printf("  send: the sensing migrant reached all %u other places of a 4 x 4 grid (%u strict) by tick %llu\n",
           reached, reached_strict, static_cast<unsigned long long>(last));

    // The blind migrant.
    World b;
    make(b, "world.places = 16\nworld.map = grid\n", "", nullptr, 2, kMixed);
    CHECK(place_migrant(b.place(5), migrant(MigrantKind::blind)) != 0);
    for (u32 t = 0; t < 600; ++t)
        b.tick();
    u32 elsewhere = 0;
    for (u32 i = 2; i < b.place_count(); ++i)
        elsewhere += landed(b.place(i)) ? 1 : 0;
    CHECK(landed(b.place(0)) > 0 && landed(b.place(1)) > 0 && elsewhere == 0);
    printf("  send: the blind migrant's copies landed only at places 0 and 1 (%llu and %llu arrivals)\n",
           static_cast<unsigned long long>(landed(b.place(0))), static_cast<unsigned long long>(landed(b.place(1))));

    // The pushy migrant and a busy place.
    World s;
    make(s, "world.places = 9\nworld.map = grid\n", "", nullptr, 2, kMixed);
    HandCode spin; // strict or lax
    spin.imm(kJmp, 0);
    Place& mid = s.place(4);
    for (u32 c = 0; c <= mid.core_count(); ++c)
        CHECK(place_hand_code(mid, spin) != ~0u);
    for (u32 i = 0; i < s.place_count(); ++i)
        if (i != 4)
            CHECK(place_migrant(s.place(i), migrant(MigrantKind::pushy)) != 0);
    for (u32 t = 0; t < 400; ++t)
        s.tick();
    u64 ran_elsewhere = 0;
    for (u32 i = 0; i < s.place_count(); ++i)
        if (i != 4)
            ran_elsewhere += s.place(i).region.arrival_runs;
    CHECK(landed(mid) > 0 && mid.region.arrival_runs == 0 && ran_elsewhere > 0);
    printf("  send: %llu arrivals reached a place kept busy and none ran; %llu ran at the other places\n",
           static_cast<unsigned long long>(landed(mid)), static_cast<unsigned long long>(ran_elsewhere));
}

} // namespace

void suite_send() {
    test_colonist();
    test_recv();
    test_ping();
    test_dark();
    test_full();
    test_busy();
    test_tally();
    test_idle_send_off();
    test_arrival_share();
    test_failures();
    test_any_device();
    test_handler();
    test_threads();
    test_migrants();
}
