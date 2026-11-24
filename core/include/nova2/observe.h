#pragma once

// The observers (design.md 13.1-13.3; build-plan-gate1.md step 10). They read
// the facts each tick hands over, and sampled read-only views of the state,
// and never change the world: its hash is the same with or without them.
//
// - The facts log: every birth and death, in order (kept in memory at gate 1).
// - The genebank: each distinct genome once, with when and where it first
//   appeared, and its group.
// - Lineages by descent (13.2): a program's lineage is its first ancestor
//   born from static (or by hand in a test), and its generation is one more
//   than its parent's.
// - Genome groups (13.2), greedy: a new genome joins the first group whose
//   reference shares enough of its 3-byte runs, or founds a group.
// - Detectors (13.3): Replicator (a birth whose genome is its parent's),
//   Scavenger (a birth into a dead program's unchanged code), cross-owner
//   execution and Hijack (both sampled).
// - Samples: world totals, region measures (compressibility, distinct paths,
//   SENDs from idle runs), deaths by cause.
// - Turnover (gate 2's question, build-plan-gate2.md step 1): each place's
//   windows, whether their programs died and were replaced, and deaths per
//   program-tick by cause; and which windows had a cold start (step 6).
// - Ground (gate 4's question, build-plan-gate4.md step 1): each place's
//   windows by what the physics did to it in them, hot, dim or fair, and
//   the programs each started with.
// - A boot's parent is the boot block's last writer (13.2), often dead by
//   then: each place keeps the record of a writer that died while it was
//   the block's last, so the boot carries on its lineage.
// - Across places (13.3, gate 3; build-plan-gate3.md step 1): an arrival's
//   parent is the program that sent it, so its lineage carries on at the new
//   place. A lineage's members are counted at the place it began in and at
//   each other place it reached, a colony, whose first member there was a
//   colonist (born from an arrival). Stage 4 holds the first place, and a
//   colony settles its place by the same hold.
// - A sender may die while its message is on the way (build-plan-gate3.md
//   step 4): the record of a program that SENT to a port 0 is kept after it
//   dies, until nothing it sent is left on the network, waiting in a port 0
//   queue, or running as a prompt run (checked at each sample). So its
//   colonist still carries on its lineage, as a boot carries on its
//   writer's.
#include "nova2/array.h"
#include "nova2/compress.h"
#include "nova2/genome_code.h"
#include "nova2/hashmap.h"
#include "nova2/heat.h"
#include "nova2/os.h"
#include "nova2/record.h"
#include "nova2/types.h"
#include "nova2/watch.h"

namespace nova2 {

class FileIn;
class FileOut;
class Map;
class Place;
class World;

// The turnover measure (design.md 14.1, gate 2's question; build-plan-gate2.md
// step 1). Each place's time is cut into windows of `window` ticks. A window
// counts if it starts with `members` programs alive or more, and it turns
// over if at least half of those have died by its end and births made up at
// least half of them (so it isn't just emptying). It's crowded if its place's
// free memory was below a tenth at its start (13.3's "crowded"). A window
// that turns over with a cold start in it counts, and the measure says how
// many did (build-plan-gate2.md 7).
struct TurnoverSettings {
    u64 window = 200000;
    u32 members = 20;
};

inline constexpr u32 kDeathCauses = 9; // DeathCause's values (os.h)
inline constexpr u32 kBirthCauses = 5; // BirthCause's

// One finished window of one place.
struct TurnoverWindow {
    u32 place;
    u64 start;
    u32 starters;      // programs alive at its start
    u32 starters_died; // of those, dead by its end
    u32 births, deaths;
    bool crowded, counted, turned;
    bool cold;         // a cold start in it
};

// One place's running record of its windows, fed by the observer. Pure
// bookkeeping, so it can be tested on its own.
struct PlaceTurnover {
    // The window under way.
    u64 start = 0;
    u32 starters = 0, starters_died = 0, births = 0, deaths = 0;
    bool crowded = false;
    bool cold = false;
    // Totals over the place's whole run.
    u64 program_ticks = 0;                    // programs alive, summed over its ticks
    u64 deaths_by_cause[kDeathCauses] = {};   // every death, counted window or not
    u32 windows = 0, turned = 0;              // counted windows, and those that turned over
    u32 crowded_windows = 0, crowded_turned = 0;
    // Counted windows with a cold start in them, and of those that turned over
    // (counted, and crowded), the ones that had one.
    u32 cold_windows = 0, cold_turned = 0, crowded_cold_turned = 0;
    u32 streak = 0, longest = 0;              // counted windows in a row without turnover
    u64 longest_end = 0;                      // the tick the longest such run ended at

    void birth() { ++births; }
    void cold_start() { cold = true; }
    // A death, of a program born at tick `born` (~0 if unknown).
    void death(u64 born, DeathCause cause);
    // The end of a tick: `done` ticks have run, with `live` programs alive and
    // the place `full` now. When `done` is a multiple of the window, closes the
    // window under way into *out (true) and starts the next.
    bool end_tick(u64 done, u32 live, bool full, const TurnoverSettings& s, u32 place, TurnoverWindow* out);
};

// The ground measure (design.md 14.1, gate 4's question; build-plan-gate4.md
// 2 and step 1). Each place's windows, the turnover measure's, are named by
// what the physics did to the place in them:
//   hot:  it lost over a quarter of its core time to heat trips;
//   dim:  it lost over a quarter of its core time to cut-outs below the
//         threshold, or the memory holding its reset region was off for
//         over a quarter of the window;
//   fair: neither.
// A place's core time is its cores times the window's ticks, P and E cores
// alike. A window that is both hot and dim takes the name of the larger
// loss of core time (hot, if they are the same), and is counted as both
// besides. The measure is the programs alive at a window's start, by kind
// of ground.
enum class Ground : u8 { fair, hot, dim };
inline constexpr u32 kGrounds = 3;
// A window of `window` ticks at a place of `cores` cores, which lost `cut`
// core-ticks to cut-outs and `heat` to heat trips, its reset region's memory
// off for `dark` ticks. *both: it was hot and dim at once.
Ground ground_of(u32 cores, u64 window, u64 cut, u64 heat, u64 dark, bool* both);

inline constexpr u32 kNoGroup = ~0u; // a genome not kept has no group

// Who led a place as a window began (build-plan-gate5.md 2, gap 1 and the
// first choice about runs): of the programs alive there then, the genome
// group with the most and the lineage with the most (the lowest number or
// origin, of two with the same), each with its programs, how many of those
// were born there as copies (stage 4's count: not an arrival's births),
// and the most programs any other group or lineage had, since one leads
// only with more than any other. A program whose genome isn't kept has no
// group. Whether a lead counts (stage 4's numbers of them) is for the
// measure that reads the row.
struct GroundLead {
    u64 group_ref = 0;       // the group's reference genome's hash, its name for the whole run (0: none)
    u64 lineage = 0;         // the lineage's origin (0: no program there)
    u32 group = kNoGroup;    // the group's number then; with forgetting on, numbers change (kNoGroup: none)
    u32 group_programs = 0, group_copies = 0, group_second = 0;
    u32 groups = 0;          // the groups with a program there
    u32 lineage_programs = 0, lineage_copies = 0, lineage_second = 0;
};

// One finished window of one place, by its ground.
struct GroundWindow {
    u64 start;
    u64 cut, heat;        // core-ticks its cores spent cut out, and off by a heat trip
    u32 dark;             // ticks its reset region's memory was off
    u32 place, cores;
    u32 starters, births; // programs alive at its start, and births in it
    Ground ground;
    bool both;            // hot and dim at once
    bool lead_reads;      // the lineage with the most programs there read input then (gate 5; Observer::reads_input)
    GroundLead lead;      // who led the place as it began
    // The reviewer's measures (build-plan-gate4.md 11; Damian, 2026-11-20).
    u64 worked;           // core time its cores ran something, a thread or an idle run, in 2^-20ths of a core-tick
    u32 births_copies;    // of births, the programs born as copies made there (as lead.lineage_copies counts)
    u32 starters_copies;  // of starters, the programs born so
    u32 threads;          // its programs' threads alive as it began
    u32 top_threads;      // and those of the one program with the most
    // Gate 5's (build-plan-gate5.md step 11; gap 1): worn ground and holds.
    u32 slow_ppm;           // its cores' speed lost to wear at the place's voltage, on average over it, in millionths
    u32 failed;             // its ticks with a device failed and waiting for its visit
    u32 held[kAgeBins];     // the holds of the place standing as it began, by age
    u32 ended[kAgeBins];    // the holds of the place that ended in it, by their age then
    u32 holder_programs;    // the holding lineages' programs there as it began
    u32 holder_births;      // and the births there in it of lineages holding the place, made there
    bool worn;              // slowed by a tenth or more on average, or a device failed in it (else fresh)
    bool flaky;             // a core or memory device there was flaky as it began (gap 12)
};

// The ground of the window at each cascade's first place over the tick it
// began, so the report can read cascades from dim places apart from the
// rest (build-plan-gate5.md step 11's second question; Damian, 2026-11-21):
// (*out)[k] for cascades[k] is that window's Ground, kRootBoth where it was
// hot and dim at once, or kRootNone where no window of that place's over
// the tick was recorded (one the observers joined part-way through, or one
// not finished). A window from tick s holds the ticks done after s, to s +
// `window`. Pure bookkeeping, so it can be tested on its own.
inline constexpr u8 kRootBoth = kGrounds, kRootNone = kGrounds + 1;
inline constexpr u32 kRootGrounds = kGrounds + 2;
void root_grounds(const Records<GroundWindow>& windows, const Array<CascadeRec>& cascades, u64 window,
                  Array<u8>* out);

// One place's running record of its ground, fed by the observer from the
// place's counters (place.h: off_ticks, heat_ticks, region_off_ticks) as
// each of its turnover windows ends. Pure bookkeeping, so it can be tested
// on its own.
struct PlaceGround {
    bool begun = false;
    // Whether the observer has watched the window under way from its first
    // tick: one it joined part-way through isn't recorded.
    bool whole = false;
    // The place's counters as the window under way began.
    u64 cut0 = 0, heat0 = 0, dark0 = 0;
    // Who led the place as the window under way began. The observer sets
    // it as each window ends, for the next; close() puts it in the row.
    GroundLead lead;
    bool lead_reads = false; // and whether its leading lineage read input then
    // The reviewer's measures, for the window under way: the place's core
    // time worked as it began (Place::worked20), the births in it so far
    // that are copies made there, and as it began the programs born so,
    // the programs' threads and the most any one program had. The observer
    // sets them and puts them in the row (close() doesn't know of them).
    u64 worked0 = 0;
    u32 births_copies = 0, starters_copies = 0, threads = 0, top_threads = 0;
    // Gate 5's, for the window under way: the slowing summed over its ticks
    // (millionths), its ticks with a device failed, the holds as it began
    // and those ended since, the holders' programs then and births since,
    // and whether a part was flaky then.
    u64 slow_sum = 0;
    u32 failed = 0, holder_programs = 0, holder_births = 0;
    u32 held[kAgeBins] = {}, ended[kAgeBins] = {};
    bool flaky = false;
    // Totals over its recorded windows, by kind of ground: how many, the
    // programs they started with and the births in them; and the windows
    // that were hot and dim at once.
    u32 windows[kGrounds] = {};
    u64 starters[kGrounds] = {}, births[kGrounds] = {};
    u32 both = 0;

    // Starts the record, after the place's tick `done` (1: its first), its
    // counters standing at `cut`, `heat` and `dark`.
    void begin(u64 done, u64 cut, u64 heat, u64 dark);
    // The place's turnover window `w` has just ended, its counters standing
    // as given. True, with the window by its ground in *out, if the observer
    // watched all of it.
    bool close(const TurnoverWindow& w, u64 window, u32 cores, u64 cut, u64 heat, u64 dark, GroundWindow* out);
};

struct ObserverSettings {
    u32 sample_every = 100;       // ticks between samples (13.5)
    // How many samples to a written row (observe.write_every; Damian,
    // 2026-11-20): with 2, a row of `samples` and each place's row of
    // `volts` is written at the samples whose tick is a multiple of twice
    // sample_every, which halves both records. Sampling itself, and all that
    // is worked out at a sample, is the same whatever this is.
    u32 write_every = 1;
    double group_threshold = 0.8; // share of 3-byte runs in common to join a group (13.2; starting point)
    bool keep_facts = true;       // keep every birth and death in memory
    u32 min_genome = 8;           // the shortest genome a faithful birth may have
    // A genome is kept once a program carrying it has descendants this many
    // generations on, itself the first (3: a grandchild), whatever their
    // genomes; until then it is held while it could still be, and then
    // forgotten (13.2; build-plan-observe.md 3.2). 1: every genome is kept.
    u32 keep_generations = 3;
    // A finished lineage let go (3.3) gets a row in finished_lineages only if
    // it went this many generations, its first program the first, as
    // keep_generations counts (3: a grandchild, so a lineage with a row is
    // one a genome could be kept from); a shorter one is counted in the
    // tally (shallow_lineages, shallow_births) and no more is kept of it
    // (Damian, 2026-11-20). 1: every one gets its row. The programs set it
    // to keep_generations (observe.keep_generations): it has no key of its own.
    u32 lineage_generations = 3;
    // A day, in ticks (supply.day): a lineage reads input while its RECVs
    // that returned bytes come to one or more a member a day, over its life
    // so far (build-plan-gate5.md 2, gap 1).
    u32 day = 2000;
    // An open-ended run (build-plan-observe.md 3.5; Damian, 2026-11-19): a kept
    // genome no living program carries, or descends from within the kept
    // generations, leaves memory, and a group with it when its last member
    // does. Both stay in the genebank's records. Off, everything kept stays
    // in memory, as the gates' runs count it. Needs keep_generations of 2 or more.
    bool forget = false;
    u32 forget_floor = 65536; // the fewest kept genomes a forgetting pass is run for
    // A kept genome's bytes go to the genebank's second file as its
    // difference from the genome it came from, with a whole copy at least
    // every this-many steps along a line (genome_code.h; Damian,
    // 2026-11-20). 1: every genome whole.
    u32 genome_key_every = 16;
    TurnoverSettings turnover;
};

inline constexpr u32 kMaxKeepGenerations = 4; // ProgramRec::line's length, plus one

// A living program, by its uid.
struct ProgramRec {
    u64 uid, parent, origin; // origin: its lineage's first program
    u64 genome;              // its genome's hash
    // Its forebears' genomes: its parent's, its grandparent's and its
    // great-grandparent's (0: none), so a birth can keep the genome of the
    // forebear it makes keep_generations deep.
    u64 line[kMaxKeepGenerations - 1];
    u32 group, depth, place; // group: its genome's when it was born (kNoGroup: not kept then)
    u32 base;                // the allocation it was born in
    u32 wlo;                 // where its genome began in it (for the views: base + wlo is the genome's address)
    u32 fdepth;             // faithful births in a row ending with its own (0: it wasn't one)
    u32 size;                // its allocation at birth, in bytes (gate 5: what the killer and the watchdog end)
    bool faithful;           // its genome is its parent's, and not empty
    bool copy;               // faithful, or a near copy: at least half like its parent's genome (stage 4)
    bool arrived;            // born from an arrival, or a static run in its landed bytes: not a copy made here (13.3)
    // It SENT to a port 0, or the current copied bytes it held (12.6): its
    // record outlives it while that could arrive.
    bool sent0;
    bool sent_other;         // it SENT to a comms port: its lineage is remembered while that could be read (gate 5)
    // Made by a static run in bytes the current landed (12.6): its parent
    // is the program that held them where they were copied.
    bool carried;
    u64 image;              // its birth image's hash: the whole allocation it started in
    u64 born;
};

// A lineage: everything descended from one origin.
struct LineageRec {
    u64 origin;
    u32 place;
    BirthCause cause;
    u64 born;
    u32 alive;        // members alive now
    u32 alive_faithful; // of those, the ones born faithful
    u32 alive_copies;   // of those, the ones born as copies, faithful or near
    // Its members at the place it began in (`place`), and of those, the ones
    // born there as copies (an arrival's births aren't) and the faithful
    // ones: stage 4 holds that place (10.6). Its first member is the only one
    // of its members not born of another, so the place is its home.
    u32 home_alive, home_copies, home_faithful;
    u32 first_colony; // its colonies, a chain through ColonyRec::next (index + 1; 0: none)
    u32 colonies;     // how many
    u32 max_depth;    // the most generations reached
    u32 max_fdepth;   // the most faithful births in a row down one line
    u32 remembered;   // its dead members' records kept for a boot or an arrival (3.3)
    u64 births;       // births in it, the origin's included
    u64 faithful;     // births whose genome was the parent's
    u64 first_copy;   // the tick of its first faithful birth (~0: none): stage 1
    u64 first_viable; // the first faithful birth from a faithfully born parent: stage 2
    u64 extinct_at;   // ~0 while it lives
    u64 groups;       // distinct genome groups among its births
    u64 groups_full;  // of those, new while its place was full (stage 5)
    // Gate 5: its RECVs that returned bytes; and its members' ticks alive,
    // added up to the tick counted_to (each member a tick for each tick it
    // lived), brought up to date as members are born and die.
    u64 recvs;
    u64 member_ticks, counted_to;
};

// A lineage at a place beyond the one it began in (13.3, gate 3): its first
// member there was a colonist, born from an arrival a member elsewhere sent.
struct ColonyRec {
    u64 origin;            // the lineage
    u32 place;
    u32 from;              // the place the colonist's sender was at
    u64 colonist, first;   // the colonist, and its birth tick
    u32 foundings;         // arrivals that found it with no member there: the first colonist and any after it died out
    u32 alive;             // its members there now
    u32 alive_copies;      // of those, born there as copies, faithful or near (an arrival's births aren't)
    // Programs the current carried that found it with no member there
    // (12.6): they are no colonists, and no foundings. One whose first
    // member came that way has no colonist until an arrival finds it empty,
    // so until then it isn't counted as spread (by_current).
    u32 drifted;
    u64 births;            // births in it there
    u64 ended;             // the tick its last member there died (~0 while any live)
    u32 next;              // the lineage's next colony (index + 1; 0: none)
    bool by_current() const { return colonist == 0 && drifted != 0; }
};
static_assert(sizeof(ColonyRec) == 72, "the observer saves its colonies as they are: drifted took padding");

// A genome's state in the genebank (build-plan-observe.md 3.2): its slot
// free for another; held, while a line through it could still last; or
// kept, for good.
enum class GenomeState : u8 { free, held, kept };

struct GenomeRec {
    u64 hash;
    u32 at, len; // its bytes, in the bank
    u32 group;   // kNoGroup until it is kept
    u32 place;
    u64 first_tick;
    u64 origin;  // the lineage it first appeared in
    u64 count;   // births with it
    u32 alloc_min, alloc_max; // the sizes of the allocations born with it
    u64 image_copies;         // births with it whose whole allocation was the parent's
    // Where it came from: its first carrier's parent's genome (0: none) and
    // that carrier's generation in its lineage. So the kept genomes are a
    // tree, each line generation by generation (Damian, 2026-11-18).
    u64 parent_genome;
    u32 generation;
    u32 holders;      // the programs alive or remembered whose line could still keep it
    GenomeState state;
    // Once kept: how many differences its entry in the genebank's second
    // file is from a whole copy (0: it is one), and its row in the genebank
    // (genome_code.h), for the genomes kept after it that differ from it.
    u8 steps;
    u32 bank_row;
    u64 kept_at;      // the tick it was kept (~0: not yet)
};

// A kept genome as the genebank's file holds it (13.2), its bytes in a
// second file from `bytes_at`: whole, or as its difference from the genome
// it came from (genome_code.h says how; Observer::kept_genome reads one).
struct GenebankRow {
    u64 hash, parent_genome, origin, first_tick, kept_at, bytes_at;
    u32 len, group, place, generation;
};

struct GroupRec {
    u32 genome;           // its reference: the first genome in it
    u32 grams_at, grams_n; // the reference's distinct 3-byte runs, sorted
    // 1: its runs are its first reference's, a genome since let go
    // (forgetting), so they can't be worked out from the reference it has
    // now: a save holds them. 0: they are its reference's, and a save leaves
    // them out.
    u32 own_runs;
    u64 count;
};

// What an overrun reached (build-plan-gate5.md 2, gap 1): bytes a RECV wrote
// past the end of the allocation its buffer began in, by what they belonged
// to then. Another of the receiver's own allocations (its code or its
// data); one of a program of its lineage; one of another lineage's; a live
// thread's stack, whoever's; or memory no program held (free, the reset
// region, or an idle run's allocation). An overrun counts once for each
// kind it reached.
enum Reach : u8 { kReachOwn = 0, kReachKin, kReachOther, kReachStack, kReachFree, kReaches };

// Bytes an overrun wrote from another lineage's message, watched while they
// stay as written: a thread found running them at a sample is an injection
// (9.8). Kept in place and address order, no two overlapping: a later
// overrun there takes an earlier one's place.
struct OverrunRec {
    u64 tick;
    u64 receiver, receiver_origin;
    u64 sender, sender_origin, sender_genome;
    u32 place, addr, len;
    u32 bytes_at; // the bytes as written, in the observer's store
};

// A dead program that SENT to a comms port, while a message of its could
// still be read: its lineage and genome, so an overrun its message makes is
// from a known lineage (gate 5). Nothing is held for it.
struct Talker {
    u64 origin, genome;
};

struct Sample {
    u64 tick;
    u64 programs, threads, threads_started;
    u64 births, faithful;
    u64 deaths[kDeathCauses]; // by DeathCause
    u64 idle_runs, idle_faults, idle_caps, paths_new, idle_sends;
    u64 instructions;
    double region_ratio; // the mean compressibility of the reset regions
    u64 cross_owner, hijack, scavenger;
    u64 genomes, groups, lineages;
    // The copying ladder (build-plan-gate1.md 11), as running totals.
    u64 written, region_copies, program_copies, near_copies, image_copies;
    u64 handovers;
    u64 run_alloc, run_alloc_ok, run_spawn, run_spawn_ok, run_cpy, run_cpy_own;
    // Gate 2 (build-plan-gate2.md step 8), as running totals: cold starts,
    // and births from a boot block a program wrote (cause boot).
    u64 cold_starts, boots;
    // Gate 3 (build-plan-gate3.md step 6), as running totals: the network's
    // counts since it was built or opened (net.h's NetCounts), the colonies
    // founded (13.3), those with a member there now, and those settled (by
    // the criteria the observer settles by), and births by arrival.
    u64 net_sent, net_delivered, net_pings, net_full, net_stranded, net_lost, net_cut;
    u64 colonies, colonies_alive, settled, colonists;
    // build-plan-observe.md step 4: `genomes` counts the genomes kept, and
    // this the ones held now, not yet kept.
    u64 genomes_held;
    // Gate 4 (build-plan-gate4.md steps 7 and 8). As the world is at the
    // sample: its sources, and those in a swell and those dropped out (at
    // under a tenth of their capacity); the microwatts the sources give the
    // network, the places draw, the links, drains and sources lose, and the
    // world's heat gives the outside; the places' mean and highest
    // temperatures, in millionths of a degree.
    u64 sources, sources_swell, sources_out;
    i64 power_given, power_drawn, power_lost, heat_out;
    i64 temp_mean, temp_high;
    // As running totals over the places: heat trips by kind of device
    // (HeatKind), core cut-outs, the low-voltage interrupt's handlers
    // registered, the interrupts raised and taken, the threads that slept
    // within a tick of taking one, and of those the ones that outlived the
    // cut; and the solves, their sweeps and those that hit the cap.
    u64 trips[kHeatKinds];
    u64 cutouts;
    u64 lv_handlers, lv_raised, lv_taken, lv_slept, lv_outlived;
    u64 solves, solve_sweeps, solves_capped;
    // Gate 5 (build-plan-gate5.md step 1; design.md 13.1). As running totals
    // over the places: the calls programs' threads made, by kind (Call), and
    // those that did what they asked (Os::calls_ok); and their SENDs to a
    // port other than 0, asked for and let go.
    u64 calls[kCallCount], calls_ok[kCallCount];
    u64 sends_other, sends_other_ok;
    // SPAWN two ways (Os::spawns_thread): those that asked for a thread in
    // the caller's own program and those that asked for a new program, with
    // those of each that got it. The rest of calls[kSpawn] asked for neither.
    u64 spawns_thread, spawns_thread_ok, spawns_program, spawns_program_ok;
    // As running totals: RECVs that returned bytes; those that overran, and
    // by what they reached (Reach); those whose sender had died and left no
    // record, so they couldn't be watched; births in lineages that read
    // input then; and births by a program found injected, of the sender's
    // kind (its genome, or at least half like it).
    u64 recvs, overruns, reached[kReaches], overruns_unknown;
    u64 reader_births, injection_births;
    // As the world is at the sample: the lineages that read input and the
    // programs in them; and the threads found running bytes an overrun
    // wrote from another lineage's message, still as written (injections).
    u64 readers, reader_programs, injections;
    // Gate 5's world (build-plan-gate5.md step 13, for the views' charts),
    // as running totals: devices failed, replaced after failing and
    // visits, over the places; respawns; links cut (by the schedule and by
    // outage patches), failed by wear and mended; strikes; and straits'
    // partings.
    u64 failures, replaced, visits, respawns, link_cuts, link_worn, mended, strikes, partings;
};

// One place's voltage over one sample's ticks (build-plan-gate2.md step 8):
// the mean and the lowest, in millivolts, so a drop-out shorter than the
// sample still shows.
struct PlaceVolts {
    i16 mean, low;
};

// The origin test's stages for one lineage (10.6): 0 to 5, each building
// on the last. Stages 1 and 2 are a lineage's first faithful birth and its
// first faithful birth from a faithfully born parent; 3: it reaches
// `generations`; 4: it has held its place for `span` ticks; 5: `groups` new
// genome groups while its place is crowded, after it reached stage 4.
//
// A lineage holds a place at a check when it has `members` living members
// there or more, at least `copies` of them born there as copies (faithful,
// or at least half like the parent's genome), and they are at least `share`
// of the place's programs. A hold is unbroken from check to check: one check
// that fails ends it, and the next hold's span starts afresh. Once a lineage
// has reached stage 4 it keeps it, as it keeps the stages below. Stage 4 is
// a hold of the place a lineage began in, and a colony settles its place by
// the same hold (13.3).
struct StageCriteria {
    u32 generations = 10;
    u64 span = 100000;
    double share = 0.1;
    u32 members = 20;
    double copies = 0.5;
    u32 groups = 3;
};
// `alive` members at a place of `place_programs`, `copies` of them born there
// as copies.
bool lineage_holds(u32 alive, u32 copies, u32 place_programs, const StageCriteria& c);
// One lineage's stage 4 and 5 clock, or a colony's settling clock, kept by
// the caller.
struct LineageClock {
    u64 held_since = ~0ull;  // the first check of its current hold (~0: it isn't holding)
    u64 established = ~0ull; // the check at which it reached stage 4, or settled (~0: not yet)
    u64 groups_then = 0;     // its groups_full at that check: stage 5 counts from there
};
// Each call is a check, and updates the clock. `place_programs` are its home
// place's.
u32 lineage_stage(const LineageRec& L, u64 now, u32 place_programs, const StageCriteria& c, LineageClock* k);
// A colony's check: true once it has settled its place, by holding it for
// `span` ticks; its clock's `established` is the check it settled at. Once
// settled it stays so. `place_programs` are its place's.
bool colony_settles(const ColonyRec& C, u64 now, u32 place_programs, const StageCriteria& c, LineageClock* k);

// Gate 5's measure (design.md 14.1, gate 5's question; build-plan-gate5.md
// 2, gap 1, and step 1): do new kinds keep taking ground late in a run? It
// is read from the ground record's rows, which hold who led each place as
// each window began (GroundLead).
// - A genome group LEADS a place as a window begins when it has more of the
//   place's programs than any other group, and stage 4's numbers of them
//   (StageCriteria): `members` or more, `share` of the place's programs,
//   and `copies` of them born there as copies.
// - A FIRST LEAD is a group leading for the first time in the run, at any
//   place. A group is named by its reference genome's hash, which it keeps
//   for the whole run.
// - A seed's run from its stage 4 to its end is cut into ERAS of `era`
//   ticks, counted from the stage 4 tick; a part of one left at the end is
//   not an era. Its LATE eras are the later half of them (of an odd number,
//   the middle one is early), and the rest are early.
// - An era HAS CHANGE if a window that begins in it has a first lead.
// Groups split by drift as well as by adaptation, so this measures change,
// not progress.
bool group_leads(const GroundWindow& g, const StageCriteria& c);

inline constexpr u32 kNoEra = ~0u;
struct FirstLead {
    u64 tick;      // the start of the window the group first led as
    u64 group_ref; // the group's reference genome's hash
    u32 place;
    u32 programs, copies; // the group's programs there then, and those born there as copies
    u32 era;       // the era it came in (kNoEra: before stage 4, after the last whole era, or no stage 4)
    bool late;     // and whether that era is a late one
};

struct EraTally {
    u64 windows = 0, led = 0; // place-windows read, and those a group led
    u64 led_readers = 0;      // of the led, those whose leading lineage read input then
    u64 first_leads = 0;      // first leads in the whole run
    u64 first_before = 0;     // of those, before stage 4 (all of them, in a seed with no stage 4)
    u32 eras = 0, late = 0;   // whole eras from stage 4 to the end, and the late ones among them
    u32 changed = 0, late_changed = 0; // eras with a first lead in them, and the late ones with one
    u32 early() const { return eras - late; }
    u32 early_changed() const { return changed - late_changed; }
};

// Reads a seed's ground record, its rows in the order they were written
// (by window, so by time): `stage4` is the tick the seed reached stage 4
// (~0: it didn't), `end` its last tick, `era` an era's ticks (0: no eras).
// Every first lead goes to *firsts, in order, if it is given.
void tally_eras(const Records<GroundWindow>& rows, u64 stage4, u64 end, u64 era, const StageCriteria& c,
                EraTally* out, Array<FirstLead>* firsts);

// The memory the observers hold, in bytes, by what it is for (room the
// tables have taken but not yet used included): build-plan-observe.md
// step 1.
struct ObserverMemory {
    u64 genomes;  // the genebank: records, bytes and their index
    u64 groups;   // the groups, their references' 3-byte runs and the runs' index
    u64 lineages; // lineages, their groups seen, colonies and their clocks
    u64 programs; // the programs alive, and the dead still remembered
    u64 history;  // facts, samples, voltages and turnover windows
    u64 other;    // scratch
    u64 total() const { return genomes + groups + lineages + programs + history + other; }
};

class Observer {
  public:
    void init(const ObserverSettings& s);
    ObserverMemory memory() const;

    // One place's facts for the tick just done (the world calls this at the
    // barrier, place by place).
    void ingest(Place& p);
    // A place was torn down and built again (7.8): the record kept of its
    // boot block's last writer goes, with the disk. Its programs' deaths
    // come with the tick's facts, so its colonies and holds end by them.
    // Gate 5: the respawn is logged as a fact (RespawnRec), with the place's
    // new incarnation and point, and the landmass it stood on before.
    void respawned(u32 place, u64 tick, u32 incarnation, double x, double y, bool moved, u32 left_mass);
    // After every place's ingest for a tick: gate 5's watching of the world
    // as a whole (watch.h): cascades, patches formed, straits joining.
    void after_tick(World& w);
    // After every place's ingest for a tick: who leads each place whose
    // window ended in it, for the window that starts now (GroundLead). The
    // world calls set_leads when leads_due.
    bool leads_due() const { return !lead_due_.empty(); }
    void set_leads(World& w);
    bool due(u64 tick) const { return s_.sample_every && tick % s_.sample_every == 0; }
    const ObserverSettings& settings() const { return s_; }
    // The criteria a colony settles its place by, for the samples' count
    // (13.3): the origin test's (origin.span and the rest), or StageCriteria's
    // own if never given.
    void settle_by(const StageCriteria& c) { stages_ = c; }
    // Tests: the start of a hold, placed by hand, of lineage `index`'s home
    // (colony false) or of colony `index`'s place. `since` may lie before
    // the run (it wraps), for an age longer than the run so far. False if
    // that hold isn't standing.
    bool place_hold(bool colony, u32 index, u64 since);
    // A sample of the world's sampled views. `clocked`: one the world takes
    // by its clock (due), written as a row only at every write_every-th
    // (ObserverSettings); any other, a seed's last among them, is always
    // written. A written row is the sample at its tick as it stands: every
    // column of Sample is a running total or the world's state at that tick,
    // none counts or peaks over one sample's ticks. A place's row of `volts`
    // does: its mean and lowest are over every tick since the row before.
    void sample(World& w, bool clocked = false);
    // The latest sample, written or not, and how many have been taken: what
    // is worked out at a sample reads these, never the written rows.
    const Sample& last_sample() const { return last_; }
    u64 samples_taken() const { return taken_; }

    // Its whole memory to a file and back (design.md 13.1: "saved beside each
    // snapshot, outside the hash, so the family tree survives a resume";
    // build-plan-viewer.md step 9). load is for an observer given, by init,
    // the settings it was saved under: false if it wasn't, or if the file is
    // another build's. Read back, it goes on as the one saved would have.
    // A save made before the ground measure is read too: its ground record
    // starts where it carries on.
    // Records kept in files (records_to) are saved as how many rows each
    // file had, and load cuts the files back to that; records in memory
    // are saved whole. Call flush_records first, so the files hold every
    // row the save counts.
    void save(FileOut& f) const;
    bool load(FileIn& f);

    // Where the finished records go (samples, voltages, windows; design.md
    // 13.1, build-plan-observe.md 3.1): in memory as init leaves them, or
    // files in `folder` named seed-<seed>.samples, .volts, .windows and
    // .ground, written every `flush_seconds`. New files, unless `carry_on`: then
    // load opens them, cut back to the rows its save counted. Call after
    // init. False if the files can't be made.
    bool records_to(const char* folder, u64 seed, double flush_seconds, bool carry_on);
    // Writes the records buffered for the files (before a save).
    bool flush_records();
    // False once a file of records can't be written or read.
    bool records_ok() const;

    const ProgramRec* program(u64 uid) const;
    const LineageRec* lineage(u64 origin) const;
    const GenomeRec* genome(u64 hash) const;
    // The lineages in memory: lineage_at(i) for i under lineage_count(), a
    // free slot's origin 0. A lineage that never copied faithfully and has
    // no colony is let go once it is extinct and nothing can bring it back
    // (build-plan-observe.md 3.3): to finished_lineages if it went
    // lineage_generations generations, and otherwise into the tally of the
    // shallow ones, how many and the births in them. lineages_ever() counts
    // all three. With records in files the tally is written beside them at
    // each flush, as seed-<seed>.lineage_tally: a line of text.
    u32 lineage_count() const { return static_cast<u32>(lin_.size()); }
    const LineageRec& lineage_at(u32 i) const { return lin_[i]; }
    u64 lineages_ever() const { return lineages_.size() + finished_lineages.size() + shallow_lineages; }
    u64 shallow_lineages = 0, shallow_births = 0;
    Records<LineageRec> finished_lineages; // to a file with records_to, as the other records
    // The colonies (13.3), in the order they were founded.
    u32 colony_count() const { return static_cast<u32>(colonies_.size()); }
    // Of those, the ones the current founded, which the spread measure
    // leaves out (12.6): counted apart.
    u32 current_colonies() const;
    const ColonyRec& colony_at(u32 i) const { return colonies_[i]; }
    // A lineage's colony at a place, or null.
    const ColonyRec* colony(u64 origin, u32 place) const;

    // Gate 5's watching (watch.h; build-plan-gate5.md step 11): cascades,
    // the totals, respawns as facts, straits joining again, the current's
    // births, and the calls as each turnover window ends.
    CascadeWatch cascade_watch;
    WatchTotals tally5;
    Array<RespawnRec> respawn_log;
    Array<JoinRec> joinings;
    // The views' events timeline's facts (watch.h), oldest first.
    Array<EventRec> event_log;
    Array<CarriedRec> carried_births;
    Array<CallsRow> calls_rows;
    // The genebank's slots: genome_at(i) for i under genome_count(), each
    // kept, held or free (GenomeState; a free slot is no genome).
    u32 genome_count() const { return static_cast<u32>(gen_.size()); }
    const GenomeRec& genome_at(u32 i) const { return gen_[i]; }
    const u8* genome_bytes(const GenomeRec& g) const { return bank_.data() + g.at; }
    u64 genomes_kept() const { return kept_n_; }
    u64 genomes_held() const { return held_n_; } // held now, not yet kept
    u32 group_count() const { return static_cast<u32>(groups_.size()); }
    // A group's reference: the first genome in it. Its hash names the group
    // for the whole run, whatever its number becomes.
    const GenomeRec& group_reference(u32 group) const { return gen_[groups_[group].genome]; }
    // With forgetting on, group_count() is the groups with a living member
    // or one a living program descends from; these count every group founded
    // and every kept genome let go.
    u64 groups_ever() const { return groups_ever_; }
    u64 genomes_forgotten() const { return forgotten_; }
    // The kept genomes, as they were kept (13.2's genebank; build-plan-
    // observe.md 3.2), and their bytes, one byte a row: to files with
    // records_to, as the other records. The bytes are entries, a whole copy
    // or a difference from an earlier row's genome (genome_code.h):
    // kept_genome builds row `row`'s genome from them, false if the record
    // can't be read or isn't well formed.
    Records<GenebankRow> genebank;
    RecordStream genebank_bytes;
    bool kept_genome(u64 row, Array<u8>* out, u32* steps = nullptr) const;
    // For measuring what a load does, on a save of any build (nova2 sizes
    // --index): takes the genomes, their bytes and the groups as a save
    // holds them (they are left empty), and works the groups' runs and their
    // index out again as a load does. False if the groups' runs aren't their
    // references', where the save says they lie. Then the runs and the
    // lists' entries, to set beside the ones an older save holds.
    bool index_from(Array<GenomeRec>& gen, Array<u8>& bank, Array<GroupRec>& groups);
    const Array<u64>& group_runs() const { return grams_; }
    const Array<u32>& group_lists() const { return run_pool_; }
    u64 runs_listed() const { return run_lists_.size(); }

    // The similarity of two byte strings: the share of 3-byte runs in common
    // (of the larger set). Strings too short to have one compare by equality.
    static double similarity(const u8* a, u32 na, const u8* b, u32 nb);

    Array<BirthFact> births; // the facts log (keep_facts)
    Array<DeathFact> deaths;
    Records<Sample> samples; // the written rows (ObserverSettings::write_every)
    // The turnover measure: each place's record (by place id), and every
    // finished window that started with a program alive.
    Array<PlaceTurnover> turnover;
    Records<TurnoverWindow> windows;
    // The ground measure: each place's record (by place id), and every
    // window the observer watched from its first tick, programs or none.
    Array<PlaceGround> ground;
    Records<GroundWindow> ground_windows;
    // Each place's voltage, a row for each written sample: row i's place p
    // is at volts[i × volt_places + p].
    Records<PlaceVolts> volts;
    u32 volt_places = 0;
    PlaceVolts volt(u32 sample, u32 place) const { return volts[static_cast<u64>(sample) * volt_places + place]; }

    // Running totals.
    u64 total_births = 0, total_faithful = 0, total_deaths = 0, scavenger = 0;
    // The copying ladder (build-plan-gate1.md 11): births with something
    // written; copies of the reset region's code (8 bytes or more, found
    // there); births by programs that wrote 8 bytes or more; and of those,
    // ones at least half like their parent's genome.
    u64 written = 0, region_copies = 0, program_copies = 0, near_copies = 0;
    // Births whose whole starting memory is their parent's, byte for byte:
    // heredity without copying (a SPAWN into a relative's leftovers, 9.3).
    u64 image_copies = 0;
    // SENDs let go, those to a port 0, and the records of senders that died
    // kept for their arrivals (now).
    u64 sends = 0, sends_port0 = 0;
    u32 dead_senders() const { return static_cast<u32>(dead_senders_.size()); }
    // Births into the allocation the parent was born in: the parent's own
    // body handed on, nothing copied (build-plan-gate1.md 11). None of the
    // counts above include them, and none is faithful.
    u64 handovers = 0;
    u64 deaths_by_cause[kDeathCauses] = {};
    u64 births_by_cause[kBirthCauses] = {};
    u64 cold_starts = 0;
    // Gate 5's (build-plan-gate5.md step 1): RECVs that returned bytes,
    // those that overran and what they reached, and the rest of a sample's
    // running totals (Sample).
    u64 recvs = 0, overruns = 0, reached[kReaches] = {}, overruns_unknown = 0;
    u64 reader_births = 0, injection_births = 0;
    // Summed over the samples: the programs alive, those in lineages that
    // read input, and the threads found injected.
    u64 program_samples = 0, reader_program_samples = 0, injections_seen = 0;
    // The programs ever found injected: injections_seen counts a thread at
    // every sample it is found at, so one program found for a long while is
    // many of those and one of these.
    u64 injected_programs = 0;
    // Whether a lineage reads input now (ObserverSettings::day).
    bool reads_input(const LineageRec& L, u64 now) const;
    // The overruns watched now.
    u32 watched_overruns() const { return static_cast<u32>(over_.size()); }

  private:
    u32 add_genome(const u8* g, u32 len, u64 hash, u32 place, u64 tick, u64 origin, u64 parent_genome,
                   u32 generation);
    void ladder(Place& p, const BirthFact& f, bool near, bool handover);
    u32 assign_group(const u8* g, u32 len, u32 genome);
    ColonyRec* colony_of(LineageRec& L, u32 place);
    u32 new_group(u32 n, u32 genome);
    // Keeping and forgetting (build-plan-observe.md 3.2). A program alive,
    // or remembered dead, holds its own genome and its forebears' up to
    // keep_generations - 2 generations back: the ones a descendant of its
    // could still keep.
    void hold(const ProgramRec& r);
    void release(const ProgramRec& r);
    void keep(u32 slot, u64 tick, bool full);
    void forget_genome(u32 slot);
    // The forgetting pass (ObserverSettings::forget): every kept genome with
    // no holder goes, the groups left empty with them, and the groups are
    // numbered afresh, in order. Run from sample() once the kept genomes
    // reach forget_at_, which is then set to twice those left.
    void forget_extinct();
    u64 forget_at_ = 0;
    u64 groups_ever_ = 0, forgotten_ = 0; // groups founded; kept genomes let go
    // A lineage's records for a boot or an arrival (3.3): one more, one
    // fewer; and the lineage let go if it is finished.
    void remember(u64 origin, i32 by);
    void finish_lineage(u32 li);
    Array<u32> free_lin_; // lin_'s free slots
    Array<u32> free_gen_;  // gen_'s free slots
    u64 bank_dead_ = 0;    // bank_ bytes of forgotten genomes, until the bank is compacted
    u64 kept_n_ = 0, held_n_ = 0;
    void list_run(u64 run, u32 group);
    void index_groups();
    bool runs_again(const Array<u8>& own);
    static void grams_of(const u8* g, u32 len, Array<u64>* out);

    ObserverSettings s_;
    // records_to's folder ("" in memory), seed and clock.
    char records_dir_[400] = {};
    u64 records_seed_ = 0;
    double records_flush_ = 30;
    void records_path(const char* kind, char* out, usize n) const;
    // A sample's compressibility estimates, a place each, are worked out on
    // the world's host threads: the places are dealt out to at most
    // kCompressLanes lanes, each with tables of its own, kept from sample to
    // sample (compress.h). Each place's ratio is a function of its bytes
    // alone, and they are summed in place order afterwards, so the sample is
    // the same on any number of threads (optimization-plan.md 4.5).
    static constexpr u32 kCompressLanes = 8;
    CompressScratch compress_[kCompressLanes];
    Array<double> ratios_; // per place, for the sample being taken
    World* ratio_world_ = nullptr;
    u32 ratio_lanes_ = 1;
    static void ratio_lane(void* ctx, u32 lane);
    // A place's threads' sampled views, worked out in the same lanes: how many
    // run code their program doesn't hold, and how many look hijacked. Each is
    // a function of the place and the records alone, and they are added in
    // place order afterwards (optimization-plan.md, pass 7).
    Array<u64> walk_cross_, walk_hijack_; // per place, for the sample being taken
    void walk_threads(const Place& p, u64* cross_owner, u64* hijack) const;
    HashMap<u32> programs_; // uid -> index in prog_
    Array<ProgramRec> prog_;
    Array<u32> free_prog_;
    HashMap<ProgramRec> dead_senders_; // uid -> the record of a sender that died
    HashMap<u8> sent_live_;            // scratch: the senders still referred to
    void forget_senders(World& w);
    HashMap<u32> lineages_; // origin -> index in lin_
    Array<LineageRec> lin_;
    HashMap<u32> genomes_;  // hash -> index in gen_
    Array<GenomeRec> gen_;
    Array<u8> bank_;
    Array<GroupRec> groups_;
    Array<u64> grams_;      // every group reference's 3-byte runs, sorted per reference
    // The groups' index, so a new genome isn't compared with every group
    // (gate2-2's seed 3 spent about 11 hours of 17 doing that): for each 3-byte run,
    // the groups whose reference holds it, in group order. A list is a chain
    // of blocks in run_pool_, each [next, capacity, used, groups...], the
    // capacity doubling from 2 to 256; offset 0 is no block.
    struct RunList {
        u32 head, tail; // its first and last blocks
        u32 n;          // the groups on it
    };
    HashMap<RunList> run_lists_; // 3-byte run -> its list
    Array<u32> run_pool_;
    Array<u32> gramless_;        // the groups whose reference is under 3 bytes, in order
    Array<u64> rarity_;          // assign_group's scratch: the genome's runs by their lists' lengths
    Array<u64> candidates_;      // and the groups on the rarest runs' lists
    Array<u8> listed_;           // per group: on candidates_ already (0 between calls)
    HashMap<u8> lineage_groups_; // (origin, group) pairs seen
    Array<ColonyRec> colonies_;
    StageCriteria stages_;               // settle_by's
    Array<LineageClock> colony_clocks_; // each colony's settling clock, for the samples
    Array<ProgramRec> boot_writer_; // per place: the boot block's last writer, kept if it died as such (uid 0: none)
    Array<u64> scratch_;
    Array<u8> entry_; // a kept genome's entry, being made
    // Who leads a place now, among the programs alive in its OS (GroundLead).
    void lead_of(const Os& os, GroundLead* out);
    Array<u64> lead_keys_; // lead_of's scratch
    Array<u32> lead_due_;  // the places whose window ended in the tick being ingested
    // Per place, its voltage since the last sample: summed, the lowest, and
    // the ticks.
    struct VoltSum {
        i64 sum = 0;
        i32 low = 0;
        u32 ticks = 0;
    };
    Array<VoltSum> volt_sum_;
    // Gate 5: the overruns watched (OverrunRec), their bytes end to end, and
    // the programs found injected, with the genome of the sender whose bytes
    // they ran. A sample drops the overruns no longer as written, then looks
    // for threads in the rest.
    Array<OverrunRec> over_;
    Array<u8> over_bytes_;
    Sample last_{};  // the latest sample
    u64 taken_ = 0;  // samples taken
    HashMap<u64> injected_; // uid -> the sender's genome
    HashMap<Talker> dead_talkers_; // uid -> a dead sender to a comms port, while its messages wait
    void forget_talkers(World& w);
    void ingest_recvs(const Os& os, u64 now);
    void watch(const OverrunRec& r, const u8* bytes);
    u64 find_injections(World& w);
    // Gate 5's watching: each lineage's and each colony's hold of its place
    // (the first sample of its current hold; ~0: not holding), by its index;
    // each place's cut-outs and trips summed at the last tick; the patches
    // there at the last tick (type in the high half, index in the low); each
    // strait's state then, by its landmasses (and a scratch list for a
    // respawn's rebuilt list); and the proximity neighbours, as of the map's
    // respawns count, with the tick's inputs (scratch).
    Array<HoldClock> lin_held_, col_held_;
    Array<u64> last_cut_, last_trip_;
    Array<u64> patch_seen_;
    Array<StraitSeen> strait_seen_, strait_next_;
    // Each place's failures by kind (HeatKind) at the last tick, for the
    // events log; and a fact onto it, the oldest let go past kEventsKept.
    Array<u64> fail_seen_;
    void log_event(u64 tick, u32 place, EventKind kind, u8 detail);
    Array<u32> adj_begin_, adj_;
    u32 adj_respawns_ = ~0u;
    Array<CascadeInput> cin_;
    void holds_at_sample(World& w);
    void holds_as_windows_begin(World& w);
    i32 feeding_sag(World& w, u32 place);
};

// Where each place stood over a run, for the report's straits section (gap
// 13). A respawn may put a place on another landmass, so a place-window, a
// colony's founding and a first lead are each read by the landmass their
// place stood on at their tick, its incarnation's, not where it stands at
// the run's end (build-plan-gate5.md 11, Damian's "fix", 2026-11-23).
struct PlaceLands {
    Array<u32> at;   // by place, its first respawn in the lists below; at[places], their end
    Array<u64> tick; // each respawn's tick, place by place, in order
    Array<u32> left; // and the landmass the place stood on before it
};
// From the observers' respawns, for `places` places.
void place_lands(const Observer& o, u32 places, PlaceLands* out);
// The landmass `place` stood on at `tick`: after its respawns at or before
// it (a respawn is made first in its tick), and the map's now after its
// last.
u32 land_then(const PlaceLands& L, const Map& m, u32 place, u64 tick);

} // namespace nova2
