// Determinism under gates 2 and 3 (build-plan-gate2.md step 7 and 6;
// build-plan-gate3.md step 5): a busy world under the varying supply, with
// drop-outs and blackouts, cut-outs, fading and cold starts, gives the same
// hash on 1, 2 and 24 host threads, with an observer on or off and with
// its big fades shared out (optimization-plan.md pass 10); and
// resumed from a snapshot taken mid-drop-out, one taken mid-off-time and one
// taken mid-fade, on another thread count and with or without an observer,
// it comes out the same as never stopped. The same world with gate 3's
// traffic on its map does the same, resumed from snapshots taken with
// messages part way across links, waiting in inboxes, a PING waiting for
// its echo, a neighbour SENSE waiting for its barrier, and an idle run
// waiting on its core for its SEND.
// The world hash is each place's whole saved state (Place::hash), then the
// network's (Network::hash), so this covers everything gates 2 and 3 added
// to it: each device's state and when it cut out, the voltage and the last
// tick's draw (the supply's schedule is a function of the tick), the fade
// doses, the boot block's writer and port 0's queue; the messages on their
// way, the inboxes and the waits on the network.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

const char* const kGate2 = "world.places = 8\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "region.retry = 2\n"
                           "noise.p_floor = 1e-4\n"
                           "noise.e_floor = 1e-4\n"
                           "noise.cosmic = 1e-6\n"
                           "supply.sag = 50\n"
                           "supply.day = 600\n"
                           "supply.night = 0.2\n"
                           "supply.swing = 0.25\n"
                           "supply.swing_period = 200\n"
                           "supply.dropouts = 1000\n"
                           "supply.dropout.min = 5\n"
                           "supply.dropout.max = 120\n"
                           "supply.blackout_share = 0.5\n"
                           "disk.threshold = 900\n";

const u64 kTicks = 2400;
const u64 kEvery = 400; // the hash is compared every kEvery ticks

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// test_boot.cpp's booter.
const u8 kBooter[34] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                        kDwrite,            encode(kJnz, kImm), 19,                 encode(kMov, kImm),
                        2,                  encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSys, kImm), kAlloc,             encode(kMov, kIp),  encode(kSub, kImm),
                        18,                 encode(kSto, kB),   encode(kCpy, kImm), 34,
                        encode(kMov, kD),   encode(kSys, kImm), kSpawn,             encode(kPush, kA),
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4)};
// DWRITE in a loop, so the disk cuts out with writes in flight.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};

// With `traffic`, each place also carries gate 3's (traffic_world.h): a colonist
// sender, a talker, a listener and a traveller.
// `more` is settings added to gate 2's.
bool build(World& w, Config& cfg, bool traffic, const char* more = "") {
    char text[2048];
    snprintf(text, sizeof text, "%s%s", kGate2, more);
    Config::ParseError pe;
    if (!cfg.parse(text, &pe))
        return false;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  determinism: %s %s\n", why.c_str(), cfg.value_error());
        return false;
    }
    // A test world (R7).
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        const u8* code[2] = {kBooter, kWriter};
        const u32 len[2] = {sizeof kBooter, sizeof kWriter};
        for (u32 k = 0; k < 2; ++k) {
            const u32 at = p.mem.alloc(1, 64, 2, 0);
            if (at) {
                memcpy(p.mem.bytes() + at, code[k], len[k]);
                p.os.place_by_hand(1, at, at, 0);
            }
        }
        if (traffic)
            place_traffic(p, w.place_count(), i);
    }
    return true;
}

// The moments a snapshot is taken at, between ticks (the next tick to run
// is w.tick_count()), after tick 100 so the world is under way. Gate 2's
// first, then gate 3's.
enum Moment { kDropout = 0, kOffTime, kFade, kMidHop, kInbox, kPingWait, kAsk, kIdleWait, kMoments };
const char* const kMomentName[kMoments] = {"mid-drop-out",      "mid-off-time",        "mid-fade",
                                           "messages mid-hop",  "messages in inboxes", "a PING waiting",
                                           "a neighbour SENSE waiting", "an idle run waiting on its SEND"};

// Whether the world is at the moment, and where (a place, or for
// messages mid-hop how many): part way through a drop-out (it started
// before the next tick and goes on through it); a device cut out, its
// off-time not yet over; a memory device cut out and fading, some of its
// cells failed and more to fail; messages part way across links; bytes in
// a comms port's inbox; a thread waiting for a PING's echo; a neighbour
// SENSE's request not yet landed; an idle run waiting on its core for its
// SEND or neighbour SENSE.
bool at_moment(World& w, Moment m, u32* where) {
    const u64 t = w.tick_count();
    if (m == kMidHop) {
        *where = w.net().on_links();
        return *where > 0;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        bool yes = false;
        if (m == kDropout) {
            Dropout d;
            d.length = 0;
            yes = p.supply.dropout(t, &d) < kCapacityOne && d.length && d.start < t;
        } else if (m == kOffTime) {
            const u64 off = p.power().off_time;
            for (u32 c = 0; c < p.core_count() && !yes; ++c)
                yes = !p.cores[c].working && t - p.cores[c].off_at < off;
            for (u32 d = 0; d < p.mem.device_count() && !yes; ++d)
                yes = !p.memory_state[d].on && t - p.memory_state[d].off_at < off;
            yes = yes || (!p.disk_state.on && t - p.disk_state.off_at < off);
        } else if (m == kFade) {
            for (u32 d = 0; d < p.mem.device_count() && !yes; ++d)
                yes = !p.memory_state[d].on && p.noise.dose(d) > 0 && p.noise.faded(d) > 0 &&
                      p.noise.faded(d) < static_cast<u64>(p.mem.device(d).size) * 8;
        } else if (m == kInbox) {
            for (u32 port = 1; port < p.os.settings().ports && !yes; ++port)
                yes = p.os.inbox_bytes(port) > 0;
        } else if (m == kPingWait) {
            for (u32 k = 0; k < p.os.thread_capacity() && !yes; ++k)
                yes = p.os.thread(k).state == ThreadState::waiting_ping;
        } else if (m == kAsk) {
            yes = !p.os.asks().empty();
        } else {
            for (u32 c = 0; c < p.core_count() && !yes; ++c)
                yes = p.cores[c].idle_run && p.cores[c].net_until > t;
        }
        if (yes) {
            *where = i;
            return true;
        }
    }
    return false;
}

struct Run {
    u64 h[kTicks / kEvery] = {};
    u64 snap_at[kMoments] = {};
    u32 snap_place[kMoments] = {};
    ByteWriter snap[kMoments];
    u64 cold = 0, boots = 0, cutouts = 0, faded = 0, torn = 0, births = 0;
    u64 sent = 0, delivered = 0, pings = 0, arrivals = 0, inbox = 0;
    u64 spread = 0, chunks = 0; // fades shared out, and their chunks
};

// A world's fades of 256 cells or more shared out over its host threads in
// chunks of 64 (optimization-plan.md pass 10), and every run of the pool's
// that no test order is set for, the chunks' among them, in reverse.
void spread_fades(World& w) {
    w.set_fade_spread(64, 256);
    w.pool().set_dispatch_reversed(true);
}

// Every fade in its tick, for the runs the shared-out ones are held to:
// a world's own defaults share out fades of 8,192 cells (pass 11).
void fades_in_ticks(World& w) { w.set_fade_spread(64, ~0ull); }

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

// Runs the world, with gate 3's traffic or not, on `threads` host threads,
// with an observer or not, hashing it every kEvery ticks; with `snaps`,
// taking a snapshot at the first of each of its gate's moments; with
// `spread`, its big fades shared out (spread_fades).
void run(Run* r, u32 threads, bool observe, bool snaps, bool traffic, bool spread = false) {
    World w;
    Config cfg;
    CHECK(build(w, cfg, traffic));
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    if (spread)
        spread_fades(w);
    else
        fades_in_ticks(w);
    const u32 first = traffic ? kMidHop : kDropout, last = traffic ? kMoments : kMidHop;
    while (w.tick_count() < kTicks) {
        if (snaps && w.tick_count() > 100)
            for (u32 m = first; m < last; ++m)
                if (!r->snap_at[m] && at_moment(w, static_cast<Moment>(m), &r->snap_place[m])) {
                    r->snap_at[m] = w.tick_count();
                    snapshot(w, cfg, r->snap[m]);
                }
        w.tick();
        if (w.tick_count() % kEvery == 0)
            r->h[w.tick_count() / kEvery - 1] = w.hash();
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        r->cold += p.cold_starts;
        r->boots += p.region.boot_runs;
        r->cutouts += p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts;
        r->faded += p.noise.cells_faded;
        r->torn += p.os.torn_writes;
        r->births += p.births;
        r->arrivals += p.region.arrival_runs;
        r->inbox += p.os.inbox_joined;
    }
    r->sent = w.net().counts().sent;
    r->delivered = w.net().counts().delivered;
    r->pings = w.net().counts().pings;
    r->spread = w.spread_fades;
    r->chunks = w.spread_chunks;
    if (observe)
        CHECK(obs.total_births == r->births && obs.cold_starts == r->cold);
}

bool same(const Run& a, const Run& b) {
    for (u64 i = 0; i < kTicks / kEvery; ++i)
        if (a.h[i] != b.h[i])
            return false;
    return true;
}

// Loads a snapshot and runs it on to kTicks: its final hash. With `spread`,
// its big fades shared out (spread_fades).
u64 resume(const ByteWriter& snap, u32 threads, bool observe, u64* at, bool spread = false) {
    SnapshotHeader sh;
    TextBuf err;
    CHECK(snapshot_parse(snap.data(), snap.size(), &sh, &err));
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(sh.config_text, &pe));
    World w;
    TextBuf why;
    CHECK(w.init(cfg, &why));
    ByteReader in(snap.data() + sh.world_offset, sh.world_size);
    CHECK(w.load(in));
    *at = w.tick_count();
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    if (spread)
        spread_fades(w);
    else
        fades_in_ticks(w);
    while (w.tick_count() < kTicks)
        w.tick();
    return w.hash();
}

// The same hash on 1, 2 and 24 threads, watched or not, and with its big
// fades shared out on 2 and 24; and resumed from each of the gate's
// snapshots on 2 and 24 threads, watched and not, the 24 sharing them out.
void check_runs(bool traffic, Run* one) {
    Run two_, many_, watched_, watched_many_, spread_two_, spread_many_;
    run(one, 1, false, true, traffic);
    run(&two_, 2, false, false, traffic);
    run(&many_, 24, false, false, traffic);
    run(&watched_, 1, true, false, traffic);
    run(&watched_many_, 24, true, false, traffic);
    run(&spread_two_, 2, false, false, traffic, true);
    run(&spread_many_, 24, true, false, traffic, true);
    CHECK(same(*one, two_) && same(*one, many_));
    CHECK(same(*one, watched_) && same(*one, watched_many_));
    CHECK(same(*one, spread_two_) && same(*one, spread_many_));
    CHECK(one->spread == 0 && (one->faded == 0 || spread_many_.spread > 0));
    CHECK(spread_two_.spread == spread_many_.spread && spread_two_.chunks == spread_many_.chunks);
    one->spread = spread_many_.spread;
    one->chunks = spread_many_.chunks;
    CHECK(one->h[0] != one->h[1]); // the world moves
    const u64 end = one->h[kTicks / kEvery - 1];
    const u32 first = traffic ? kMidHop : kDropout, last = traffic ? kMoments : kMidHop;
    for (u32 m = first; m < last; ++m) {
        CHECK(one->snap_at[m] > 100);
        if (!one->snap_at[m])
            continue;
        u64 at = 0;
        CHECK_EQ_U64(resume(one->snap[m], 2, false, &at), end);
        CHECK(at == one->snap_at[m]);
        CHECK_EQ_U64(resume(one->snap[m], 24, true, &at, true), end);
    }
}

void test_gate2() {
    Run one_;
    Run* one = &one_;
    check_runs(false, one);
    // It went through what gate 2 adds.
    CHECK(one->cold > 0 && one->boots > 0 && one->cutouts > 0 && one->faded > 0 && one->torn > 0);
    printf("  determinism: a gate 2 world (%llu cut-outs, %llu cells faded, %llu writes torn, %llu cold starts, "
           "%llu boots, %llu births in %llu ticks) hashed the same on 1, 2 and 24 threads, watched or not, and "
           "with %llu fades shared out in %llu chunks; resumed the same from snapshots %s at tick %llu (place %u), "
           "%s at %llu (place %u) and %s at %llu (place %u)\n",
           static_cast<unsigned long long>(one->cutouts), static_cast<unsigned long long>(one->faded),
           static_cast<unsigned long long>(one->torn), static_cast<unsigned long long>(one->cold),
           static_cast<unsigned long long>(one->boots), static_cast<unsigned long long>(one->births),
           static_cast<unsigned long long>(kTicks), static_cast<unsigned long long>(one->spread),
           static_cast<unsigned long long>(one->chunks), kMomentName[0],
           static_cast<unsigned long long>(one->snap_at[0]), one->snap_place[0], kMomentName[1],
           static_cast<unsigned long long>(one->snap_at[1]), one->snap_place[1], kMomentName[2],
           static_cast<unsigned long long>(one->snap_at[2]), one->snap_place[2]);
}

void test_gate3() {
    Run one_;
    Run* one = &one_;
    check_runs(true, one);
    // It carried gate 3's traffic, through what gate 2 adds.
    CHECK(one->sent > 0 && one->delivered > 0 && one->pings > 0 && one->arrivals > 0 && one->inbox > 0);
    CHECK(one->cold > 0 && one->cutouts > 0);
    printf("  determinism: with gate 3's traffic (%llu sent, %llu delivered, %llu PINGs answered, %llu arrivals "
           "run, %llu inbox joins, %llu cold starts) the same on 1, 2 and 24 threads, watched or not, and with %llu "
           "fades shared out in %llu chunks; resumed the same from snapshots with %u %s at tick %llu, and with %s "
           "(place %u) at %llu, %s (%u) at %llu, %s (%u) at %llu and %s (%u) at %llu\n",
           static_cast<unsigned long long>(one->sent), static_cast<unsigned long long>(one->delivered),
           static_cast<unsigned long long>(one->pings), static_cast<unsigned long long>(one->arrivals),
           static_cast<unsigned long long>(one->inbox), static_cast<unsigned long long>(one->cold),
           static_cast<unsigned long long>(one->spread), static_cast<unsigned long long>(one->chunks),
           one->snap_place[kMidHop], kMomentName[kMidHop], static_cast<unsigned long long>(one->snap_at[kMidHop]),
           kMomentName[kInbox], one->snap_place[kInbox], static_cast<unsigned long long>(one->snap_at[kInbox]),
           kMomentName[kPingWait], one->snap_place[kPingWait],
           static_cast<unsigned long long>(one->snap_at[kPingWait]), kMomentName[kAsk], one->snap_place[kAsk],
           static_cast<unsigned long long>(one->snap_at[kAsk]), kMomentName[kIdleWait], one->snap_place[kIdleWait],
           static_cast<unsigned long long>(one->snap_at[kIdleWait]));
}

// Gate 2's world with a falling cosmic rate (7.5): from 2e-5 to its 1e-6, a
// step every 100 ticks. The same on 1 and 24 threads, and resumed the same
// from a snapshot taken just before a step (the next tick to run is the
// step's first) and one taken between steps.
void test_fall() {
    const char* const fall = "noise.cosmic_start = 2e-5\nnoise.cosmic_half_life = 800\n";
    const u64 at[2] = {300, 350};
    ByteWriter snap[2];
    u64 end[3] = {}, flips[3] = {};
    for (u32 k = 0; k < 3; ++k) { // 1 thread taking the snapshots, 24 threads, and no fall
        World w;
        Config cfg;
        CHECK(build(w, cfg, false, k < 2 ? fall : ""));
        w.start_threads(k == 1 ? 24 : 1);
        while (w.tick_count() < kTicks) {
            for (u32 i = 0; i < 2; ++i)
                if (k == 0 && w.tick_count() == at[i])
                    snapshot(w, cfg, snap[i]);
            w.tick();
        }
        end[k] = w.hash();
        for (u32 i = 0; i < w.place_count(); ++i)
            flips[k] += w.place(i).noise.cosmic;
    }
    CHECK_EQ_U64(end[1], end[0]);
    CHECK(flips[0] > 2 * flips[2] && end[0] != end[2]);
    for (u32 i = 0; i < 2; ++i) {
        u64 got = 0;
        CHECK_EQ_U64(resume(snap[i], 2, false, &got), end[0]);
        CHECK(got == at[i]);
        CHECK_EQ_U64(resume(snap[i], 24, true, &got), end[0]);
    }
    printf("  determinism: with cosmic rays falling from 2e-5 to 1e-6 (%llu flips in %llu ticks, against %llu "
           "without the fall) the same on 1 and 24 threads; resumed the same from snapshots at tick %llu, just "
           "before a step, and %llu, between steps\n",
           static_cast<unsigned long long>(flips[0]), static_cast<unsigned long long>(kTicks),
           static_cast<unsigned long long>(flips[2]), static_cast<unsigned long long>(at[0]),
           static_cast<unsigned long long>(at[1]));
}

} // namespace

void suite_determinism() {
    test_gate2();
    test_gate3();
    test_fall();
}
