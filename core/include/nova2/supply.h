#pragma once

// A place's power supply before gate 4 (design.md 2, 4.2, 4.3, appendix C;
// build-plan-gate2.md step 3): a source of its own, a voltage behind an
// internal resistance, whose capacity follows a day, drifts by swings, and
// now and then drops out.
//
// - Capacity, a share of the source's full, is the product of three
//   factors. Each is a pure function of (seed, place, tick), so nothing the
//   place does changes it (principle 3), and a snapshot needs no schedule
//   state.
//   - The day: each place at its own phase. Capacity runs between the
//     night's share at midnight and full at noon as sin² of the day's
//     phase, worked with Bhaskara's rational sine (within 0.2%), so a tick
//     needs no floating point.
//   - Swings: levels drawn evenly between (1 − swing) and 1, one every swing
//     period, eased from each to the next with a smoothstep.
//   - Drop-outs: in each stretch of B ticks (B the smallest power of two at
//     least the longest drop-out), one starts with a chance set by the rate,
//     at a tick drawn evenly in the stretch. Its length is drawn evenly on a
//     log scale between the shortest and the longest. A share of them are
//     blackouts, capacity nothing; the rest leave a share of capacity drawn
//     evenly between keep_min and keep_max. If two overlap, the deeper one
//     counts.
// - Night, swings and drop-outs scale the source's conductance, not its
//   voltage (4.3), so they punish crowding, not presence.
// - The voltage, appendix C's steps 2 and 3 for one source and one place:
//   the place's conductance from its last tick, G = P ÷ V², then
//   V = Vs × Gs ÷ (Gs + G), with Gs the source's conductance at this tick's
//   capacity. The one-tick lag is the world's physics (4.1).
// - The internal resistance is set for each place so that at its own full
//   draw (every core working all tick at stock clock, every device on), at
//   full capacity, the voltage sags by `sag` (build-plan-gate2.md 7). A sag
//   of 0 is a stiff source: the full voltage whenever capacity is above
//   nothing.
//
// In a tick everything is in integers: millivolts, microwatts, capacity in
// 2^-20ths, conductance in 2^-20ths of a microwatt per millivolt squared
// (a siemens). Setup works out the fixed-point numbers from the settings.
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kCapacityBits = 20;
inline constexpr u32 kCapacityOne = 1u << kCapacityBits;
inline constexpr u32 kDropoutLengths = 1024; // the table of log-spaced lengths
inline constexpr u64 kMaxDay = 1000000;      // keeps the day's products in 64 bits

struct SupplySettings {
    i32 voltage = 1000;       // mV: the source's voltage
    i32 sag = 0;              // mV at the place's full draw, full capacity; 0: stiff
    u64 day = 2000;           // ticks
    double night = 1.0;       // capacity at midnight, a share of noon's; 1: no day
    double swing = 0.0;       // how far below full the swings reach, at most
    u64 swing_period = 20000; // ticks from one swing level to the next
    double dropouts = 0.0;    // drop-outs starting per place per million ticks
    u64 dropout_min = 10, dropout_max = 1000; // ticks
    double blackout_share = 0.5;              // the share of drop-outs that leave nothing
    double keep_min = 0.0, keep_max = 0.5;    // what the rest leave, a share of capacity
};

// a × b ÷ c with a 128-bit product, rounded down or to the nearest;
// saturating if the quotient won't fit.
u64 mul_div_u64(u64 a, u64 b, u64 c);
u64 mul_div_round_u64(u64 a, u64 b, u64 c);

// The stretch of ticks in which at most one drop-out starts: the smallest
// power of two at least the longest drop-out, so one can run into the next
// stretch but no further.
u64 dropout_block(u64 longest);

// A drop-out: when it starts, how long it lasts, and the share of capacity
// it leaves (0 for a blackout).
struct Dropout {
    u64 start = 0, length = 0;
    u32 keep = kCapacityOne;
    bool blackout() const { return keep == 0; }
};

// The law a source's capacity follows (the comment above), apart from whose
// source it is: the day, the swings' size and pace, and the drop-outs' rate,
// lengths and depths. The stream, the purposes and the day's phase are the
// caller's, so a place's own source (Supply, below) and gate 4's sources on
// the map (sources.h) share one law and never share a draw.
class CapacityLaw {
  public:
    // False, with the reason, if the settings can't make it.
    bool init(const SupplySettings& s, TextBuf* why);

    // The day's share at x ticks after a midnight (taken modulo the day).
    u32 day_at(u64 x) const;
    // The swing's share at a tick, for a stream.
    u32 swing(u64 seed, u32 stream, u32 purpose, u64 tick) const;
    // The drop-out starting in stretch j of a stream, if any; and the share
    // drop-outs leave at a tick (the deeper, if two overlap).
    bool dropout_in(u64 seed, u32 stream, u32 purpose, u64 j, Dropout* d) const;
    u32 dropout(u64 seed, u32 stream, u32 purpose, u64 tick, Dropout* which = nullptr) const;

    u64 day_length() const { return day_; }
    u64 block() const { return block_; }
    u64 length(u32 k) const { return lengths_[k]; }
    bool has_dropouts() const { return start_ != 0; }

  private:
    u32 knot(u64 seed, u32 stream, u32 purpose, u64 i) const;

    u64 day_ = 2000;
    u32 night_ = kCapacityOne, swing_ = 0;
    u64 period_ = 20000;
    u64 block_ = 1, start_ = 0, blackout_ = 0;
    u32 keep_min_ = 0, keep_max_ = 0;
    u32 lengths_[kDropoutLengths] = {};
};

// A source's conductance at full capacity, in 2^-20 S, from the load it's
// rated for: it sags `sag` mV below its voltage when `rated_uw` is drawn at
// (voltage - sag). 0 if it can't be made (no sag, or no load): a stiff source.
u64 source_conductance(i32 voltage, i32 sag, u64 rated_uw);

class Supply {
  public:
    // Works out the schedule's numbers, the place's day phase, and its
    // source's conductance from its full draw at the full-draw voltage
    // (voltage − sag). False, with the reason, if they can't be made.
    bool init(const SupplySettings& s, u64 seed, u32 place, u64 full_draw_uw, TextBuf* why);

    // The three factors of capacity at a tick, and their product, in 2^-20ths.
    u32 day(u64 tick) const;
    u32 swing(u64 tick) const;
    u32 dropout(u64 tick, Dropout* which = nullptr) const;
    u32 capacity(u64 tick) const;
    // The drop-out starting in stretch j, if any.
    bool dropout_in(u64 j, Dropout* d) const;

    // The voltage at capacity c, given the place's last tick: what it drew
    // and the voltage it drew it at.
    i32 voltage(u32 c, u64 last_draw_uw, i32 last_mv) const;

    i32 source_mv() const { return vs_; }
    i32 full_mv() const { return vs_ - sag_; }
    bool stiff() const { return stiff_; }
    u64 conductance() const { return gs_; } // at full capacity, 2^-20 S
    u64 full_draw() const { return full_draw_; }
    u64 phase() const { return phase_; }
    u64 block() const { return law_.block(); }
    u64 length(u32 k) const { return law_.length(k); }

  private:
    CapacityLaw law_;
    u64 seed_ = 0;
    u32 place_ = 0;
    i32 vs_ = 1000, sag_ = 0;
    bool stiff_ = true;
    u64 gs_ = 0, full_draw_ = 0;
    u64 phase_ = 0;
};

} // namespace nova2
