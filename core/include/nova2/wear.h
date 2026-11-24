#pragma once

// Wear (design.md 7.4, 7.6, appendix C; build-plan-gate5.md step 2 and its
// gaps 3, 4 and 11).
//
// - Every device has a wear count and an endurance, its life. It fails when
//   its wear reaches its life, and stays off until a visit replaces it (7.8).
// - Wear is counted in ticks at the reference conditions, in 2^-20ths. A
//   tick adds
//     w0 x 2^(d x (T - Tref) / 10) x 2^(v x max(V - Vmax, 0) / 100 mV)
//   of them: d doublings for each 10 degrees the device is above the
//   reference temperature (and as many halvings below it), v doublings for
//   each 100 mV the place's voltage is over the device's maximum. A slope of
//   0 turns its cause off, and a base rate of 0 turns wear off: nothing
//   wears, nothing fails, and the world is gate 4's.
// - The two exponents are worked in 32nds of a doubling, rounded down, and
//   the factor read from a table of 2^(k/32) worked at setup, so a tick's
//   code stays in integers. Ten degrees, or 100 mV, is exactly a doubling.
//   A device far past either limit wears its whole life in a tick: burnout
//   is wear's extreme, not a law of its own.
// - Each device's life is drawn evenly on a log scale from half to twice its
//   kind's endurance, in whole ticks, so like devices under like stress
//   don't fail on the same tick.
// - Wear raises a device's threshold, in proportion, to wear.threshold_rise
//   at the end of its life. Its speed, its cut-out, its restart, its edge
//   errors and the low-voltage margin all follow the worn threshold; SENSE
//   still reads the nominal one (R25).
// - A memory device's leakiest cells turn weak as it wears: their share
//   rises in proportion to its wear, to wear.weak.share at the end of its
//   life. While the device is on, each weak cell loses its bit, reading
//   back random, with the chance wear.weak.chance a tick (noise.h).
// - A device wears whether on or off: cut out or tripped it still sits at
//   its place's voltage and temperature. A failed one wears no more.
//
// The kinds are heat's (heat.h): p, e, memory, disk, network, cooler.
#include "nova2/config.h"
#include "nova2/heat.h"
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kWearBits = 20;               // wear's fixed point: 2^-20ths of a tick at the reference
inline constexpr u64 kWearMost = ~0ull >> 1;       // a wear count never passes it
inline constexpr u64 kWearTick = 1ull << kWearBits; // a tick at the reference conditions, at a base rate of 1

struct WearSettings {
    double rate = 1;       // w0: ticks of wear a tick at the reference conditions; 0: nothing wears
    double heat = 1;       // doublings for each 10 degrees above the reference; 0: heat doesn't wear
    double reference = 45; // degrees C
    double voltage = 1;    // doublings for each 100 mV over the device's maximum; 0: overvoltage doesn't wear
    u32 rise = 50;         // mV a threshold has risen at the end of its device's life
    u64 endurance[kHeatKinds] = {200000000, 200000000, 200000000, 200000000, 200000000, 200000000};
    double weak_share = 0.001;     // of a memory device's cells, weak at the end of its life; 0: none
    double weak_chance = 0.000125; // a weak cell loses its bit with this chance a tick while its device is on
};

// Reads wear's settings through the registry.
void read_wear_settings(Config& cfg, WearSettings* s);

// The settings in a tick's units, worked once.
struct WearLaw {
    bool on = false;            // the base rate is above 0
    u64 rate = 0;               // w0, in wear units a tick
    i64 reference = 45 * kDegree;
    i64 heat32k = 0;            // thousandths of a 32nd of a doubling for each 10 degrees
    i64 volt32k = 0;            // and for each 100 mV over the maximum
    u32 frac[32] = {};          // 2^(k/32), in 2^-16ths
    i32 rise = 0;               // mV, at the end of life
    u64 endurance[kHeatKinds] = {}; // ticks
    u64 weak_share = 0;         // in 2^-32ths of a device's cells
    u64 weak_p64 = 0;           // a weak cell's chance a tick, as a share of 2^64

    void init(const WearSettings& s);
    // A tick's wear for a device at temperature t (millionths of a degree)
    // whose kind's maximum is `maximum`, at a place at mv. At most kWearMost.
    u64 step(i64 t, i32 mv, i32 maximum) const;
    // A device's life in wear units: its kind's endurance times a draw from
    // a half to twice, evenly on a log scale. `slot` is its place among its
    // place's devices (wear_slot), and `fresh` how many times it has been
    // replaced (7.8): each replacement draws afresh.
    u64 life(u64 seed, u32 place, u32 slot, u32 kind, u32 fresh) const;
    // How far its threshold has risen, in mV, and how many of a memory
    // device's cells are weak.
    i32 risen(u64 wear, u64 life) const;
    u64 weak_cells(u64 wear, u64 life, u64 cells) const;
};

// A device's wear (part of its place's state while wear is on).
struct Worn {
    u64 wear = 0;        // in wear units
    u64 life = 0;        // its endurance as drawn, in wear units
    bool failed = false; // at the end of its life: off until it is replaced (7.6)
    u32 fresh = 0;       // which device at its slot this is: 0 the first, one more at each replacement (7.8)
};

// A device's slot at its place, for its draws: the cores, then the memory
// devices, then the disk, the network device and the cooling device.
inline constexpr u32 kWearSlotMemory = 64, kWearSlotDisk = 80, kWearSlotNetwork = 81, kWearSlotCooler = 82;

} // namespace nova2
