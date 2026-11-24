#pragma once

// The scheduler (design.md 9.5; build-plan-gate1.md step 6): one place's
// tick, as rounds of rotating quanta.
//
// - A tick is a fixed span of simulated time, split into rounds. In each
//   round every core runs for one quantum of time, cores taken in index order
//   rotated by one each round, so no core is always first. A faster core runs
//   more cycles in the same quantum; all finish the tick together.
// - Plain round robin: a thread keeps a core for a turn of several quanta,
//   then goes to the back of the ready queue if anyone is waiting.
// - Time is kept in "stock sixteenths": sixteenths of a cycle at the core's
//   speed this tick, at the stock clock. A thread at clock level k (tenths of
//   stock) gets k/10 of the cycles in the same time. The speed follows the
//   place's voltage tick by tick (7.5; build-plan-gate2.md step 4), so a
//   debt, in cycles, is repaid at whatever speed the core has then.
// - A long instruction runs whole; an overrun becomes the thread's debt,
//   repaid from its next quantum (R29).
// - Waking sleepers, firing timers and delivering interrupts happen here;
//   the calls that ask for them are the OS's.
//
// An idle core runs its idle run (step 7) through the idle hooks.
#include "nova2/array.h"
#include "nova2/cpu.h"
#include "nova2/memory.h"
#include "nova2/os.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h: saving and loading state

struct SchedSettings {
    u32 quanta_per_tick = 16;      // a quantum is a sixteenth of a tick (appendix E)
    u32 quanta_per_turn = 4;       // a turn is a quarter of a tick
    u32 switch_cycles = 1000;      // a thread switch (8.4: 1,000-3,000 cycles)
    u32 clock_switch_cycles = 500; // extra when the clock level changes (7.5)
    // The low-voltage interrupt (9.5; build-plan-gate4.md step 7): a thread
    // with a handler takes it when it starts a quantum on a core whose
    // voltage is less than this many millivolts above the core's threshold.
    // Once a dip (os.h, kLowVoltageTaken). 0: never.
    i32 low_voltage_margin = 30;
};

// What an idle core does (step 7). run: given the stock time left in the
// quantum, runs the core's idle run (starting one if it has none) and returns
// the time it left unused, halted or waiting on a DREAD (0 if it used it
// all); the core is idle for the rest of the quantum either way. end: the
// idle run on the core ends because a thread takes the core.
struct IdleHooks {
    i64 (*run)(void* ctx, u32 core, i64 left16) = nullptr;
    void (*end)(void* ctx, u32 core) = nullptr;
    void* ctx = nullptr;
};

class Scheduler {
  public:
    bool init(Os* os, Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const SchedSettings& s,
              const CostTable* p_costs, const CostTable* e_costs, TextBuf* why);

    // One tick: wakes and timers, then every round.
    void tick();
    void begin_tick();
    void run_round();

    // A full quantum of a core's time, in its stock sixteenths this tick.
    i64 quantum16(u32 core) const;
    // Runs a thread's instructions within a cycle budget (sixteenths, at its
    // clock). Returns the budget left (negative: an overrun). Stops early if
    // the thread leaves the core.
    i64 run_thread(u32 core, i64 budget16);

    IdleHooks idle;
    const SchedSettings& settings() const { return s_; }
    void serialize(Archive& ar);
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);

    // Counters (not part of the world's state).
    u64 instructions = 0;
    u64 switches = 0;
    // The low-voltage interrupt: raised for a thread, and taken (its handler
    // entered); and the threads that took one this tick, for the place's
    // watch (place.h).
    u64 lv_raised = 0, lv_taken = 0;
    Array<u32> lv_taken_now;
    // Each core's time this tick running something, a thread or an idle run
    // (its switches and debts included), in its stock sixteenths; and the
    // same weighted by the clock it ran at, in tenths of stock. The rest of
    // the tick it was halted. For its draw (4.5, power.h).
    i64 busy16(u32 core) const { return busy_[core]; }
    i64 work10(u32 core) const { return work_[core]; }

  private:
    void run_quantum(u32 core);
    i64 quantum(u32 core); // quantum16, worked out once for each speed a core has
    bool switch_in(u32 core, i64* left16);
    bool deliver(u32 core, i64* budget16);
    void low_voltage(u32 core, Thread& th);

    Os* os_ = nullptr;
    Memory* mem_ = nullptr;
    Core* cores_ = nullptr;
    u32 core_count_ = 0;
    PlaceEnv* env_ = nullptr;
    SchedSettings s_;
    const CostTable* costs_[2] = {nullptr, nullptr};
    Array<u32> turn_left_;  // per core: quanta left in its thread's turn
    Array<u8> last_clock_;  // per core: the clock level it ran last
    Array<i64> busy_, work_; // per core: this tick's busy time (busy16, work10)
    // Per core: the speed its quantum was last worked out for, and that
    // quantum. A core's speed changes once a tick at most, and a quantum is a
    // division by a setting, so it isn't divided out 16 times a tick.
    Array<u32> quantum_speed_;
    Array<i64> quantum_;
    u64 rounds_ = 0;
};

} // namespace nova2
