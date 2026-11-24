// Gate 5's two rules of 9.1's amendment (design.md 9.1; build-plan-gate5.md
// step 10, gap 14).
//
// - The watchdog: a thread that runs its count of cycles without a call
//   faults, to the cycle, before its next instruction, on one core or
//   sharing it; a call in time starts the count again, so it never does; a
//   handler takes the fault (code 3) and the thread goes on where it was;
//   with none, or inside a handler, the thread ends, cause "watchdog".
// - The out-of-memory killer: a place crowded at the set number of
//   barriers in a row loses one program, and the next only after as many
//   again; the programs ended match their shares of the bytes programs
//   hold; idle runs' holdings and every other record are left alone; every
//   thread of it ends, cause "out of memory", its memory freed with its
//   bytes left.
// - On 1 host thread and 3 and from a snapshot, and each at 0 off.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/sched.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

constexpr u32 RUN = 1;
constexpr u64 kSpinCost16 = 4 * 16 + 16; // JMP # 0 from fast memory: fetch + work

const u8 kSpin[2] = {encode(kJmp, kImm), 0}; // JMP # 0
const u8 kFault = encode(kPush, kA);         // an idle run has no stack: this faults

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

// A spinner with a fault handler. B = the handler, at 14; D = the byte at
// 30, where the handler puts the code it was given; A = 0, a fault handler.
// It spins at 12; the handler stores A through D and HRETs.
const u8 kHandled[17] = {encode(kMov, kIp),  encode(kAdd, kImm), 14, encode(kSto, kB),  encode(kMov, kIp),
                         encode(kAdd, kImm), 26,                 encode(kSto, kD),  encode(kMov, kImm), 0,
                         encode(kSys, kImm), kSetHandler,        encode(kJmp, kImm), 0, encode(kSto, kAtD),
                         encode(kSys, kImm), kHret};
constexpr u32 kHandledSetup = 8; // instructions before it spins

// A scheduler and an OS on one memory, as the scheduler's suite has them.
struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Os os;
    Scheduler sched;
    CostTable p = default_p_costs(), e = default_e_costs();
    u32 last_at = 0; // where place() put the last program

    explicit Rig(u64 watchdog) {
        static const MemDeviceSpec d16[] = {{16384, 4 * 16, 900}};
        MemorySpec ms;
        ms.page_size = 256;
        ms.region_size = 256;
        ms.devices = d16;
        ms.device_count = 1;
        CHECK(m.init(ms, nullptr));
        for (u32 c = 0; c < 2; ++c) {
            cores[c].kind = DeviceKind::p_core;
            cores[c].stock_speed = cores[c].speed = 25000;
        }
        env.tick = 100;
        OsSettings os_s;
        os_s.watchdog = watchdog;
        os_s.killer = 0;
        CHECK(os.init(&m, cores, 1, &env, os_s, nullptr));
        SchedSettings ss;
        CHECK(sched.init(&os, &m, cores, 1, &env, ss, &p, &e, nullptr));
    }

    u32 place(const u8* code, u32 n, u32 size = 64) {
        const u32 at = m.alloc(RUN, size, 2, 0);
        CHECK(at != 0);
        memcpy(m.bytes() + at, code, n);
        last_at = at;
        return os.place_by_hand(RUN, at, at, 0);
    }

    void ticks(u32 n) {
        for (u32 i = 0; i < n; ++i) {
            sched.tick();
            ++env.tick;
        }
    }

    u32 deaths_by(DeathCause c) const {
        u32 n = 0;
        for (usize i = 0; i < os.deaths.size(); ++i)
            n += os.deaths[i].cause == c ? 1 : 0;
        return n;
    }
};

u64 spins_to(u64 limit) { return (limit * 16 + kSpinCost16 - 1) / kSpinCost16; }

// To the cycle: a thread spinning alone faults after the instruction that
// takes its count to the limit, and with no handler ends. Over quanta and
// ticks, and by turns with another thread on the same core, the count is
// the thread's own.
void test_exact() {
    u32 limits = 0;
    for (u64 L = 100; L <= 5000; L += 97, ++limits) {
        Rig r(L);
        r.place(kSpin, 2);
        r.ticks(2);
        CHECK_EQ_U64(r.sched.instructions, spins_to(L));
        CHECK(r.os.live_threads() == 0 && r.deaths_by(DeathCause::watchdog) == 1);
        CHECK(r.os.watchdog_faults == 1 && r.os.watchdog_handled == 0 && r.os.deaths.size() == 1);
    }
    Rig r(60000);
    r.place(kSpin, 2);
    r.ticks(2);
    CHECK(r.os.live_threads() == 1);
    r.ticks(3);
    CHECK_EQ_U64(r.sched.instructions, spins_to(60000));
    CHECK(r.os.live_threads() == 0 && r.deaths_by(DeathCause::watchdog) == 1);
    Rig two(60000);
    two.place(kSpin, 2);
    two.place(kSpin, 2);
    two.ticks(3);
    CHECK(two.os.live_threads() == 2 && two.sched.switches > 6);
    two.ticks(5);
    CHECK_EQ_U64(two.sched.instructions, 2 * spins_to(60000));
    CHECK(two.os.live_threads() == 0 && two.deaths_by(DeathCause::watchdog) == 2);
    printf("  guards: the watchdog, to the cycle: at %u limits from 100 to 5,000 cycles a spinning thread ran "
           "exactly the instructions that take it to the limit and ended, cause watchdog; at 60,000 cycles, "
           "over five ticks, %llu instructions, and two threads by turns on one core %llu each\n",
           limits, static_cast<ull>(spins_to(60000)), static_cast<ull>(spins_to(60000)));
}

// A call in time starts the count again: a loop that calls SENSE every 182
// instructions (910 cycles) never trips a watchdog of 1,003 cycles; one
// that calls every 222 (1,110) faults in its first round, to the cycle.
// And with the watchdog off a spinner runs on.
void test_calls() {
    const u8 often[10] = {encode(kMov, kImm), 90,         encode(kSub, kImm), 1, encode(kJnz, kImm),
                          static_cast<u8>(-2), encode(kSys, kImm), kSense, encode(kJmp, kImm),
                          static_cast<u8>(-8)};
    Rig r(1003);
    r.place(often, 10);
    r.ticks(40);
    CHECK(r.os.live_threads() == 1 && r.os.watchdog_faults == 0 && r.os.calls[kSense] > 500);
    u8 seldom[10];
    memcpy(seldom, often, sizeof seldom);
    seldom[1] = 110;
    Rig s(1003);
    s.place(seldom, 10);
    s.ticks(2);
    CHECK_EQ_U64(s.sched.instructions, spins_to(1003));
    CHECK(s.os.live_threads() == 0 && s.deaths_by(DeathCause::watchdog) == 1 && s.os.calls[kSense] == 0);
    Rig off(0);
    off.place(kSpin, 2);
    off.ticks(60);
    CHECK(off.os.live_threads() == 1 && off.os.watchdog_faults == 0 && off.sched.instructions > 290000);
    printf("  guards: a thread calling every 910 cycles ran 40 ticks under a watchdog of 1,003 (%llu calls) and "
           "never faulted; calling every 1,110 it faulted at its first round's cycle 1,003; with the watchdog "
           "off a spinner ran %llu instructions and lived\n",
           static_cast<ull>(r.os.calls[kSense]), static_cast<ull>(off.sched.instructions));
}

// A handler takes the fault, with the code 3 in A, and HRET goes back to
// the instruction the thread was about to run, its count started again:
// every round is the limit's spins and the handler's two instructions. A
// handler that itself runs out its count ends the thread.
void test_handled() {
    const u64 L = 1003;
    const u64 n = spins_to(L);
    Rig r(L);
    r.place(kHandled, sizeof kHandled);
    const u32 at = r.last_at;
    r.ticks(20);
    const u64 f = r.os.watchdog_faults;
    const u64 ran = r.sched.instructions - kHandledSetup;
    CHECK(r.os.live_threads() == 1 && f > 100 && r.os.watchdog_handled == f);
    CHECK(r.m.bytes()[at + 30] == kFaultWatchdog);
    CHECK(f * (n + 2) <= ran && ran < (f + 1) * (n + 2));
    u8 loops[sizeof kHandled];
    memcpy(loops, kHandled, sizeof loops);
    loops[14] = encode(kJmp, kImm); // the handler spins too
    loops[15] = 0;
    Rig h(L);
    h.place(loops, sizeof loops);
    h.ticks(3);
    CHECK_EQ_U64(h.sched.instructions, kHandledSetup + 2 * n);
    CHECK(h.os.live_threads() == 0 && h.os.watchdog_faults == 2 && h.os.watchdog_handled == 1);
    CHECK(h.deaths_by(DeathCause::watchdog) == 1);
    printf("  guards: a handler took %llu watchdog faults in 20 ticks, each with the code 3, each round %llu spins "
           "and the handler's two instructions; a handler that spun too ended the thread at its own count\n",
           static_cast<ull>(f), static_cast<ull>(n));
}

// A world (the tests' defaults turn gate 5 off) with these settings over
// its own.
bool build(World& w, Config& cfg, const char* base, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(base, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  guards: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        fflush(stdout);
        return false;
    }
    return true;
}

// One place on no map, with nothing to change its memory but what a test
// does: no noise, a steady supply.
const char* const kOne = "world.places = 1\nworld.map = none\nmemory.size = 32768\ndisk.size = 65536\n"
                         "noise.cosmic = 0\nnoise.p_floor = 0\nnoise.e_floor = 0\nsupply.dropouts = 0\n"
                         "supply.swing = 0\nsupply.sag = 0\n";

// Sleeps for ever: SLEEP 0 waits for an interrupt no handler takes.
const u8 kSleeper[6] = {encode(kMov, kImm), 0, encode(kSys, kImm), kSleep, encode(kJmp, kImm),
                        static_cast<u8>(-4)};
// SPAWNs two threads of its own at 16, then sleeps there with them.
const u8 kThree[22] = {encode(kMov, kIp),  encode(kAdd, kImm), 16, encode(kSys, kImm), kSpawn, encode(kMov, kIp),
                       encode(kAdd, kImm), 11,                 encode(kSys, kImm), kSpawn, encode(kJmp, kImm),
                       6,                  0,                  0,  0,  0,  encode(kMov, kImm), 0,
                       encode(kSys, kImm), kSleep,             encode(kJmp, kImm), static_cast<u8>(-4)};

// A hand-placed program at place p: code at the start of an allocation of
// `size` bytes. Its holder, or 0.
u32 hand(Place& p, const u8* code, u32 n, u32 size) {
    const u32 at = p.mem.alloc(RUN, size, p.mem.strict() ? 1 : 2, 0);
    if (!at)
        return 0;
    memcpy(p.mem.bytes() + at, code, n);
    if (!p.os.place_by_hand(RUN, at, at, 0))
        return 0;
    return p.mem.record(p.mem.alloc_at(at)).holder;
}

// The draws by themselves: programs of 1, 2, 3 and 4 kilobytes, three of
// each, and an allocation held by an idle run half as big as all of them. Over
// 200,000 of the killer's draws each program comes up in its share of the
// bytes programs hold, and the run never. Ending one frees its records
// alone, and leaves every byte as it was.
void test_draws() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, kOne, "memory.size = 65536\nmemory.max_alloc = 32768\n", &why));
    Place& p = w.place(0);
    u32 holders[12];
    for (u32 i = 0; i < 12; ++i) {
        holders[i] = hand(p, kSleeper, sizeof kSleeper, 1024 * (1 + i % 4));
        CHECK(holders[i] >= kFirstProgramHolder);
    }
    const u32 run = p.mem.alloc(2, 16384, 0, 0); // idle run 2's: the memory's records only, no core runs it
    CHECK(run != 0);
    u64 total = 0, bytes[12];
    for (u32 i = 0; i < 12; ++i)
        total += bytes[i] = p.os.bytes_held(holders[i]);
    u64 held = 0, run_bytes = 0;
    for (u32 id = 1; id < p.mem.record_capacity(); ++id) {
        const Alloc& a = p.mem.record(id);
        held += a.holder >= kFirstProgramHolder ? a.extent : 0;
        run_bytes += a.holder == 2 ? a.extent : 0;
    }
    CHECK(held == total && run_bytes >= 16384);
    const u32 kDraws = 200000;
    u32 got[12] = {}, other = 0;
    for (u32 k = 0; k < kDraws; ++k) {
        Rng r(w.seed(), 0, kPurposeKiller, k);
        const u32 h = p.os.pick_by_bytes(r.next());
        u32 i = 0;
        while (i < 12 && holders[i] != h)
            ++i;
        if (i < 12)
            ++got[i];
        else
            ++other;
    }
    CHECK(other == 0);
    double worst = 0;
    for (u32 i = 0; i < 12; ++i) {
        const double share = static_cast<double>(bytes[i]) / static_cast<double>(total);
        const double want = share * kDraws, sd = sqrt(want * (1 - share));
        const double z = fabs(got[i] - want) / sd;
        worst = z > worst ? z : worst;
    }
    CHECK(worst < 4);
    // Ending the biggest: its records go, nothing else changes, not a byte.
    Array<Alloc> before;
    for (u32 id = 0; id < p.mem.record_capacity(); ++id)
        before.push(p.mem.record(id));
    Array<u8> image;
    image.resize(p.mem.size());
    memcpy(image.data(), p.mem.bytes(), p.mem.size());
    const u32 victim = holders[3];
    const u32 threads = p.os.live_threads();
    p.os.end_program(victim, DeathCause::oom);
    u32 freed = 0;
    for (u32 id = 0; id < p.mem.record_capacity(); ++id) {
        const Alloc& a = p.mem.record(id);
        if (before[id].holder == victim) {
            CHECK(a.holder == 0);
            ++freed;
        } else {
            CHECK(a.holder == before[id].holder && a.base == before[id].base && a.extent == before[id].extent);
        }
    }
    CHECK(freed == 2 && memcmp(image.data(), p.mem.bytes(), p.mem.size()) == 0);
    CHECK(p.os.live_threads() == threads - 1 && !p.os.program(victim).alive);
    CHECK(p.os.deaths.size() && p.os.deaths[p.os.deaths.size() - 1].cause == DeathCause::oom);
    printf("  guards: the killer's draws: 200,000 among 12 programs holding %llu bytes, each within %.1f standard "
           "deviations of its share, none of an idle run's %llu; ending one freed its 2 records alone and left "
           "every byte\n",
           static_cast<ull>(total), worst, static_cast<ull>(run_bytes));
}

struct Mine {
    u32 holder;
    u64 uid;
    u32 size, kind; // kind: 0 one thread, 1 three
    u32 at;
    u32 threads; // its live threads as a tick began
};

// Placed again at once whenever one is ended, of the same size and kind.
bool place_mine(Place& p, Mine& m) {
    m.holder = m.kind ? hand(p, kThree, sizeof kThree, m.size) : hand(p, kSleeper, sizeof kSleeper, m.size);
    if (!m.holder)
        return false;
    m.uid = p.os.program(m.holder).uid;
    m.at = p.mem.record(p.mem.first_of(m.holder)).base;
    return true;
}

// A place kept crowded: programs of three sizes, the biggest with three
// threads, each placed again when the killer ends it. With a spell of 5 it
// loses one at the fifth barrier it is crowded, and the next five barriers
// after; never one while it isn't. The ends by size match the bytes'
// shares; every thread of each ends, cause "out of memory", and its bytes
// are left as they were.
void test_spells() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, kOne, "os.killer = 5\n", &why));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 100;
    obs.init(os);
    w.watch(&obs);
    Place& p = w.place(0);
    memset(p.mem.bytes() + p.mem.region_base(), kFault, p.mem.region_size());
    const u32 line = p.mem.size() / 10;
    const u32 kSizes[3] = {512, 1024, 2048};
    Array<Mine> mine;
    for (u32 k = 0; p.mem.free_bytes() > line / 2; ++k) {
        Mine m{0, 0, kSizes[k % 3], k % 3 == 2 ? 1u : 0u, 0};
        if (!place_mine(p, m))
            break;
        mine.push(m);
    }
    CHECK(mine.size() > 12 && p.mem.free_bytes() < line);
    w.start_threads(1);
    u64 clock = 0, kills = 0, spells = 0, idle = 0, wrong = 0, three = 0;
    double want[3] = {}, var[3] = {};
    u32 got[3] = {};
    for (u32 t = 0; t < 3000; ++t) {
        // Each program's bytes and its share, as the barrier will see them.
        u64 total = 0;
        for (usize i = 0; i < mine.size(); ++i) {
            total += p.os.bytes_held(mine[i].holder);
            mine[i].threads = p.os.program(mine[i].holder).threads;
        }
        double share[3] = {};
        for (usize i = 0; i < mine.size(); ++i)
            share[mine[i].size == kSizes[0] ? 0 : (mine[i].size == kSizes[1] ? 1 : 2)] +=
                static_cast<double>(p.os.bytes_held(mine[i].holder)) / static_cast<double>(total);
        const u64 k0 = p.os.kills, b0 = p.os.killed_bytes, th0 = p.os.killed_threads;
        const u64 live0 = p.os.live_threads(), st0 = p.os.threads_started;
        Array<u8> image;
        image.resize(p.mem.size());
        memcpy(image.data(), p.mem.bytes(), p.mem.size());
        w.tick();
        const u64 killed = p.os.kills - k0;
        // The barrier's view: memory as the tick left it, before the end.
        const bool crowded = p.mem.free_bytes() - (p.os.killed_bytes - b0) < line;
        clock = crowded ? clock + 1 : 0;
        const bool due = clock == 5;
        if (due)
            clock = 0;
        wrong += killed != (due ? 1u : 0u) ? 1 : 0;
        idle += crowded ? 0 : 1;
        if (!killed)
            continue;
        ++kills;
        for (u32 c = 0; c < 3; ++c) {
            want[c] += share[c];
            var[c] += share[c] * (1 - share[c]);
        }
        // Which it was: all its threads gone, its bytes left, placed again.
        // The threads the killer ended are all that ended this tick (a
        // crowded place's SPAWN can find no room for a stack, so a program
        // of three may have fewer).
        const u64 ended = p.os.killed_threads - th0;
        CHECK(p.os.live_threads() + ended == live0 + (p.os.threads_started - st0));
        for (usize i = 0; i < mine.size(); ++i) {
            Mine& m = mine[i];
            if (p.os.program(m.holder).alive && p.os.program(m.holder).uid == m.uid)
                continue;
            ++got[m.size == kSizes[0] ? 0 : (m.size == kSizes[1] ? 1 : 2)];
            CHECK(ended >= m.threads && ended >= 1 && ended <= (m.kind ? 3u : 1u));
            three += ended == 3 ? 1 : 0;
            CHECK(memcmp(image.data() + m.at, p.mem.bytes() + m.at, m.size) == 0);
            CHECK(place_mine(p, m));
        }
        spells += p.os.crowded_for() == 0 ? 1 : 0;
    }
    CHECK(wrong == 0 && kills > 500 && spells == kills && idle == 0 && three > 50);
    double worst = 0;
    for (u32 c = 0; c < 3; ++c) {
        const double z = fabs(got[c] - want[c]) / sqrt(var[c]);
        worst = z > worst ? z : worst;
    }
    CHECK(worst < 4);
    u64 other = 0;
    for (u32 c = 0; c < kDeathCauses; ++c)
        other += c == static_cast<u32>(DeathCause::oom) ? 0 : obs.deaths_by_cause[c];
    CHECK(obs.deaths_by_cause[static_cast<u32>(DeathCause::oom)] == kills && other == 0);
    printf("  guards: a place kept crowded by %u programs lost one at its fifth crowded barrier and every fifth "
           "after, %llu in 3,000 ticks, none out of turn; by size %u, %u and %u where their shares of the bytes "
           "gave %.1f, %.1f and %.1f; each with all its threads (%llu with three), cause out of memory, its "
           "bytes left\n",
           static_cast<u32>(mine.size()), static_cast<ull>(kills), got[0], got[1], got[2], want[0], want[1],
           want[2], static_cast<ull>(three));
}

// An uncrowded place loses nothing, however long; and with the killer off
// a crowded one loses nothing either.
void test_uncrowded() {
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, kOne, pass ? "os.killer = 0\n" : "os.killer = 5\n", &why));
        Place& p = w.place(0);
        memset(p.mem.bytes() + p.mem.region_base(), kFault, p.mem.region_size());
        const u32 line = p.mem.size() / 10;
        u32 n = 0;
        // Uncrowded: just over the line free. Crowded: well under.
        while (p.mem.free_bytes() > (pass ? line / 2 : line + 1200) && hand(p, kSleeper, sizeof kSleeper, 512))
            ++n;
        CHECK(n > 10 && (pass ? p.mem.free_bytes() < line : p.mem.free_bytes() >= line));
        w.start_threads(1);
        for (u32 t = 0; t < 500; ++t)
            w.tick();
        CHECK(p.os.kills == 0 && p.os.live_programs() == n && p.os.crowded_for() == 0);
    }
    printf("  guards: a place just over the line ran 500 ticks and lost nothing; with the killer off a crowded "
           "one lost nothing\n");
}

const char* const kLand = "world.places = 12\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n";

bool has_tag(const ByteWriter& b, u32 tag) {
    for (usize i = 0; i + 4 <= b.size(); ++i)
        if (memcmp(b.data() + i, &tag, 4) == 0)
            return true;
    return false;
}

struct Ran {
    u64 hash = 0, mid = 0;
    u64 kills = 0, faults = 0, handled = 0, oom = 0, dog = 0;
    u64 crowded = 0, counting = 0; // at the snapshot: places part way through a spell, threads part way through a count
};

// Replicators and traffic at every place, crowding them, and a spinner
// with a handler at each lax one: the killer at a spell of 30, the watchdog
// at 20,000 cycles.
Ran run(const char* extra, u32 ticks, u32 threads, ByteWriter* saved = nullptr, u32 save_at = 0, bool from = false) {
    Ran r;
    World w;
    Config cfg;
    TextBuf why;
    if (from) {
        SnapshotHeader h;
        TextBuf err;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved->data(), saved->size(), &h, &err) && snapshot_config(h, &cfg, &pe));
        CHECK(w.init(cfg, &why));
        ByteReader in(saved->data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
    } else {
        CHECK(build(w, cfg, kLand, extra, &why));
        for (u32 i = 0; i < w.place_count(); ++i) {
            Place& p = w.place(i);
            if (!p.mem.strict())
                memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
            place_traffic(p, w.place_count(), i);
            if (!p.mem.strict()) // its handler writes into its own code
                CHECK(hand(p, kHandled, sizeof kHandled, 64) != 0);
            // Sleepers fill it to just over the line, for the replicators
            // and arrivals to push it under.
            while (p.mem.free_bytes() > p.mem.size() / 10 + 1500 && hand(p, kSleeper, sizeof kSleeper, 512)) {
            }
        }
    }
    Observer obs;
    ObserverSettings os;
    os.sample_every = 100;
    obs.init(os);
    if (!from)
        w.watch(&obs);
    w.start_threads(threads);
    while (w.tick_count() < ticks) {
        w.tick();
        if (saved && !from && w.tick_count() == save_at) {
            r.mid = w.hash();
            TextBuf text;
            cfg.write(text);
            snapshot_begin(w.seed(), w.tick_count(), text.view(), *saved);
            w.save(*saved);
            snapshot_finish(*saved);
            for (u32 i = 0; i < w.place_count(); ++i) {
                const Os& o = w.place(i).os;
                r.crowded += o.crowded_for() ? 1 : 0;
                for (u32 t = 0; t < o.thread_top(); ++t)
                    r.counting += o.thread(t).state != ThreadState::free && o.thread(t).quiet16 ? 1 : 0;
            }
        }
    }
    r.hash = w.hash();
    for (u32 i = 0; i < w.place_count(); ++i) {
        r.kills += w.place(i).os.kills;
        r.faults += w.place(i).os.watchdog_faults;
        r.handled += w.place(i).os.watchdog_handled;
    }
    r.oom = obs.deaths_by_cause[static_cast<u32>(DeathCause::oom)];
    r.dog = obs.deaths_by_cause[static_cast<u32>(DeathCause::watchdog)];
    return r;
}

// The same on 1 host thread and 3, and from a snapshot taken part way
// through spells and counts. Each rule at 0 saves nothing of its own, and
// with both on the save has both sections.
void test_same() {
    const char* const kOn = "os.killer = 30\nos.watchdog = 20000\n";
    const u32 ticks = 600, at = 270;
    ByteWriter saved;
    const Ran a = run(kOn, ticks, 1, &saved, at);
    const Ran b = run(kOn, ticks, 3);
    const Ran c = run(kOn, ticks, 2, &saved, at, true);
    CHECK_EQ_U64(a.hash, b.hash);
    CHECK_EQ_U64(a.hash, c.hash);
    CHECK(a.kills > 20 && a.oom == a.kills && a.faults > 50 && a.handled > 0 && a.crowded > 0 && a.counting > 0);
    CHECK(has_tag(saved, 0x4F4F4D31) && has_tag(saved, 0x57444731)); // "OOM1", "WDG1"
    const u32 kTags[2] = {0x4F4F4D31, 0x57444731};
    const char* const kOff[3] = {"os.killer = 0\nos.watchdog = 20000\n", "os.killer = 30\nos.watchdog = 0\n",
                                 "os.killer = 0\nos.watchdog = 0\n"};
    for (u32 k = 0; k < 3; ++k) {
        ByteWriter s;
        const Ran d = run(kOff[k], 120, 2, &s, 100);
        CHECK(has_tag(s, kTags[0]) == (k == 1) && has_tag(s, kTags[1]) == (k == 0));
        CHECK((d.kills == 0) == (k != 1) && (d.faults == 0) == (k != 0));
    }
    printf("  guards: 12 places crowded by replicators, a spinner with a handler at each lax one, 600 ticks: %llu "
           "programs ended by the killer, %llu watchdog faults (%llu taken by a handler); the same on 1 host thread "
           "and 3 and from a snapshot of tick %u with %llu places part way through a spell and %llu threads part "
           "way through a count; each rule at 0 saves nothing of its own\n",
           static_cast<ull>(a.kills), static_cast<ull>(a.faults), static_cast<ull>(a.handled), at,
           static_cast<ull>(a.crowded), static_cast<ull>(a.counting));
}

} // namespace

void suite_guards() {
    test_exact();
    test_calls();
    test_handled();
    test_draws();
    test_spells();
    test_uncrowded();
    test_same();
}
