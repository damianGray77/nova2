#pragma once

// Heat (design.md 5, 7.7, appendix C step 7; build-plan-gate4.md steps 5 and
// 6): two temperatures, a device's own and its place's.
//
// - A device's temperature rises with its whole draw and falls as heat
//   passes to its place at its heatsink rate:
//   T <- T + (P - h x (T - Tplace)) / C, a tick a step.
// - A place's rises with what its devices pass it, its halves of its links'
//   losses on land and its sources' internal losses (solve.h), and falls as
//   heat leaves the world at its cooling rate, and to cooler neighbours
//   along the proximity links with no water under them:
//   Tp <- Tp + (Q - c x (Tp - Tout) - sum k x (Tp - Tn)) / Cp.
//   Its cooling rate is the base rate x its cooling field's value x (1 +
//   the coast factor x its share of water) (map.h), and a link's k the
//   setting / its length in average links. Every place steps from the
//   temperatures the tick began with, so the order never matters.
// - Every step is clamped: it never passes the balance it's moving toward
//   (where its net heat is nothing), so a large rate can't overshoot. The
//   step is the net heat / the larger of its capacity and the rates out of
//   it in all: with a capacity under the rates, one step lands on the
//   balance and stays.
// - What a device passes to its place in a step is its draw less what its
//   own warming kept, so no heat is made or lost by the clamp.
// - Leakage: a device draws more as it gets hotter, by heat.leakage for each
//   degree it is above the outside temperature (cooling devices aside).
// - A device at or past its heat limit trips: it's off, as at a cut-out,
//   until it has cooled to its cool-down point, heat.cooldown below the
//   limit. No off-time: it comes back there, if the voltage carries it.
// - Memory fades faster when hot: a cell's retention halves for each
//   heat.fade.halving degrees its device is above heat.fade.from.
// - A cooling device (7.7), at the places the coolers' field picks: its
//   cooling rises evenly from nothing at its start temperature to its full
//   rate at its top temperature, by its place's temperature, and its draw
//   rises with it. It has a voltage range; cut out or tripped it cools
//   nothing.
// - The outside temperature is hw.temperature, constant; the world starts
//   at it.
//
// With heat.on off every temperature stays the outside one, nothing trips
// or leaks, and no place has a cooling device: gates 1 to 3's world.
//
// In a tick everything is in integers: temperatures in millionths of a
// degree C, powers in microwatts, rates in microwatts a degree, capacities
// in microwatt-ticks a degree.
#include "nova2/config.h"
#include "nova2/types.h"

namespace nova2 {

enum HeatKind : u32 { kHeatP = 0, kHeatE, kHeatMemory, kHeatDisk, kHeatNetwork, kHeatCooler, kHeatKinds };
extern const char* const kHeatKindNames[kHeatKinds]; // the keys' names: p, e, memory, disk, network, cooler

inline constexpr i64 kDegree = 1000000; // a degree C, in a temperature's units

struct HeatPart {
    double limit = 100;  // degrees C: at or past it, it trips
    double sink = 0.1;   // its heatsink rate, watts a degree
    double capacity = 2; // watt-ticks a degree
};

struct HeatSettings {
    bool on = true;
    HeatPart part[kHeatKinds];
    double cooldown = 10;   // degrees below its limit a tripped device comes back at
    double leakage = 0.01;  // more draw for each degree above the outside, as a share
    double place_cooling = 2.5, place_capacity = 1250; // a usual place: watts a degree, watt-ticks a degree
    double coast = 1;       // a place wholly on the coast cools (1 + this) times as well
    double spread = 0.25;   // between places: watts a degree for an average link on land; 0: none
    double fade_from = 45, fade_halving = 10; // memory's retention halves for each `halving` degrees above `from`
    // Cooling devices (7.7).
    double cooler_share = 1.0 / 3; // of the places, by the coolers' field
    double cooler_start = 40, cooler_top = 80; // the place's temperature it starts at, and is at full at
    double cooler_rate = 2.5;      // its cooling at full, watts a degree
    double cooler_draw = 3;        // its draw at full, watts
    i32 cooler_threshold = 600, cooler_maximum = 1300; // mV
    i32 outside = 250;             // hw.temperature, tenths of a degree
};

void read_heat_settings(Config& cfg, HeatSettings* s);

// The settings in a tick's units, worked once.
struct HeatLaw {
    bool on = false;
    i64 outside = 25 * kDegree;
    i64 limit[kHeatKinds] = {}, cool[kHeatKinds] = {}; // trip at or past limit; back at or under cool
    u64 sink[kHeatKinds] = {}, capacity[kHeatKinds] = {};
    u64 leak = 0;                    // more draw a degree above the outside, in 2^-32ths
    u64 place_cooling = 0, place_capacity = 1; // a usual place's
    u64 spread = 0;                  // an average land link's
    i64 fade_from = 45 * kDegree, fade_halving = 10 * kDegree;
    u32 fade_frac[32] = {};          // 2^(k/32), in 2^-16ths
    i64 cooler_start = 0, cooler_top = 1;
    u64 cooler_rate = 0, cooler_draw = 0;
    i32 cooler_threshold = 600, cooler_maximum = 1300;

    void init(const HeatSettings& s);
    // A draw with leakage at a device's temperature.
    u64 leaky(u64 draw_uw, i64 t) const;
    // How many times faster memory fades at a temperature, in 2^-16ths (1.0
    // at or under fade_from; capped at 2^15).
    u32 fade_factor(i64 t) const;
    // How hard a cooling device works at its place's temperature, in 2^-20ths.
    u32 cooler_share(i64 place_t) const;
};

// One step of a body's temperature toward its balance with what's around it
// (the comment above): `p` microwatts made in it, `h` microwatts a degree to
// surroundings at `around`, heat capacity `c`. Returns the new temperature,
// and in `passed` the microwatts it handed on (p less what its warming kept;
// negative while it takes heat in).
i64 heat_step(i64 t, i64 around, u64 p, u64 h, u64 c, i64* passed);

// The microwatts a rate carries from a body at t to one at `to` (negative:
// the other way).
i64 heat_flow(u64 rate, i64 t, i64 to);
// The same step for a place among its surroundings: `net` microwatts into
// it in all, with every flow out of it counted at the temperatures the tick
// began with, and `rate` the microwatts a degree out of it in all. Returns
// the new temperature.
i64 heat_step_net(i64 t, i64 net, u64 rate, u64 c);

} // namespace nova2
