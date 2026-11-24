#pragma once

// A program's family, and the figures of a place and of the world
// (build-plan-viewer.md step 3; design.md 13.4). As in programs.h, what the
// views show is worked out here apart from its drawing, so the tests can
// set each figure beside the observers' own number. Everything is read
// from the observers' records and the places' counters as they stand: no
// counter is added to the engine, and nothing here changes the world.
#include "canvas.h"
#include "nova2/observe.h"
#include "nova2/types.h"
#include "programs.h"

namespace nova2 {

// The place a program was born in, by its uid (programs.h's uid_text).
inline u32 uid_place(u64 uid) { return (uid >> 40) ? static_cast<u32>((uid >> 40) - 1) : 0; }

// A program's family: its parent and its children here, its lineage, and
// its genome.
struct Family {
    bool known = false;       // the observer has its record; without one only the parent and children are known
    u64 parent = 0;           // 0: none (born of static bytes, or placed by hand)
    bool parent_alive = false;
    u32 parent_place = 0;     // where the parent lives, if it does
    u32 children = 0;         // living programs in this place whose parent it is
    static constexpr u32 kShown = 6;
    u64 child[kShown] = {};   // the first of them, the oldest first
    u64 origin = 0;           // its lineage
    u32 lineage_alive = 0;    // the lineage's members alive in the world
    u32 lineage_here = 0;     // and in this place
    u32 lineage_places = 0;   // places with a member alive: its home and its colonies
    u64 lineage_births = 0, lineage_faithful = 0;
    u32 depth = 0;            // generations from its lineage's first program
    u32 fdepth = 0;           // faithful births in a row ending with its own
    bool faithful = false, copy = false, arrived = false;
    u32 group = 0;            // its genome's group
    u32 genome_len = 0;
    u64 genome_births = 0;    // births with its genome, anywhere, ever
};

void family_of(World& w, const Place& p, const Observer* obs, const ProgramRow& row, Family* out);

// One of the commonest of something: a lineage (its origin) or a genome
// (its hash, and its length), and how many.
struct Share {
    u64 key = 0;
    u64 count = 0;
    u32 len = 0;
};

inline constexpr u32 kTop = 5;

// A place's figures now.
struct PlaceFigures {
    u32 programs = 0, threads = 0;
    u32 known = 0;                 // living programs the observer has records of: what the shares are of
    u64 births = 0, deaths = 0;    // the place's counts since it was built or opened
    bool watched = false;          // the observer keeps this place's turnover record
    u64 deaths_by_cause[kDeathCauses] = {}; // every death the observer saw here
    u32 faithful = 0, copies = 0;  // living programs born faithful, and born as copies (faithful or near)
    u32 lineage_count = 0, genome_count = 0; // distinct among the living
    Share lineages[kTop], genomes[kTop];     // the commonest among the living, the commonest first
    u32 lineages_shown = 0, genomes_shown = 0;
    PlaceTurnover turnover;        // its turnover record, the window under way included
    u64 window = 0;                // a turnover window's length in ticks
};

void place_figures(const Place& p, const Observer* obs, PlaceFigures* out);

// The world's figures now.
struct WorldFigures {
    u64 programs = 0, threads = 0;
    u64 births = 0, faithful = 0, deaths = 0;
    u64 deaths_by_cause[kDeathCauses] = {};
    u64 births_by_cause[kBirthCauses] = {};
    u64 cold_starts = 0;
    u64 genomes = 0, groups = 0, lineages = 0; // genomes: those kept (13.2)
    u64 genomes_held = 0;          // held, while a line through them could still last
    u64 lineages_alive = 0;        // lineages with a member alive
    u64 colonies = 0, colonies_alive = 0;
    Share top_lineages[kTop];      // the lineages with the most members alive
    Share top_genomes[kTop];       // the kept genomes born most often
    u32 lineages_shown = 0, genomes_shown = 0;
};

void world_figures(World& w, const Observer* obs, WorldFigures* out);

const char* death_name(u32 cause);

// The family of the program picked, in r (the programs page, in place of
// its code and threads).
void draw_family(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                 const ProgramRow& row, bool alive, const UiInput& in);

// The inspector's figures page: the place's, or the world's.
void draw_figures(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                  const UiInput& in);

} // namespace nova2
