// The low-voltage interrupt (design.md 9.5, 12.4; build-plan-gate4.md step 7
// and section 2, gap 6): through a slow dip a thread whose handler sleeps
// survives the cut-out, and its twin with no handler ends; the interrupt
// fires once a dip, and again for the next; nothing asleep or waiting is
// woken by it; a drop too fast to warn of ends both; the margin at 0 turns
// it off; and the counters the observers read.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 1\n"
                           "world.map = none\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 1000000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 2\n"
                           "hw.e_share = 0\n";

bool build(World& w, Config& cfg, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(Str(kSmall, strlen(kSmall)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    return w.init(cfg, why);
}

// Counts in A as it runs: ADD # 1; JMP back.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// Sets a low-voltage handler (B = its start + 12, A = 3), then counts in A.
// The handler sleeps 60 ticks and returns.
const u8 kGuarded[18] = {encode(kMov, kB),   encode(kAdd, kImm), 12,     encode(kSto, kB),
                         encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                         encode(kAdd, kImm), 1,                  encode(kJmp, kImm), static_cast<u8>(-2),
                         encode(kMov, kImm), 60,                 encode(kSys, kImm), kSleep,
                         encode(kSys, kImm), kHret};
// Sets a low-voltage handler (its start + 16), then sleeps 10,000 ticks at
// a time. Its handler only returns.
const u8 kSleeper[18] = {encode(kMov, kB),   encode(kAdd, kImm), 16,     encode(kSto, kB),
                         encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                         encode(kMov, kImm), 100,                encode(kMul, kImm), 100,
                         encode(kSys, kImm), kSleep,             encode(kJmp, kImm), static_cast<u8>(-6),
                         encode(kSys, kImm), kHret};
// Sets a low-voltage handler (its start + 14), then waits for an interrupt
// (SLEEP 0) for ever.
const u8 kWaiter[16] = {encode(kMov, kB),   encode(kAdd, kImm), 14,     encode(kSto, kB),
                        encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4), encode(kSys, kImm), kHret};

u32 place_program(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 128, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    CHECK(false);
    return 0;
}

// A place whose voltage is given by hand, a tick at a time.
void run_at(Place& p, i32 mv) {
    p.supplied = true;
    p.given_mv = mv;
    p.os.clear_facts();
    p.tick();
}

// A dip: down 5 mV a tick from 1,000 to 800, 30 ticks there, and back up.
// Calls `each` after every tick.
struct Seen {
    u32 guarded, counter, sleeper, waiter;
    bool sleeper_slept = true, waiter_waited = true;
    u64 sleeper_wake = 0;
};

void watch(Place& p, Seen& s) {
    const Thread& sl = p.os.thread(s.sleeper);
    const Thread& wa = p.os.thread(s.waiter);
    s.sleeper_slept = s.sleeper_slept && sl.state == ThreadState::sleeping && sl.wake_tick == s.sleeper_wake &&
                      !sl.in_handler && (sl.pending & kPendingKinds) == 0;
    s.waiter_waited = s.waiter_waited && wa.state == ThreadState::waiting_interrupt && !wa.in_handler &&
                      (wa.pending & kPendingKinds) == 0;
}

void dip(Place& p, Seen& s) {
    for (i32 mv = 995; mv >= 800; mv -= 5) {
        run_at(p, mv);
        watch(p, s);
    }
    for (u32 k = 0; k < 30; ++k) {
        run_at(p, 800);
        watch(p, s);
    }
    for (i32 mv = 805; mv <= 1000; mv += 5) {
        run_at(p, mv);
        watch(p, s);
    }
}

void test_slow_dip() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    CHECK(p.core_count() == 2 && p.sched.settings().low_voltage_margin == 30);
    Seen s;
    s.guarded = place_program(p, kGuarded, sizeof kGuarded);
    s.sleeper = place_program(p, kSleeper, sizeof kSleeper);
    s.waiter = place_program(p, kWaiter, sizeof kWaiter);
    for (u32 k = 0; k < 20; ++k)
        run_at(p, 1000);
    // Three handlers registered; the sleeper asleep, the waiter waiting,
    // the guarded one counting, and nothing raised at 1,000 mV.
    CHECK(p.os.lv_handlers == 3 && p.sched.lv_raised == 0 && p.sched.lv_taken == 0);
    CHECK(p.os.thread(s.sleeper).state == ThreadState::sleeping);
    CHECK(p.os.thread(s.waiter).state == ThreadState::waiting_interrupt);
    CHECK(p.os.thread(s.guarded).core != 0 && p.os.thread(s.guarded).handler[kOnLowVoltage] != 0);
    s.sleeper_wake = p.os.thread(s.sleeper).wake_tick;
    // The twin with no handler joins it on the other core.
    s.counter = place_program(p, kCounter, sizeof kCounter);
    for (u32 k = 0; k < 5; ++k)
        run_at(p, 1000);
    CHECK(p.os.thread(s.counter).core != 0 && p.os.thread(s.guarded).core != 0);
    const u64 counter_uid = p.os.program(p.os.thread(s.counter).program).uid;

    // The first dip, a tick at a time down to the cut-out.
    u32 raised_at_mv = 0, slept_at_mv = 0, cut_at_mv = 0;
    bool counter_cut = false;
    for (i32 mv = 995; mv >= 800; mv -= 5) {
        run_at(p, mv);
        watch(p, s);
        if (!raised_at_mv && p.sched.lv_raised)
            raised_at_mv = static_cast<u32>(mv);
        if (!slept_at_mv && p.os.thread(s.guarded).state == ThreadState::sleeping)
            slept_at_mv = static_cast<u32>(mv);
        if (!cut_at_mv && !p.cores[0].working) {
            cut_at_mv = static_cast<u32>(mv);
            for (usize i = 0; i < p.os.deaths.size(); ++i)
                counter_cut = counter_cut ||
                              (p.os.deaths[i].cause == DeathCause::cut_out && p.os.deaths[i].uid == counter_uid);
        }
    }
    // Raised the first tick under 880 mV (850 + the margin), taken at once,
    // and the handler put the thread to sleep in that tick; the cut-out
    // came under 850, and ended the twin.
    CHECK(raised_at_mv == 875 && slept_at_mv == 875 && cut_at_mv == 845);
    CHECK(p.sched.lv_raised == 1 && p.sched.lv_taken == 1 && p.lv_slept == 1);
    CHECK(counter_cut && p.os.thread(s.counter).state == ThreadState::free && p.cut_threads == 1);
    CHECK(p.os.thread(s.guarded).state == ThreadState::sleeping && p.os.thread(s.guarded).in_handler);
    CHECK((p.os.thread(s.guarded).pending & kLowVoltageTaken) != 0);
    // The rest of the dip: it wakes with the voltage still low, and isn't
    // interrupted again while the dip lasts.
    for (u32 k = 0; k < 30; ++k) {
        run_at(p, 800);
        watch(p, s);
    }
    for (i32 mv = 805; mv <= 1000; mv += 5) {
        run_at(p, mv);
        watch(p, s);
    }
    for (u32 k = 0; k < 30; ++k) {
        run_at(p, 1000);
        watch(p, s);
    }
    const Thread& g = p.os.thread(s.guarded);
    CHECK(g.state != ThreadState::free && g.core != 0 && !g.in_handler);
    CHECK(p.sched.lv_raised == 1 && p.sched.lv_taken == 1 && p.lv_slept == 1 && p.lv_outlived == 1);
    CHECK((g.pending & kLowVoltageTaken) == 0); // back above the margin: the latch is clear
    CHECK(p.cut_threads == 1 && p.cutouts[0] == 2);
    // It went on counting where it stopped: A is far above what the handler
    // left it at.
    CHECK(p.cores[g.core - 1].x.r.a > 1000);

    // The next dip: it fires again, once.
    dip(p, s);
    for (u32 k = 0; k < 30; ++k) {
        run_at(p, 1000);
        watch(p, s);
    }
    CHECK(p.sched.lv_raised == 2 && p.sched.lv_taken == 2 && p.lv_slept == 2 && p.lv_outlived == 2);
    CHECK(p.os.thread(s.guarded).state != ThreadState::free && p.os.thread(s.guarded).core != 0);
    CHECK(p.cut_threads == 1 && p.cutouts[0] == 4);
    // Nothing asleep or waiting was woken by either dip.
    CHECK(s.sleeper_slept && s.waiter_waited);
    printf("  lowv: a slow dip, 5 mV a tick: the interrupt was raised at %u mV (the P cores' 850 + the margin of "
           "30), its handler slept, the cores cut out at %u mV and the twin with no handler ended; the thread "
           "woke and went on, and the next dip raised it once more (%llu raised, %llu taken, %llu slept within a "
           "tick, %llu outlived a cut-out); a sleeping thread and one waiting for an interrupt, both with "
           "handlers, never stirred\n",
           raised_at_mv, cut_at_mv, static_cast<unsigned long long>(p.sched.lv_raised),
           static_cast<unsigned long long>(p.sched.lv_taken), static_cast<unsigned long long>(p.lv_slept),
           static_cast<unsigned long long>(p.lv_outlived));
}

// A drop straight to under the threshold gives no warning: the core is cut
// out before a quantum starts, and the thread ends, handler or no.
void test_fast_drop() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const u32 g = place_program(p, kGuarded, sizeof kGuarded);
    for (u32 k = 0; k < 20; ++k)
        run_at(p, 1000);
    CHECK(p.os.thread(g).core != 0);
    run_at(p, 800);
    CHECK(p.os.thread(g).state == ThreadState::free && p.cut_threads == 1 && p.sched.lv_raised == 0);
}

// With the margin at 0 it's never raised.
void test_off() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "power.low_voltage_margin = 0\n", &why));
    Place& p = w.place(0);
    const u32 g = place_program(p, kGuarded, sizeof kGuarded);
    for (u32 k = 0; k < 20; ++k)
        run_at(p, 1000);
    for (i32 mv = 995; mv >= 800; mv -= 5)
        run_at(p, mv);
    CHECK(p.sched.lv_raised == 0 && p.os.thread(g).state == ThreadState::free && p.cut_threads == 1);
    CHECK(p.os.lv_handlers == 1);
}

// A handler set after the dip began still gets its one interrupt, and a
// handler cleared before it's delivered gets none.
void test_margins() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "power.low_voltage_margin = 100\n", &why));
    Place& p = w.place(0);
    for (u32 k = 0; k < 5; ++k)
        run_at(p, 940); // inside the wider margin from the start: 850 + 100
    const u32 g = place_program(p, kGuarded, sizeof kGuarded);
    for (u32 k = 0; k < 3; ++k)
        run_at(p, 940);
    // It set its handler in its first quantum; the next quantum raised it.
    CHECK(p.sched.lv_raised == 1 && p.sched.lv_taken == 1);
    CHECK(p.os.thread(g).state == ThreadState::sleeping);
    for (u32 k = 0; k < 100; ++k)
        run_at(p, 940);
    // Awake again in the same dip: no second interrupt.
    CHECK(p.os.thread(g).core != 0 && p.sched.lv_raised == 1);
    // What the handler finds in A is the voltage: the frame kept the count,
    // so look at the kind's argument through a fresh raise instead.
    run_at(p, 1000);
    run_at(p, 940);
    CHECK(p.sched.lv_raised == 2 && p.sched.lv_taken == 2);
}

} // namespace

void suite_lowv() {
    test_slow_dip();
    test_fast_drop();
    test_off();
    test_margins();
}
