#pragma once

// Maintenance visits (design.md 7.8; build-plan-gate5.md step 4).
//
// Each place's visit times are drawn from the seed, its id and its visit
// count, before anything in the run can move them (principle 3): a gap is
// the mean × (the regularity + (1 − the regularity) × an exponential draw of
// mean 1), in whole ticks and at least one. The mean is the world's, scaled
// at a place by the density tie. A place stores its visit count and its next
// visit's tick.
//
// A visit replaces every device that has failed since the last: fresh, with
// no wear, the same size and the values its place was dealt, new draws of
// what is the device's own, and static in new memory and disks. With the
// replacement threshold under 1 it also replaces, like for like, each device
// whose wear has reached that share of its life: no wear, a new life, and
// its contents as they were. Nothing pauses and no program is touched; the
// reset region's device coming back this way is a hot swap, not a cold
// start. The code is the place's (visit.cpp).
#include "nova2/config.h"
#include "nova2/types.h"

namespace nova2 {

struct VisitSettings {
    u64 mean = 1000000;       // ticks between a place's visits, on average (0: never)
    double regularity = 0;    // 0: random gaps; 1: exactly periodic
    double density_tie = 0;   // a place's mean is the world's × (its density ÷ the average)^(−tie)
    double replace_at = 1;    // the share of its life at which a worn device is replaced like for like (1: failed only)
    double respawn_share = 1; // the share of a place's devices failed at which it is torn down (step 6)
};

void read_visit_settings(Config& cfg, VisitSettings* s);

inline constexpr u64 kNoVisit = ~0ull; // a place that is never visited

// Gap number `count` (0 for the one before the first visit) of a place whose
// mean is `mean` ticks: whole ticks, at least 1. Integers only, so a visit
// can draw its next in the tick.
u64 visit_gap(u64 seed, u32 place, u64 count, u64 mean, double regularity);

} // namespace nova2
