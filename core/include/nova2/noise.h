#pragma once

// The noise floors, edge errors and fading (design.md 4.4, 7.5, appendix C;
// build-plan-gate1.md step 8, build-plan-gate2.md step 5).
//
// - The error floor: every result a core gives has a small chance of being
//   wrong. Each core counts down the results until the next wrong one, drawn
//   from a geometric distribution, and flips a random bit of that result
//   (cpu.h's ErrorClock). The rate rises above stock clock and falls below
//   it, by a setting's doubling.
// - Edge errors: near either edge of its voltage range a device's rate rises,
//   added to the floor (ε = floor + edge(V) + clock). Within `edge_band` mV
//   of an edge the total at stock doubles every `edge_doubling` mV closer,
//   so at the edge itself it's 2^(band ÷ doubling) times the floor. A core's
//   extra rate is set each tick from the voltage, and its countdown redrawn
//   when it changes (a countdown at a fixed rate has no memory, so that
//   keeps it exact). Memory's floor is the cosmic-ray floor, and its wrong
//   result a bit it fails to hold, which reads back random.
// - The cosmic-ray floor: every bit of memory has a chance per tick of
//   flipping. Each memory device counts down bit-ticks to its next flip the
//   same way, so the flips are exact per bit and per tick, and cost only per
//   flip.
// - A falling rate (design.md 7.5; agreed with Damian 2026-11-18): with
//   `cosmic_start`, the cosmic rate starts there and falls to `cosmic`, its
//   excess over it halving every `cosmic_half_life` ticks, in eight steps a
//   half-life. The step is worked from the tick, so it isn't state; at each
//   step the devices' countdowns are redrawn (the first draw of that tick's
//   stream), which keeps the rate exact. The rates are a table worked once at
//   setup. Memory's edge errors stay on the floor: they're the device's, not
//   the sky's.
// - Fading: below its threshold a memory device that's cut out loses cells
//   (bits) by leak rank. A seeded bijection from rank to cell, a small
//   Feistel network over the bit index, is computed, not stored. A cell's
//   retention at 0 V rises evenly on a log scale with its rank, from
//   `fade_fastest` to `fade_slowest` ticks, and it holds `fade_at_threshold`
//   times longer just below the threshold, on a log scale with the voltage
//   between. So each tick below the threshold adds at_threshold^(−V ÷ Vth)
//   ticks of 0 V to the device's dose, and the cells whose retention the
//   dose has passed fail: only those are visited, and each reads back
//   random. The device restarting refreshes the cells still held: the dose
//   goes back to 0.
//
// - Wear (wear.h; design.md 7.4; build-plan-gate5.md step 2): a worn
//   device's threshold has risen, and its edge errors and its fading follow
//   the worn one: it behaves at V as a new one would at V less the rise. Its
//   leakiest cells, the first ranks of the same bijection, are weak: while
//   the device is on, each loses its bit, reading back random, with a set
//   chance a tick. The device counts down weak-cell-ticks to the next, as
//   the cosmic-ray floor counts bit-ticks, so they cost per flip and land
//   on the same cells every time. A failed device is gone: it neither
//   fades nor flips until it is replaced.
//
// All draw in integers only (tick code, design.md 2): the geometric draw
// uses a fixed-point log2, and the tables are worked once at setup, so a
// world is the same on any machine.
#include "nova2/array.h"
#include "nova2/memory.h"
#include "nova2/os.h"
#include "nova2/power.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h: saving and loading state

// Rowhammer (9.8; build-plan-gate5.md step 8).
// - Each page's row activations are counted over a refresh window, the same
//   ticks for the whole world, and start again at its end.
// - A memory device has a threshold, by its latency: the fastest memory the
//   lowest, the slowest the highest, evenly on a log scale between.
// - At a tick's end, each activation of a page past its device's threshold
//   in the window flips one bit in the page before it and one in the page
//   after, in the same device, each with the strength as its chance: the
//   count is drawn, then the bits.
// - A strength of 0 is off: nothing is counted over a window, drawn or
//   saved.
struct HammerSettings {
    double strength = 0;             // the chance; 0: off
    u64 window = 8000;               // ticks
    u32 fast = 10000, slow = 140000; // activations in a window: the fastest memory's threshold, and the slowest's
};

// Flaky parts (7.5; build-plan-gate5.md step 9, gap 12).
// - Each core and memory device is flaky with a set chance, drawn when it is
//   built and again when it is replaced. A flaky one's floor is its kind's
//   times a factor drawn evenly on a log scale between two settings: a
//   core's rate per result, a memory device's cosmic-ray rate, a falling
//   rate included. Edge errors stay their kind's.
// - A core's floor also rises with its wear, in proportion, to `worn` times
//   its own at the end of its life (1: none), in 256 steps.
// - Factors are in 2^-16ths (kFlakyOne: not flaky), from a table of 256
//   worked at setup, so a draw in a tick stays in integers.
// - A share of 0: no device is flaky.
inline constexpr u32 kFlakyOne = 65536, kFlakySteps = 256;
struct FlakySettings {
    double share = 0;                       // of cores and memory devices
    double factor_min = 3, factor_max = 100; // a flaky device's floor over its kind's
    double worn = 1;                        // a core's floor at the end of its life over its own when new
};

struct NoiseSettings {
    double p_floor = 1e-5;   // per result on a P core, mid-range (catalogue; placeholder)
    double e_floor = 1e-5;   // per result on an E core
    u32 clock_doubling = 1;  // tenths of clock per doubling of the error rate above stock, halving below; 0: no effect
    double cosmic = 1e-9;    // per bit per tick, all memory; 0 is off
    double cosmic_start = 0;          // the cosmic rate at tick 0, falling to `cosmic`; 0: no fall
    u64 cosmic_half_life = 1000000;   // ticks over which its excess over `cosmic` halves
    u32 edge_band = 50;      // mV from either edge of a range where its errors start rising; 0: no edge errors
    u32 edge_doubling = 10;  // mV closer per doubling there
    double fade_fastest = 1;        // ticks at 0 V the leakiest cell holds
    double fade_slowest = 1000;     // and the most enduring
    double fade_at_threshold = 1000; // how many times longer they hold just below the threshold
};

// A probability as a fraction of 2^64 (0 means never; at most one half).
u64 to_p64(double p);
// log2(v), for v >= 1, with 56 fractional bits.
u64 log2_q56(u64 v);
// log2_q56(v) to within a few dozen 2^-56ths, a few times faster, for
// geometric_gap, which uses it only where the slack can't change its answer.
u64 log2_q56_near(u64 v);
// The number of trials to the next success (1, 2, ...) at probability p64,
// from a uniform 64-bit draw r. ~0 when p64 is 0 (never).
u64 geometric_gap(u64 p64, u64 r);
// -log2(1 - p64) in 2^-56ths: the half of a gap's work that depends on p64
// alone, and the gap's divisor.
u64 gap_log(u64 p64);
// A gap_log kept by a caller that draws at one probability again and
// again, with its reciprocal (2^64 - 1) / log rounded down (0 for a log of
// 0): by that, a gap's divide is a multiply and one correction, which
// gives the same quotient for every dividend (optimization-plan.md, pass 9).
// Worked out from the rate, never saved.
struct GapLog {
    u64 log = 0, inv = 0;
};
GapLog gap_divisor(u64 log);
GapLog keep_gap_log(u64 p64); // gap_divisor(gap_log(p64))
u64 gap_quotient(u64 a, const GapLog& g); // a / g.log, for a log above 0
u64 geometric_gap(u64 p64, const GapLog& g, u64 r);

// A thread's saved state in memory, for the saved-state rule (9.4): its
// saved registers while it's off its core, or the frame of a handler it's in.
struct SavedState {
    u32 lo, hi; // [lo, hi), addresses
    u32 thread; // its record
};

// A memory device's cells to fail in a tick's fade (Noise::fade_plan):
// ranks [from, upto), rank k reading back bit (k - from) % 64 of draw
// (k - from) / 64 of the stream with this base.
struct FadeJob {
    u32 device;
    u64 from, upto;
    u64 base;
};

// The blocks of memory the saved states reach, for a fade to look for a
// failed cell's saved states only in those (optimization-plan.md pass 11):
// one bit for each block of 2^kSavedBlockShift bytes, set for each block
// any state [lo, hi) has a byte in. A cell in a block whose bit is clear
// is in no saved state. saved_blocks makes them for a memory of `bytes`.
inline constexpr u32 kSavedBlockShift = 8;
void saved_blocks(const SavedState* saved, u32 count, u32 bytes, Array<u64>* bits);

class Noise {
  public:
    bool init(Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, u64 seed, const NoiseSettings& s,
              const PowerSettings& power, TextBuf* why);

    // Each working core's edge errors at this tick's voltage (appendix C step
    // 4, after the device states): its countdown is redrawn where they change.
    void core_edges(i32 mv);
    // Memory's step (appendix C step 8). fade_check refreshes the devices
    // that are on and says whether any fades this tick; fade then fails the
    // cells whose time has come, appending to hits each saved state (by its
    // index in `saved`, sorted by lo) that lost a bit. memory_errors is the
    // edge errors of the devices that are on; cosmic_flips the floor. Given
    // `failed`, each appends there the address of every byte a bit of which
    // it failed, in the order they fail (for os.saved_state_fails).
    // With heat on (heat.h), `tripped` says which devices are off by a heat
    // trip, which fade as with no voltage whatever the voltage is; and
    // `faster` each device's fading at its temperature, in 2^-16ths (1.0 at
    // or under heat.fade.from), multiplying the tick's dose.
    bool fade_check(const DeviceState* states, i32 mv, const bool* tripped = nullptr);
    void fade(const DeviceState* states, i32 mv, const SavedState* saved, u32 saved_count, Array<u32>* hits,
              const bool* tripped = nullptr, const u32* faster = nullptr);
    // fade in its two parts, for a big one shared out over the host threads
    // (optimization-plan.md pass 10). fade_plan adds each fading device's
    // dose, leaves in fade_jobs() a job for each with cells to fail, counted
    // as failed already, and returns how many cells that is. fade_cells
    // fails a job's ranks [k0, k1), k0 - from a multiple of 64, appending
    // to hits as fade does, and says whether it changed a byte of the reset
    // region; it changes nothing of the noise's own, so a job's ranks may be
    // cut into chunks, one task each, their hits joined in order after. With
    // `shared` it writes bytes with interlocked operations, for chunks of
    // one device at once: the bits differ, and each comes out the same.
    // Given the saved states' blocks (saved_blocks), it looks for a cell's
    // saved states only in a block one reaches: the same hits, fewer looks.
    u64 fade_plan(const DeviceState* states, i32 mv, const bool* tripped = nullptr, const u32* faster = nullptr);
    const Array<FadeJob>& fade_jobs() const { return fade_jobs_; }
    bool fade_cells(const FadeJob& job, u64 k0, u64 k1, const SavedState* saved, u32 saved_count,
                    const u64* blocks, Array<u32>* hits, bool shared) const;
    void memory_errors(const DeviceState* states, i32 mv, Array<u32>* failed = nullptr);
    void cosmic_flips(Array<u32>* failed = nullptr);

    // Wear's part (the comment above). The place sets a weak cell's chance a
    // tick once, as a share of 2^64 (0: no weak cells), and tells each core's
    // and each memory device's wear as it changes: how far its threshold has
    // risen, in mV, and for memory how many of its cells are weak and whether
    // it has failed. weak_flips is the weak cells' step, after the floor.
    void init_wear(u64 weak_p64);
    void set_core_worn(u32 core, i32 rise) { core_rise_[core] = rise; }
    void set_worn(u32 d, i32 rise, u64 weak_cells, bool gone) {
        rise_[d] = rise;
        weak_n_[d] = weak_cells;
        gone_[d] = gone ? 1 : 0;
    }
    void weak_flips(const DeviceState* states, Array<u32>* failed = nullptr);
    // A replaced device (7.8; visit.h): what is its own is drawn again. A
    // core's error countdown; a memory device's leak ranks, by its number at
    // its slot, and its countdowns, with nothing weak and nothing faded.
    // set_fresh gives a device's leak ranks alone, after a load.
    void renew_core(u32 core);
    void renew_memory(u32 d, u32 fresh);
    void set_fresh(u32 d, u32 fresh);
    u64 weak_cells(u32 d) const { return weak_n_[d]; }
    // The weak cells' countdowns: state, saved with the place's wear.
    void serialize_wear(Archive& ar);
    u64 weak = 0; // counter: bits the weak cells lost

    // Rowhammer (HammerSettings above). The place gives each memory
    // device's threshold once; hammer_flips is the tick's step, after the
    // weak cells'.
    void init_hammer(const HammerSettings& s, const u32* thresholds);
    bool hammer_on() const { return hammer_p64_ != 0; }
    u32 hammer_threshold(u32 d) const { return hammer_at_[d]; }
    u64 hammer_window() const { return hammer_window_; }
    void hammer_flips(const DeviceState* states, Array<u32>* failed = nullptr);
    u64 hammered = 0, hammer = 0; // counters: activations past a threshold, and the bits they flipped

    // Flaky parts (FlakySettings above). init_flaky works the factors' table
    // and takes each device's draw as the place was built; the two setters
    // are for a replacement, a load and a core's wear.
    // - flaky_factor: the factor of the device at a slot (wear.h) that has
    //   been replaced `fresh` times, from the place's seed.
    // - set_core_floor: a core's factor and its wear's step (0 to 256).
    //   With `again` its countdown is redrawn at the new rate if it changed,
    //   which keeps the rate exact; without (a load), the countdown stands.
    // - set_memory_flaky: a memory device's factor. Its countdown is the
    //   caller's to redraw (renew_memory).
    void init_flaky(const FlakySettings& s);
    void start_flaky(const u32* core_q16, const u32* memory_q16);
    u32 flaky_factor(u32 slot, u32 fresh) const;
    void set_core_floor(u32 core, u32 q16, u32 step, bool again);
    void set_memory_flaky(u32 d, u32 q16) { flaky_mem_[d] = q16; }
    u32 core_flaky(u32 core) const { return core_flaky_[core]; }
    u32 core_step(u32 core) const { return core_step_[core]; }
    u32 memory_flaky(u32 d) const { return flaky_mem_[d]; }
    bool worn_floor() const { return worn_q16_ != 0; }
    // A memory device's cosmic-ray rate now, as p64.
    u64 cosmic_rate(u32 d) {
        GapLog log;
        return cosmic_of(d, &log);
    }

    // The error rate per result for a core at a clock level, as p64: the
    // floor at that level, and its edge errors this tick.
    u64 rate(u32 core, u8 level) const { return cores_n_[core].p[level]; }
    u64 edge(u32 core) const { return cores_n_[core].edge; }
    // The edge errors' extra rate for a range [lo, hi] at mv, from a table
    // (kind 0 P cores, 1 E cores, 2 memory).
    u64 edge_rate(u32 kind, i32 mv, i32 lo, i32 hi) const;
    // A memory device's fading: its dose (ticks at 0 V, in 2^-32ths), the
    // cells it has failed since it last restarted, the cell (bit index on
    // the device) of a leak rank, and how many cells a dose fails.
    u64 dose(u32 d) const { return fade_[d].dose; }
    u64 faded(u32 d) const { return fade_[d].done; }
    u64 cell_of(u32 d, u64 rank) const;
    u64 cells_failed(u32 d, u64 dose) const;
    void serialize(Archive& ar);
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);

    // Counters (not part of the world's state): results gone wrong, and those
    // of them made while the core was near an edge; cosmic flips, memory's
    // edge errors, and cells faded.
    u64 result_errors = 0, band_errors = 0, cosmic = 0, memory_edge = 0, cells_faded = 0;

    struct CoreNoise {
        Noise* noise;
        u32 core;
        u64 tick_seen;
        u64 count; // draws so far in tick_seen
        u64 edge;  // its edge errors' rate this tick
        u64 p[256];
        // The rate its last gap was drawn at, and gap_log of it: a core's rate
        // changes only with its clock level and the voltage, and that log is
        // half of a draw's work.
        u64 gap_p;
        GapLog gap_log;
    };

  private:
    struct DeviceFade {
        u64 dose = 0;  // ticks at 0 V since it last restarted, in 2^-32ths
        u64 done = 0;  // cells failed since then: ranks [0, done)
        u64 cells = 0; // its bits
        u32 half = 0;  // the Feistel network's half width, in bits
        u32 per_mv = 0; // where its row of per_mv_ starts (its threshold's millivolts long)
        u64 key[4] = {};
    };
    static u32 next(ErrorClock* c);
    static void redraw(ErrorClock* c);
    static u64 total(const CoreNoise& n, u8 level);
    u64 draw(CoreNoise& n);

    Memory* mem_ = nullptr;
    Core* cores_ = nullptr;
    u32 core_count_ = 0;
    PlaceEnv* env_ = nullptr;
    u64 seed_ = 0;
    NoiseSettings s_;
    PowerSettings power_;
    u64 cosmic_p64_ = 0;
    GapLog cosmic_log_; // gap_log(cosmic_p64_)
    // The falling rate: p64 at each step (empty: no fall), and the step at a
    // tick.
    static constexpr u64 kFallPerHalf = 8, kFallSteps = 8 * 64;
    Array<u64> fall_;
    u64 fall_step(u64 tick) const;
    Array<CoreNoise> cores_n_;
    Array<u64> cosmic_left_;  // per memory device: bit-ticks to its next flip
    Array<u64> edge_tab_[3];  // P cores, E cores, memory: the extra rate at each millivolt inside the band
    Array<u64> mem_rate_;     // per memory device: its edge errors' rate this tick
    Array<u64> mem_left_;     // and bit-ticks to its next
    // The rate each device's edge errors last had their gap_log worked out
    // at, and that log: not saved, worked out again when the rate differs.
    Array<u64> mem_log_p_;
    Array<GapLog> mem_log_;
    const GapLog& mem_log(u32 d, u64 rate);
    Array<DeviceFade> fade_;
    Array<FadeJob> fade_jobs_; // scratch: the tick's (fade_plan)
    Array<u64> per_mv_; // per device, the dose a tick at each millivolt below its threshold adds
    // Wear's: each core's and each memory device's threshold rise, each
    // device's weak cells and whether it has failed (the place's, mirrored
    // here), and the weak-cell-ticks to each device's next lost bit.
    Array<i32> core_rise_, rise_;
    Array<u64> weak_n_;
    Array<u8> gone_;
    Array<u64> weak_left_;
    u64 weak_p64_ = 0;
    GapLog weak_log_;
    // Flaky parts: the share as a chance of 2^64 (and whether it is all of
    // them), the factors' table, each core's factor and wear step, each
    // memory device's factor with its rate and that rate's gap_log kept, the
    // kinds' floors at each clock level, and a worn core's rise in 2^-16ths.
    u64 flaky_p64_ = 0;
    bool flaky_all_ = false;
    u32 flaky_tab_[kFlakySteps] = {};
    u64 worn_q16_ = 0;
    Array<u32> core_flaky_, core_step_, flaky_mem_;
    Array<u64> flaky_p_;
    Array<GapLog> flaky_log_;
    u64 base_p_[2][256] = {};
    u64 cosmic_of(u32 d, GapLog* log);
    void core_table(u32 core);
    u64 hammer_p64_ = 0, hammer_window_ = 0;
    GapLog hammer_log_;
    Array<u32> hammer_at_; // per memory device: its threshold
    u64 fade_lo_ = 0, fade_hi_ = 0; // log2 (Q56) of the fastest and slowest retention at 0 V, in 2^-32ths of a tick
    u64 dose_cap_ = 0;              // past the slowest retention every cell has failed: the dose stops there
};

} // namespace nova2
