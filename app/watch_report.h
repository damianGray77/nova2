#pragma once
// Gate 5's watching in the origin test's report (build-plan-gate5.md step
// 11; core's watch.h): at each seed's end, what gate 5's mechanisms did,
// its named checks' readings beside its measure, and the four mechanics',
// from the world's counters, the observers' tallies and the ground record.
// Reported, not judged. With origin.windows set, each seed's respawns,
// cascades and calls by window go to files beside its ground record.
#include "nova2/array.h"
#include "nova2/current.h"
#include "nova2/events.h"
#include "nova2/heat.h"
#include "nova2/observe.h"
#include "nova2/types.h"
#include "nova2/watch.h"

namespace nova2 {

class World;

struct WatchParams {
    u64 era = 0, window = 0; // origin.era and the turnover window, in ticks
    u32 sample_every = 100, day = 2000;
    StageCriteria stages;
};

// One seed's.
struct SeedWatch {
    u64 place_ticks = 0;
    // What the mechanisms did: failures by kind, the device-ticks spent
    // failed, the devices there are and those replaced; visits, respawns
    // (those that found a new point, and the programs torn down); links cut
    // and mended; straits parted and joined; patches formed by type;
    // strikes; rowhammer's activations past its threshold and the bits they
    // flipped; flaky parts among the cores and memory devices at the end;
    // the current's counts; deaths by cause.
    u64 failures[kHeatKinds] = {}, failed_ticks[kHeatKinds] = {};
    u64 devices[kHeatKinds] = {}, replaced[kHeatKinds] = {};
    u64 visits = 0, respawns = 0, respawns_moved = 0, torn = 0, left_out = 0;
    u64 link_cuts = 0, link_mends = 0, patch_cuts = 0, partings = 0, joinings = 0;
    u64 patches[kPatchTypes] = {}, strikes = 0, hammered = 0, hammer = 0;
    u64 flaky_parts = 0, parts = 0;
    // Of the disks' failures, those by a worn boot block: its first block
    // past its write endurance (7.4, 10.5; Place::boot_failures).
    u64 boot_failures = 0;
    CurrentCounts drift;
    u64 deaths[kDeathCauses] = {};
    // Scars (7.8): place-ticks with a device failed and waiting, and the
    // most scarred place's share of its recorded windows' ticks.
    u64 scarred = 0;
    double scar_most = 0;
    u32 scar_place = 0;
    // Cascades (12.2): how many, by what began them, by size (2, 3 to 4, 5
    // to 9, 10 or more), the largest; the sag of the sources feeding their
    // first places, summed, against all the sources' at the end.
    u64 cascades = 0, by_cause[kCascadeCauses] = {}, sizes[4] = {}, largest = 0;
    double root_sag = 0, source_sag = 0;
    u64 sources = 0;
    // And by the ground of the window at the first place over the tick
    // it began (root_grounds: fair, hot, dim, hot and dim at once, none
    // recorded): how many, by what began them, by size; and the
    // place-windows of each ground, for rates (step 11's second question).
    u64 by_root[kRootGrounds] = {}, root_cause[kRootGrounds][kCascadeCauses] = {};
    u64 root_sizes[kRootGrounds][4] = {}, root_windows[kRootGrounds] = {};
    // Worn ground and holds (gap 1), fresh place-windows [0] and worn [1]:
    // how many; holds standing as they began, by age; holds ended in them,
    // by age; the holders' programs as they began and their births in them.
    u64 windows[2] = {}, held[2][kAgeBins] = {}, ended[2][kAgeBins] = {};
    u64 holder_programs[2] = {}, holder_births[2] = {};
    // Flaky parts (gap 12), at places with none [0] and with one [1]:
    // births a parent made there, mutations among them, place-ticks;
    // place-windows and the holds standing as they began.
    u64 flaky_births[2] = {}, flaky_mutations[2] = {}, flaky_ticks[2] = {};
    u64 flaky_windows[2] = {}, flaky_held[2] = {};
    // The killer [0] and the watchdog [1] (gap 14): what they ended by size
    // at birth, those of their place's leading lineage, and that lineage's
    // share summed; births by size.
    u64 ended_by[2][kSizeBins] = {}, ended_leader[2] = {}, births_by_size[kSizeBins] = {};
    double leader_share[2] = {};
    // The current (gap 15): its births, their lineages, their links from
    // their lineage's home (0, 1, 2, 3, 4 to 7, 8 or more, none), and the
    // colonies it founded (no colonist yet).
    u64 carried = 0, carried_lineages = 0, carried_far[7] = {}, current_colonies = 0;
    // Straits joining again (gap 13): joinings; groups leading on each
    // side as they joined, and those in common; joinings with a group
    // leading on both; over the era after each, colonies founded across it
    // each way and first leads on either side.
    u64 joins = 0, join_groups[2] = {}, join_common = 0, join_both = 0, across[2] = {}, join_firsts = 0;
    // Calls by era (1), early eras [0] and late [1]: by kind, and the
    // program-days they are over; the eras counted.
    u64 era_calls[2][kCallCount] = {};
    double era_days[2] = {};
    u32 era_n[2] = {};

    void add(const SeedWatch& b);
};

// At a seed's end. `stage4`: the tick it reached stage 4 (~0: it didn't);
// `firsts`: its first leads (tally_eras).
void tally_watch(World& w, const Observer& o, const WatchParams& q, u64 stage4, const Array<FirstLead>& firsts,
                 SeedWatch& x);
// <dir>/seed-N-respawns.csv, -cascades.csv and -calls.csv; `window`: the
// turnover window, in ticks; `calls`: the calls' column names, in Call's
// order.
void write_watch_files(const Observer& o, const char* dir, u64 seed, u64 window, const char* const* calls);
// The report's sections, over the seeds that ran: `x[k]` is seed
// `seeds[k]`'s.
void print_watch(const SeedWatch* const* x, const u64* seeds, u32 n, const WatchParams& q,
                 const char* const* calls);

} // namespace nova2
