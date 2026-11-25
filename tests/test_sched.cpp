// The scheduler (nova2/sched.h; design.md 9.5; build-plan-gate1.md step 6):
// exact cycle budgets, fair round robin, strict pins, clocks, overruns as
// debt, sleeps, timers, waits, yields, SENSE's turn time, and a hand-placed
// replicator filling a place, the same way twice.
#include <cstring>

#include "check.h"
#include "nova2/hash.h"
#include "nova2/sched.h"

using namespace nova2;

namespace {

constexpr u32 RUN = 1;
constexpr u32 kLoopCost16 = 4 * 16 + 16; // JMP # 0 from fast memory: fetch + work

struct World {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Os os;
    Scheduler sched;
    CostTable p = default_p_costs(), e = default_e_costs();
    u32 ncores;
    u32 last_at = 0; // where place() put the last program

    explicit World(u32 n = 1, DeviceKind second = DeviceKind::e_core, u32 mem = 16384) : ncores(n) {
        static const MemDeviceSpec d16[] = {{16384, 4 * 16, 900}};
        static const MemDeviceSpec d32[] = {{32768, 4 * 16, 900}};
        MemorySpec ms;
        ms.page_size = 256;
        ms.region_size = 256;
        ms.devices = mem == 16384 ? d16 : d32;
        ms.device_count = 1;
        CHECK(m.init(ms, nullptr));
        cores[0].kind = DeviceKind::p_core;
        cores[0].stock_speed = cores[0].speed = 25000;
        cores[1].kind = second;
        cores[1].stock_speed = cores[1].speed = second == DeviceKind::e_core ? 18000 : 25000;
        env.tick = 100;
        OsSettings os_s;
        CHECK(os.init(&m, cores, n, &env, os_s, nullptr));
        SchedSettings ss;
        CHECK(sched.init(&os, &m, cores, n, &env, ss, &p, &e, nullptr));
    }

    // A hand-placed program running `code` (R7: tests only). Its thread id.
    u32 place(const u8* code, u32 n, u32 size = 64) {
        const u32 at = m.alloc(RUN, size, 2, 0);
        CHECK(at != 0);
        memcpy(m.bytes() + at, code, n);
        last_at = at;
        return os.place_by_hand(RUN, at, at, 0);
    }

    u32 index_of(u32 id) const {
        for (u32 t = 0; t < os.thread_capacity(); ++t)
            if (os.thread(t).state != ThreadState::free && os.thread(t).id == id)
                return t;
        return ~0u;
    }

    // A thread's A register, wherever it is.
    u32 reg_a(u32 id) {
        const u32 t = index_of(id);
        const Thread& th = os.thread(t);
        if (th.core)
            return cores[th.core - 1].x.r.a;
        const u8* s = m.bytes() + th.stack;
        return static_cast<u32>(s[0]) | static_cast<u32>(s[1]) << 8 | static_cast<u32>(s[2]) << 16 |
               static_cast<u32>(s[3]) << 24;
    }

    void ticks(u32 n) {
        for (u32 i = 0; i < n; ++i) {
            sched.tick();
            ++env.tick;
        }
    }
};

const u8 kSpin[2] = {encode(kJmp, kImm), 0};                                  // JMP # 0
const u8 kCount[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)}; // A += 1, forever

// One thread alone on a P core gets exactly its quanta's cycles: the first
// tick less one switch.
void test_budget() {
    World w;
    w.place(kSpin, 2);
    w.ticks(1);
    const i64 tick16 = 25000ll * 16;
    const i64 first = (tick16 - 1000 * 16) / kLoopCost16;
    CHECK(static_cast<i64>(w.sched.instructions) >= first && static_cast<i64>(w.sched.instructions) <= first + 1);
    w.ticks(1); // alone, its turn just renews: no more switches
    const i64 two = (2 * tick16 - 1000 * 16) / kLoopCost16;
    CHECK(static_cast<i64>(w.sched.instructions) >= two && static_cast<i64>(w.sched.instructions) <= two + 1);
    CHECK(w.sched.switches == 1);
}

// Two threads on one core share it fairly, a turn of four quanta each.
void test_fair_share() {
    World w;
    const u32 a = w.place(kCount, 4);
    const u32 b = w.place(kCount, 4);
    w.ticks(8);
    const double ra = w.reg_a(a), rb = w.reg_a(b);
    CHECK(ra > 1000 && rb > 1000);
    CHECK(ra / rb > 0.97 && ra / rb < 1.03);
    CHECK(w.sched.switches >= 8 * 16 / 4 - 2); // a switch every turn
}

// A pinned thread waits for its kind of core; an unpinned one goes anywhere.
void test_pins() {
    World w(2); // P and E
    const u32 pinned = w.place(kCount, 4);
    const u32 free_t = w.place(kCount, 4);
    w.os.thread(w.index_of(pinned)).pin = kPinE;
    while (w.os.pick(DeviceKind::p_core) != ~0u) // requeue both under their pins
        ;
    w.os.make_ready(w.index_of(pinned));
    w.os.make_ready(w.index_of(free_t));
    bool only_e = true;
    for (u32 r = 0; r < 64; ++r) {
        if (r % 16 == 0)
            w.sched.begin_tick();
        w.sched.run_round();
        const Thread& th = w.os.thread(w.index_of(pinned));
        if (th.state == ThreadState::running)
            only_e = only_e && th.core == 2;
    }
    CHECK(only_e && w.reg_a(pinned) > 0 && w.reg_a(free_t) > 0);

    // Strict: two threads pinned to P with one P core; the E core stays idle.
    World s(2);
    const u32 p1 = s.place(kCount, 4);
    const u32 p2 = s.place(kCount, 4);
    s.os.thread(s.index_of(p1)).pin = kPinP;
    s.os.thread(s.index_of(p2)).pin = kPinP;
    Os& os = s.os;
    while (os.pick(DeviceKind::p_core) != ~0u)
        ;
    os.make_ready(s.index_of(p1));
    os.make_ready(s.index_of(p2));
    bool e_idle = true;
    for (u32 r = 0; r < 64; ++r) {
        s.sched.run_round();
        e_idle = e_idle && s.cores[1].thread == 0;
    }
    CHECK(e_idle && s.reg_a(p1) > 0 && s.reg_a(p2) > 0);
    // A pin to a kind with no working core falls back to any.
    s.cores[0].working = false;
    s.sched.begin_tick();
    bool ran_on_e = false;
    for (u32 r = 0; r < 32; ++r) {
        s.sched.run_round();
        ran_on_e = ran_on_e || s.cores[1].thread != 0;
    }
    CHECK(ran_on_e);
}

// The clock: 1.5 times stock gets three times the cycles of half stock.
void test_clock() {
    u64 got[2] = {};
    const u8 levels[2] = {15, 5};
    for (int i = 0; i < 2; ++i) {
        World w;
        const u32 id = w.place(kSpin, 2);
        w.os.thread(w.index_of(id)).clock = levels[i];
        w.ticks(4);
        got[i] = w.sched.instructions;
    }
    const double ratio = static_cast<double>(got[0]) / static_cast<double>(got[1]);
    CHECK(ratio > 2.9 && ratio < 3.1);
}

// A long CPY runs whole, and the overrun is repaid from the next quanta.
void test_overrun() {
    World w(1, DeviceKind::e_core, 32768);
    const u8 code[5] = {encode(kCpy, kC), encode(kJmp, kImm), 0};
    const u32 id = w.place(code, 3);
    const u32 src = w.m.alloc(RUN, 4000, 0, 0), dst = w.m.alloc(RUN, 4000, 0, 0);
    // Hand the buffers to the program so it may use them.
    const u32 t = w.index_of(id);
    const u32 holder = w.os.thread(t).program;
    w.m.give(w.m.alloc_at(src), holder);
    w.m.give(w.m.alloc_at(dst), holder);
    u8* saved = w.m.bytes() + w.os.thread(t).stack;
    const u32 regs[3] = {src, 4000, dst}; // B, C, D
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k)
            saved[4 + r * 4 + k] = static_cast<u8>(regs[r] >> (8 * k));
    w.ticks(1);
    const i64 cpy = 4 * 16 + 16 + 64 + 32 * 4000 + 4 * 16 + 4 * 16;
    const i64 rest = (25000ll * 16 - 1000 * 16 - cpy) / kLoopCost16;
    const i64 got = static_cast<i64>(w.sched.instructions) - 1;
    CHECK(got >= rest - 1 && got <= rest + 1);
    CHECK(memcmp(w.m.bytes() + src, w.m.bytes() + dst, 4000) == 0);
}

// SLEEP n wakes n ticks later; SLEEP 0 waits for an interrupt; a timer
// handler runs every period and HRETs back.
void test_sleep_and_timers() {
    World w;
    const u8 sleeper[8] = {encode(kMov, kImm), 3, encode(kSys, kImm), kSleep, encode(kAdd, kImm), 1,
                           encode(kJmp, kImm), static_cast<u8>(-2)};
    const u32 s = w.place(sleeper, 8);
    w.ticks(1); // tick 100: it sleeps until 103
    const Thread& th = w.os.thread(w.index_of(s));
    CHECK(th.state == ThreadState::sleeping && th.wake_tick == 103 && w.reg_a(s) == 3);
    w.ticks(2); // 101, 102: still asleep
    CHECK(th.state == ThreadState::sleeping);
    w.ticks(1); // 103: awake and counting
    CHECK(w.reg_a(s) > 3);

    // A timer every 2 ticks. HRET restores the registers, so the handler
    // counts in memory: the byte at offset 30, through D. The main loop counts in A.
    World t;
    u8 code[32] = {};
    const u8 setup[] = {encode(kMov, kIp),  encode(kAdd, kImm), 30, encode(kSto, kD), // D = the counter
                        encode(kMov, kIp),  encode(kAdd, kImm), 16, encode(kSto, kB), // B = the handler, at 20
                        encode(kMov, kImm), 2, encode(kSto, kC),                       // C = a period of 2
                        encode(kSys, kImm), kSetHandler,                              // A = 2: a timer
                        encode(kMov, kImm), 0, encode(kAdd, kImm), 1, encode(kJmp, kImm),
                        static_cast<u8>(-2)}; // 13..18: count in A
    memcpy(code, setup, sizeof setup);
    const u8 handler[] = {encode(kMov, kAtD), encode(kAdd, kImm), 1, encode(kSto, kAtD), encode(kSys, kImm), kHret};
    memcpy(code + 20, handler, sizeof handler);
    const u32 id = t.place(code, 32);
    const u32 at = t.last_at;
    t.ticks(11); // ticks 100..110: it fires at 102, 104, ..., 110
    const Thread& tt = t.os.thread(t.index_of(id));
    const u32 fired = t.m.bytes()[at + 30];
    CHECK(fired >= 4 && fired <= 5);
    CHECK(t.reg_a(id) > 1000 && !tt.in_handler);

    // SLEEP 0 waits for that interrupt, then goes on after the SLEEP.
    World z;
    u8 code2[32] = {};
    const u8 setup2[] = {encode(kMov, kIp), encode(kAdd, kImm), 20, encode(kSto, kB), encode(kMov, kImm), 2,
                         encode(kSto, kC), encode(kSys, kImm), kSetHandler, encode(kMov, kImm), 0,
                         encode(kSys, kImm), kSleep, encode(kMov, kImm), 77, encode(kJmp, kImm), 0};
    memcpy(code2, setup2, sizeof setup2);
    const u8 h2[] = {encode(kSys, kImm), kHret};
    memcpy(code2 + 20, h2, sizeof h2);
    const u32 zid = z.place(code2, 32);
    z.ticks(1);
    CHECK(z.os.thread(z.index_of(zid)).state == ThreadState::waiting_interrupt);
    z.ticks(2); // the timer fires at 102
    CHECK(z.reg_a(zid) == 77);
}

// RECV with a timeout waits, then finds nothing: A = 0, B = -1.
void test_recv_timeout() {
    World w;
    const u8 code[] = {encode(kMov, kImm), 2, encode(kSto, kD), encode(kMov, kImm), 8, encode(kSto, kC),
                       encode(kMov, kImm), 3, encode(kSys, kImm), kRecv, encode(kSto, kC), encode(kJmp, kImm), 0};
    const u32 id = w.place(code, sizeof code);
    w.ticks(1);
    CHECK(w.os.thread(w.index_of(id)).state == ThreadState::waiting_recv);
    w.ticks(2);
    const Thread& th = w.os.thread(w.index_of(id));
    CHECK(th.state == ThreadState::running && th.core == 1);
    CHECK(w.cores[0].x.r.a == 0 && w.cores[0].x.r.b == static_cast<u32>(-1) && w.cores[0].x.r.c == 0);
}

// A thread that yields all the time gets far less than one that doesn't.
void test_yield() {
    World w;
    const u8 yielder[] = {encode(kMov, kImm), static_cast<u8>(-1), encode(kSys, kImm), kSleep,
                          encode(kJmp, kImm), static_cast<u8>(-4)};
    w.place(yielder, sizeof yielder);
    const u32 worker = w.place(kCount, 4);
    w.ticks(4);
    // The worker gets well over half of 4 ticks of a 25,000-cycle core (10 cycles a count),
    // even after paying a switch each time the yielder takes and gives back the core.
    CHECK(w.reg_a(worker) > 5000);
}

// SENSE reads what's left of the turn: at most four quanta of cycles.
void test_turn_left() {
    World w;
    const u8 code[] = {encode(kMov, kImm), kTurnLeft, encode(kSys, kImm), kSense, encode(kJmp, kImm), 0};
    const u32 id = w.place(code, sizeof code);
    w.ticks(1);
    const u32 left = w.reg_a(id);
    CHECK(left > 4000 && left <= 4 * 25000 / 16);
}

// A hand-placed replicator fills a two-core place under the scheduler, and
// the same start gives the same memory.
u64 replicate_once(u32* births) {
    World w(2, DeviceKind::e_core, 32768);
    static const u8 lax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                               encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                               encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
    const u32 at = w.m.alloc(RUN, 14, 2, 0);
    memcpy(w.m.bytes() + at, lax, 14);
    w.os.place_by_hand(RUN, at, at, 0);
    w.ticks(60);
    *births = static_cast<u32>(w.os.births.size());
    TextBuf why;
    CHECK(w.m.check(&why));
    return hash_bytes(w.m.bytes(), w.m.size());
}

void test_replicator_fills_a_place() {
    u32 b1 = 0, b2 = 0;
    const u64 h1 = replicate_once(&b1);
    const u64 h2 = replicate_once(&b2);
    CHECK(b1 > 20 && b1 == b2 && h1 == h2);
    printf("  sched: a replicator made %u copies in 60 ticks\n", b1);
}

// Each core's busy time, for its draw (power.h): none when it has nothing to
// run, the whole tick when a thread spins on it, weighted by the thread's
// clock, and the time until a thread sleeps.
void test_busy_time() {
    {
        World w(1);
        w.ticks(1);
        CHECK(w.sched.busy16(0) == 0 && w.sched.work10(0) == 0); // halted all tick (no idle hook)
    }
    {
        World w(1);
        w.place(kSpin, 2);
        w.ticks(2);
        const i64 T = w.sched.quantum16(0) * w.sched.settings().quanta_per_tick;
        CHECK(w.sched.busy16(0) == T && w.sched.work10(0) == T * 10);
    }
    {
        World w(1);
        const u32 id = w.place(kSpin, 2);
        w.os.thread(w.index_of(id)).clock = 15;
        w.ticks(2); // the second tick has no switch: all of it at 1.5 times stock
        const i64 T = w.sched.quantum16(0) * w.sched.settings().quanta_per_tick;
        CHECK(w.sched.busy16(0) == T && w.sched.work10(0) == T * 15);
    }
    {
        // SLEEP 5, then spin: the first tick it runs a switch and one call,
        // then the core is idle until the thread wakes.
        World w(1);
        const u8 code[6] = {encode(kMov, kImm), 5, encode(kSys, kImm), kSleep, encode(kJmp, kImm), 0};
        w.place(code, 6);
        w.ticks(1);
        const i64 T = w.sched.quantum16(0) * w.sched.settings().quanta_per_tick;
        CHECK(w.sched.busy16(0) > 0 && w.sched.busy16(0) < T / 16 && w.sched.work10(0) == w.sched.busy16(0) * 10);
        w.ticks(1);
        CHECK(w.sched.busy16(0) == 0); // asleep all tick
    }
}

} // namespace

// A tick's start passes over a group of thread records with nothing due
// (Os::due_min; pass 13). Two hundred threads, a group of records each
// sleeping for 100 to 127 ticks, waiting on timers of 1 to 7, and sleeping
// for 1 to 23, some ended and others started as they
// go: each tick the OS's check holds every group's least due to its
// threads', so a group passed over has no thread a walk of them all would
// have woken; and groups are passed over, and threads woken, throughout.
void test_due_groups() {
    World w(2, DeviceKind::p_core, 32768);
    u8 sleeper[6] = {encode(kMov, kImm), 0, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
    u8 waiter[32] = {encode(kMov, kIp), encode(kAdd, kImm), 20, encode(kSto, kB), encode(kMov, kImm), 0,
                     encode(kSto, kC), encode(kMov, kImm), 2, encode(kSys, kImm), kSetHandler,
                     encode(kMov, kImm), 0, encode(kSys, kImm), kSleep, encode(kJmp, kImm),
                     static_cast<u8>(-4)};
    waiter[20] = encode(kSys, kImm);
    waiter[21] = kHret;
    u32 ids[200];
    for (u32 i = 0; i < 200; ++i) {
        if (i / 64 == 1) { // a group of records for each kind
            waiter[5] = static_cast<u8>(1 + i % 7);
            ids[i] = w.place(waiter, 32);
        } else {
            sleeper[1] = static_cast<u8>(i / 64 == 2 ? 1 + i % 23 : 100 + i % 28); // under 128: an immediate is signed
            ids[i] = w.place(sleeper, 6);
        }
        CHECK(ids[i] != 0);
    }
    u32 passed = 0, woken = 0, broken = 0;
    for (u32 k = 0; k < 400; ++k) {
        const u64 now = w.env.tick;
        const u8* attention = w.os.attention();
        const u64* due = w.os.due();
        const u64* least = w.os.due_min();
        for (u32 g = 0; g << Os::kDueGroupShift < w.os.thread_top(); ++g) {
            u32 waiting = 0;
            for (u32 t = g << Os::kDueGroupShift; t < (g + 1) << Os::kDueGroupShift && t < w.os.thread_top(); ++t) {
                waiting += attention[t] != 0;
                woken += attention[t] && due[t] <= now;
            }
            passed += waiting && least[g] > now;
        }
        if (k % 9 == 4) { // one ends, and another starts in its place
            const u32 t = w.index_of(ids[k % 200]);
            if (t != ~0u && !w.os.thread(t).core) {
                w.os.end_thread(t, DeathCause::exit);
                ids[k % 200] = w.place(sleeper, 6);
            }
        }
        w.ticks(1);
        TextBuf why;
        broken += !w.os.check(&why);
    }
    printf("  due groups: %u groups with waiting threads passed over in 400 ticks, %u threads woken or timed\n",
           passed, woken);
    CHECK(broken == 0 && passed > 400 && woken > 400);
}

void suite_sched() {
    test_budget();
    test_fair_share();
    test_pins();
    test_clock();
    test_overrun();
    test_sleep_and_timers();
    test_recv_timeout();
    test_yield();
    test_turn_left();
    test_replicator_fills_a_place();
    test_busy_time();
    test_due_groups();
}
