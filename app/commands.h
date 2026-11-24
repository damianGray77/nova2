#pragma once

// The `nova2` commands beyond run and resume (main.cpp).
#include "nova2/config.h"

namespace nova2 {

struct StageCriteria;
// The origin test's stage criteria from origin.generations, origin.span,
// origin.share, origin.members, origin.copies and origin.groups (origin.cpp);
// the viewer settles colonies by them too.
void read_stages(Config& cfg, StageCriteria* c);

// nova2 origin: the origin test (10.6; build-plan-gate1.md step 11).
// nova2 busy (`busy` set): the same loop on a test world kept busy from its
// first tick, for profiling (R7: placed by hand, so its results are no
// evidence about life): A.5's replicator at every core's first start spot in
// the lax places, and a sensing migrant (traffic_world.h) in every place.
int cmd_origin(Config& cfg, bool busy = false);
// nova2 samples: each seed's samples as a CSV, made from its records
// (origin.records) into origin.samples, as a run with its records in memory
// writes them as it goes (origin.cpp).
int cmd_samples(Config& cfg);
// nova2 genomes: a seed's kept genomes read whole from its records, each
// built and checked; --pack writes an earlier record again as differences
// (genomes.cpp; genome_code.h).
int cmd_genomes(int argc, char** argv);
// The tag a seed's test save begins with (origin.cpp: seed-S-tick-T.test.snap,
// beside its world's save): then the seed, the tick, the size of the test's
// own record, the world's counters (a count of bytes, then World's
// save_counters) and the observers' memory (Observer::save). The viewer
// reads that far when it opens the world's save (view_cmd.cpp).
inline constexpr u64 kTestSaveTag = 0x3154535432564F4Eull; // "NOV2TST1"
// nova2 bench: the first benchmark (design.md 2; build-plan-gate1.md step 11).
int cmd_bench(Config& cfg);
// nova2 taker: 9.4's test world, a memory-taker placed by hand in every place
// under the config's supply (build-plan-gate2.md step 9; taker.cpp).
int cmd_taker(Config& cfg);
// nova2 worlds: gate 3's test worlds at full size (build-plan-gate3.md step 7;
// worlds.cpp): migrants sensing and blind, places kept busy, and nightfall.
int cmd_worlds(Config& cfg);
// Gate 4's test worlds at full size (build-plan-gate4.md step 10;
// power_worlds.cpp), which nova2 worlds runs when worlds.test names one: the
// band, heat, water, the sleeper and load rejection.
int cmd_power_worlds(Config& cfg, Str test);
// Gate 5's test worlds at full size (build-plan-gate5.md step 14;
// wear_worlds.cpp), which nova2 worlds runs when worlds.test names one.
int cmd_wear_worlds(Config& cfg, Str test);
// nova2 trace: one genome alone in a sandbox place, or a place from a place
// snapshot, instruction by instruction (trace.cpp).
struct TraceOptions {
    const char* hex = nullptr;
    const char* snapshot = nullptr; // a place (or world) snapshot instead of a genome
    u32 place = 0;                  // in a world snapshot: the place
    u64 program = 0;                // in a snapshot: the program to trace (0: every one)
    bool list = false;              // in a snapshot: list its programs and stop
    bool wake = false;              // in a snapshot: end every sleep and wait first
    u32 page = 1024;
    bool strict = false;
    bool zero = false;
    bool quiet = false;
    u32 copies = 1;
    u32 at = 0; // the genome goes in the first block at or after this address
    u32 a = 0;
    u32 steps = 2000;
    u32 turn = 200;
};
// Takes the trace's own arguments from argv; the rest (the config and --set
// lines) go to rest, for the usual settings.
bool trace_options(int argc, char** argv, TraceOptions* t, int* rest_argc, char** rest);
int cmd_trace(Config& cfg, const TraceOptions& t);
// nova2 view: the viewer (13.4; build-plan-gate1.md step 12).
int cmd_view(int argc, char** argv);

} // namespace nova2
