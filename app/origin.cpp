// nova2 origin: the origin test (design.md 10.6; build-plan-gate1.md step 11).
//
// Runs origin.seeds worlds, seeds world.seed, world.seed + 1, ..., side by
// side (origin.parallel at once, sharing the host threads). Each runs
// origin.ticks ticks (or until origin.budget instructions), and the report
// gives, per seed and per OS type (strict or lax, page size):
//   stage 1: a copying sequence: a lineage's first faithful birth;
//   stage 2: viable offspring: a faithful birth from a faithfully born parent;
//   stage 3: a copying lineage reaches origin.generations generations;
//   stage 4: it establishes: it holds its place for origin.span ticks
//            without a break. It holds it while it has origin.members living
//            members or more, at least origin.copies of them born as copies
//            (faithful, or at least half like the parent's genome), and
//            they are at least origin.share of the place's programs;
//   stage 5: it keeps adapting: origin.groups new genome groups while its
//            place is crowded, counted from when it reached stage 4.
// A seed runs on after stage 4 and 5, to see whether its lineage lasts.
// Stages are checked at every observer sample, so a stage's tick is the
// sample's (stages 1 and 2 are exact, from the births). A pass is stage 4 in
// at least origin.pass_share of the seeds. Also reported: distinct paths
// tried per second, the region measures, deaths by cause. With
// origin.samples set, each seed's samples go to <folder>/seed-N.csv. With
// origin.snapshots set, places worth tracing are saved as place snapshots
// (snapshot.h) for nova2 trace --snapshot: at the end, each place where a
// lineage origin.generations deep has half origin.members alive or more; and
// a place as its lineage reaches stage 4. Through the run too, every
// origin.snapshot_every ticks, only if that is set (0: Damian, 2026-11-20).
//
// The turnover measure (gate 2's question; build-plan-gate2.md step 1) is
// reported per seed, per OS type and over all seeds: windows of
// observe.turnover_window ticks that start with observe.turnover_members
// programs or more, how many turned over (half their starters died, and
// births made up half the dead), the crowded ones apart, deaths per program
// per day (supply.day ticks) by cause, and the longest run of windows in one
// place without turnover; and how many of the windows had a cold start in
// them (step 6). With origin.windows set, each seed's windows go to
// <folder>/seed-N.csv. Gate 2's pass mark (build-plan-gate2.md 7; step 9) is
// checked last: origin.turnover_share of the counted windows turned over, and
// of the crowded ones, on origin.crowded_min crowded windows or more from
// origin.crowded_seeds seeds or more, and stage 4 at gate 1's pass mark.
//
// The spread measure (gate 3's question, 13.3; build-plan-gate3.md step 1)
// is reported per seed: the colonies lineages founded at places beyond the
// one they began in, each by a colonist (born from an arrival), and those
// that settled their place, holding it as stage 4 holds a lineage's first
// place, for origin.span ticks, with only the copies born there counted.
// Settling is checked at every observer sample, as stage 4 is. Gate 3's pass
// mark is checked after gate 2's: a settlement in origin.spread_share of the
// seeds, with gate 2's mark.
//
// The ground measure (gate 4's question, 14.1; build-plan-gate4.md step 1) is
// reported per seed for its lax places, and by OS kind over all seeds: each
// place's windows by what the physics did to it in them (hot, dim or fair:
// observe.h), and the programs they started with. With origin.windows set,
// each seed's go to <folder>/seed-N-ground.csv. Gate 4's pass mark
// (build-plan-gate4.md 7 and 11, as changed on 2026-11-20) is checked after
// gate 3's, with ground named by what a full place gets there: each place's
// class from <origin.windows>/seed-N-classes.csv, which nova2 worlds makes
// from the seed after the run (worlds.test = classes), a place-window taking
// the latest classing at or before its start. In the lax places of the seeds
// that reach stage 4, fair place-windows and hot ones differ by a factor of
// origin.ground_ratio or more in the programs they start with, whichever
// way, and fair and dim ones likewise, on origin.ground_min windows or more
// of each kind from origin.ground_seeds seeds or more, with gate 3's mark.
// The mark as first worded and named (ground by what happened to the place
// in the window; fair with that many times the programs of hot and of dim)
// is reported beside it, not judged. A run's report is made again, with the
// classes in, by carrying the finished run on (origin.resume).
//
// On a map, each seed also reports its arrivals and messages, and gate 3's
// named checks (14.1; build-plan-gate3.md step 8): the SENDs let go; the
// arrivals at port 0s, what became of those that ran, and the first call to
// fail of those that kept their core and made nothing; the network's
// messages; the places arrivals landed at and ran at (10.4: does a full CPU
// shut them out?); how the messages spread over the places' ports (11.1: do
// they pile onto a few?); and the routes' work and time as network devices
// go off and on (11.3). Each seed's map is reported with the rest (6.1).
#include <windows.h>

#include <cstdio>
#include <cstdlib> // qsort
#include <cstring>
#include <intrin.h> // __rdtsc: the routes' and the network step's timings are in its cycles

#include "arrivals.h"
#include "commands.h"
#include "run_view.h"
#include "ui/settings.h"
#include "nova2/io.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"
#include "watch_report.h"

namespace nova2 {

namespace {

constexpr u64 kNone = ~0ull;
constexpr u32 kDumpBytes = 256; // the most of a genome the dump shows

// nova2 busy's test world (R7: placed by hand, for profiling; never a run).
// A.5's 14-byte replicator at every core's first start spot in each lax
// place, so its first idle runs copy themselves; and a sensing migrant in
// every place, so the network carries copies and the strict places fill with
// arrivals.
const u8 kBusyLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                         encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                         encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

void place_busy(World& w) {
    const HandCode code = migrant(MigrantKind::sense, false);
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            for (u32 c = 0; c < p.core_count(); ++c) {
                const u32 spot = p.region.state(c).spot;
                if (spot + sizeof kBusyLax <= p.mem.region_size())
                    memcpy(p.mem.bytes() + p.mem.region_base() + spot, kBusyLax, sizeof kBusyLax);
            }
        place_migrant(p, code);
    }
}
constexpr u32 kTopPlaces = 5;   // where messages land (11.1): the share the busiest places took

struct Params {
    u32 seeds;
    u64 ticks, budget;
    StageCriteria stages;
    double pass_share;
    double spread_share; // gate 3's (build-plan-gate3.md 7)
    // Gate 2's pass mark (build-plan-gate2.md 7): the share of counted and of
    // crowded windows that must turn over, and the floor of crowded windows
    // and of the seeds they come from.
    double turnover_share;
    u32 crowded_min, crowded_seeds;
    // Gate 4's (build-plan-gate4.md 7): how many times a hot or a dim
    // place-window's programs a fair one must start with, and the floor of
    // windows of each kind and of the seeds they come from.
    double ground_ratio;
    u32 ground_min, ground_seeds;
    // Gate 5's (build-plan-gate5.md 7): an era's ticks; the share of a seed's
    // late eras that must have a first lead; the fewest late eras a seed is
    // counted on; and the seeds that must meet it, a number and a share of
    // those counted.
    u64 era;
    double era_share, era_seed_share;
    u32 era_late, era_seeds;
    bool calls; // gate 5's columns in the samples' CSV
    u32 parallel, threads_each;
    bool busy = false; // nova2 busy: a test world, placed by hand (R7)
    RunView* view = nullptr; // the window on the run, open or not (run_view.h), which only watches
    ObserverSettings obs;
    char samples[400];
    char genomes[400];
    char snapshots[400];
    char windows[400];
    char records[400]; // origin.records: the folder for the finished records (empty: in memory)
    double flush_seconds;
    char saves[400];
    char resume[400]; // origin.resume: the folder of saves each seed is carried on from
    u64 snapshot_every;
    u64 save_every;
    // By the clock on the wall, for a run with no end set (Damian, 2026-11-19):
    // minutes between a seed's saves and between its progress lines (0:
    // none), and the megabytes of memory the machine must have left, under
    // which a seed saves and stops (0: not watched).
    double save_minutes, progress_minutes;
    u64 memory_floor;
    u32 saves_kept;
    u64 day; // ticks, for rates per day
};

// What gate 5's watching reads of them (watch_report.h).
WatchParams watch_params(const Params& p) {
    WatchParams q;
    q.era = p.era;
    q.window = p.obs.turnover.window;
    q.sample_every = p.obs.sample_every;
    q.day = p.obs.day;
    q.stages = p.stages;
    return q;
}

struct TypeStat {
    u32 places = 0;
    u32 best = 0; // the best stage any lineage in these places reached
};

// The turnover measure, summed over places (build-plan-gate2.md step 1).
struct TurnStat {
    u32 places = 0;        // places with a counted window
    u32 places_turned = 0; // places with a window that turned over
    u32 windows = 0, turned = 0, crowded = 0, crowded_turned = 0;
    u32 cold = 0, cold_turned = 0, crowded_cold_turned = 0; // with a cold start in them (step 6)
    u64 program_ticks = 0;
    u64 deaths[kDeathCauses] = {};
    // The longest run of counted windows without turnover in any one place.
    u32 longest = 0, longest_place = 0;
    u64 longest_seed = 0, longest_end = 0;

    void add(const PlaceTurnover& T, u64 seed, u32 place) {
        places += T.windows ? 1 : 0;
        places_turned += T.turned ? 1 : 0;
        windows += T.windows;
        turned += T.turned;
        crowded += T.crowded_windows;
        crowded_turned += T.crowded_turned;
        cold += T.cold_windows;
        cold_turned += T.cold_turned;
        crowded_cold_turned += T.crowded_cold_turned;
        program_ticks += T.program_ticks;
        for (u32 c = 0; c < kDeathCauses; ++c)
            deaths[c] += T.deaths_by_cause[c];
        if (T.longest > longest) {
            longest = T.longest;
            longest_place = place;
            longest_seed = seed;
            longest_end = T.longest_end;
        }
    }
    void add(const TurnStat& s) {
        places += s.places;
        places_turned += s.places_turned;
        windows += s.windows;
        turned += s.turned;
        crowded += s.crowded;
        crowded_turned += s.crowded_turned;
        cold += s.cold;
        cold_turned += s.cold_turned;
        crowded_cold_turned += s.crowded_cold_turned;
        program_ticks += s.program_ticks;
        for (u32 c = 0; c < kDeathCauses; ++c)
            deaths[c] += s.deaths[c];
        if (s.longest > longest) {
            longest = s.longest;
            longest_place = s.longest_place;
            longest_seed = s.longest_seed;
            longest_end = s.longest_end;
        }
    }
    u64 all_deaths() const {
        u64 n = 0;
        for (u32 c = 0; c < kDeathCauses; ++c)
            n += deaths[c];
        return n;
    }
};

// The ground measure, summed over places (build-plan-gate4.md step 1): the
// place-windows of each kind of ground (fair, hot, dim: observe.h), the
// programs they started with and the births in them; and the windows that
// were hot and dim at once.
struct GroundStat {
    u64 windows[kGrounds] = {}, starters[kGrounds] = {}, births[kGrounds] = {};
    u64 both = 0;

    void add(const PlaceGround& G) {
        for (u32 g = 0; g < kGrounds; ++g) {
            windows[g] += G.windows[g];
            starters[g] += G.starters[g];
            births[g] += G.births[g];
        }
        both += G.both;
    }
    void add(const GroundStat& s) {
        for (u32 g = 0; g < kGrounds; ++g) {
            windows[g] += s.windows[g];
            starters[g] += s.starters[g];
            births[g] += s.births[g];
        }
        both += s.both;
    }
    // The programs a place-window of a kind started with, on average.
    double mean(Ground g) const {
        const u32 k = static_cast<u32>(g);
        return windows[k] ? static_cast<double>(starters[k]) / windows[k] : 0.0;
    }
};

// The reviewer's measures (build-plan-gate4.md 11; Damian, 2026-11-20), for
// one naming of ground, over a seed's lax place-windows by kind. Reported
// beside gate 4's mark, none of it judged.
struct GroundMore {
    inline static constexpr u32 kBins = 1000; // a share of core time, to a thousandth
    u64 windows[kGrounds], starters[kGrounds], copies[kGrounds]; // programs they started with; those born as copies
    u64 births[kGrounds], births_copies[kGrounds];
    u64 core_time[kGrounds], cut[kGrounds], heat[kGrounds]; // core-ticks: cores x window; lost to cut-outs, to trips
    u64 worked[kGrounds];                                   // core time worked, in 2^-20ths of a core-tick
    // The windows held by one program: as they began, one program had over
    // half the place's threads and as many threads as the place has cores
    // or more (the gate 4 plan's line, not judged).
    u64 held[kGrounds], held_starters[kGrounds], held_copies[kGrounds], held_worked[kGrounds], held_time[kGrounds];
    // The share of core time behind each window's name, lost to heat trips
    // [0] and to cut-outs [1]: the lowest and highest, and a count of
    // windows by thousandth, for the middle one.
    float lo[2][kGrounds], hi[2][kGrounds];
    u32 hist[2][kGrounds][kBins + 1];

    void count(u32 k, const GroundWindow& x, u64 window, double heat_share, double cut_share) {
        const double share[2] = {heat_share, cut_share};
        for (u32 j = 0; j < 2; ++j) {
            const float v = static_cast<float>(share[j] < 0 ? 0 : share[j] > 1 ? 1 : share[j]);
            lo[j][k] = !windows[k] || v < lo[j][k] ? v : lo[j][k];
            hi[j][k] = !windows[k] || v > hi[j][k] ? v : hi[j][k];
            ++hist[j][k][static_cast<u32>(v * kBins + 0.5f)];
        }
        const u64 time = static_cast<u64>(x.cores) * window;
        ++windows[k];
        starters[k] += x.starters;
        copies[k] += x.starters_copies;
        births[k] += x.births;
        births_copies[k] += x.births_copies;
        core_time[k] += time;
        cut[k] += x.cut;
        heat[k] += x.heat;
        worked[k] += x.worked;
        if (x.top_threads >= x.cores && 2ull * x.top_threads > x.threads) {
            ++held[k];
            held_starters[k] += x.starters;
            held_copies[k] += x.starters_copies;
            held_worked[k] += x.worked;
            held_time[k] += time;
        }
    }
    void add(const GroundMore& s) {
        for (u32 k = 0; k < kGrounds; ++k) {
            for (u32 j = 0; j < 2; ++j) {
                if (!s.windows[k])
                    continue;
                lo[j][k] = !windows[k] || s.lo[j][k] < lo[j][k] ? s.lo[j][k] : lo[j][k];
                hi[j][k] = !windows[k] || s.hi[j][k] > hi[j][k] ? s.hi[j][k] : hi[j][k];
                for (u32 b = 0; b <= kBins; ++b)
                    hist[j][k][b] += s.hist[j][k][b];
            }
            u64* const mine[] = {windows,       starters,   copies, births, births_copies, core_time,   cut,
                                 heat,          worked,     held,   held_starters, held_copies, held_worked,
                                 held_time};
            const u64* const theirs[] = {s.windows, s.starters, s.copies, s.births, s.births_copies, s.core_time,
                                         s.cut,     s.heat,     s.worked, s.held,   s.held_starters, s.held_copies,
                                         s.held_worked, s.held_time};
            for (usize i = 0; i < sizeof mine / sizeof mine[0]; ++i)
                mine[i][k] += theirs[i][k];
        }
    }
    // The middle window's share, to a thousandth.
    double middle(u32 j, u32 k) const {
        u64 seen = 0;
        for (u32 b = 0; b <= kBins; ++b) {
            seen += hist[j][k][b];
            if (seen * 2 >= windows[k] && seen)
                return static_cast<double>(b) / kBins;
        }
        return 0;
    }
};

// A seed's ground: its lax places', then its strict ones'. Kept beside its
// SeedResult and not in it: a save holds a SeedResult whole, and a seed
// carried on from a save made before the measure must still read it back.
// It is worked out at the seed's end from the observer, which a save holds.
struct SeedGround {
    GroundStat os[2];
    // The same place-windows under the judged naming: by their places'
    // classes (tally_classes), lax then strict; those with no classing at or
    // before their start; and whether the seed has a classes file, with how
    // many classings.
    GroundStat cls[2];
    u64 unclassed[2] = {};
    // And those of a place that has respawned (7.8), from the window its
    // first respawn fell in: left out, since the class is of the place as
    // built and it is somewhere else now (build-plan-gate5.md 7).
    u64 respawned[2] = {};
    bool classed = false;
    u32 classings = 0;
    // With a classes file worked with the run's respawns applied (its rows
    // carry each place's incarnation; build-plan-gate5.md 11): every
    // place-window is named by its place's class in its incarnation then,
    // and only one with a respawn of its place inside it is left out,
    // counted here in place of `respawned`.
    bool by_incarnation = false;
    u64 straddled[2] = {};
    // The reviewer's measures over its lax place-windows: [0] by class (the
    // judged naming; only with a classes file), [1] by what the physics did
    // to the place in the window (the first naming).
    GroundMore more[2];
    // The reading on untouched ground (build-plan-gate4.md 11; the gate 4
    // session's rule): its lax place-windows by the name of their place's
    // last window that began with no program; those with programs and no
    // such window before them, left out; and over the windows with
    // programs, the ticks since the window that named them.
    GroundStat untouched;
    u64 untouched_left = 0, untouched_age = 0, untouched_aged = 0;
    // Gate 5's measure, read from the same record (observe.h: tally_eras).
    EraTally eras;
    // Gate 5's readings at the seed's end (step 1): the calls its programs'
    // threads made and those that succeeded, by kind; and reading input,
    // overruns and injections (Observer's totals of the same names).
    u64 calls[kCallCount] = {}, calls_ok[kCallCount] = {};
    u64 sends_other = 0, sends_other_ok = 0;
    u64 spawns_thread = 0, spawns_thread_ok = 0, spawns_program = 0, spawns_program_ok = 0;
    u64 recvs = 0, overruns = 0, reached[kReaches] = {}, overruns_unknown = 0;
    u64 births = 0, reader_births = 0, injection_births = 0;
    u64 program_samples = 0, reader_program_samples = 0, injections_seen = 0, injected_programs = 0;
    u64 watched = 0; // overruns still watched at the end
    // Gate 5's watching (step 11; watch_report.h).
    SeedWatch watch;
};

// How a seed's power map came out (map.h; gate 4). Beside its SeedResult,
// for the same reason; it is worked out with the map, whenever the seed's
// world is set up.
struct SeedPowerMap {
    char line[400];
};

struct SeedResult {
    bool ok = false;
    u64 seed = 0, ticks = 0, instructions = 0;
    char map[400] = {}; // how its map came out (map.h)
    double secs = 0;
    u64 stage[6] = {kNone, kNone, kNone, kNone, kNone, kNone}; // the first tick of stages 1..5
    TypeStat types[2][kMaxPageSizes];                          // [strict][page size]
    Sample last{};
    double ratio_min = 1e9;
    // The first lineage to reach stage 4, as it stood then.
    u64 s4_origin = kNone;
    u32 s4_place = 0, s4_alive = 0, s4_alive_faithful = 0, s4_alive_copies = 0, s4_programs = 0, s4_depth = 0,
        s4_fdepth = 0;
    u64 s4_held_since = kNone;
    u32 s4_index = 0; // its index in the observer
    // The same lineage at the seed's end.
    u32 end_alive = 0, end_alive_copies = 0, end_programs = 0;
    u64 end_held_since = kNone, end_extinct = kNone;
    // The longest hold of a place seen (stage 4 needs origin.span), and whose.
    u64 hold = 0, hold_origin = kNone, hold_to = 0; // the best hold: its length, lineage and last check
    u32 hold_place = 0;
    // The turnover measure: all places, and by OS type.
    TurnStat turn;
    TurnStat turn_types[2][kMaxPageSizes];
    // Power (build-plan-gate2.md step 2): every place's draw summed over its
    // ticks, by device; its place-ticks; memory's row activations; and the
    // most any place drew in one tick.
    PlaceDraw energy;
    u64 place_ticks = 0, activations = 0, peak_draw = 0;
    u32 peak_place = 0;
    // The supply (step 3): every place's voltage summed over its ticks, the
    // lowest any place saw, its place-ticks below the P cores', the E cores'
    // and the reset region's memory's thresholds, and the places' full draws.
    u64 mv_total = 0, below_p = 0, below_e = 0, below_region = 0, full_draw = 0;
    i32 mv_lowest = 0x7FFFFFFF;
    u32 lowest_place = 0, places = 0;
    // Cut-outs (step 4): the cores' cut-outs, the core-ticks they spent cut
    // out and all their core-ticks (P, then E); the threads ended or paused
    // at them, and the idle runs ended; the disk's cut-outs, and the writes
    // torn and reads failed by them.
    u64 cutouts[2] = {}, off_ticks[2] = {}, core_ticks[2] = {};
    u64 cut_threads = 0, paused_threads = 0, cut_runs = 0, disk_cutouts = 0, torn_writes = 0, failed_reads = 0;
    // Errors and fading (step 5): the cores' wrong results, and those made
    // near an edge; memory devices' cut-outs, the device-ticks they spent cut
    // out and all their device-ticks; cells faded, and the threads ended by
    // it, and those ended by a flip or an edge error (os.saved_state_fails);
    // bits that failed to hold near an edge, and cosmic flips.
    u64 result_errors = 0, band_errors = 0, memory_cutouts = 0, memory_off = 0, memory_ticks = 0;
    u64 cells_faded = 0, faded_threads = 0, failed_threads = 0, memory_edge = 0, cosmic = 0;
    // Cold starts (step 6): how many, and how many booted a block a program
    // wrote; prompt runs, those that ran a boot block, and births from boots.
    u64 cold_starts = 0, cold_written = 0, prompt_runs = 0, boot_runs = 0, boot_births = 0;
    // The spread measure (gate 3, 13.3): colonies founded, arrivals that
    // founded one (a first colonist, or one after it died out), births from
    // arrivals, colonies alive at the end, and those that settled.
    u32 colonies = 0, foundings = 0, colonies_alive = 0, settled = 0;
    u64 arrival_births = 0;
    // The first colony to settle, as it stood then: its lineage, the place
    // the lineage began in, its place, where its colonist came from, and when
    // it was founded and settled.
    u64 settle_origin = kNone, settle_founded = kNone, settle_tick = kNone;
    u32 settle_home = 0, settle_place = 0, settle_from = 0, settle_alive = 0, settle_copies = 0, settle_programs = 0;
    // Gate 3's arrivals and network (build-plan-gate3.md step 8), on a map:
    // the arrivals and the messages, summed over places, and the SENDs let
    // go (those to a port 0 apart). The named checks (14.1): the places
    // arrivals landed at, those where any ran, and those where none did
    // (10.4); the messages delivered to the places' ports, to how many
    // places, the busiest kTopPlaces' share and the busiest (11.1); the
    // routes' topology changes, columns worked out and cycles, the network
    // step's cycles, and the seed's (11.3); and the place-ticks with the
    // network device off.
    bool mapped = false;
    Arrivals arr;
    NetCounts net;
    u32 in_flight = 0;
    u64 sends = 0, sends_port0 = 0;
    u32 land_places = 0, ran_places = 0, shut_places = 0;
    u64 msg_all = 0, msg_top = 0, msg_most = 0;
    u32 msg_places = 0, msg_most_place = 0;
    u64 route_changes = 0, route_columns = 0, route_cycles = 0, net_cycles = 0, cycles = 0, dark_ticks = 0;
    // The tick of the save it was carried on from (origin.resume; kNone: it
    // began at tick 0).
    u64 carried_from = kNone;
    // Gate 4 (build-plan-gate4.md step 8), with supply.model = sources or
    // heat on: the sources at the end; heat trips and the device-ticks
    // spent tripped, by kind of device (HeatKind); the core-ticks lost to
    // trips (P, then E); the threads ended by them; the ticks reset regions'
    // memory was off by a trip; the places' mean temperature over the run
    // (degrees, summed over places) and the highest any reached; the places
    // with a cooling device, the draw of those and their cut-outs; the
    // solves, their sweeps and those that hit the cap; the places at 0 V at
    // the end; and the low-voltage interrupt's use.
    bool powered = false, heated = false;
    char sources[240] = {};
    u64 trips[kHeatKinds] = {}, tripped_ticks[kHeatKinds] = {};
    u64 heat_ticks[2] = {};
    u64 heat_threads = 0, region_heat_ticks = 0, cooler_cutouts = 0;
    double temp_sum = 0, temp_highest = 0, outside = 0;
    u32 hottest_place = 0, coolers = 0, dark_places = 0;
    u64 solves = 0, sweeps = 0, capped = 0, solve_cycles = 0, heat_cycles = 0;
    u64 lv_handlers = 0, lv_raised = 0, lv_taken = 0, lv_slept = 0, lv_outlived = 0;
};

// Gate 4's power and heat (build-plan-gate4.md step 8), into the seed's
// result.
void tally_gate4(World& w, SeedResult& r) {
    r.powered = w.powered();
    r.heated = w.heated();
    if (!r.powered && !r.heated)
        return;
    if (r.powered) {
        TextBuf t;
        w.sources().describe(&t);
        snprintf(r.sources, sizeof r.sources, "%s", t.c_str());
        r.solves = w.grid().solves;
        r.sweeps = w.grid().sweeps;
        r.capped = w.grid().capped;
    }
    r.solve_cycles = w.solve_cycles;
    r.heat_cycles = w.heat_cycles;
    const double ticks = w.tick_count() ? static_cast<double>(w.tick_count()) : 1.0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& pl = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k) {
            r.trips[k] += pl.trips[k];
            r.tripped_ticks[k] += pl.tripped_ticks[k];
        }
        r.heat_ticks[0] += pl.heat_ticks[0];
        r.heat_ticks[1] += pl.heat_ticks[1];
        r.heat_threads += pl.heat_threads;
        r.region_heat_ticks += pl.region_heat_ticks;
        r.cooler_cutouts += pl.cooler_cutouts;
        r.coolers += pl.has_cooler ? 1 : 0;
        r.dark_places += r.powered && pl.env.voltage_mv == 0 ? 1 : 0;
        r.temp_sum += static_cast<double>(pl.temp_total) / 1000.0 / ticks; // summed in thousandths of a degree
        const double high = static_cast<double>(pl.temp_highest) / kDegree;
        if (i == 0 || high > r.temp_highest) {
            r.temp_highest = high;
            r.hottest_place = i;
        }
        r.lv_handlers += pl.os.lv_handlers;
        r.lv_raised += pl.sched.lv_raised;
        r.lv_taken += pl.sched.lv_taken;
        r.lv_slept += pl.lv_slept;
        r.lv_outlived += pl.lv_outlived;
    }
    r.outside = w.settings().heat.outside / 10.0;
}

// Gate 3's arrivals and network (build-plan-gate3.md step 8), into the
// seed's result.
void tally_network(World& w, const Observer& o, SeedResult& r) {
    r.arr = arrivals(w);
    r.net = w.net().counts();
    r.in_flight = w.net().in_flight();
    r.sends = o.sends;
    r.sends_port0 = o.sends_port0;
    Array<u64> to; // the messages delivered to each place's ports
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& pl = w.place(i);
        const u64 in = landed(pl);
        r.land_places += in ? 1 : 0;
        r.ran_places += pl.region.arrival_runs ? 1 : 0;
        r.shut_places += in && !pl.region.arrival_runs ? 1 : 0;
        const u64 k = delivered_to(pl);
        r.msg_all += k;
        r.msg_places += k ? 1 : 0;
        if (k > r.msg_most) {
            r.msg_most = k;
            r.msg_most_place = i;
        }
        to.push(k);
    }
    sort_u64(to.data(), to.size());
    for (usize k = 0; k < kTopPlaces && k < to.size(); ++k)
        r.msg_top += to[to.size() - 1 - k];
    Routes& rt = w.net().routes();
    r.route_changes = rt.changes;
    r.route_columns = rt.columns;
    r.route_cycles = rt.cycles;
    r.net_cycles = w.net_cycles;
}

// What every seed's task needs.
struct Job {
    const Params* p;
    const char* config_text;
    u64 first_seed;
    SeedResult* results;
    SeedGround* grounds;
    SeedPowerMap* power_maps;
};

u64 instructions_of(World& w) {
    u64 n = 0;
    for (u32 i = 0; i < w.place_count(); ++i)
        n += w.place(i).sched.instructions + w.place(i).region.instructions;
    return n;
}

u32 size_index(const WorldSettings& ws, u32 page) {
    for (u32 i = 0; i < ws.page_size_count; ++i)
        if (ws.page_sizes[i] == page)
            return i;
    return 0;
}

void reach(SeedResult& r, u32 stage, u64 tick) {
    if (r.stage[stage] == kNone || tick < r.stage[stage])
        r.stage[stage] = tick;
}

u32 best_stage(const SeedResult& r) {
    u32 b = 0;
    for (u32 s = 1; s <= 5; ++s)
        if (r.stage[s] != kNone)
            b = s;
    return b;
}

// Every colony's settling now (13.3). `clocks` is each colony's settling
// clock, by its index in the observer.
void evaluate_colonies(World& w, const Observer& o, const Params& p, SeedResult& r, Array<LineageClock>& clocks) {
    const u64 now = w.tick_count();
    while (clocks.size() < o.colony_count())
        clocks.push(LineageClock{});
    u32 settled = 0;
    for (u32 i = 0; i < o.colony_count(); ++i) {
        const ColonyRec& C = o.colony_at(i);
        const u32 programs = w.place(C.place).os.live_programs();
        if (!colony_settles(C, now, programs, p.stages, &clocks[i]))
            continue;
        ++settled;
        if (r.settle_origin == kNone) {
            r.settle_origin = C.origin;
            r.settle_home = o.lineage(C.origin) ? o.lineage(C.origin)->place : 0;
            r.settle_place = C.place;
            r.settle_from = C.from;
            r.settle_founded = C.first;
            r.settle_tick = now;
            r.settle_alive = C.alive;
            r.settle_copies = C.alive_copies;
            r.settle_programs = programs;
        }
    }
    r.settled = settled;
}

// Every lineage's stage now (stages build on each other within a lineage).
// `clocks` is each lineage's stage 4 and 5 clock, by its index in the
// observer. A lineage is checked once it has copied faithfully (before that
// it has no stage), so its hold can't start earlier.
void evaluate(World& w, const Observer& o, const Params& p, SeedResult& r, Array<LineageClock>& clocks) {
    const u64 now = w.tick_count();
    while (clocks.size() < o.lineage_count())
        clocks.push(LineageClock{});
    for (u32 i = 0; i < o.lineage_count(); ++i) {
        const LineageRec& L = o.lineage_at(i);
        if (L.first_copy == kNone) // never copied; a free slot (origin 0) is marked so too
            continue;
        Place& pl = w.place(L.place);
        const u32 s = lineage_stage(L, now, pl.os.live_programs(), p.stages, &clocks[i]);
        const u64 held = clocks[i].held_since;
        if (held != kNone && now - held > r.hold) {
            r.hold = now - held;
            r.hold_origin = L.origin;
            r.hold_place = L.place;
            r.hold_to = now;
        }
        if (s >= 1)
            reach(r, 1, L.first_copy); // exact, from the births
        if (s >= 2)
            reach(r, 2, L.first_viable);
        for (u32 k = 3; k <= s; ++k)
            reach(r, k, now); // as of this sample
        if (s >= 4 && r.s4_origin == kNone) {
            r.s4_origin = L.origin;
            r.s4_place = L.place;
            r.s4_alive = L.home_alive;
            r.s4_alive_faithful = L.home_faithful;
            r.s4_alive_copies = L.home_copies;
            r.s4_held_since = held;
            r.s4_index = i;
            r.s4_programs = pl.os.live_programs();
            r.s4_depth = L.max_depth;
            r.s4_fdepth = L.max_fdepth;
        }
        TypeStat& ts = r.types[pl.mem.strict() ? 1 : 0][size_index(w.settings(), pl.mem.page_size())];
        if (s > ts.best)
            ts.best = s;
    }
}

// The calls' names in the samples' columns, in Call's order.
const char* const kCallColumn[kCallCount] = {"alloc",      "free", "protect", "spawn", "exit", "sleep",
                                             "setclock",   "setcore", "sethandler", "hret", "sense", "send",
                                             "recv",       "dread", "dwrite", "ping"};

// The samples' column names (seed-N.csv), gate 4's among them when the world
// has sources or heat, and gate 5's when asked for (origin.calls).
void write_sample_header(FILE* f, bool gate4, bool gate5) {
    fprintf(f, "tick,programs,threads,threads_started,births,faithful,deaths_exit,deaths_fault,deaths_other,"
               "idle_runs,idle_faults,idle_caps,paths_new,idle_sends,instructions,cross_owner,region_ratio,"
               "hijack,scavenger,genomes,groups,lineages,written,region_copies,program_copies,"
               "near_copies,image_copies,run_alloc,run_alloc_ok,run_spawn,run_spawn_ok,run_cpy,run_cpy_own,"
               "handovers,genomes_held%s",
            gate4 ? ",deaths_heat,sources,sources_swell,sources_out,watts_given,watts_drawn,watts_lost,"
                    "watts_heat_out,temp_mean,temp_high,trips,core_trips,cutouts,lv_handlers,lv_raised,"
                    "lv_taken,lv_slept,lv_outlived,solve_sweeps"
                  : "");
    if (gate5) {
        for (u32 k = 0; k < kCallCount; ++k)
            fprintf(f, ",calls_%s", kCallColumn[k]);
        for (u32 k = 0; k < kCallCount; ++k)
            fprintf(f, ",ok_%s", kCallColumn[k]);
        fprintf(f, ",sends_other,sends_other_ok,spawns_thread,ok_spawns_thread,spawns_program,ok_spawns_program,"
                   "recvs,overruns,over_own,over_kin,over_other,over_stack,over_free,"
                   "overruns_unknown,readers,reader_programs,reader_births,injections,injection_births");
    }
    fputc('\n', f);
}

void write_sample(FILE* f, const Sample& s, bool gate4, bool gate5) {
    fprintf(f, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.4f,%llu,%llu,%llu,%llu,%llu",
            static_cast<unsigned long long>(s.tick), static_cast<unsigned long long>(s.programs),
            static_cast<unsigned long long>(s.threads), static_cast<unsigned long long>(s.threads_started),
            static_cast<unsigned long long>(s.births), static_cast<unsigned long long>(s.faithful),
            static_cast<unsigned long long>(s.deaths[0]), static_cast<unsigned long long>(s.deaths[1]),
            static_cast<unsigned long long>(s.deaths[2] + s.deaths[3] + s.deaths[4] + s.deaths[5] + s.deaths[6] +
                                            s.deaths[7] + s.deaths[8]),
            static_cast<unsigned long long>(s.idle_runs), static_cast<unsigned long long>(s.idle_faults),
            static_cast<unsigned long long>(s.idle_caps), static_cast<unsigned long long>(s.paths_new),
            static_cast<unsigned long long>(s.idle_sends), static_cast<unsigned long long>(s.instructions),
            static_cast<unsigned long long>(s.cross_owner), s.region_ratio, static_cast<unsigned long long>(s.hijack),
            static_cast<unsigned long long>(s.scavenger), static_cast<unsigned long long>(s.genomes),
            static_cast<unsigned long long>(s.groups), static_cast<unsigned long long>(s.lineages));
    // The copying ladder, on the same line (the header names them).
    fprintf(f, ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
            static_cast<unsigned long long>(s.written), static_cast<unsigned long long>(s.region_copies),
            static_cast<unsigned long long>(s.program_copies), static_cast<unsigned long long>(s.near_copies),
            static_cast<unsigned long long>(s.image_copies), static_cast<unsigned long long>(s.run_alloc),
            static_cast<unsigned long long>(s.run_alloc_ok), static_cast<unsigned long long>(s.run_spawn),
            static_cast<unsigned long long>(s.run_spawn_ok), static_cast<unsigned long long>(s.run_cpy),
            static_cast<unsigned long long>(s.run_cpy_own), static_cast<unsigned long long>(s.handovers),
            static_cast<unsigned long long>(s.genomes_held));
    // Gate 4's, when the world has sources or heat (the header names them):
    // watts, degrees, and running totals.
    if (gate4) {
        u64 trips = 0;
        for (u32 k = 0; k < kHeatKinds; ++k)
            trips += s.trips[k];
        fprintf(f, ",%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                static_cast<unsigned long long>(s.deaths[6]), static_cast<unsigned long long>(s.sources),
                static_cast<unsigned long long>(s.sources_swell), static_cast<unsigned long long>(s.sources_out),
                s.power_given / 1e6, s.power_drawn / 1e6, s.power_lost / 1e6, s.heat_out / 1e6, s.temp_mean / 1e6,
                s.temp_high / 1e6, static_cast<unsigned long long>(trips),
                static_cast<unsigned long long>(s.trips[kHeatP] + s.trips[kHeatE]),
                static_cast<unsigned long long>(s.cutouts), static_cast<unsigned long long>(s.lv_handlers),
                static_cast<unsigned long long>(s.lv_raised), static_cast<unsigned long long>(s.lv_taken),
                static_cast<unsigned long long>(s.lv_slept), static_cast<unsigned long long>(s.lv_outlived),
                static_cast<unsigned long long>(s.solve_sweeps));
    }
    // Gate 5's, when asked for: running totals, but for the readers and
    // injections, which are as the world is at the sample.
    if (gate5) {
        for (u32 k = 0; k < kCallCount; ++k)
            fprintf(f, ",%llu", static_cast<unsigned long long>(s.calls[k]));
        for (u32 k = 0; k < kCallCount; ++k)
            fprintf(f, ",%llu", static_cast<unsigned long long>(s.calls_ok[k]));
        const u64 rest[] = {s.sends_other, s.sends_other_ok, s.spawns_thread, s.spawns_thread_ok, s.spawns_program,
                            s.spawns_program_ok, s.recvs,    s.overruns,  s.reached[kReachOwn],
                            s.reached[kReachKin], s.reached[kReachOther], s.reached[kReachStack],
                            s.reached[kReachFree], s.overruns_unknown,    s.readers,         s.reader_programs,
                            s.reader_births,      s.injections,           s.injection_births};
        for (usize k = 0; k < sizeof rest / sizeof rest[0]; ++k)
            fprintf(f, ",%llu", static_cast<unsigned long long>(rest[k]));
    }
    fputc('\n', f);
}

// The seed's most common genomes, with their instructions (13.2), to read
// what is reproducing.
// Makes a folder and any missing parents. False if it can't, or if something
// that isn't a folder is in the way.
bool make_folder(const char* path) {
    char buf[400];
    const usize n = strlen(path);
    if (n == 0 || n >= sizeof buf)
        return false;
    memcpy(buf, path, n + 1);
    for (usize i = 1; i <= n; ++i) {
        if (buf[i] != '/' && buf[i] != '\\' && buf[i] != 0)
            continue;
        if (buf[i - 1] == ':') // a drive, as in E:/
            continue;
        const char c = buf[i];
        buf[i] = 0;
        if (!CreateDirectoryA(buf, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        buf[i] = c;
    }
    const DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void print_genome(FILE* f, const Observer& o, const GenomeRec& g, const char* indent) {
    fprintf(f, "%s%llu births, %u bytes, group %u, first at tick %llu in place %u\n", indent,
            static_cast<unsigned long long>(g.count), g.len, g.group, static_cast<unsigned long long>(g.first_tick),
            g.place);
    fprintf(f, "%s  allocations of %u to %u bytes; %llu births whose whole allocation was the parent's\n", indent,
            g.alloc_min, g.alloc_max, static_cast<unsigned long long>(g.image_copies));
    const u8* b = o.genome_bytes(g);
    fprintf(f, "%s  ", indent);
    for (u32 i = 0; i < g.len && i < kDumpBytes; ++i)
        fprintf(f, "%02x ", b[i]);
    TextBuf text;
    disassemble(b, g.len < kDumpBytes ? g.len : kDumpBytes, &text);
    fprintf(f, "\n%s  %s\n\n", indent, g.len ? text.c_str() : "(empty: nothing written)");
}

// The stage 4 lineage at the seed's end: does it last?
void print_end(FILE* f, const SeedResult& r) {
    if (r.end_extinct != kNone) {
        fprintf(f, "  at the end (tick %llu): extinct since tick %llu\n", static_cast<unsigned long long>(r.ticks),
                static_cast<unsigned long long>(r.end_extinct));
        return;
    }
    fprintf(f, "  at the end (tick %llu): %u of the place's %u programs were in it, %u of them born as copies; ",
            static_cast<unsigned long long>(r.ticks), r.end_alive, r.end_programs, r.end_alive_copies);
    if (r.end_held_since != kNone)
        fprintf(f, "holding the place since tick %llu\n", static_cast<unsigned long long>(r.end_held_since));
    else
        fprintf(f, "not holding the place\n");
}

// The seed's most common genomes, and the lineages that went 10 generations
// deep (origin.generations), each with the genomes that first appeared in it.
void dump_genomes(World& w, const Observer& o, const Params& p, const SeedResult& r, const char* dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/genomes-seed-%llu.txt", dir, static_cast<unsigned long long>(r.seed));
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return;
    }
    Array<u64> order; // count_key(count, index): most common last after sorting
    for (u32 i = 0; i < o.genome_count(); ++i) // the kept genomes (13.2)
        if (o.genome_at(i).state == GenomeState::kept)
            order.push(count_key(o.genome_at(i).count, i));
    sort_u64(order.data(), order.size());

    if (r.s4_origin != kNone) {
        const Place& pl = w.place(r.s4_place);
        fprintf(f, "seed %llu: stage 4 at tick %llu, by the lineage of program %llu in place %u (%s, %u-byte pages)\n",
                static_cast<unsigned long long>(r.seed), static_cast<unsigned long long>(r.stage[4]),
                static_cast<unsigned long long>(r.s4_origin), r.s4_place, pl.mem.strict() ? "strict" : "lax",
                pl.mem.page_size());
        fprintf(f, "  then: %u of the place's %u programs were in it, %u of them born as copies (%u faithful);\n",
                r.s4_alive, r.s4_programs, r.s4_alive_copies, r.s4_alive_faithful);
        fprintf(f, "  it had held the place since tick %llu;\n", static_cast<unsigned long long>(r.s4_held_since));
        fprintf(f, "  %u generations deep, at most %u faithful births in a row\n", r.s4_depth, r.s4_fdepth);
        print_end(f, r);
        fprintf(f, "\n");
    }

    Array<u64> deep; // count_key(births, index)
    for (u32 i = 0; i < o.lineage_count(); ++i) {
        const LineageRec& L = o.lineage_at(i);
        if (L.first_viable != kNone && L.max_depth >= p.stages.generations)
            deep.push(count_key(L.births, i));
    }
    sort_u64(deep.data(), deep.size());
    fprintf(f, "seed %llu: %u lineages %u generations deep after a faithful birth from a faithful parent (most births "
               "first, 20 at most)\n\n",
            static_cast<unsigned long long>(r.seed), static_cast<u32>(deep.size()), p.stages.generations);
    u32 shown = 0;
    for (usize k = deep.size(); k-- > 0 && shown < 20; ++shown) {
        const LineageRec& L = o.lineage_at(count_key_index(deep[k]));
        const Place& pl = w.place(L.place);
        fprintf(f, "lineage of program %llu, place %u (%s, %u-byte pages), born at tick %llu, first copy at %llu\n",
                static_cast<unsigned long long>(L.origin), L.place, pl.mem.strict() ? "strict" : "lax",
                pl.mem.page_size(), static_cast<unsigned long long>(L.born),
                static_cast<unsigned long long>(L.first_copy));
        fprintf(f, "  %llu births, %llu faithful; %u generations deep, at most %u faithful births in a row; %llu groups\n",
                static_cast<unsigned long long>(L.births), static_cast<unsigned long long>(L.faithful), L.max_depth,
                L.max_fdepth, static_cast<unsigned long long>(L.groups));
        if (L.alive)
            fprintf(f, "  at the end: %u alive, %u of them born as copies (%u faithful), of the place's %u programs\n",
                    L.home_alive, L.home_copies, L.home_faithful, pl.os.live_programs());
        else
            fprintf(f, "  extinct at tick %llu\n", static_cast<unsigned long long>(L.extinct_at));
        if (L.colonies)
            fprintf(f, "  %u colonies at other places, %u members alive in them\n", L.colonies,
                    L.alive - L.home_alive);
        fprintf(f, "  its most common genomes (that first appeared in it):\n\n");
        u32 g_shown = 0;
        for (usize j = order.size(); j-- > 0 && g_shown < 4;) {
            const GenomeRec& g = o.genome_at(count_key_index(order[j]));
            if (g.origin != L.origin)
                continue;
            print_genome(f, o, g, "    ");
            ++g_shown;
        }
    }

    fprintf(f, "seed %llu: %llu genomes kept, most common first (40 at most)\n\n",
            static_cast<unsigned long long>(r.seed), static_cast<unsigned long long>(o.genomes_kept()));
    shown = 0;
    for (usize k = order.size(); k-- > 0 && shown < 40; ++shown)
        print_genome(f, o, o.genome_at(count_key_index(order[k])), "");
    fclose(f);
}

// A place snapshot (snapshot.h) of place i, as
// <origin.snapshots>/seed-S-tick-T-place-P.snap, for nova2 trace --snapshot.
void save_place_snapshot(World& w, Str config_text, const Params& p, u64 seed, u32 i) {
    ByteWriter out;
    snapshot_begin(seed, w.tick_count(), config_text, out, kSnapshotPlace);
    out.put_u32(i);
    w.save_place(i, out);
    snapshot_finish(out);
    char path[512];
    snprintf(path, sizeof path, "%s/seed-%llu-tick-%llu-place-%u.snap", p.snapshots,
             static_cast<unsigned long long>(seed), static_cast<unsigned long long>(w.tick_count()), i);
    if (!write_file(path, out.data(), out.size()))
        fprintf(stderr, "nova2: can't write %s\n", path);
}

// A seed's save (build-plan-viewer.md steps 8 and 9) is two files in
// origin.saves, taken between two ticks on the seed's own thread:
//
//   seed-S-tick-T.world.snap  its whole world (snapshot.h): what `nova2
//                             view` opens and `nova2 resume` carries on,
//                             tick for tick as this seed goes on from here;
//   seed-S-tick-T.test.snap   beside it, what the world's save leaves out
//                             and the origin test needs to go on as if it
//                             had never stopped (design.md 13.1): the
//                             counters kept for reports, the observers'
//                             memory, and the test's own record of the seed.
//
// `nova2 origin` with origin.resume carries each seed on from the newest
// pair there. The second file is streamed, since the observers' memory
// runs to gigabytes late in a long seed, and holds records as they lie in
// memory, so it is read back by the build that wrote it.
constexpr u64 kTestTag = kTestSaveTag; // "NOV2TST1" (commands.h: the viewer reads its start)

// What a seed's loop carries from tick to tick beside its world, its
// observer and its result.
struct SeedLoop {
    u64 instr = 0;
    u64 tenths_shown = 0;        // progress lines at each tenth of the ticks or the budget, whichever is further
    Array<LineageClock> clocks;  // each lineage's stage 4 and 5 clock
    Array<LineageClock> cclocks; // each colony's settling clock
    double secs_before = 0;      // a carried-on seed's time before this run, and its cycles
    u64 cycles_before = 0;
    Array<u64> clocked;          // the ticks of the saves taken by the clock this run and still kept
};

void save_path(char* out, usize size, const char* folder, u64 seed, u64 tick, const char* kind) {
    snprintf(out, size, "%s%sseed-%llu-tick-%llu.%s.snap", folder, folder[0] ? "/" : "",
             static_cast<unsigned long long>(seed), static_cast<unsigned long long>(tick), kind);
}

// A file takes its name only once it is whole: it is written beside it
// first, so a run that ends part way through a save leaves no half of one
// to be carried on from.
bool take_name(const char* written, const char* path) {
    return MoveFileExA(written, path, MOVEFILE_REPLACE_EXISTING) != 0;
}

// Saves the seed: its world, then its test. `secs` and `cycles` are its
// running time so far. `clocked`: taken by the clock (origin.save_every),
// so the older of those go (origin.saves_kept); saves asked for are kept.
void save_seed(World& w, Observer& o, Str config_text, const Params& p, u64 seed, SeedSlot* slot,
               const SeedResult& r, SeedLoop& L, double secs, u64 cycles, bool clocked) {
    const u64 tick = w.tick_count();
    char path[512], test[512], part[520];
    save_path(path, sizeof path, p.saves, seed, tick, "world");
    save_path(test, sizeof test, p.saves, seed, tick, "test");
    // The window says the seed is saving while it waits for it (run_view.h).
    struct Saving {
        SeedSlot* s;
        explicit Saving(SeedSlot* slot) : s(slot) {
            if (s)
                seed_saving(s, true);
        }
        ~Saving() {
            if (s)
                seed_saving(s, false);
        }
    } saving(slot);
    u64 world_bytes = 0;
    {
        // Written as it is made: a world of big places is gigabytes.
        SnapshotFile out;
        snprintf(part, sizeof part, "%s.part", path);
        bool ok = out.open(part);
        if (ok) {
            snapshot_begin(seed, tick, config_text, out.writer());
            w.save(out.writer());
            ok = out.close();
        }
        world_bytes = out.bytes();
        if (!ok || !take_name(part, path)) {
            fprintf(stderr, "nova2: can't write %s\n", path);
            DeleteFileA(part);
            return;
        }
    }
    FileOut f;
    snprintf(part, sizeof part, "%s.part", test);
    if (f.open(part)) {
        f.put_u64(kTestTag);
        f.put_u64(seed);
        f.put_u64(tick);
        f.put_u64(sizeof(SeedResult));
        ByteWriter counters;
        w.save_counters(counters);
        f.put_u64(counters.size());
        f.put(counters.data(), counters.size());
        o.flush_records(); // the files hold every row the save counts
        o.save(f);
        f.put(&r, sizeof r);
        f.put_table(L.clocks);
        f.put_table(L.cclocks);
        f.put_u64(L.instr);
        f.put_u64(L.tenths_shown);
        f.put(&secs, sizeof secs);
        f.put_u64(cycles);
    }
    const u64 test_bytes = f.bytes() + 8;
    if (!f.close() || !take_name(part, test)) {
        // The world's save stands by itself: it can be viewed and resumed,
        // but the test can't be carried on from it.
        fprintf(stderr, "nova2: can't write %s\n", test);
        DeleteFileA(part);
        return;
    }
    fprintf(stderr,
            "  seed %llu: saved its world at tick %llu as %s (%llu bytes), and its test beside it (%llu bytes)\n",
            static_cast<unsigned long long>(seed), static_cast<unsigned long long>(tick), path,
            static_cast<unsigned long long>(world_bytes), static_cast<unsigned long long>(test_bytes));
    if (slot)
        seed_saved(slot, tick);
    if (clocked && p.saves_kept) {
        L.clocked.push(tick);
        while (L.clocked.size() > p.saves_kept) {
            save_path(path, sizeof path, p.saves, seed, L.clocked[0], "world");
            save_path(test, sizeof test, p.saves, seed, L.clocked[0], "test");
            DeleteFileA(path);
            DeleteFileA(test);
            for (usize i = 1; i < L.clocked.size(); ++i)
                L.clocked[i - 1] = L.clocked[i];
            L.clocked.pop();
        }
    }
}

// A finished seed's earlier saves: every pair the clock took in this run
// goes but the one at `last`, the tick it ended on, and only if that pair
// is there whole to stand for them.
void drop_earlier_saves(const Params& p, u64 seed, u64 last, SeedLoop& L) {
    char path[512], test[512];
    save_path(path, sizeof path, p.saves, seed, last, "world");
    save_path(test, sizeof test, p.saves, seed, last, "test");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES || GetFileAttributesA(test) == INVALID_FILE_ATTRIBUTES)
        return;
    usize kept = 0;
    for (usize i = 0; i < L.clocked.size(); ++i) {
        if (L.clocked[i] == last) {
            L.clocked[kept++] = last;
            continue;
        }
        save_path(path, sizeof path, p.saves, seed, L.clocked[i], "world");
        save_path(test, sizeof test, p.saves, seed, L.clocked[i], "test");
        DeleteFileA(path);
        DeleteFileA(test);
    }
    while (L.clocked.size() > kept)
        L.clocked.pop();
}

// The newest tick for which a folder holds both of a seed's files, or kNone.
u64 newest_save(const char* folder, u64 seed) {
    char pattern[512], path[512];
    snprintf(pattern, sizeof pattern, "%s%sseed-%llu-tick-*.test.snap", folder, folder[0] ? "/" : "",
             static_cast<unsigned long long>(seed));
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE)
        return kNone;
    u64 best = kNone;
    do {
        unsigned long long s = 0, t = 0;
        char tail[32] = "";
        if (sscanf(found.cFileName, "seed-%llu-tick-%llu.%31s", &s, &t, tail) != 3 || s != seed ||
            strcmp(tail, "test.snap") != 0)
            continue;
        save_path(path, sizeof path, folder, seed, t, "world");
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            continue;
        if (best == kNone || t > best)
            best = t;
    } while (FindNextFileA(h, &found));
    FindClose(h);
    return best;
}

// The settings a seed carried on may differ in from those it was saved
// under: the ones that shape the run and its files, and the pass marks,
// which only the report reads. Any other would make the test's record a
// mix of two tests'.
bool free_setting(Str key) {
    static const char* const kFree[] = {
        "origin.ticks",         "origin.budget",      "origin.seeds",          "origin.parallel",
        "origin.view",          "origin.resume",      "origin.saves",          "origin.save_every",
        "origin.picture",       "origin.picture_minutes",
        "origin.saves_kept",    "origin.samples",     "origin.genomes",        "origin.snapshots",
        "origin.records",       "observe.flush_seconds",
        "origin.windows",       "origin.snapshot_every", "origin.pass_share",  "origin.spread_share",
        "origin.save_minutes",  "origin.progress_minutes", "origin.memory_floor",
        "origin.turnover_share", "origin.crowded_min", "origin.crowded_seeds",
        "origin.ground_ratio",  "origin.ground_min",  "origin.ground_seeds",
        "origin.era",           "origin.era_share",   "origin.era_late",       "origin.era_seeds",
        "origin.era_seed_share", "origin.calls"};
    if (key.n > 4 && strncmp(key.p, "run.", 4) == 0)
        return true;
    for (usize i = 0; i < sizeof kFree / sizeof kFree[0]; ++i)
        if (key.n == strlen(kFree[i]) && memcmp(key.p, kFree[i], key.n) == 0)
            return true;
    return false;
}

// Whether a save's settings and a seed's are the same, the free ones aside.
// False with the first that differs in why.
bool same_settings(Str saved, Str now, TextBuf* why) {
    Config a, b;
    Config::ParseError pe;
    if (!a.parse(saved, &pe) || !b.parse(now, &pe)) {
        why->append("its settings can't be read");
        return false;
    }
    char line[400];
    for (usize i = 0; i < a.count(); ++i) {
        const Str k = a.key(i), v = a.value(i);
        if (free_setting(k))
            continue;
        if (!b.has(k)) {
            snprintf(line, sizeof line, "it was saved with %.*s = %.*s, which this run doesn't set",
                     static_cast<int>(k.n), k.p, static_cast<int>(v.n), v.p);
            why->append(line);
            return false;
        }
        const Str u = b.get_str(k, Str());
        if (u.n != v.n || memcmp(u.p, v.p, v.n) != 0) {
            snprintf(line, sizeof line, "it was saved with %.*s = %.*s, and this run has %.*s",
                     static_cast<int>(k.n), k.p, static_cast<int>(v.n), v.p, static_cast<int>(u.n), u.p);
            why->append(line);
            return false;
        }
    }
    for (usize i = 0; i < b.count(); ++i) {
        const Str k = b.key(i), v = b.value(i);
        if (free_setting(k) || a.has(k))
            continue;
        snprintf(line, sizeof line, "this run sets %.*s = %.*s, which it was saved without", static_cast<int>(k.n),
                 k.p, static_cast<int>(v.n), v.p);
        why->append(line);
        return false;
    }
    return true;
}

// Carries a seed on from its save of `tick` in origin.resume: its world,
// which must have been built from the same settings and have its observer
// watching, then its counters, its observer's memory and the test's record.
// False, with why, if it can't; the seed is then in no state to run.
bool load_seed(World& w, Observer& o, Str config_text, const Params& p, u64 seed, u64 tick, SeedResult& r,
               SeedLoop& L, TextBuf* why) {
    char path[512];
    save_path(path, sizeof path, p.resume, seed, tick, "world");
    {
        Array<u8> bytes;
        SnapshotHeader h;
        if (!read_file(path, &bytes) || !snapshot_parse(bytes.data(), bytes.size(), &h, why)) {
            if (why->size() == 0)
                why->append("its world's save can't be read");
            return false;
        }
        if (h.flags != 0 || h.seed != seed || h.tick != tick) {
            why->append("its world's save is not this seed's at that tick");
            return false;
        }
        if (!same_settings(h.config_text, config_text, why))
            return false;
        ByteReader in(bytes.data() + h.world_offset, h.world_size);
        if (!w.load(in, h.version) || w.tick_count() != tick) {
            why->append("its world's save doesn't match its settings");
            return false;
        }
    }
    save_path(path, sizeof path, p.resume, seed, tick, "test");
    FileIn f;
    u64 tag = 0, s = 0, t = 0, size = 0, n = 0;
    if (!f.open(path) || !f.get_u64(&tag) || !f.get_u64(&s) || !f.get_u64(&t) || !f.get_u64(&size) ||
        tag != kTestTag || s != seed || t != tick || size != sizeof(SeedResult)) {
        why->append("its test's save can't be read, or is another build's");
        return false;
    }
    Array<u8> counters;
    if (!f.get_u64(&n) || n > f.left()) {
        why->append("its test's save is cut short");
        return false;
    }
    counters.resize(static_cast<usize>(n));
    f.get(counters.data(), counters.size());
    ByteReader in(counters.data(), counters.size());
    if (!f.ok() || !w.load_counters(in)) {
        why->append("its counters don't match its world");
        return false;
    }
    if (!o.load(f)) {
        why->append("its observers' memory can't be read, or was gathered under other settings");
        return false;
    }
    f.get(&r, sizeof r);
    f.get_table(&L.clocks);
    f.get_table(&L.cclocks);
    f.get_u64(&L.instr);
    f.get_u64(&L.tenths_shown);
    f.get(&L.secs_before, sizeof L.secs_before);
    f.get_u64(&L.cycles_before);
    if (!f.close() || L.clocks.size() > o.lineage_count() || L.cclocks.size() > o.colony_count()) {
        why->append("its test's save is damaged");
        return false;
    }
    r.seed = seed;
    r.carried_from = tick;
    return true;
}

// Snapshots of the places worth tracing now: each where a lineage
// origin.generations deep has half origin.members alive or more (one near
// stage 4, holding or booming), and `also` if it's a place (a lineage's
// stage 4 place).
void snapshot_places(World& w, const Observer& o, const Params& p, Str config_text, u64 seed, u32 also) {
    Array<u8> mark;
    mark.resize(w.place_count()); // zero-filled
    for (u32 i = 0; i < o.lineage_count(); ++i) {
        const LineageRec& L = o.lineage_at(i);
        if (L.place < mark.size() && L.max_depth >= p.stages.generations && L.home_alive * 2 >= p.stages.members)
            mark[L.place] = 1;
    }
    if (also < mark.size())
        mark[also] = 1;
    for (u32 i = 0; i < mark.size(); ++i)
        if (mark[i])
            save_place_snapshot(w, config_text, p, seed, i);
}

// Each place's draw, into the seed's totals.
void tally_power(World& w, SeedResult& r) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& pl = w.place(i);
        r.energy.cores += pl.energy.cores;
        r.energy.memory += pl.energy.memory;
        r.energy.disk += pl.energy.disk;
        r.energy.network += pl.energy.network;
        r.energy.cooling += pl.energy.cooling;
        r.activations += pl.activations_total;
        if (pl.peak_draw > r.peak_draw) {
            r.peak_draw = pl.peak_draw;
            r.peak_place = i;
        }
        r.mv_total += pl.mv_total;
        r.below_p += pl.below_p;
        r.below_e += pl.below_e;
        r.below_region += pl.below_region;
        r.full_draw += pl.supply.full_draw();
        ++r.places;
        for (u32 k = 0; k < 2; ++k) {
            r.cutouts[k] += pl.cutouts[k];
            r.off_ticks[k] += pl.off_ticks[k];
        }
        for (u32 c = 0; c < pl.core_count(); ++c)
            r.core_ticks[pl.cores[c].kind == DeviceKind::e_core ? 1 : 0] += w.tick_count();
        r.cut_threads += pl.cut_threads;
        r.paused_threads += pl.paused_threads;
        r.cut_runs += pl.region.cut_runs;
        r.disk_cutouts += pl.disk_cutouts;
        r.torn_writes += pl.os.torn_writes;
        r.failed_reads += pl.os.failed_reads;
        r.result_errors += pl.noise.result_errors;
        r.band_errors += pl.noise.band_errors;
        r.memory_cutouts += pl.memory_cutouts;
        r.memory_off += pl.memory_off_ticks;
        r.memory_ticks += static_cast<u64>(pl.mem.device_count()) * w.tick_count();
        r.cells_faded += pl.noise.cells_faded;
        r.faded_threads += pl.faded_threads;
        r.failed_threads += pl.failed_threads;
        r.memory_edge += pl.noise.memory_edge;
        r.cosmic += pl.noise.cosmic;
        r.cold_starts += pl.cold_starts;
        r.cold_written += pl.cold_written;
        r.prompt_runs += pl.region.prompt_runs;
        r.boot_runs += pl.region.boot_runs;
        if (pl.mv_lowest < r.mv_lowest) {
            r.mv_lowest = pl.mv_lowest;
            r.lowest_place = i;
        }
    }
    r.place_ticks += w.tick_count() * w.place_count();
}

// One line of the supply report: a place's mean voltage, the lowest one saw,
// the shares of place-ticks below the cores' and the reset region's memory's
// thresholds, and a place's mean full draw.
void print_supply(const char* label, const SeedResult& r, bool with_place) {
    if (!r.place_ticks || !r.places)
        return;
    const double n = static_cast<double>(r.place_ticks);
    printf("  %-10s %6.1f mV a place, lowest %d mV", label, r.mv_total / n, r.mv_lowest);
    if (with_place)
        printf(" (place %u)", r.lowest_place);
    printf("; below the P cores' threshold %.2f%% of place-ticks, the E cores' %.2f%%, the reset region's memory's "
           "%.3f%%; full draw %.1f W a place\n",
           100 * r.below_p / n, 100 * r.below_e / n, 100 * r.below_region / n, r.full_draw / 1e6 / r.places);
}

// One line of the cut-out report: how often each kind of core cut out and
// the share of its ticks it spent out, what the cut-outs ended, and the
// disk's.
void print_cutouts(const char* label, const SeedResult& r) {
    if (!r.place_ticks)
        return;
    double off[2], each[2];
    for (u32 k = 0; k < 2; ++k) {
        off[k] = r.core_ticks[k] ? 100.0 * r.off_ticks[k] / r.core_ticks[k] : 0;
        each[k] = r.cutouts[k] ? static_cast<double>(r.off_ticks[k]) / r.cutouts[k] : 0;
    }
    printf("  %-10s P cores %llu times, %.1f ticks out a time, %.2f%% of their ticks; E cores %llu, %.1f, %.2f%%; "
           "%llu threads ended, %llu paused, %llu idle runs ended; the disk %llu times, %llu writes torn, %llu reads "
           "failed\n",
           label, static_cast<unsigned long long>(r.cutouts[0]), each[0], off[0],
           static_cast<unsigned long long>(r.cutouts[1]), each[1], off[1],
           static_cast<unsigned long long>(r.cut_threads), static_cast<unsigned long long>(r.paused_threads),
           static_cast<unsigned long long>(r.cut_runs), static_cast<unsigned long long>(r.disk_cutouts),
           static_cast<unsigned long long>(r.torn_writes), static_cast<unsigned long long>(r.failed_reads));
}

// One line of the errors-and-fading report: the cores' wrong results, and
// memory's cut-outs, fading, and bits lost near an edge or to the floor;
// with os.saved_state_fails (`fails`), the threads those bits ended.
void print_errors(const char* label, const SeedResult& r, bool fails) {
    if (!r.place_ticks)
        return;
    const double out = r.memory_ticks ? 100.0 * r.memory_off / r.memory_ticks : 0;
    printf("  %-10s %llu wrong results, %llu of them near an edge; memory cut out %llu times, %.3f%% of its "
           "device-ticks; %llu cells faded, ending %llu thread%s; %llu bits failed near an edge, %llu cosmic flips",
           label, static_cast<unsigned long long>(r.result_errors), static_cast<unsigned long long>(r.band_errors),
           static_cast<unsigned long long>(r.memory_cutouts), out, static_cast<unsigned long long>(r.cells_faded),
           static_cast<unsigned long long>(r.faded_threads), r.faded_threads == 1 ? "" : "s",
           static_cast<unsigned long long>(r.memory_edge),
           static_cast<unsigned long long>(r.cosmic));
    if (fails)
        printf(", ending %llu thread%s", static_cast<unsigned long long>(r.failed_threads),
               r.failed_threads == 1 ? "" : "s");
    printf("\n");
}

// One line of the cold-start report: cold starts, how many booted a block a
// program wrote, the boot blocks run and the births they gave.
void print_cold(const char* label, const SeedResult& r) {
    if (!r.place_ticks)
        return;
    printf("  %-10s %llu cold start%s, %llu of them booting a block a program wrote; %llu boot block%s run, "
           "%llu birth%s from them\n",
           label, static_cast<unsigned long long>(r.cold_starts), r.cold_starts == 1 ? "" : "s",
           static_cast<unsigned long long>(r.cold_written), static_cast<unsigned long long>(r.boot_runs),
           r.boot_runs == 1 ? "" : "s", static_cast<unsigned long long>(r.boot_births), r.boot_births == 1 ? "" : "s");
}

// One line of the power report: a place's mean draw over its ticks, by
// device, in watts; the most one drew in a tick; row activations a tick.
void print_power(const char* label, const SeedResult& r, bool with_place) {
    if (!r.place_ticks)
        return;
    const double n = static_cast<double>(r.place_ticks) * 1e6; // microwatt-ticks to watts a place
    printf("  %-10s %7.2f W a place (cores %.2f, memory %.2f, disk %.2f, network %.2f), at most %.2f W in a tick",
           label, r.energy.total() / n, r.energy.cores / n, r.energy.memory / n, r.energy.disk / n,
           r.energy.network / n, r.peak_draw / 1e6);
    if (with_place)
        printf(" (place %u)", r.peak_place);
    printf("; %.1f row activations a place a tick\n", static_cast<double>(r.activations) / r.place_ticks);
}

// Each place's turnover, into the seed's totals and its OS type's.
void tally_turnover(World& w, const Observer& o, SeedResult& r) {
    for (u32 i = 0; i < w.place_count() && i < o.turnover.size(); ++i) {
        Place& pl = w.place(i);
        r.turn.add(o.turnover[i], r.seed, i);
        r.turn_types[pl.mem.strict() ? 1 : 0][size_index(w.settings(), pl.mem.page_size())].add(o.turnover[i], r.seed,
                                                                                               i);
    }
}

// <origin.windows>/seed-N.csv: every place's finished windows that started
// with a program alive, in the order they ended.
void write_windows(World& w, const Observer& o, const char* dir, u64 seed) {
    char path[512];
    snprintf(path, sizeof path, "%s/seed-%llu.csv", dir, static_cast<unsigned long long>(seed));
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return;
    }
    fprintf(f, "place,strict,page,start,starters,starters_died,births,deaths,crowded,counted,turned,cold\n");
    for (usize k = 0; k < o.windows.size(); ++k) {
        const TurnoverWindow& x = o.windows[k];
        Place& pl = w.place(x.place);
        fprintf(f, "%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u\n", x.place, pl.mem.strict() ? 1u : 0u, pl.mem.page_size(),
                static_cast<unsigned long long>(x.start), x.starters, x.starters_died, x.births, x.deaths,
                x.crowded ? 1u : 0u, x.counted ? 1u : 0u, x.turned ? 1u : 0u, x.cold ? 1u : 0u);
    }
    fclose(f);
}

// Each place's ground, into the seed's totals for its OS kind.
void tally_ground(World& w, const Observer& o, SeedGround& g) {
    for (u32 i = 0; i < w.place_count() && i < o.ground.size(); ++i)
        g.os[w.place(i).mem.strict() ? 1 : 0].add(o.ground[i]);
}

// Whether the place a window is of was strict: the place as set up, or for
// a window after its first respawn the place as it stands (7.8; a place's
// records don't carry its incarnation, so after a second respawn this is
// the latest one's).
bool strict_then(World& w, u32 place, u64 start) {
    return start < w.respawned_at(place) ? w.dealt(place).strict : w.place(place).mem.strict();
}

// A row of a classes file worked with the run's respawns applied.
struct ClassAt {
    u64 tick = 0;
    u32 place = 0, incarnation = 0, order = 0;
    u8 cls = 0, strict = 0;
    float heat = 0, cut = 0;
};

// By place, then tick, then the file's order.
int cmp_class_at(const void* a, const void* b) {
    const ClassAt& x = *static_cast<const ClassAt*>(a);
    const ClassAt& y = *static_cast<const ClassAt*>(b);
    if (x.place != y.place)
        return x.place < y.place ? -1 : 1;
    if (x.tick != y.tick)
        return x.tick < y.tick ? -1 : 1;
    return x.order < y.order ? -1 : x.order > y.order ? 1 : 0;
}

// Place p's respawns at or before a tick, from the run's by place: p's
// ticks, ascending, are when[at[p]] to when[at[p + 1] - 1].
u32 respawns_upto(const Array<u64>& when, const Array<u32>& at, u32 p, u64 tick) {
    u32 lo = at[p], hi = at[p + 1];
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2;
        if (when[mid] <= tick)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo - at[p];
}

// tally_classes for a classes file worked with the run's respawns applied
// (build-plan-gate5.md 11; its rows carry each place's incarnation), `f`
// past its head. A place-window of incarnation k (its place's respawns at or
// before its start: a tick's respawns come first in it) takes its place's
// latest row at or before its start, if that row is of incarnation k; with
// none such it is left out and counted as unclassed. One with a respawn of
// its place inside it is left out and counted apart. Lax or strict is the
// row's: the place as its incarnation was dealt (the default put to Damian
// with "Yes, both leans", 2026-11-22), for those left out too where there is
// such a row. Every row must give its
// place the incarnation the run's respawns give it at the row's tick, or
// the file isn't this run's and the seed is reported without classes.
void tally_by_incarnation(World& w, const Observer& o, FILE* f, const char* path, u64 seed, u64 window,
                          SeedGround& g) {
    const u32 n = w.place_count();
    Array<ClassAt> rows;
    char line[400];
    bool bad = false;
    u32 classings = 0;
    u64 last = 0;
    while (!bad && fgets(line, sizeof line, f)) {
        unsigned long long tick = 0;
        unsigned place = 0, strict = 0, inc = 0;
        char name[8] = "";
        double cut_share = 0, heat_share = 0, mv = 0;
        if (sscanf(line, "%llu,%u,%u,%*u,%*u,%*d,%7[a-z],%*u,%*u,%*u,%*u,%*u,%lf,%lf,%lf,%u", &tick, &place, &strict,
                   name, &cut_share, &heat_share, &mv, &inc) != 8 ||
            place >= n || strict > 1 || (rows.size() && tick < last)) {
            bad = true;
            break;
        }
        classings += !rows.size() || tick != last ? 1 : 0;
        last = tick;
        ClassAt r;
        r.tick = tick;
        r.place = place;
        r.incarnation = inc;
        r.order = static_cast<u32>(rows.size());
        r.strict = static_cast<u8>(strict);
        r.heat = static_cast<float>(heat_share);
        r.cut = static_cast<float>(cut_share);
        r.cls = strcmp(name, "fair") == 0  ? static_cast<u8>(Ground::fair)
                : strcmp(name, "hot") == 0 ? static_cast<u8>(Ground::hot)
                : strcmp(name, "dim") == 0 ? static_cast<u8>(Ground::dim)
                                           : 255;
        bad = r.cls == 255;
        rows.push(r);
    }
    // The run's respawns by place, each place's in tick order: [at[p], at[p + 1]).
    Array<u32> at;
    at.resize(n + 1);
    at.fill(0);
    for (usize k = 0; k < o.respawn_log.size(); ++k)
        if (o.respawn_log[k].place < n)
            ++at[o.respawn_log[k].place + 1];
    for (u32 p = 0; p < n; ++p)
        at[p + 1] += at[p];
    Array<u64> when;
    when.resize(at[n]);
    {
        Array<u32> fill;
        fill.assign(at.data(), n);
        for (usize k = 0; k < o.respawn_log.size(); ++k)
            if (o.respawn_log[k].place < n)
                when[fill[o.respawn_log[k].place]++] = o.respawn_log[k].tick;
    }
    if (!bad && rows.size()) {
        qsort(rows.data(), rows.size(), sizeof(ClassAt), cmp_class_at);
        for (usize k = 0; k < rows.size() && !bad; ++k)
            bad = rows[k].incarnation != respawns_upto(when, at, rows[k].place, rows[k].tick) ||
                  (k && rows[k - 1].place == rows[k].place && rows[k - 1].tick == rows[k].tick);
    }
    if (bad || rows.empty()) {
        fprintf(stderr,
                "nova2: %s isn't a classes file for this world and its respawns: seed %llu is reported without "
                "classes\n",
                path, static_cast<unsigned long long>(seed));
        return;
    }
    // Each place's rows: [first[p], first[p + 1]).
    Array<u32> first;
    first.resize(n + 1);
    first.fill(0);
    for (usize k = 0; k < rows.size(); ++k)
        ++first[rows[k].place + 1];
    for (u32 p = 0; p < n; ++p)
        first[p + 1] += first[p];
    g.classed = true;
    g.by_incarnation = true;
    g.classings = classings;
    for (u64 i = 0; i < o.ground_windows.size(); ++i) {
        const GroundWindow x = o.ground_windows[i];
        if (x.place >= n)
            continue;
        const u32 p = x.place;
        const u32 inc = respawns_upto(when, at, p, x.start);
        // The place's latest row at or before the start, if of incarnation
        // `inc`: lax or strict is its, the counts of those left out too.
        u32 lo = first[p], hi = first[p + 1];
        while (lo < hi) {
            const u32 mid = lo + (hi - lo) / 2;
            if (rows[mid].tick <= x.start)
                lo = mid + 1;
            else
                hi = mid;
        }
        const bool named = lo > first[p] && rows[lo - 1].incarnation == inc;
        const u32 os = named ? rows[lo - 1].strict : strict_then(w, p, x.start) ? 1 : 0;
        // A respawn after its start and before its end (the window holds
        // the ticks from its start to its start + window - 1).
        if (at[p] + inc < at[p + 1] && when[at[p] + inc] - x.start < window) {
            ++g.straddled[os];
            continue;
        }
        if (!named) {
            ++g.unclassed[os];
            continue;
        }
        const ClassAt& r = rows[lo - 1];
        ++g.cls[os].windows[r.cls];
        g.cls[os].starters[r.cls] += x.starters;
        g.cls[os].births[r.cls] += x.births;
        if (os == 0)
            g.more[0].count(r.cls, x, window, r.heat, r.cut);
    }
}

// The judged naming of ground (build-plan-gate4.md 11, the pass mark as
// changed): each place's class from the classing tool's rows
// (<dir>/seed-N-classes.csv; power_worlds.cpp has the columns), a
// place-window taking the latest classing at or before its start. Counted:
// the seed's place-windows by class, lax and strict, with the programs they
// started with and the births in them. A seed with no file is left
// unclassed. A file worked with the run's respawns applied is read by
// incarnation (tally_by_incarnation); one without them leaves out a
// respawned place's windows from its first respawn on.
void tally_classes(World& w, const Observer& o, const char* dir, u64 seed, u64 window, SeedGround& g) {
    char path[400];
    snprintf(path, sizeof path, "%s/seed-%llu-classes.csv", dir, static_cast<unsigned long long>(seed));
    FILE* f = fopen(path, "rb");
    if (!f)
        return;
    char line[400];
    if (fgets(line, sizeof line, f) && strncmp(line, "tick,place,strict,page,cores,links,class,", 41) == 0 &&
        strstr(line, ",incarnation")) {
        tally_by_incarnation(w, o, f, path, seed, window, g);
        fclose(f);
        return;
    }
    rewind(f);
    const u32 n = w.place_count();
    Array<u64> ticks; // the classings' ticks, ascending
    Array<u8> cls;    // [classing x places + place]: its Ground, or 255 with no row
    Array<float> lost; // [(classing x places + place) x 2]: the row's heat share, then its cut share
    bool head = true, bad = false;
    while (!bad && fgets(line, sizeof line, f)) {
        if (head) {
            head = false;
            bad = strncmp(line, "tick,place,strict,page,cores,links,class,", 41) != 0;
            continue;
        }
        unsigned long long tick = 0;
        unsigned place = 0;
        char name[8] = "";
        double cut_share = 0, heat_share = 0;
        if (sscanf(line, "%llu,%u,%*u,%*u,%*u,%*d,%7[a-z],%*u,%*u,%*u,%*u,%*u,%lf,%lf", &tick, &place, name,
                   &cut_share, &heat_share) != 5 ||
            place >= n) {
            bad = true;
            break;
        }
        if (ticks.empty() || ticks[ticks.size() - 1] != tick) {
            if (!ticks.empty() && tick < ticks[ticks.size() - 1]) {
                bad = true;
                break;
            }
            ticks.push(tick);
            for (u32 i = 0; i < n; ++i) {
                cls.push(255);
                lost.push(0);
                lost.push(0);
            }
        }
        lost[((ticks.size() - 1) * n + place) * 2] = static_cast<float>(heat_share);
        lost[((ticks.size() - 1) * n + place) * 2 + 1] = static_cast<float>(cut_share);
        const u8 c = strcmp(name, "fair") == 0  ? static_cast<u8>(Ground::fair)
                     : strcmp(name, "hot") == 0 ? static_cast<u8>(Ground::hot)
                     : strcmp(name, "dim") == 0 ? static_cast<u8>(Ground::dim)
                                                : 255;
        bad = c == 255;
        cls[(ticks.size() - 1) * n + place] = c;
    }
    fclose(f);
    if (bad || ticks.empty()) {
        fprintf(stderr, "nova2: %s isn't a classes file for this world: seed %llu is reported without classes\n",
                path, static_cast<unsigned long long>(seed));
        return;
    }
    g.classed = true;
    g.classings = static_cast<u32>(ticks.size());
    for (u64 i = 0; i < o.ground_windows.size(); ++i) {
        const GroundWindow x = o.ground_windows[i];
        if (x.place >= n)
            continue;
        const u32 os = strict_then(w, x.place, x.start) ? 1 : 0;
        if (window > w.respawned_at(x.place) || x.start > w.respawned_at(x.place) - window) {
            ++g.respawned[os]; // it ends after its place's first respawn
            continue;
        }
        usize k = ticks.size();
        while (k > 0 && ticks[k - 1] > x.start)
            --k;
        const u8 c = k ? cls[(k - 1) * n + x.place] : 255;
        if (c == 255) {
            ++g.unclassed[os];
            continue;
        }
        ++g.cls[os].windows[c];
        g.cls[os].starters[c] += x.starters;
        g.cls[os].births[c] += x.births;
        // The reviewer's measures under this naming: the shares behind the
        // name are the classing row's, what a full place loses there.
        if (os == 0)
            g.more[0].count(c, x, window, lost[((k - 1) * n + x.place) * 2], lost[((k - 1) * n + x.place) * 2 + 1]);
    }
}

// The reviewer's measures under the first naming, and the reading on
// untouched ground, from the ground record alone: a seed's lax place-windows.
// - First naming: a window's name is the record's own, and the shares behind
//   it are what the place lost in the window.
// - Untouched ground: a window that begins with no program is untouched: it
//   counts under the record's name, and that name becomes its place's until
//   the place next begins a window with none. A window with programs counts
//   under its place's name; one with no untouched window before it is left
//   out and counted.
void tally_ground_more(World& w, const Observer& o, u64 window, SeedGround& g) {
    const u32 n = w.place_count();
    Array<u8> name; // by place: its last untouched window's Ground, or 255
    Array<u64> named;
    name.resize(n);
    name.fill(255);
    named.resize(n);
    for (u64 i = 0; i < o.ground_windows.size(); ++i) {
        const GroundWindow x = o.ground_windows[i];
        if (x.place >= n)
            continue;
        const bool lax = !strict_then(w, x.place, x.start);
        const u32 own = static_cast<u32>(x.ground) < kGrounds ? static_cast<u32>(x.ground) : 0;
        const double time = static_cast<double>(x.cores) * static_cast<double>(window);
        if (lax && time > 0)
            g.more[1].count(own, x, window, static_cast<double>(x.heat) / time, static_cast<double>(x.cut) / time);
        if (x.starters == 0) {
            name[x.place] = static_cast<u8>(own);
            named[x.place] = x.start;
            if (lax)
                ++g.untouched.windows[own];
            continue;
        }
        if (!lax)
            continue;
        const u8 c = name[x.place];
        if (c == 255) {
            ++g.untouched_left;
            continue;
        }
        ++g.untouched.windows[c];
        g.untouched.starters[c] += x.starters;
        g.untouched.births[c] += x.births;
        g.untouched_age += x.start - named[x.place];
        ++g.untouched_aged;
    }
}

const char* ground_name(Ground g) {
    static const char* const kNames[kGrounds] = {"fair", "hot", "dim"};
    return kNames[static_cast<u32>(g) < kGrounds ? static_cast<u32>(g) : 0];
}

// <origin.windows>/seed-N-ground.csv: every place's finished windows by their
// ground, in the order they ended: the core-ticks lost to cut-outs and to
// heat trips, of cores x the window's ticks, and the ticks its reset
// region's memory was off.
void write_ground(World& w, const Observer& o, const char* dir, u64 seed) {
    char path[512];
    snprintf(path, sizeof path, "%s/seed-%llu-ground.csv", dir, static_cast<unsigned long long>(seed));
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return;
    }
    // The last ten columns are who led the place as the window began
    // (GroundLead: build-plan-gate5.md 2): the genome group with the most of
    // its programs (-1 and a ref of 0: none had a kept genome), and the
    // lineage with the most.
    fprintf(f, "place,strict,page,start,cores,cut,heat,dark,ground,both,starters,births,"
               "lead_group,lead_group_ref,lead_group_programs,lead_group_copies,second_group_programs,groups,"
               "lead_lineage,lead_lineage_programs,lead_lineage_copies,second_lineage_programs,"
               "worked,births_copies,starters_copies,threads,top_threads,"
               "slow_ppm,failed,worn,flaky,held_1m,held_4m,held_16m,held_more,ended_1m,ended_4m,ended_16m,"
               "ended_more,holder_programs,holder_births\n");
    for (usize k = 0; k < o.ground_windows.size(); ++k) {
        const GroundWindow x = o.ground_windows[k];
        const GroundLead& l = x.lead;
        Place& pl = w.place(x.place);
        fprintf(f, "%u,%u,%u,%llu,%u,%llu,%llu,%u,%s,%u,%u,%u,", x.place, pl.mem.strict() ? 1u : 0u,
                pl.mem.page_size(), static_cast<unsigned long long>(x.start), x.cores,
                static_cast<unsigned long long>(x.cut), static_cast<unsigned long long>(x.heat), x.dark,
                ground_name(x.ground), x.both ? 1u : 0u, x.starters, x.births);
        fprintf(f, "%d,%016llx,%u,%u,%u,%u,%llu,%u,%u,%u,", l.group == kNoGroup ? -1 : static_cast<int>(l.group),
                static_cast<unsigned long long>(l.group_ref), l.group_programs, l.group_copies, l.group_second,
                l.groups, static_cast<unsigned long long>(l.lineage), l.lineage_programs, l.lineage_copies,
                l.lineage_second);
        // The reviewer's measures, after the columns the record had: core
        // time worked, in whole core-ticks (rounded down).
        fprintf(f, "%llu,%u,%u,%u,%u,", static_cast<unsigned long long>(x.worked >> 20), x.births_copies,
                x.starters_copies, x.threads, x.top_threads);
        // Gate 5's (step 11; gap 1): worn ground, a flaky part as it began,
        // and the place's holds, standing as it began and ended in it, by
        // age (under 1, 4 and 16 million ticks, and older).
        fprintf(f, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", x.slow_ppm, x.failed, x.worn ? 1u : 0u,
                x.flaky ? 1u : 0u, x.held[0], x.held[1], x.held[2], x.held[3], x.ended[0], x.ended[1], x.ended[2],
                x.ended[3], x.holder_programs, x.holder_births);
    }
    fclose(f);
}

// <origin.windows>/seed-N-leads.csv: gate 5's first leads (observe.h), a row
// each, in order: the start of the window a genome group first led a place
// as, the place, the group by its reference genome's hash, its programs
// there then and those born there as copies, and the era it came in (-1:
// before stage 4, after the last whole era, or the seed reached no stage 4)
// with whether that era is a late one.
void write_leads(World& w, const Array<FirstLead>& firsts, const char* dir, u64 seed) {
    char path[400];
    snprintf(path, sizeof path, "%s/seed-%llu-leads.csv", dir, static_cast<unsigned long long>(seed));
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return;
    }
    fprintf(f, "tick,place,strict,page,group_ref,programs,copies,era,late\n");
    for (usize i = 0; i < firsts.size(); ++i) {
        const FirstLead& x = firsts[i];
        Place& pl = w.place(x.place);
        fprintf(f, "%llu,%u,%u,%u,%016llx,%u,%u,%d,%u\n", static_cast<unsigned long long>(x.tick), x.place,
                pl.mem.strict() ? 1u : 0u, pl.mem.page_size(), static_cast<unsigned long long>(x.group_ref),
                x.programs, x.copies, x.era == kNoEra ? -1 : static_cast<int>(x.era), x.late ? 1u : 0u);
    }
    fclose(f);
}

// A seed's progress, with the observers' memory by what it is for
// (build-plan-observe.md step 1). `done`: how far through its ticks or its
// budget (under 0: a line by the clock, of a run with no end in sight).
void progress_line(u64 seed, World& w, const Observer& o, const SeedResult& r, double done, double secs) {
    u32 lived = 0; // the places with a program alive
    u64 programs = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const u32 n = w.place(i).os.live_programs();
        lived += n ? 1 : 0;
        programs += n;
    }
    // The lineages alive: how many, the largest by members with the places it
    // has colonies in, and the most generations any has reached.
    u32 alive = 0, largest = 0, largest_colonies = 0, deepest = 0;
    for (u32 i = 0; i < o.lineage_count(); ++i) {
        const LineageRec& L = o.lineage_at(i);
        if (L.origin == 0 || L.alive == 0)
            continue;
        ++alive;
        if (L.alive > largest) {
            largest = L.alive;
            largest_colonies = L.colonies;
        }
        if (L.max_depth > deepest)
            deepest = L.max_depth;
    }
    const ObserverMemory m = o.memory();
    const double mb = 1.0 / (1 << 20);
    char share[16] = "";
    if (done >= 0)
        snprintf(share, sizeof share, " (%3.0f%%)", 100.0 * done);
    fprintf(stderr,
            "  seed %llu: tick %llu%s, %llu births, best stage %u, %.0f s; observers %.0f MB (genomes "
            "%.0f, groups %.0f, lineages %.0f, programs %.0f, history %.0f); %llu programs in %u of %u places; "
            "%u lineages alive, the largest %u members with %u colonies, the deepest alive %u generations; %llu "
            "groups ever, %llu kept genomes let go\n",
            static_cast<unsigned long long>(seed), static_cast<unsigned long long>(w.tick_count()), share,
            static_cast<unsigned long long>(o.total_births), best_stage(r), secs, m.total() * mb, m.genomes * mb,
            m.groups * mb, m.lineages * mb, m.programs * mb, m.history * mb,
            static_cast<unsigned long long>(programs), lived, w.place_count(), alive, largest, largest_colonies,
            deepest, static_cast<unsigned long long>(o.groups_ever()),
            static_cast<unsigned long long>(o.genomes_forgotten()));
}

// The memory the machine has left, in megabytes: the lesser of what is free
// and what may still be committed (origin.memory_floor).
u64 memory_left_mb() {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms))
        return ~0ull;
    const u64 least = ms.ullAvailPhys < ms.ullAvailPageFile ? ms.ullAvailPhys : ms.ullAvailPageFile;
    return least >> 20;
}

void run_seed(void* ctx, u32 k) {
    const Job& job = *static_cast<const Job*>(ctx);
    const Params& p = *job.p;
    SeedResult& r = job.results[k];
    const u64 seed = job.first_seed + k;
    r.seed = seed;
    Config cfg;
    Config::ParseError pe;
    cfg.parse(job.config_text, &pe);
    char v[32];
    snprintf(v, sizeof v, "%llu", static_cast<unsigned long long>(seed));
    cfg.set("world.seed", v);
    World w;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: seed %llu: %s\n", static_cast<unsigned long long>(seed),
                cfg.ok() ? why.c_str() : cfg.value_error());
        return;
    }
    {
        TextBuf t;
        w.map().describe(&t);
        snprintf(r.map, sizeof r.map, "%s", t.c_str());
        TextBuf pm;
        w.map().describe_power(&pm);
        snprintf(job.power_maps[k].line, sizeof job.power_maps[k].line, "%s", pm.c_str());
    }
    r.mapped = w.map().placed();
    TextBuf seed_text; // this seed's settings, for its place snapshots
    cfg.write(seed_text);
    // Carried on from a save (origin.resume), if the folder has one of this
    // seed's; if not, it starts at tick 0 as ever.
    const u64 from = p.resume[0] ? newest_save(p.resume, seed) : kNone;
    if (p.resume[0] && from == kNone)
        fprintf(stderr, "  seed %llu: no save of it in %s: it starts at tick 0\n",
                static_cast<unsigned long long>(seed), p.resume);
    if (p.busy && from == kNone)
        place_busy(w);
    Observer o;
    o.init(p.obs);
    // Its finished records to files as it goes (13.1), carried on with the
    // seed: then its save says how much of each file to keep.
    if (p.records[0] && !o.records_to(p.records, seed, p.flush_seconds, from != kNone)) {
        fprintf(stderr, "nova2: seed %llu: can't make its records in %s\n", static_cast<unsigned long long>(seed),
                p.records);
        return;
    }
    w.watch(&o);
    w.start_threads(p.threads_each);
    // The window on the run, open or not: this seed's world is there to be
    // watched from here to the end of its ticks.
    SeedSlot* slot = p.view ? &p.view->slots[k] : nullptr;
    if (slot)
        seed_begin(slot, &w, &o);
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& pl = w.place(i);
        ++r.types[pl.mem.strict() ? 1 : 0][size_index(w.settings(), pl.mem.page_size())].places;
    }
    // Its samples as a CSV, as the run goes: only when the records stay in
    // memory. With the records in files (origin.records) seed-N.samples holds
    // every row already, and `nova2 samples` makes the same CSV from it when
    // one is wanted (Damian, 2026-11-20: samples are stored once).
    FILE* csv = nullptr;
    if (p.samples[0] && !p.records[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/seed-%llu.csv", p.samples, static_cast<unsigned long long>(seed));
        csv = fopen(path, "w");
        if (!csv)
            fprintf(stderr, "nova2: can't write %s\n", path);
        else
            write_sample_header(csv, w.powered() || w.heated(), p.calls);
    }
    SeedLoop loop;
    if (from != kNone) {
        TextBuf refused;
        if (!load_seed(w, o, seed_text.view(), p, seed, from, r, loop, &refused)) {
            fprintf(stderr, "nova2: seed %llu can't be carried on from tick %llu in %s: %s\n",
                    static_cast<unsigned long long>(seed), static_cast<unsigned long long>(from), p.resume,
                    refused.c_str());
            if (slot)
                seed_end(slot);
            if (csv)
                fclose(csv);
            r = SeedResult{};
            r.seed = seed;
            return;
        }
        // Its samples so far, as the run it is carried on from wrote them.
        if (csv)
            for (usize i = 0; i < o.samples.size(); ++i)
                write_sample(csv, o.samples[i], w.powered() || w.heated(), p.calls);
        fprintf(stderr, "  seed %llu: carried on from tick %llu (%llu births so far)\n",
                static_cast<unsigned long long>(seed), static_cast<unsigned long long>(from),
                static_cast<unsigned long long>(o.total_births));
    }
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    u64 taken = o.samples_taken();
    usize rows = o.samples.size(); // of them, written
    u64& instr = loop.instr;
    u64& tenths_shown = loop.tenths_shown;
    Array<LineageClock>& clocks = loop.clocks;
    Array<LineageClock>& cclocks = loop.cclocks;
    u64 last_save = from; // the tick of its newest save
    double last_save_at = t0, last_progress_at = t0, last_memory_at = t0; // on the wall's clock
    while (w.tick_count() < p.ticks && (p.budget == 0 || instr < p.budget)) {
        w.tick();
        if (r.mapped)
            for (u32 i = 0; i < w.place_count(); ++i)
                r.dark_ticks += w.place(i).network_state.on ? 0 : 1;
        bool reached4 = false;
        bool sampled = false;
        if (o.samples_taken() != taken) {
            taken = o.samples_taken();
            sampled = true;
            if (!o.records_ok()) { // a full or failing disk: stop, rather than run on without records
                fprintf(stderr, "nova2: seed %llu: its records in %s can't be written: it stops at tick %llu\n",
                        static_cast<unsigned long long>(seed), p.records,
                        static_cast<unsigned long long>(w.tick_count()));
                break;
            }
            const u64 before4 = r.stage[4];
            evaluate(w, o, p, r, clocks);
            evaluate_colonies(w, o, p, r, cclocks);
            reached4 = before4 == kNone && r.stage[4] != kNone;
            const Sample& s = o.last_sample();
            if (s.region_ratio < r.ratio_min)
                r.ratio_min = s.region_ratio;
            if (o.samples.size() != rows) { // a written one (observe.write_every)
                rows = o.samples.size();
                if (csv)
                    write_sample(csv, s, w.powered() || w.heated(), p.calls);
            }
            instr = s.instructions;
            if (slot) {
                double done = static_cast<double>(w.tick_count()) / p.ticks;
                if (p.budget && static_cast<double>(instr) / p.budget > done)
                    done = static_cast<double>(instr) / p.budget;
                seed_publish(slot, w.tick_count(), o.total_births, best_stage(r), r.stage,
                             static_cast<u32>(done < 1 ? done * 1000 : 1000));
            }
        }
        if (p.snapshots[0] && (reached4 || (p.snapshot_every && w.tick_count() % p.snapshot_every == 0)))
            snapshot_places(w, o, p, seed_text.view(), seed, reached4 ? r.s4_place : ~0u);
        u64 tenths = w.tick_count() * 10 / p.ticks;
        if (p.budget && instr * 10 / p.budget > tenths)
            tenths = instr * 10 / p.budget;
        if (tenths > tenths_shown) {
            tenths_shown = tenths;
            double done = static_cast<double>(w.tick_count()) / p.ticks;
            if (p.budget && static_cast<double>(instr) / p.budget > done)
                done = static_cast<double>(instr) / p.budget;
            progress_line(seed, w, o, r, done, clock_seconds() - t0);
        }
        // The window reads the world here, between two ticks, or not at all;
        // and here the world is saved, when asked or when it is time.
        const bool asked = slot && seed_between_ticks(slot);
        const bool clocked = p.save_every && w.tick_count() % p.save_every == 0;
        // The wall's clock, looked at once a sample: a save or a progress line
        // that is due, and the machine's memory.
        bool timed = false, short_of = false;
        if (sampled && (p.save_minutes > 0 || p.progress_minutes > 0 || p.memory_floor)) {
            const double now = clock_seconds();
            timed = p.save_minutes > 0 && now - last_save_at >= p.save_minutes * 60;
            if (p.progress_minutes > 0 && now - last_progress_at >= p.progress_minutes * 60) {
                last_progress_at = now;
                progress_line(seed, w, o, r, -1, now - t0);
            }
            if (p.memory_floor && now - last_memory_at >= 5) {
                last_memory_at = now;
                const u64 left = memory_left_mb();
                if (left < p.memory_floor) {
                    short_of = true;
                    fprintf(stderr,
                            "nova2: seed %llu: the machine has %llu MB of memory left, under origin.memory_floor "
                            "(%llu): it saves and stops at tick %llu\n",
                            static_cast<unsigned long long>(seed), static_cast<unsigned long long>(left),
                            static_cast<unsigned long long>(p.memory_floor),
                            static_cast<unsigned long long>(w.tick_count()));
                }
            }
        }
        if (asked || clocked || timed || short_of) {
            save_seed(w, o, seed_text.view(), p, seed, slot, r, loop, loop.secs_before + clock_seconds() - t0,
                      loop.cycles_before + __rdtsc() - c0, clocked || timed || short_of);
            last_save = w.tick_count();
            last_save_at = clock_seconds();
        }
        if (short_of)
            break;
    }
    // A seed that saves by the clock saves where it stops too, so it can be
    // carried on from there with more ticks or a bigger budget.
    if ((p.save_every || p.save_minutes > 0) && last_save != w.tick_count())
        save_seed(w, o, seed_text.view(), p, seed, slot, r, loop, loop.secs_before + clock_seconds() - t0,
                  loop.cycles_before + __rdtsc() - c0, true);
    // A seed that has run its ticks or its budget keeps the save it ended
    // on, and no earlier one the clock took in this run: the last is the
    // one a longer run carries on from and the viewer opens (Damian,
    // 2026-11-20). One stopped short (memory, a failing disk) keeps what it
    // has, and so do saves a person asked for.
    if (w.tick_count() >= p.ticks || (p.budget && instr >= p.budget))
        drop_earlier_saves(p, seed, w.tick_count(), loop);
    if (slot) {
        seed_publish(slot, w.tick_count(), o.total_births, best_stage(r), r.stage, 1000);
        seed_end(slot); // from here the world is this thread's alone again
    }
    o.sample(w); // the end, whatever the interval
    evaluate(w, o, p, r, clocks);
    evaluate_colonies(w, o, p, r, cclocks);
    r.colonies = o.colony_count() - o.current_colonies(); // the current's aren't spread (12.6)
    for (u32 i = 0; i < o.colony_count(); ++i) {
        r.foundings += o.colony_at(i).foundings;
        r.colonies_alive += o.colony_at(i).alive && !o.colony_at(i).by_current() ? 1 : 0;
    }
    r.arrival_births = o.births_by_cause[static_cast<u8>(BirthCause::arrival)];
    r.ticks = w.tick_count();
    if (r.s4_origin != kNone) {
        const LineageRec& L = o.lineage_at(r.s4_index);
        r.end_alive = L.home_alive;
        r.end_alive_copies = L.home_copies;
        r.end_programs = w.place(L.place).os.live_programs();
        r.end_held_since = r.s4_index < clocks.size() ? clocks[r.s4_index].held_since : kNone;
        r.end_extinct = L.alive ? kNone : L.extinct_at;
    }
    if (p.snapshots[0]) // the end: the places near stage 4, and the stage 4 one
        snapshot_places(w, o, p, seed_text.view(), seed, r.s4_origin != kNone ? r.s4_place : ~0u);
    if (p.genomes[0])
        dump_genomes(w, o, p, r, p.genomes);
    tally_turnover(w, o, r);
    tally_ground(w, o, job.grounds[k]);
    tally_power(w, r);
    tally_gate4(w, r);
    if (r.mapped)
        tally_network(w, o, r);
    r.boot_births = o.births_by_cause[static_cast<u8>(BirthCause::boot)];
    // Gate 5's measure, from the ground record's rows.
    Array<FirstLead> firsts;
    tally_eras(o.ground_windows, r.stage[4], w.tick_count(), p.era, p.stages, &job.grounds[k].eras, &firsts);
    {
        // And gate 5's readings, as the last sample and the observers have them.
        SeedGround& g = job.grounds[k];
        const Sample& s = o.last_sample();
        for (u32 c = 0; c < kCallCount; ++c) {
            g.calls[c] = s.calls[c];
            g.calls_ok[c] = s.calls_ok[c];
        }
        g.sends_other = s.sends_other;
        g.sends_other_ok = s.sends_other_ok;
        g.spawns_thread = s.spawns_thread;
        g.spawns_thread_ok = s.spawns_thread_ok;
        g.spawns_program = s.spawns_program;
        g.spawns_program_ok = s.spawns_program_ok;
        g.recvs = o.recvs;
        g.overruns = o.overruns;
        for (u32 c = 0; c < kReaches; ++c)
            g.reached[c] = o.reached[c];
        g.overruns_unknown = o.overruns_unknown;
        g.births = o.total_births;
        g.reader_births = o.reader_births;
        g.injection_births = o.injection_births;
        g.program_samples = o.program_samples;
        g.reader_program_samples = o.reader_program_samples;
        g.injections_seen = o.injections_seen;
        g.injected_programs = o.injected_programs;
        g.watched = o.watched_overruns();
    }
    // Gate 5's watching: what its mechanisms did, its named checks and the
    // four mechanics' readings (step 11).
    tally_watch(w, o, watch_params(p), r.stage[4], firsts, job.grounds[k].watch);
    if (p.windows[0]) {
        write_windows(w, o, p.windows, seed);
        write_ground(w, o, p.windows, seed);
        write_leads(w, firsts, p.windows, seed);
        write_watch_files(o, p.windows, seed, p.obs.turnover.window, kCallColumn);
        tally_classes(w, o, p.windows, seed, p.obs.turnover.window, job.grounds[k]);
        job.grounds[k].watch.left_out = job.grounds[k].respawned[0] + job.grounds[k].respawned[1];
    }
    if (p.obs.turnover.window)
        tally_ground_more(w, o, p.obs.turnover.window, job.grounds[k]);
    if (csv) {
        write_sample(csv, o.last_sample(), w.powered() || w.heated(), p.calls);
        fclose(csv);
    }
    o.flush_records(); // the record files whole, the last sample and the lineages' tally with them
    r.secs = loop.secs_before + clock_seconds() - t0;
    r.cycles = loop.cycles_before + __rdtsc() - c0;
    r.ticks = w.tick_count();
    r.instructions = instructions_of(w);
    r.last = o.last_sample();
    if (r.last.region_ratio < r.ratio_min)
        r.ratio_min = r.last.region_ratio;
    r.ok = true;
}

// One line of the turnover report: windows and crowded windows, how many
// turned over, deaths per program per day, the mean life in days (programs
// alive over deaths: Little's law, exact in a steady state whatever shape the
// lifetimes have), and the longest run without turnover.
void print_turn(const char* label, const TurnStat& t, u64 day, bool with_seed) {
    printf("  %-18s %8u %7u %8u %7u", label, t.windows, t.turned, t.crowded, t.crowded_turned);
    const u64 dead = t.all_deaths();
    if (t.program_ticks && dead) {
        const double rate = static_cast<double>(dead) * day / t.program_ticks;
        printf(" %11.4f %10.1f", rate, 1.0 / rate);
    } else {
        printf(" %11s %10s", "-", "-");
    }
    if (t.longest) {
        printf("  %u windows, place %u", t.longest, t.longest_place);
        if (with_seed)
            printf(" of seed %llu", static_cast<unsigned long long>(t.longest_seed));
        printf(", to tick %llu", static_cast<unsigned long long>(t.longest_end));
    }
    printf("\n");
}

// One line of the ground report: for each kind of ground, its place-windows
// and the programs one started with, on average.
void print_ground(const char* label, const GroundStat& s) {
    printf("  %-20s", label);
    for (u32 g = 0; g < kGrounds; ++g) {
        printf(" %10llu", static_cast<unsigned long long>(s.windows[g]));
        if (s.windows[g])
            printf(" %9.2f", s.mean(static_cast<Ground>(g)));
        else
            printf(" %9s", "-");
    }
    printf("\n");
}

// Gate 4's mark as changed (build-plan-gate4.md 11): two kinds of ground
// differ if one's place-windows start with `ratio` times the other's
// programs or more, whichever way. A kind whose windows start with none
// differs from one whose windows start with any.
bool differs(double a, double b, double ratio) { return (a > 0 || b > 0) && (a >= ratio * b || b >= ratio * a); }
// Which way round fair ground and another kind stand, for the report.
const char* which_way(double fair, double other, const char* name, char* out, usize size) {
    if (fair == other)
        snprintf(out, size, " (the same)");
    else if (fair > other && other > 0)
        snprintf(out, size, " (fair ground holds more: %.2f times %s's)", fair / other, name);
    else if (fair > other)
        snprintf(out, size, " (fair ground holds more: %s ground's windows start with none)", name);
    else if (fair > 0)
        snprintf(out, size, " (%s ground holds more: %.2f times fair's)", name, other / fair);
    else
        snprintf(out, size, " (%s ground holds more: fair ground's windows start with none)", name);
    return out;
}

double share_of(u32 a, u32 b) { return b ? 100.0 * a / b : 0.0; }
double share_of(u64 a, u64 b) { return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0; }

// The fewest of n that make up `share` of it: 9 of 10 at 0.9, though 0.9 in
// binary is a hair over.
u32 need_of(double share, u32 n) {
    const double x = share * n - 1e-9;
    const u32 k = x > 0 ? static_cast<u32>(x) : 0;
    return k < x ? k + 1 : k;
}

const char* met(bool ok) { return ok ? "met" : "not met"; }

// The reviewer's measures for one naming of ground (GroundMore), over the
// mark's own windows. Four tables, a row a kind of ground.
void print_more(const GroundMore& m, double ratio) {
    static const char* const kNames[kGrounds] = {"fair", "hot", "dim"};
    printf("    %-5s %9s %9s %9s   %-26s %-26s %8s\n", "", "windows", "programs", "copies",
           "lost to heat: low mid high", "to cut-outs: low mid high", "worked");
    for (u32 k = 0; k < kGrounds; ++k) {
        if (!m.windows[k]) {
            printf("    %-5s %9u\n", kNames[k], 0u);
            continue;
        }
        const double w = static_cast<double>(m.windows[k]);
        printf("    %-5s %9llu %9.2f %9.2f   %7.1f%% %7.1f%% %7.1f%%  %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n", kNames[k],
               static_cast<unsigned long long>(m.windows[k]), m.starters[k] / w, m.copies[k] / w,
               100.0 * m.lo[0][k], 100 * m.middle(0, k), 100.0 * m.hi[0][k], 100.0 * m.lo[1][k],
               100 * m.middle(1, k), 100.0 * m.hi[1][k],
               m.core_time[k] ? 100.0 * (static_cast<double>(m.worked[k]) / 1048576.0) / m.core_time[k] : 0.0);
    }
    // The mark's two ratios again on the programs born as copies alone.
    double c[kGrounds];
    for (u32 k = 0; k < kGrounds; ++k)
        c[k] = m.windows[k] ? static_cast<double>(m.copies[k]) / m.windows[k] : 0.0;
    char way[120];
    printf("    on programs born as copies alone (not the mark): fair against hot %s%s;",
           differs(c[0], c[1], ratio) ? "differ" : "don't differ", which_way(c[0], c[1], "hot", way, sizeof way));
    printf(" fair against dim %s%s\n", differs(c[0], c[2], ratio) ? "differ" : "don't differ",
           which_way(c[0], c[2], "dim", way, sizeof way));
    printf("    births for each million core-ticks: of all core time, of core time not cut out or tripped, of core "
           "time worked; then births that are copies, the same three ways\n");
    for (u32 k = 0; k < kGrounds; ++k) {
        if (!m.windows[k])
            continue;
        const double all = static_cast<double>(m.core_time[k]);
        const double usable = all - static_cast<double>(m.cut[k]) - static_cast<double>(m.heat[k]);
        const double worked = static_cast<double>(m.worked[k]) / 1048576.0;
        const double b = 1e6 * static_cast<double>(m.births[k]), bc = 1e6 * static_cast<double>(m.births_copies[k]);
        printf("    %-5s %12.1f %12.1f %12.1f   %12.1f %12.1f %12.1f\n", kNames[k], all > 0 ? b / all : 0.0,
               usable > 0 ? b / usable : 0.0, worked > 0 ? b / worked : 0.0, all > 0 ? bc / all : 0.0,
               usable > 0 ? bc / usable : 0.0, worked > 0 ? bc / worked : 0.0);
    }
    printf("    windows held by one program (as they began it had over half the place's threads, and as many as "
           "the place has cores or more): windows, programs, copies and core time worked; then the rest\n");
    for (u32 k = 0; k < kGrounds; ++k) {
        if (!m.windows[k])
            continue;
        const u64 rest = m.windows[k] - m.held[k], rest_time = m.core_time[k] - m.held_time[k];
        printf("    %-5s %9llu %9.2f %9.2f %7.1f%%   %9llu %9.2f %9.2f %7.1f%%\n", kNames[k],
               static_cast<unsigned long long>(m.held[k]),
               m.held[k] ? static_cast<double>(m.held_starters[k]) / m.held[k] : 0.0,
               m.held[k] ? static_cast<double>(m.held_copies[k]) / m.held[k] : 0.0,
               m.held_time[k] ? 100.0 * (static_cast<double>(m.held_worked[k]) / 1048576.0) / m.held_time[k] : 0.0,
               static_cast<unsigned long long>(rest),
               rest ? static_cast<double>(m.starters[k] - m.held_starters[k]) / rest : 0.0,
               rest ? static_cast<double>(m.copies[k] - m.held_copies[k]) / rest : 0.0,
               rest_time ? 100.0 * (static_cast<double>(m.worked[k] - m.held_worked[k]) / 1048576.0) / rest_time
                         : 0.0);
    }
}

// Gate 3's arrivals and network, and its named checks (14.1;
// build-plan-gate3.md step 8), for the seeds on a map.
// Gate 4's section (build-plan-gate4.md step 8): each seed's sources, its
// solves, its heat and the low-voltage interrupt's use, then all seeds'.
void print_gate4(const SeedResult* results, u32 seeds) {
    typedef unsigned long long ull;
    static const char* const kKind[kHeatKinds] = {"P cores", "E cores", "memory", "disk", "network", "cooling"};
    bool powered = false, heated = false;
    for (u32 k = 0; k < seeds; ++k) {
        powered = powered || (results[k].ok && results[k].powered);
        heated = heated || (results[k].ok && results[k].heated);
    }
    if (!powered && !heated)
        return;
    printf("\npower and heat (gate 4): %s; %s\n",
           powered ? "the voltage is solved over the map from its sources (supply.model = sources)"
                   : "each place has its own supply (supply.model = place)",
           heated ? "heat is on" : "heat is off");
    SeedResult all;
    u32 n = 0;
    for (u32 k = 0; k <= seeds; ++k) {
        const SeedResult& r = k < seeds ? results[k] : all;
        if (k < seeds && !r.ok)
            continue;
        char label[32];
        if (k < seeds)
            snprintf(label, sizeof label, "seed %llu", static_cast<ull>(r.seed));
        else
            snprintf(label, sizeof label, "all seeds");
        if (k < seeds) {
            ++n;
            all.places += r.places;
            all.place_ticks += r.place_ticks;
            all.solves += r.solves;
            all.sweeps += r.sweeps;
            all.capped += r.capped;
            all.dark_places += r.dark_places;
            all.coolers += r.coolers;
            all.cooler_cutouts += r.cooler_cutouts;
            all.energy.cooling += r.energy.cooling;
            all.heat_threads += r.heat_threads;
            all.region_heat_ticks += r.region_heat_ticks;
            all.temp_sum += r.temp_sum;
            all.outside = r.outside;
            if (n == 1 || r.temp_highest > all.temp_highest)
                all.temp_highest = r.temp_highest;
            for (u32 j = 0; j < kHeatKinds; ++j) {
                all.trips[j] += r.trips[j];
                all.tripped_ticks[j] += r.tripped_ticks[j];
            }
            for (u32 j = 0; j < 2; ++j) {
                all.heat_ticks[j] += r.heat_ticks[j];
                all.core_ticks[j] += r.core_ticks[j];
                all.cutouts[j] += r.cutouts[j];
                all.off_ticks[j] += r.off_ticks[j];
            }
            all.lv_handlers += r.lv_handlers;
            all.lv_raised += r.lv_raised;
            all.lv_taken += r.lv_taken;
            all.lv_slept += r.lv_slept;
            all.lv_outlived += r.lv_outlived;
        } else if (n < 2) {
            break; // one seed: its own lines say it all
        }
        if (powered && k < seeds)
            printf("  %-10s %s\n", label, r.sources);
        if (powered)
            printf("  %-10s solves: %.1f sweeps each, %.3f%% stopped at the cap; %u of %u places at 0 V at the end\n",
                   label, r.solves ? static_cast<double>(r.sweeps) / r.solves : 0.0,
                   r.solves ? 100.0 * r.capped / r.solves : 0.0, r.dark_places, r.places);
        if (heated) {
            const u64 cores = r.core_ticks[0] + r.core_ticks[1];
            printf("  %-10s heat: a place %.2f degrees above the outside on average, the hottest reached %.1f", label,
                   r.places ? r.temp_sum / r.places - r.outside : 0.0, r.temp_highest);
            if (k < seeds)
                printf(" (place %u)", r.hottest_place);
            printf("; core time lost to heat trips %.3f%%, to cut-outs %.3f%%; %llu threads ended by heat; reset "
                   "regions' memory tripped for %llu ticks\n",
                   cores ? 100.0 * (r.heat_ticks[0] + r.heat_ticks[1]) / cores : 0.0,
                   cores ? 100.0 * (r.off_ticks[0] + r.off_ticks[1]) / cores : 0.0, static_cast<ull>(r.heat_threads),
                   static_cast<ull>(r.region_heat_ticks));
            printf("  %-10s trips:", label);
            for (u32 j = 0; j < kHeatKinds; ++j)
                printf("%s %s %llu", j ? "," : "", kKind[j], static_cast<ull>(r.trips[j]));
            printf("; cooling devices at %u of %u places, drawing %.3f W each on average, cut out %llu times\n",
                   r.coolers, r.places,
                   r.coolers && r.place_ticks && r.places
                       ? r.energy.cooling / 1e6 / (static_cast<double>(r.place_ticks) / r.places) / r.coolers
                       : 0.0,
                   static_cast<ull>(r.cooler_cutouts));
        }
        printf("  %-10s the low-voltage interrupt: %llu handlers registered, raised %llu times, taken %llu; %llu "
               "threads slept within a tick of taking one, and %llu of those outlived the cut\n",
               label, static_cast<ull>(r.lv_handlers), static_cast<ull>(r.lv_raised), static_cast<ull>(r.lv_taken),
               static_cast<ull>(r.lv_slept), static_cast<ull>(r.lv_outlived));
    }
}

void print_gate3(const SeedResult* results, u32 seeds) {
    typedef unsigned long long ull;
    printf("\narrivals: copies SENT to a port 0 from another place and run there as prompt runs (10.4), and the "
           "network's messages\n");
    Arrivals arr;
    NetCounts net;
    u32 flying = 0;
    u64 sends = 0, sends0 = 0;
    for (u32 k = 0; k < seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok || !r.mapped)
            continue;
        printf("seed %llu: %llu SENDs let go, %llu of them to a port 0\n", static_cast<ull>(r.seed),
               static_cast<ull>(r.sends), static_cast<ull>(r.sends_port0));
        print_arrivals(r.arr);
        print_network(r.net, r.in_flight);
        arr.add(r.arr);
        add_counts(net, r.net);
        flying += r.in_flight;
        sends += r.sends;
        sends0 += r.sends_port0;
    }
    printf("all seeds: %llu SENDs let go, %llu of them to a port 0\n", static_cast<ull>(sends),
           static_cast<ull>(sends0));
    print_arrivals(arr);
    print_network(net, flying);

    printf("\nthe named checks (14.1):\n");
    printf("  arrivals shut out (10.4): the places arrivals landed at, those where any ran, and those where none "
           "did\n");
    for (u32 k = 0; k < seeds; ++k) {
        const SeedResult& r = results[k];
        if (r.ok && r.mapped)
            printf("    seed %-4llu landed at %u of %u places, ran at %u; none ran at %u\n", static_cast<ull>(r.seed),
                   r.land_places, r.places, r.ran_places, r.shut_places);
    }
    printf("  where messages land (11.1): the messages delivered to each place's ports, port 0's arrivals "
           "included, and the busiest %u places' share of them, against an even spread's\n",
           kTopPlaces);
    for (u32 k = 0; k < seeds; ++k) {
        const SeedResult& r = results[k];
        if (r.ok && r.mapped)
            printf("    seed %-4llu %llu messages to %u of %u places; the busiest %u took %.1f%% (even: %.1f%%), the "
                   "busiest, place %u, %.1f%%\n",
                   static_cast<ull>(r.seed), static_cast<ull>(r.msg_all), r.msg_places, r.places, kTopPlaces,
                   r.msg_all ? 100.0 * r.msg_top / r.msg_all : 0, r.places ? 100.0 * kTopPlaces / r.places : 0,
                   r.msg_most_place, r.msg_all ? 100.0 * r.msg_most / r.msg_all : 0);
    }
    printf("  routes at nightfall (11.3): topology changes as network devices go off and on, the columns worked "
           "out, and their time a tick (the threads' time, summed), against the network step's and the tick's\n");
    for (u32 k = 0; k < seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok || !r.mapped || !r.ticks)
            continue;
        const double us = r.cycles ? r.secs * 1e6 / static_cast<double>(r.cycles) : 0; // a cycle
        const double per = 1.0 / static_cast<double>(r.ticks);
        printf("    seed %-4llu %llu topology changes (%.3f a tick), %llu columns worked out; routes %.2f us a tick, "
               "the network step %.2f us, of a %.0f us tick; network devices off %.2f%% of place-ticks\n",
               static_cast<ull>(r.seed), static_cast<ull>(r.route_changes),
               static_cast<double>(r.route_changes) * per, static_cast<ull>(r.route_columns),
               us * static_cast<double>(r.route_cycles) * per, us * static_cast<double>(r.net_cycles) * per,
               r.secs * 1e6 * per,
               r.places ? 100.0 * static_cast<double>(r.dark_ticks) * per / r.places : 0);
    }
    printf("  the map (6.1): each seed's is under maps, above, and the messages its network carried under "
           "arrivals\n");
}

void print_stage(u64 t) {
    if (t == kNone)
        printf(" %9s", "-");
    else
        printf(" %9llu", static_cast<unsigned long long>(t));
}

} // namespace

void read_stages(Config& cfg, StageCriteria* c) {
    c->generations = key_u32(cfg, "origin.generations");
    c->span = key_u64(cfg, "origin.span");
    c->share = key_f64(cfg, "origin.share");
    c->members = key_u32(cfg, "origin.members");
    c->copies = key_f64(cfg, "origin.copies");
    c->groups = key_u32(cfg, "origin.groups");
}

int cmd_origin(Config& cfg, bool busy) {
    Params p;
    p.busy = busy;
    // The settings the run reads are noted from here, for the settings page
    // of the window on the run (ui/settings.h), which can open at any time.
    Settings book;
    settings_begin(&book);
    p.seeds = key_u32(cfg, "origin.seeds");
    p.ticks = key_u64(cfg, "origin.ticks");
    p.budget = key_u64(cfg, "origin.budget");
    read_stages(cfg, &p.stages);
    p.pass_share = key_f64(cfg, "origin.pass_share");
    p.spread_share = key_f64(cfg, "origin.spread_share");
    p.turnover_share = key_f64(cfg, "origin.turnover_share");
    p.crowded_min = key_u32(cfg, "origin.crowded_min");
    p.crowded_seeds = key_u32(cfg, "origin.crowded_seeds");
    p.ground_ratio = key_f64(cfg, "origin.ground_ratio");
    p.ground_min = key_u32(cfg, "origin.ground_min");
    p.ground_seeds = key_u32(cfg, "origin.ground_seeds");
    p.era = key_u64(cfg, "origin.era");
    p.era_share = key_f64(cfg, "origin.era_share");
    p.era_seed_share = key_f64(cfg, "origin.era_seed_share");
    p.calls = key_flag(cfg, "origin.calls");
    p.era_late = key_u32(cfg, "origin.era_late");
    p.era_seeds = key_u32(cfg, "origin.era_seeds");
    const u32 parallel = key_u32(cfg, "origin.parallel");
    const bool view = key_flag(cfg, "origin.view");
    const Str picture = key_str(cfg, "origin.picture");
    const double picture_minutes = key_f64(cfg, "origin.picture_minutes");
    const u32 threads = key_u32(cfg, "run.threads");
    const Str samples = key_str(cfg, "origin.samples");
    const Str genomes = key_str(cfg, "origin.genomes");
    const Str snapshots = key_str(cfg, "origin.snapshots");
    const Str windows = key_str(cfg, "origin.windows");
    const Str records = key_str(cfg, "origin.records");
    p.flush_seconds = key_f64(cfg, "observe.flush_seconds");
    const Str saves = key_str(cfg, "origin.saves");
    const Str resume = key_str(cfg, "origin.resume");
    p.saves_kept = key_u32(cfg, "origin.saves_kept");
    p.snapshot_every = key_u64(cfg, "origin.snapshot_every");
    p.save_every = key_u64(cfg, "origin.save_every");
    p.save_minutes = key_f64(cfg, "origin.save_minutes");
    p.progress_minutes = key_f64(cfg, "origin.progress_minutes");
    p.memory_floor = key_u64(cfg, "origin.memory_floor");
    p.obs.sample_every = key_u32(cfg, "observe.sample_every");
    p.obs.write_every = key_u32(cfg, "observe.write_every");
    p.obs.genome_key_every = key_u32(cfg, "observe.genome_key_every");
    p.obs.group_threshold = key_f64(cfg, "observe.group_threshold");
    p.obs.min_genome = key_u32(cfg, "observe.min_genome");
    p.obs.keep_generations = key_u32(cfg, "observe.keep_generations");
    p.obs.lineage_generations = p.obs.keep_generations; // a lineage with a row is one whose genomes could be kept
    p.obs.day = key_u32(cfg, "supply.day");
    p.obs.forget = key_flag(cfg, "observe.forget");
    p.obs.turnover.window = key_u64(cfg, "observe.turnover_window");
    p.obs.turnover.members = key_u32(cfg, "observe.turnover_members");
    p.day = key_u64(cfg, "supply.day");
    p.obs.keep_facts = false; // long runs: the observers keep what the stages need
    const u64 first_seed = key_u64(cfg, "world.seed");
    key_accept_known(cfg); // settings the world reads itself are fine
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    if (!cfg.ok()) {
        fprintf(stderr, "nova2: %s\n", cfg.value_error());
        return 2;
    }
    if (samples.n >= sizeof p.samples) {
        fprintf(stderr, "nova2: origin.samples: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.samples, samples.p, samples.n);
    p.samples[samples.n] = 0;
    if (genomes.n >= sizeof p.genomes) {
        fprintf(stderr, "nova2: origin.genomes: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.genomes, genomes.p, genomes.n);
    p.genomes[genomes.n] = 0;
    if (snapshots.n >= sizeof p.snapshots) {
        fprintf(stderr, "nova2: origin.snapshots: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.snapshots, snapshots.p, snapshots.n);
    p.snapshots[snapshots.n] = 0;
    if (windows.n >= sizeof p.windows) {
        fprintf(stderr, "nova2: origin.windows: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.windows, windows.p, windows.n);
    p.windows[windows.n] = 0;
    if (records.n >= sizeof p.records - 32) { // room for its files' names
        fprintf(stderr, "nova2: origin.records: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.records, records.p, records.n);
    p.records[records.n] = 0;
    if (p.records[0] && !make_folder(p.records)) {
        fprintf(stderr, "nova2: origin.records: can't make the folder '%s'\n", p.records);
        return 2;
    }
    if (saves.n >= sizeof p.saves) {
        fprintf(stderr, "nova2: origin.saves: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.saves, saves.p, saves.n);
    p.saves[saves.n] = 0;
    if (p.saves[0] && !make_folder(p.saves)) {
        fprintf(stderr, "nova2: origin.saves: can't make the folder '%s'\n", p.saves);
        return 2;
    }
    if (resume.n >= sizeof p.resume) {
        fprintf(stderr, "nova2: origin.resume: the folder's path is too long\n");
        return 2;
    }
    memcpy(p.resume, resume.p, resume.n);
    p.resume[resume.n] = 0;
    if (p.resume[0]) {
        const DWORD a = GetFileAttributesA(p.resume);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, "nova2: origin.resume: there is no folder '%s'\n", p.resume);
            return 2;
        }
    }
    // Hours of running are no use if the files can't be written at the end.
    if (p.samples[0] && !make_folder(p.samples)) {
        fprintf(stderr, "nova2: origin.samples: can't make the folder '%s'\n", p.samples);
        return 2;
    }
    if (p.genomes[0] && !make_folder(p.genomes)) {
        fprintf(stderr, "nova2: origin.genomes: can't make the folder '%s'\n", p.genomes);
        return 2;
    }
    if (p.snapshots[0] && !make_folder(p.snapshots)) {
        fprintf(stderr, "nova2: origin.snapshots: can't make the folder '%s'\n", p.snapshots);
        return 2;
    }
    if (p.windows[0] && !make_folder(p.windows)) {
        fprintf(stderr, "nova2: origin.windows: can't make the folder '%s'\n", p.windows);
        return 2;
    }

    // Seeds side by side: worlds of static use host threads poorly, so many
    // seeds with a few threads each beat one seed with all of them.
    const u32 hw = threads ? threads : ThreadPool::hardware_threads();
    p.parallel = parallel ? parallel : (hw / 2 ? hw / 2 : 1);
    if (p.parallel > p.seeds)
        p.parallel = p.seeds;
    p.threads_each = hw / p.parallel ? hw / p.parallel : 1;

    TextBuf text;
    cfg.write(text);
    WorldSettings ws;
    {
        Config probe;
        Config::ParseError pe;
        probe.parse(text.view(), &pe);
        read_world_settings(probe, &ws);
    }
    if (p.busy)
        printf("A TEST WORLD (R7), for profiling: replicators and migrants placed by hand in every place. Nothing "
               "below is evidence about life.\n");
    printf("origin test: %u seeds, %llu ticks each%s, %u places a seed; %u seeds at once, %u host threads each\n",
           p.seeds, static_cast<unsigned long long>(p.ticks), p.budget ? " (or the instruction cap)" : "", ws.places,
           p.parallel, p.threads_each);
    printf("stage 3: %u generations; stage 4: %llu ticks holding %.0f%% of a place with %u members or more, %.0f%% of "
           "them born as copies; stage 5: %u new groups while crowded, after stage 4\n",
           p.stages.generations, static_cast<unsigned long long>(p.stages.span), 100 * p.stages.share,
           p.stages.members, 100 * p.stages.copies, p.stages.groups);
    fflush(stdout);

    Array<SeedResult> results;
    results.resize(p.seeds);
    for (u32 k = 0; k < p.seeds; ++k)
        results[k] = SeedResult{}; // resize zero-fills; the stages start as none
    Array<SeedGround> grounds;
    grounds.resize(p.seeds); // zero-filled: nothing counted yet
    Array<SeedPowerMap> power_maps;
    power_maps.resize(p.seeds); // zero-filled: no lines yet
    Job job{&p, text.c_str(), first_seed, results.data(), grounds.data(), power_maps.data()};
    // The window on the run, on a thread of its own: open from the start
    // (origin.view), or when `nova2 watch` asks.
    RunView run_view;
    {
        // What a seed's world reads: from a world built here to be asked,
        // and let go. The noting ends before any seed starts, since the
        // seeds read their settings side by side.
        Config first;
        Config::ParseError pe;
        first.parse(text.c_str(), &pe);
        {
            World asked;
            TextBuf why;
            asked.init(first, &why);
        }
        settings_end(&book, first);
        char seeds_text[96];
        const unsigned long long from = first_seed, to = first_seed + p.seeds - 1;
        if (p.seeds > 1)
            snprintf(seeds_text, sizeof seeds_text, "%llu to %llu, one a seed", from, to);
        else
            snprintf(seeds_text, sizeof seeds_text, "%llu", from);
        settings_say(&book, "world.seed", seeds_text);
        run_view.settings = &book;
        snprintf(run_view.picture, sizeof run_view.picture, "%.*s", static_cast<int>(picture.n), picture.p);
        run_view.picture_minutes = picture_minutes;
    }
    if (run_view_open(&run_view, p.seeds, first_seed, busy ? "a busy test world (R7)" : "the origin test", view))
        p.view = &run_view;
    else
        fprintf(stderr, "nova2: the run can't listen for `nova2 watch` and `nova2 save`; it goes on without\n");
    const double t0 = clock_seconds();
    ThreadPool seeds;
    seeds.start(p.parallel);
    seeds.run(p.seeds, run_seed, &job);
    seeds.stop();
    if (p.view)
        run_view_close(&run_view);
    const double wall = clock_seconds() - t0;

    if (p.resume[0]) {
        // What was carried on, and from where: the tables below are of each
        // seed's whole life, the part before its save included.
        printf("\ncarried on from saves in %s:", p.resume);
        for (u32 k = 0; k < p.seeds; ++k) {
            const SeedResult& r = results[k];
            if (r.carried_from != kNone)
                printf("%s seed %llu from tick %llu", k ? ";" : "", static_cast<unsigned long long>(r.seed),
                       static_cast<unsigned long long>(r.carried_from));
            else
                printf("%s seed %llu from the start", k ? ";" : "", static_cast<unsigned long long>(r.seed));
        }
        printf("\n");
    }
    printf("\n%6s %9s %15s %8s %8s %9s %9s %9s %9s %9s %9s %10s %9s\n", "seed", "ticks", "instructions", "secs",
           "M/s", "paths/s", "stage 1", "stage 2", "stage 3", "stage 4", "stage 5", "births", "best hold");
    u32 passed = 0;
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok) {
            printf("%6llu  failed to start\n", static_cast<unsigned long long>(r.seed));
            continue;
        }
        printf("%6llu %9llu %15llu %8.0f %8.1f %9.0f", static_cast<unsigned long long>(r.seed),
               static_cast<unsigned long long>(r.ticks), static_cast<unsigned long long>(r.instructions), r.secs,
               r.secs > 0 ? r.instructions / r.secs / 1e6 : 0.0, r.secs > 0 ? r.last.paths_new / r.secs : 0.0);
        for (u32 s = 1; s <= 5; ++s)
            print_stage(r.stage[s]);
        printf(" %10llu %9llu", static_cast<unsigned long long>(r.last.births),
               static_cast<unsigned long long>(r.hold));
        if (r.hold_origin != kNone)
            printf("  (place %u)", r.hold_place);
        printf("\n");
        passed += r.stage[4] != kNone ? 1 : 0;
    }
    // Whose each best hold was, and when.
    bool any = false;
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok || r.hold_origin == kNone)
            continue;
        if (!any)
            printf("\nthe best hold in each seed:\n");
        any = true;
        printf("  seed %llu: %llu ticks, from tick %llu to %llu%s, by the lineage of program %llu in place %u\n",
               static_cast<unsigned long long>(r.seed), static_cast<unsigned long long>(r.hold),
               static_cast<unsigned long long>(r.hold_to - r.hold), static_cast<unsigned long long>(r.hold_to),
               r.hold_to == r.ticks ? " (the end)" : "", static_cast<unsigned long long>(r.hold_origin),
               r.hold_place);
    }
    // Whether each seed's first established lineage lasted.
    any = false;
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok || r.s4_origin == kNone)
            continue;
        if (!any)
            printf("\nthe first lineage to reach stage 4 in each seed, at the seed's end:\n");
        any = true;
        printf("seed %llu, place %u, stage 4 at tick %llu\n", static_cast<unsigned long long>(r.seed), r.s4_place,
               static_cast<unsigned long long>(r.stage[4]));
        print_end(stdout, r);
    }

    printf("\nthe best stage reached, by OS type (places across all seeds):\n");
    for (u32 strict = 0; strict < 2; ++strict)
        for (u32 i = 0; i < ws.page_size_count; ++i) {
            u32 places = 0, best = 0;
            for (u32 k = 0; k < p.seeds; ++k) {
                places += results[k].types[strict][i].places;
                if (results[k].types[strict][i].best > best)
                    best = results[k].types[strict][i].best;
            }
            printf("  %-6s %5u-byte pages: %4u places, stage %u\n", strict ? "strict" : "lax", ws.page_sizes[i],
                   places, best);
        }

    // The turnover measure (build-plan-gate2.md step 1).
    if (p.obs.turnover.window) {
        printf("\nturnover: windows of %llu ticks starting with %u programs or more; one turns over if half its "
               "starters die in it\nand births make up half the dead; crowded: under a tenth of memory free at its "
               "start; a day is %llu ticks\n",
               static_cast<unsigned long long>(p.obs.turnover.window), p.obs.turnover.members,
               static_cast<unsigned long long>(p.day));
        printf("  %-18s %8s %7s %8s %7s %11s %10s  %s\n", "", "windows", "turned", "crowded", "turned", "deaths/day",
               "mean life", "the longest run without turnover");
        TurnStat all;
        for (u32 k = 0; k < p.seeds; ++k) {
            const SeedResult& r = results[k];
            if (!r.ok)
                continue;
            char label[32];
            snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
            print_turn(label, r.turn, p.day, false);
            all.add(r.turn);
        }
        for (u32 strict = 0; strict < 2; ++strict)
            for (u32 i = 0; i < ws.page_size_count; ++i) {
                TurnStat t;
                for (u32 k = 0; k < p.seeds; ++k)
                    if (results[k].ok)
                        t.add(results[k].turn_types[strict][i]);
                char label[32];
                snprintf(label, sizeof label, "%s %u-byte", strict ? "strict" : "lax", ws.page_sizes[i]);
                print_turn(label, t, p.day, true);
            }
        print_turn("all seeds", all, p.day, true);
        printf("  turned over: %u of %u counted windows (%.0f%%), %u of %u crowded ones (%.0f%%); %u of the %u places "
               "with counted windows turned over at least once\n",
               all.turned, all.windows, share_of(all.turned, all.windows), all.crowded_turned, all.crowded,
               share_of(all.crowded_turned, all.crowded), all.places_turned, all.places);
        printf("  with a cold start in them: %u of the counted windows; %u of the %u that turned over, %u of the %u "
               "crowded ones that did\n",
               all.cold, all.cold_turned, all.turned, all.crowded_cold_turned, all.crowded_turned);
        if (all.program_ticks) {
            const double per = static_cast<double>(p.day) / all.program_ticks;
            printf("  deaths per program per day by cause: exit %.4f, fault %.4f, cut-out %.4f, device %.4f, memory "
                   "%.4f, cold start %.4f, heat %.4f\n",
                   all.deaths[0] * per, all.deaths[1] * per, all.deaths[2] * per, all.deaths[3] * per,
                   all.deaths[4] * per, all.deaths[5] * per, all.deaths[6] * per);
        }
    }

    // The ground measure (build-plan-gate4.md step 1).
    if (p.obs.turnover.window) {
        printf("\nground: each place's windows of %llu ticks, by what the physics did to it in them. hot: over a "
               "quarter of its core time\nlost to heat trips; dim: over a quarter lost to cut-outs, or its reset "
               "region's memory off for over a quarter\nof the window; fair: neither. programs: those alive at a "
               "window's start, a place-window\n",
               static_cast<unsigned long long>(p.obs.turnover.window));
        printf("  %-20s %10s %9s %10s %9s %10s %9s\n", "", "fair", "programs", "hot", "programs", "dim", "programs");
        GroundStat by_os[2], judged;
        for (u32 k = 0; k < p.seeds; ++k) {
            const SeedResult& r = results[k];
            if (!r.ok)
                continue;
            char label[32];
            snprintf(label, sizeof label, "seed %llu, lax", static_cast<unsigned long long>(r.seed));
            print_ground(label, grounds[k].os[0]);
            by_os[0].add(grounds[k].os[0]);
            by_os[1].add(grounds[k].os[1]);
            if (r.stage[4] != kNone)
                judged.add(grounds[k].os[0]);
        }
        print_ground("lax, all seeds", by_os[0]);
        print_ground("strict, all seeds", by_os[1]);
        print_ground("lax, stage 4 seeds", judged);
        printf("  hot and dim at once, named by the larger loss of core time: %llu windows\n",
               static_cast<unsigned long long>(by_os[0].both + by_os[1].both));

        // The same place-windows under the judged naming.
        u32 classed = 0, seeds_ok = 0, by_incarnation = 0;
        for (u32 k = 0; k < p.seeds; ++k) {
            seeds_ok += results[k].ok ? 1 : 0;
            classed += results[k].ok && grounds[k].classed ? 1 : 0;
            by_incarnation += results[k].ok && grounds[k].by_incarnation ? 1 : 0;
        }
        printf("\nground by class: each place named by what a full place gets there with nothing else living, under "
               "the sources of the time\n(seed-N-classes.csv, from nova2 worlds with worlds.test = classes); a "
               "place-window takes the latest classing at or before its start.\ngate 4's mark is judged on this "
               "naming\n");
        if (by_incarnation)
            printf("with the run's respawns applied (worlds.respawns; %u of the seeds' files): a place-window takes "
                   "its place's latest classing of\nits incarnation then, and is lax or strict as that "
                   "incarnation was dealt; one with a respawn of its place inside it is left out\n",
                   by_incarnation);
        if (!classed) {
            printf("  no seed has a classes file in the windows folder (origin.windows)\n");
        } else {
            printf("  %-20s %10s %9s %10s %9s %10s %9s\n", "", "fair", "programs", "hot", "programs", "dim",
                   "programs");
            GroundStat cls_os[2], cls_judged;
            u64 unclassed = 0, respawned = 0, straddled = 0;
            for (u32 k = 0; k < p.seeds; ++k) {
                const SeedResult& r = results[k];
                if (!r.ok)
                    continue;
                char label[32];
                snprintf(label, sizeof label, "seed %llu, lax", static_cast<unsigned long long>(r.seed));
                if (!grounds[k].classed) {
                    printf("  %-20s no classes file\n", label);
                    continue;
                }
                print_ground(label, grounds[k].cls[0]);
                cls_os[0].add(grounds[k].cls[0]);
                cls_os[1].add(grounds[k].cls[1]);
                unclassed += grounds[k].unclassed[0] + grounds[k].unclassed[1];
                respawned += grounds[k].respawned[0] + grounds[k].respawned[1];
                if (grounds[k].respawned[0] + grounds[k].respawned[1])
                    printf("  %-20s %llu lax and %llu strict place-windows of respawned places left out\n", "",
                           static_cast<unsigned long long>(grounds[k].respawned[0]),
                           static_cast<unsigned long long>(grounds[k].respawned[1]));
                straddled += grounds[k].straddled[0] + grounds[k].straddled[1];
                if (grounds[k].by_incarnation)
                    printf("  %-20s %llu lax and %llu strict place-windows with a respawn of their place inside, "
                           "left out; %llu with no classing of their incarnation\n",
                           "", static_cast<unsigned long long>(grounds[k].straddled[0]),
                           static_cast<unsigned long long>(grounds[k].straddled[1]),
                           static_cast<unsigned long long>(grounds[k].unclassed[0] + grounds[k].unclassed[1]));
                if (r.stage[4] != kNone)
                    cls_judged.add(grounds[k].cls[0]);
            }
            print_ground("lax, all seeds", cls_os[0]);
            print_ground("strict, all seeds", cls_os[1]);
            print_ground("lax, stage 4 seeds", cls_judged);
            printf("  %u of %u seeds have classes; place-windows with no classing at or before their start, left "
                   "out: %llu; place-windows of a place from its first respawn on (it is somewhere else than the "
                   "place classed), left out: %llu\n",
                   classed, seeds_ok, static_cast<unsigned long long>(unclassed),
                   static_cast<unsigned long long>(respawned));
            if (by_incarnation)
                printf("  with the respawns applied: place-windows with a respawn of their place inside them, left "
                       "out: %llu\n",
                       static_cast<unsigned long long>(straddled));
        }

        // The reviewer's measures and the reading on untouched ground, over
        // the mark's own windows. Kept apart from the seeds' rows: these
        // structures are large, so they are summed on the heap.
        Array<GroundMore> more;
        more.resize(2);
        GroundStat untouched;
        u64 left = 0, age = 0, aged = 0;
        for (u32 k = 0; k < p.seeds; ++k) {
            if (!results[k].ok || results[k].stage[4] == kNone)
                continue;
            more[0].add(grounds[k].more[0]);
            more[1].add(grounds[k].more[1]);
            untouched.add(grounds[k].untouched);
            left += grounds[k].untouched_left;
            age += grounds[k].untouched_age;
            aged += grounds[k].untouched_aged;
        }
        printf("\nground, the mark's windows more closely (lax places of the seeds that reached stage 4): reported, "
               "none of it judged.\nprograms and copies: those a window starts with, and of them those born as "
               "copies made there. lost: the share of core time\nbehind the window's name, its lowest, middle and "
               "highest. worked: the share of core time the cores ran something, a thread\nor a run of static\n");
        printf("  by class (the judged naming; lost: what a full place loses there, from the classing)\n");
        print_more(more[0], p.ground_ratio);
        printf("  by what the physics did to the place in the window (the first naming; lost: what the place lost "
               "in the window)\n");
        print_more(more[1], p.ground_ratio);
        {
            const double fair = untouched.mean(Ground::fair), hot = untouched.mean(Ground::hot),
                         dim = untouched.mean(Ground::dim);
            char way[120];
            printf("  on untouched ground: a window that begins with no program keeps its own name and gives it to "
                   "its place; a window with programs\n  counts under its place's last such name. with nothing "
                   "living no place is hot, and a name can be many windows old\n");
            printf("  %-20s %10s %9s %10s %9s %10s %9s\n", "", "fair", "programs", "hot", "programs", "dim",
                   "programs");
            print_ground("lax, stage 4 seeds", untouched);
            if (!untouched.windows[static_cast<u32>(Ground::hot)])
                printf("    fair against hot: no hot windows;");
            else
                printf("    fair against hot: %s%s;", differs(fair, hot, p.ground_ratio) ? "differ" : "don't differ",
                       which_way(fair, hot, "hot", way, sizeof way));
            printf(" fair against dim: %s%s\n", differs(fair, dim, p.ground_ratio) ? "differ" : "don't differ",
                   which_way(fair, dim, "dim", way, sizeof way));
            printf("    a window with programs is named by one %.0f ticks before it, on average; left out, with no "
                   "untouched window before them: %llu\n",
                   aged ? static_cast<double>(age) / static_cast<double>(aged) : 0.0,
                   static_cast<unsigned long long>(left));
        }
    }

    // Gate 5's measure (build-plan-gate5.md 2, gap 1, and step 1).
    if (p.obs.turnover.window) {
        printf("\nchange: a genome group leads a place as a window begins with more of its programs than any other "
               "group, %u or more,\n%.0f%% of the place's, %.0f%% of them born there as copies. a first lead: a "
               "group leading for the first time in its seed,\nat any place. eras: each %llu ticks, from a seed's "
               "stage 4 to its end; the late eras are the later half. an era has\nchange if a first lead comes in "
               "it. groups split by drift as well as by adaptation: this is change, not progress\n",
               p.stages.members, 100 * p.stages.share, 100 * p.stages.copies, static_cast<unsigned long long>(p.era));
        printf("  %-10s %11s %9s %9s %7s %8s %6s %13s %13s\n", "", "stage 4 at", "windows", "led", "first", "before",
               "eras", "early: change", "late: change");
        EraTally all;
        for (u32 k = 0; k < p.seeds; ++k) {
            const SeedResult& r = results[k];
            if (!r.ok)
                continue;
            const EraTally& e = grounds[k].eras;
            char label[32], at[24] = "-", early[24], late[24];
            snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
            if (r.stage[4] != kNone)
                snprintf(at, sizeof at, "%llu", static_cast<unsigned long long>(r.stage[4]));
            snprintf(early, sizeof early, "%u of %u", e.early_changed(), e.early());
            snprintf(late, sizeof late, "%u of %u", e.late_changed, e.late);
            printf("  %-10s %11s %9llu %9llu %7llu %8llu %6u %13s %13s\n", label, at,
                   static_cast<unsigned long long>(e.windows), static_cast<unsigned long long>(e.led),
                   static_cast<unsigned long long>(e.first_leads), static_cast<unsigned long long>(e.first_before),
                   e.eras, early, late);
            all.windows += e.windows;
            all.led += e.led;
            all.led_readers += e.led_readers;
            all.first_leads += e.first_leads;
            all.first_before += e.first_before;
            all.eras += e.eras;
            all.late += e.late;
            all.changed += e.changed;
            all.late_changed += e.late_changed;
        }
        printf("  all seeds: %llu of %llu place-windows led (%.1f%%); %llu first leads, %llu of them before stage 4; "
               "change in %u of %u early eras (%.0f%%) and %u of %u late ones (%.0f%%)\n",
               static_cast<unsigned long long>(all.led), static_cast<unsigned long long>(all.windows),
               all.windows ? 100.0 * static_cast<double>(all.led) / static_cast<double>(all.windows) : 0.0,
               static_cast<unsigned long long>(all.first_leads), static_cast<unsigned long long>(all.first_before),
               all.early_changed(), all.early(), share_of(all.early_changed(), all.early()), all.late_changed,
               all.late, share_of(all.late_changed, all.late));

        // Reading input and injection (gap 1; 9.8). Reported, not judged.
        printf("\nreading input: a lineage reads input while its RECVs that returned bytes come to one or more a "
               "member a day (%u ticks),\nover its life so far. the readers' share of programs (over the samples), "
               "of births, and of the place-windows a group\nled (by the lineage with the most programs there): a "
               "correlation, not a cause. an overrun: a RECV that wrote past the end\nof the allocation its buffer "
               "began in, counted once for each kind of thing it reached. an injection: a thread found at\na "
               "sample running bytes an overrun wrote from another lineage's message, still as written; "
               "\"sightings\" counts it at every sample\nit is found at, \"injected\" the programs it was ever found "
               "in, and the last column their births of the sender's kind\n",
               p.obs.day);
        printf("  %-10s %12s %9s %9s %9s %9s %7s %7s %7s %7s %7s %8s %10s %9s %8s\n", "", "RECVs", "programs",
               "births", "led", "overruns", "own", "kin", "other", "stack", "free", "unknown", "sightings",
               "injected", "births");
        SeedGround sum;
        for (u32 k = 0; k < p.seeds; ++k) {
            const SeedResult& r = results[k];
            if (!r.ok)
                continue;
            const SeedGround& g = grounds[k];
            char label[32];
            snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
            printf("  %-10s %12llu %8.2f%% %8.2f%% %8.2f%% %9llu %7llu %7llu %7llu %7llu %7llu %8llu %10llu %9llu "
                   "%8llu\n",
                   label, static_cast<unsigned long long>(g.recvs),
                   share_of(g.reader_program_samples, g.program_samples), share_of(g.reader_births, g.births),
                   share_of(g.eras.led_readers, g.eras.led), static_cast<unsigned long long>(g.overruns),
                   static_cast<unsigned long long>(g.reached[kReachOwn]),
                   static_cast<unsigned long long>(g.reached[kReachKin]),
                   static_cast<unsigned long long>(g.reached[kReachOther]),
                   static_cast<unsigned long long>(g.reached[kReachStack]),
                   static_cast<unsigned long long>(g.reached[kReachFree]),
                   static_cast<unsigned long long>(g.overruns_unknown),
                   static_cast<unsigned long long>(g.injections_seen),
                   static_cast<unsigned long long>(g.injected_programs),
                   static_cast<unsigned long long>(g.injection_births));
            for (u32 c = 0; c < kCallCount; ++c) {
                sum.calls[c] += g.calls[c];
                sum.calls_ok[c] += g.calls_ok[c];
            }
            sum.sends_other += g.sends_other;
            sum.sends_other_ok += g.sends_other_ok;
            sum.spawns_thread += g.spawns_thread;
            sum.spawns_thread_ok += g.spawns_thread_ok;
            sum.spawns_program += g.spawns_program;
            sum.spawns_program_ok += g.spawns_program_ok;
            sum.recvs += g.recvs;
            sum.overruns += g.overruns;
            for (u32 c = 0; c < kReaches; ++c)
                sum.reached[c] += g.reached[c];
            sum.overruns_unknown += g.overruns_unknown;
            sum.births += g.births;
            sum.reader_births += g.reader_births;
            sum.injection_births += g.injection_births;
            sum.program_samples += g.program_samples;
            sum.reader_program_samples += g.reader_program_samples;
            sum.injections_seen += g.injections_seen;
            sum.injected_programs += g.injected_programs;
            sum.watched += g.watched;
        }
        printf("  all seeds: %llu RECVs returned bytes; lineages that read input had %.2f%% of the programs, %.2f%% "
               "of the births and led %.2f%% of the led place-windows\n  %llu overruns (%llu from a sender no "
               "longer remembered, so not watched); %llu still watched at the end; injections: %llu sightings at "
               "samples, of %llu programs; births by a program found injected, of the sender's kind: %llu\n",
               static_cast<unsigned long long>(sum.recvs), share_of(sum.reader_program_samples, sum.program_samples),
               share_of(sum.reader_births, sum.births), share_of(all.led_readers, all.led),
               static_cast<unsigned long long>(sum.overruns), static_cast<unsigned long long>(sum.overruns_unknown),
               static_cast<unsigned long long>(sum.watched), static_cast<unsigned long long>(sum.injections_seen),
               static_cast<unsigned long long>(sum.injected_programs),
               static_cast<unsigned long long>(sum.injection_births));

        // The calls programs make (design.md 13.1; build-plan-gate5.md 1).
        const double days = static_cast<double>(sum.program_samples) * p.obs.sample_every / p.obs.day;
        printf("\ncalls: what programs' threads asked for over all seeds, by kind, and how often a call did what it "
               "asked; a program-day is one\nprogram alive for a day (%.0f of them, from the samples). idle runs' "
               "calls aren't here\n",
               days);
        printf("  %-11s %16s %16s %9s %24s\n", "", "calls", "succeeded", "", "per 1,000 program-days");
        for (u32 c = 0; c < kCallCount; ++c) {
            printf("  %-11s %16llu %16llu %8.1f%% %24.2f\n", kCallColumn[c],
                   static_cast<unsigned long long>(sum.calls[c]), static_cast<unsigned long long>(sum.calls_ok[c]),
                   share_of(sum.calls_ok[c], sum.calls[c]),
                   days > 0 ? 1000.0 * static_cast<double>(sum.calls[c]) / days : 0.0);
            if (c != kSpawn)
                continue;
            // SPAWN two ways: a thread in the caller's own program, or a new
            // program. The rest asked at an address that could be neither.
            const u64 two[2][2] = {{sum.spawns_thread, sum.spawns_thread_ok},
                                   {sum.spawns_program, sum.spawns_program_ok}};
            const char* const names[2] = {"a thread", "a program"};
            for (u32 k = 0; k < 2; ++k)
                printf("    %-9s %16llu %16llu %8.1f%% %24.2f\n", names[k],
                       static_cast<unsigned long long>(two[k][0]), static_cast<unsigned long long>(two[k][1]),
                       share_of(two[k][1], two[k][0]),
                       days > 0 ? 1000.0 * static_cast<double>(two[k][0]) / days : 0.0);
        }
        printf("  SENDs to a port other than 0: %llu asked for, %llu let go\n",
               static_cast<unsigned long long>(sum.sends_other), static_cast<unsigned long long>(sum.sends_other_ok));
    }

    // Gate 5's watching (step 11): what its mechanisms did, its named
    // checks and the four mechanics' readings. Reported, not judged.
    {
        Array<const SeedWatch*> seen;
        Array<u64> ids;
        for (u32 k = 0; k < p.seeds; ++k)
            if (results[k].ok) {
                seen.push(&grounds[k].watch);
                ids.push(results[k].seed);
            }
        print_watch(seen.data(), ids.data(), static_cast<u32>(seen.size()), watch_params(p), kCallColumn);
    }

    // Power (build-plan-gate2.md steps 2 and 3): what places drew, and the
    // voltage their sources gave them for it.
    printf("\npower: each place's mean draw over its ticks\n");
    SeedResult power_all;
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        char label[32];
        snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
        print_power(label, r, true);
        power_all.energy.cores += r.energy.cores;
        power_all.energy.memory += r.energy.memory;
        power_all.energy.disk += r.energy.disk;
        power_all.energy.network += r.energy.network;
        power_all.energy.cooling += r.energy.cooling;
        power_all.place_ticks += r.place_ticks;
        power_all.activations += r.activations;
        if (r.peak_draw > power_all.peak_draw)
            power_all.peak_draw = r.peak_draw;
        power_all.mv_total += r.mv_total;
        power_all.below_p += r.below_p;
        power_all.below_e += r.below_e;
        power_all.below_region += r.below_region;
        power_all.full_draw += r.full_draw;
        power_all.places += r.places;
        if (r.mv_lowest < power_all.mv_lowest)
            power_all.mv_lowest = r.mv_lowest;
        for (u32 e = 0; e < 2; ++e) {
            power_all.cutouts[e] += r.cutouts[e];
            power_all.off_ticks[e] += r.off_ticks[e];
            power_all.core_ticks[e] += r.core_ticks[e];
        }
        power_all.cut_threads += r.cut_threads;
        power_all.paused_threads += r.paused_threads;
        power_all.cut_runs += r.cut_runs;
        power_all.disk_cutouts += r.disk_cutouts;
        power_all.torn_writes += r.torn_writes;
        power_all.failed_reads += r.failed_reads;
        power_all.result_errors += r.result_errors;
        power_all.band_errors += r.band_errors;
        power_all.memory_cutouts += r.memory_cutouts;
        power_all.memory_off += r.memory_off;
        power_all.memory_ticks += r.memory_ticks;
        power_all.cells_faded += r.cells_faded;
        power_all.faded_threads += r.faded_threads;
        power_all.failed_threads += r.failed_threads;
        power_all.memory_edge += r.memory_edge;
        power_all.cosmic += r.cosmic;
        power_all.cold_starts += r.cold_starts;
        power_all.cold_written += r.cold_written;
        power_all.prompt_runs += r.prompt_runs;
        power_all.boot_runs += r.boot_runs;
        power_all.boot_births += r.boot_births;
    }
    print_power("all seeds", power_all, false);
    const SupplySettings& su = ws.supply;
    printf("\nsupply: each place's voltage over its ticks; a source of %d mV sagging %d mV at a place's full draw, "
           "night at %.2f of noon over a %llu-tick day, swings to %.2f below full, %.2f drop-outs a place per "
           "million ticks\n",
           su.voltage, su.sag, su.night, static_cast<unsigned long long>(su.day), su.swing, su.dropouts);
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        char label[32];
        snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
        print_supply(label, r, true);
    }
    print_supply("all seeds", power_all, false);
    printf("\ncut-outs: each device off below its threshold, for %llu ticks at least, back at its threshold plus %d "
           "mV; a thread running on a core that cuts out is %s\n",
           static_cast<unsigned long long>(ws.power.off_time), ws.power.restart_gap,
           ws.os.cutout_pause ? "paused (os.cutout_pause)" : "ended");
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        char label[32];
        snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
        print_cutouts(label, r);
    }
    print_cutouts("all seeds", power_all);
    printf("\nerrors and fading: error rates doubling every %u mV closer than %u mV to either edge of a range; "
           "memory cut out fades, its cells holding %g to %g ticks at 0 V and %g times longer just below its "
           "threshold%s\n",
           ws.noise.edge_doubling, ws.noise.edge_band, ws.noise.fade_fastest, ws.noise.fade_slowest,
           ws.noise.fade_at_threshold,
           ws.os.saved_state_fails ? "; a thread ends when a bit of its saved state fails, by a fade, an edge error "
                                     "or a cosmic flip (os.saved_state_fails)"
                                   : "");
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        char label[32];
        snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
        print_errors(label, r, ws.os.saved_state_fails);
    }
    print_errors("all seeds", power_all, ws.os.saved_state_fails);
    printf("\ncold starts: power back to the reset region's memory after the voltage fell below its threshold; "
           "every program ends, and the boot block (%u bytes) runs from the head of port 0's queue (%u bytes)\n",
           ws.os.boot_block, ws.region.queue_bytes);
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        char label[32];
        snprintf(label, sizeof label, "seed %llu", static_cast<unsigned long long>(r.seed));
        print_cold(label, r);
    }
    print_cold("all seeds", power_all);

    // Each seed's map (6.1).
    printf("\nmaps:\n");
    for (u32 k = 0; k < p.seeds; ++k)
        if (results[k].ok)
            printf("  seed %-4llu %s\n", static_cast<unsigned long long>(results[k].seed), results[k].map);
    // And its power map (6.1, 6.2; gate 4): the proximity links, their water
    // and borders, and the catchments' shores.
    printf("\npower maps:\n");
    for (u32 k = 0; k < p.seeds; ++k)
        if (results[k].ok && results[k].mapped)
            printf("  seed %-4llu %s\n", static_cast<unsigned long long>(results[k].seed), power_maps[k].line);

    // The spread measure (gate 3, 13.3).
    printf("\nspread: a colony is a lineage's members at a place beyond the one it began in, founded by a colonist "
           "(born from an arrival);\nit settles the place by holding it as stage 4 holds, for %llu ticks, counting "
           "only the copies born there\n",
           static_cast<unsigned long long>(p.stages.span));
    u32 spread = 0;
    for (u32 k = 0; k < p.seeds; ++k) {
        const SeedResult& r = results[k];
        if (!r.ok)
            continue;
        printf("  seed %-4llu %u colonies (%u arrivals founded one), %u alive at the end, %u settled; %llu births "
               "from arrivals",
               static_cast<unsigned long long>(r.seed), r.colonies, r.foundings, r.colonies_alive, r.settled,
               static_cast<unsigned long long>(r.arrival_births));
        if (r.settle_origin != kNone)
            printf("; first settled at tick %llu: place %u, by the lineage of program %llu from place %u, founded "
                   "from place %u at tick %llu, %u of the place's %u programs, %u born there as copies",
                   static_cast<unsigned long long>(r.settle_tick), r.settle_place,
                   static_cast<unsigned long long>(r.settle_origin), r.settle_home, r.settle_from,
                   static_cast<unsigned long long>(r.settle_founded), r.settle_alive, r.settle_programs,
                   r.settle_copies);
        printf("\n");
        spread += r.settled ? 1 : 0;
    }
    bool mapped = false;
    for (u32 k = 0; k < p.seeds; ++k)
        mapped = mapped || (results[k].ok && results[k].mapped);
    if (mapped)
        print_gate3(results.data(), p.seeds);
    print_gate4(results.data(), p.seeds);

    Sample sum{};
    double ratio = 0, ratio_min = 1e9;
    u32 ran = 0;
    for (u32 k = 0; k < p.seeds; ++k) {
        if (!results[k].ok)
            continue;
        ++ran;
        const Sample& s = results[k].last;
        for (u32 c = 0; c < kDeathCauses; ++c)
            sum.deaths[c] += s.deaths[c];
        sum.idle_runs += s.idle_runs;
        sum.idle_faults += s.idle_faults;
        sum.idle_caps += s.idle_caps;
        sum.paths_new += s.paths_new;
        sum.idle_sends += s.idle_sends;
        sum.births += s.births;
        sum.faithful += s.faithful;
        sum.scavenger += s.scavenger;
        sum.written += s.written;
        sum.region_copies += s.region_copies;
        sum.program_copies += s.program_copies;
        sum.near_copies += s.near_copies;
        sum.image_copies += s.image_copies;
        sum.handovers += s.handovers;
        sum.run_alloc += s.run_alloc;
        sum.run_alloc_ok += s.run_alloc_ok;
        sum.run_spawn += s.run_spawn;
        sum.run_spawn_ok += s.run_spawn_ok;
        sum.run_cpy += s.run_cpy;
        sum.run_cpy_own += s.run_cpy_own;
        ratio += s.region_ratio;
        if (results[k].ratio_min < ratio_min)
            ratio_min = results[k].ratio_min;
    }
    printf("\nall seeds: %llu births (%llu faithful, %llu scavenged)\n", static_cast<unsigned long long>(sum.births),
           static_cast<unsigned long long>(sum.faithful), static_cast<unsigned long long>(sum.scavenger));
    printf("deaths by cause: exit %llu, fault %llu, cut-out %llu, device %llu, memory %llu, cold start %llu, heat "
           "%llu, out of memory %llu, watchdog %llu\n",
           static_cast<unsigned long long>(sum.deaths[0]), static_cast<unsigned long long>(sum.deaths[1]),
           static_cast<unsigned long long>(sum.deaths[2]), static_cast<unsigned long long>(sum.deaths[3]),
           static_cast<unsigned long long>(sum.deaths[4]), static_cast<unsigned long long>(sum.deaths[5]),
           static_cast<unsigned long long>(sum.deaths[6]), static_cast<unsigned long long>(sum.deaths[7]),
           static_cast<unsigned long long>(sum.deaths[8]));
    printf("idle runs: %llu (%llu faulted, %llu capped), %llu distinct paths, %llu SENDs\n",
           static_cast<unsigned long long>(sum.idle_runs), static_cast<unsigned long long>(sum.idle_faults),
           static_cast<unsigned long long>(sum.idle_caps), static_cast<unsigned long long>(sum.paths_new),
           static_cast<unsigned long long>(sum.idle_sends));
    printf("reset regions: compressibility %.3f at the end (mean), %.3f at the lowest\n", ran ? ratio / ran : 0.0,
           ratio_min);
    printf("\nthe copying ladder (all seeds):\n");
    printf("  idle runs: %llu ALLOCs (%llu worked), %llu SPAWNs (%llu worked)\n",
           static_cast<unsigned long long>(sum.run_alloc), static_cast<unsigned long long>(sum.run_alloc_ok),
           static_cast<unsigned long long>(sum.run_spawn), static_cast<unsigned long long>(sum.run_spawn_ok));
    printf("  idle runs: %llu CPYs, %llu of them into memory the run held\n",
           static_cast<unsigned long long>(sum.run_cpy), static_cast<unsigned long long>(sum.run_cpy_own));
    printf("  births with something written: %llu of %llu\n", static_cast<unsigned long long>(sum.written),
           static_cast<unsigned long long>(sum.births));
    printf("  copies of the reset region's code (8 bytes or more): %llu\n",
           static_cast<unsigned long long>(sum.region_copies));
    printf("  births by programs that wrote 8 bytes or more: %llu, %llu at least half like the parent\n",
           static_cast<unsigned long long>(sum.program_copies), static_cast<unsigned long long>(sum.near_copies));
    printf("  births whose whole starting memory is the parent's: %llu\n",
           static_cast<unsigned long long>(sum.image_copies));
    printf("  hand-overs (births into the parent's own birth block, on no rung above): %llu\n",
           static_cast<unsigned long long>(sum.handovers));
    printf("  faithful births (the parent's genome, %u bytes or more): %llu\n", p.obs.min_genome,
           static_cast<unsigned long long>(sum.faithful));
    printf("wall time %.0f s\n", wall);
    const u32 need = static_cast<u32>(p.pass_share * p.seeds + 0.999999);
    printf("\n%s: stage 4 in %u of %u seeds (the pass mark is %u)\n", passed >= need ? "PASS" : "FAIL", passed,
           p.seeds, need);

    // Gate 2's pass mark (build-plan-gate2.md 7), when the turnover measure
    // ran: nearly every counted window turns over, and nearly every crowded
    // one, on enough crowded windows from enough seeds to judge, and the
    // origin regression still reaches stage 4 as gate 1's test asked.
    if (p.obs.turnover.window) {
        TurnStat all;
        u32 crowded_from = 0; // seeds with a crowded window
        for (u32 k = 0; k < p.seeds; ++k) {
            if (!results[k].ok)
                continue;
            all.add(results[k].turn);
            crowded_from += results[k].turn.crowded ? 1 : 0;
        }
        const u32 need_counted = need_of(p.turnover_share, all.windows);
        const u32 need_crowded = need_of(p.turnover_share, all.crowded);
        const bool counted_ok = all.windows > 0 && all.turned >= need_counted;
        const bool crowded_ok = all.crowded > 0 && all.crowded_turned >= need_crowded;
        const bool floor_ok = all.crowded >= p.crowded_min && crowded_from >= p.crowded_seeds;
        const bool origin_ok = passed >= need;
        printf("\ngate 2's pass mark: %.0f%% of the counted windows turn over, and %.0f%% of the crowded ones (those "
               "with a cold start in them count),\non %u crowded windows or more from %u seeds or more; and stage 4 "
               "in %u of the %u seeds or more\n",
               100 * p.turnover_share, 100 * p.turnover_share, p.crowded_min, p.crowded_seeds, need, p.seeds);
        printf("  counted windows: %u of %u turned over (%.1f%%), %u needed: %s\n", all.turned, all.windows,
               share_of(all.turned, all.windows), need_counted, met(counted_ok));
        printf("  crowded windows: %u of %u turned over (%.1f%%), %u needed: %s\n", all.crowded_turned, all.crowded,
               share_of(all.crowded_turned, all.crowded), need_crowded, met(crowded_ok));
        printf("  the floor: %u crowded windows from %u seeds: %s%s\n", all.crowded, crowded_from, met(floor_ok),
               floor_ok ? "" : " (too few to judge: places stopped filling)");
        printf("  the origin regression: stage 4 in %u of %u seeds: %s\n", passed, p.seeds, met(origin_ok));
        const bool gate2_ok = counted_ok && crowded_ok && floor_ok && origin_ok;
        printf("%s: gate 2's pass mark\n", gate2_ok ? "PASS" : "FAIL");

        // Gate 3's (build-plan-gate3.md 7): a lineage settles a place beyond
        // the one it began in, in enough seeds, and gate 2's mark holds.
        const u32 need_spread = need_of(p.spread_share, p.seeds);
        const bool spread_ok = spread >= need_spread && spread > 0;
        printf("\ngate 3's pass mark: a lineage settles a place beyond the one it began in, in %u of the %u seeds or "
               "more; and gate 2's pass mark\n",
               need_spread, p.seeds);
        printf("  spread: in %u of %u seeds: %s\n", spread, p.seeds, met(spread_ok));
        printf("  gate 2's pass mark: %s\n", met(gate2_ok));
        printf("%s: gate 3's pass mark\n", spread_ok && gate2_ok ? "PASS" : "FAIL");

        // Gate 4's (build-plan-gate4.md 7 and 11, as changed on 2026-11-20):
        // in the lax places of the seeds that reach stage 4, with ground
        // named by its class, fair ground and hot ground differ enough in
        // the programs their place-windows start with, whichever way, and
        // fair and dim likewise, on enough windows of each kind from enough
        // seeds; and gate 3's mark holds. The mark as first worded and
        // named is reported after it, not judged.
        {
            GroundStat judged;
            u32 from[kGrounds] = {}; // seeds with a window of each kind
            u32 staged = 0, without = 0;
            for (u32 k = 0; k < p.seeds; ++k) {
                if (!results[k].ok || results[k].stage[4] == kNone)
                    continue;
                ++staged;
                without += grounds[k].classed ? 0 : 1;
                judged.add(grounds[k].cls[0]);
                for (u32 g = 0; g < kGrounds; ++g)
                    from[g] += grounds[k].cls[0].windows[g] ? 1 : 0;
            }
            bool ground_floor = true;
            for (u32 g = 0; g < kGrounds; ++g)
                ground_floor = ground_floor && judged.windows[g] >= p.ground_min && from[g] >= p.ground_seeds;
            const double fair = judged.mean(Ground::fair), hot = judged.mean(Ground::hot),
                         dim = judged.mean(Ground::dim);
            const bool hot_ok = ground_floor && differs(fair, hot, p.ground_ratio);
            const bool dim_ok = ground_floor && differs(fair, dim, p.ground_ratio);
            printf("\ngate 4's pass mark: in the lax places of the seeds that reach stage 4, with ground named by "
                   "what a full place gets there,\nfair place-windows and hot ones differ by a factor of %.3g or "
                   "more in the programs they start with, whichever way, and fair\nand dim ones likewise, on %u "
                   "place-windows or more of each kind from %u seeds or more; and gate 3's pass mark\n",
                   p.ground_ratio, p.ground_min, p.ground_seeds);
            printf("  programs a place-window: fair %.2f, hot %.2f, dim %.2f\n", fair, hot, dim);
            char way[120];
            printf("  fair against hot: %s%s\n", met(hot_ok), which_way(fair, hot, "hot", way, sizeof way));
            printf("  fair against dim: %s%s\n", met(dim_ok), which_way(fair, dim, "dim", way, sizeof way));
            printf("  the floor: fair %llu windows from %u seeds, hot %llu from %u, dim %llu from %u: %s%s\n",
                   static_cast<unsigned long long>(judged.windows[0]), from[0],
                   static_cast<unsigned long long>(judged.windows[1]), from[1],
                   static_cast<unsigned long long>(judged.windows[2]), from[2], met(ground_floor),
                   ground_floor ? "" : " (too few to judge: the world has too little of some ground)");
            printf("  gate 3's pass mark: %s\n", met(spread_ok && gate2_ok));
            if (without) {
                printf("  %u of the %u seeds that reached stage 4 have no classes file: their place-windows aren't "
                       "in these counts\n",
                       without, staged);
                printf("NOT JUDGED: gate 4's pass mark (class every seed with nova2 worlds, worlds.test = classes, "
                       "then carry the run on for its report)\n");
            } else {
                printf("%s: gate 4's pass mark\n", hot_ok && dim_ok && spread_ok && gate2_ok ? "PASS" : "FAIL");
            }
        }
        {
            GroundStat judged;
            u32 from[kGrounds] = {};
            for (u32 k = 0; k < p.seeds; ++k) {
                if (!results[k].ok || results[k].stage[4] == kNone)
                    continue;
                judged.add(grounds[k].os[0]);
                for (u32 g = 0; g < kGrounds; ++g)
                    from[g] += grounds[k].os[0].windows[g] ? 1 : 0;
            }
            bool ground_floor = true;
            for (u32 g = 0; g < kGrounds; ++g)
                ground_floor = ground_floor && judged.windows[g] >= p.ground_min && from[g] >= p.ground_seeds;
            const double fair = judged.mean(Ground::fair), hot = judged.mean(Ground::hot),
                         dim = judged.mean(Ground::dim);
            const bool hot_ok = ground_floor && fair > 0 && fair >= p.ground_ratio * hot;
            const bool dim_ok = ground_floor && fair > 0 && fair >= p.ground_ratio * dim;
            printf("  reported, not judged: the mark as first worded and named (ground by what the physics did to "
                   "the place in the window;\n  a fair place-window starts with %.3g times the programs of a hot "
                   "one or more, and of a dim one, on the same floor)\n",
                   p.ground_ratio);
            printf("    programs a place-window: fair %.2f, hot %.2f, dim %.2f\n", fair, hot, dim);
            printf("    fair against hot: %s; fair against dim: %s\n", met(hot_ok), met(dim_ok));
            printf("    the floor: fair %llu windows from %u seeds, hot %llu from %u, dim %llu from %u: %s\n",
                   static_cast<unsigned long long>(judged.windows[0]), from[0],
                   static_cast<unsigned long long>(judged.windows[1]), from[1],
                   static_cast<unsigned long long>(judged.windows[2]), from[2], met(ground_floor));
        }

        // Gate 5's (build-plan-gate5.md 7): over the seeds that reach stage
        // 4 and run long enough past it, most late eras have a first lead,
        // in enough seeds and in enough of those counted; and gate 3's mark
        // holds. Gate 4's mark is judged beside it, in the same run.
        u32 counted = 0, changing = 0;
        for (u32 k = 0; k < p.seeds; ++k) {
            const EraTally& e = grounds[k].eras;
            if (!results[k].ok || results[k].stage[4] == kNone || e.late < p.era_late || e.late == 0)
                continue;
            ++counted;
            changing += e.late_changed >= need_of(p.era_share, e.late) ? 1 : 0;
        }
        const u32 need_share = need_of(p.era_seed_share, counted);
        const u32 need_changing = need_share > p.era_seeds ? need_share : p.era_seeds;
        const bool era_ok = counted > 0 && changing >= need_changing;
        printf("\ngate 5's pass mark: over the seeds that reach stage 4 and have %u late eras or more, %.0f%% of a "
               "seed's late eras have a first lead,\nin %u seeds or more and in %.0f%% of the seeds counted or "
               "more; and gate 3's pass mark\n",
               p.era_late, 100 * p.era_share, p.era_seeds, 100 * p.era_seed_share);
        printf("  seeds counted: %u; with a first lead in enough of their late eras: %u, %u needed: %s%s\n", counted,
               changing, need_changing, met(era_ok),
               counted >= p.era_seeds ? "" : " (too few seeds ran long enough past stage 4 to judge)");
        printf("  gate 3's pass mark: %s\n", met(spread_ok && gate2_ok));
        printf("%s: gate 5's pass mark\n", era_ok && spread_ok && gate2_ok ? "PASS" : "FAIL");
    }
    return 0;
}

} // namespace nova2

namespace nova2 {

// nova2 samples: each seed's samples as a CSV, made from its records
// (origin.records' seed-N.samples) into origin.samples' seed-N.csv. A run
// that keeps its records in files writes no CSV as it goes; this writes the
// one it would have, byte for byte, from the same settings the run had (the
// folders, the seeds, and whether the world has sources or heat, which
// decides the columns). The records are read as they lie, so by the build
// that wrote them; a run still going gives the rows it has written so far.
int cmd_samples(Config& cfg) {
    const u32 seeds = key_u32(cfg, "origin.seeds");
    const u64 first_seed = key_u64(cfg, "world.seed");
    const Str records = key_str(cfg, "origin.records");
    const Str samples = key_str(cfg, "origin.samples");
    WorldSettings ws;
    read_world_settings(cfg, &ws);
    key_accept_known(cfg); // the run's other settings, checked when it began
    if (!cfg.ok()) {
        fprintf(stderr, "nova2: %s\n", cfg.value_error());
        return 2;
    }
    char from[400], to[400];
    if (!records.n || !samples.n || records.n >= sizeof from || samples.n >= sizeof to) {
        fprintf(stderr, "nova2: samples needs origin.records, where the records are, and origin.samples, where the "
                        "CSVs go\n");
        return 2;
    }
    snprintf(from, sizeof from, "%.*s", static_cast<int>(records.n), records.p);
    snprintf(to, sizeof to, "%.*s", static_cast<int>(samples.n), samples.p);
    if (!make_folder(to)) {
        fprintf(stderr, "nova2: origin.samples: can't make the folder '%s'\n", to);
        return 2;
    }
    const bool gate4 = ws.sources || ws.heat.on;
    const bool gate5 = key_flag(cfg, "origin.calls");
    u32 made = 0;
    Array<Sample> rows;
    rows.resize(4096);
    for (u32 k = 0; k < seeds; ++k) {
        const unsigned long long seed = first_seed + k;
        char in_path[512], out_path[512];
        snprintf(in_path, sizeof in_path, "%s/seed-%llu.samples", from, seed);
        snprintf(out_path, sizeof out_path, "%s/seed-%llu.csv", to, seed);
        // Shared for reading and writing: the run may still be adding to it.
        HANDLE h = CreateFileA(in_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "nova2: seed %llu: no samples in %s\n", seed, in_path);
            continue;
        }
        FILE* csv = fopen(out_path, "w");
        if (!csv) {
            fprintf(stderr, "nova2: can't write %s\n", out_path);
            CloseHandle(h);
            return 1;
        }
        write_sample_header(csv, gate4, gate5);
        u64 n = 0;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(h, rows.data(), static_cast<DWORD>(rows.size() * sizeof(Sample)), &got, nullptr) || !got)
                break;
            const u32 whole = got / static_cast<u32>(sizeof(Sample)); // a row half written isn't one yet
            for (u32 i = 0; i < whole; ++i)
                write_sample(csv, rows[i], gate4, gate5);
            n += whole;
            if (got % sizeof(Sample))
                break;
        }
        CloseHandle(h);
        const bool ok = ferror(csv) == 0;
        if (fclose(csv) != 0 || !ok) {
            fprintf(stderr, "nova2: can't write %s\n", out_path);
            return 1;
        }
        printf("seed %llu: %llu samples to %s\n", seed, static_cast<unsigned long long>(n), out_path);
        ++made;
    }
    return made ? 0 : 1;
}

} // namespace nova2
