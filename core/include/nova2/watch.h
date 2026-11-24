#pragma once
// Gate 5's watching (build-plan-gate5.md step 11; gap 1): what the observers
// keep for the named checks and the watched mechanics, beside the ground
// record. Pure bookkeeping, so each part can be tested on its own.
//
// Cascades (12.2; gap 1, and section 7's row). A place GOES OFF in a tick
// when devices there cut out or trip by heat. It PASSES IT ON to a proximity
// neighbour (a power link) that goes off after it, within kCascadeTicks of
// the start of a move of the neighbour's voltage toward the edge, a move
// that began within kCascadeTicks of the first place going off: a rise past
// the neighbour's least maximum before a trip, a fall before a cut-out.
// Places that go off in the same tick pass nothing to each other, as a
// source's drop-out takes them. A cascade is a chain of two places or more,
// its size the places it reached, and it is named by what began it: a
// strike at its first place, or a swell over it, within kCascadeTicks
// before that place went off; and otherwise load (a crowd going off).
#include "nova2/array.h"
#include "nova2/os.h" // kCallCount
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kCascadeTicks = 10;
inline constexpr u32 kCascadeRing = 32; // ticks of voltage kept a place: back to 3 x kCascadeTicks + 1
enum class CascadeCause : u8 { load = 0, strike, swell };
inline constexpr u32 kCascadeCauses = 3;

// What a place did in a tick, for the cascades.
struct CascadeInput {
    i32 mv = 0;           // its voltage
    i32 maximum = 0;      // the least maximum of its devices
    bool cut = false;     // devices there cut out this tick
    bool trip = false;    // or tripped by heat
    bool struck = false;  // a strike landed there this tick
    bool swelled = false; // a swell is over it
};

struct CascadePlace {
    i32 mv[kCascadeRing] = {};
    u64 off = ~0ull;    // the last tick it went off (~0: never)
    u64 strike = ~0ull; // the last tick a strike landed there (~0: never)
    u64 swell = ~0ull;  // the last tick a swell was over it (~0: never)
    u32 cascade = ~0u;  // the cascade its last going off is part of (~0: none yet)
    CascadeCause why = CascadeCause::load; // what was there as it last went off: a strike, a swell, or neither
    u32 seen = 0;       // ticks of voltage it has (up to kCascadeRing)
};

struct CascadeRec {
    u64 start = 0, last = 0; // its first place's going off, and the latest going off in it
    u32 root = 0;            // its first place
    u32 size = 0;            // the places it reached
    CascadeCause cause = CascadeCause::load;
    i32 sag = 0;             // the stiffness of the source feeding its first place: mV at rated load (0: none)
};

class CascadeWatch {
  public:
    void init(u32 places);
    // Tick `t` at every place (in[i] for place i of n), each place's
    // proximity neighbours adj[begin[i]] to adj[begin[i + 1]] - 1. A cascade
    // it finds begun goes on the end of `cascades` with a sag of 0, for the
    // caller to fill in; it returns how many there were before the tick.
    u32 step(u64 t, const CascadeInput* in, u32 n, const u32* begin, const u32* adj);
    // A place built again (7.8): what it had goes.
    void respawned(u32 place);
    Array<CascadePlace> places;
    Array<CascadeRec> cascades;

  private:
    i32 mv_at(const CascadePlace& p, u64 tick) const { return p.mv[tick % kCascadeRing]; }
    // Whether place b, going off at t as `in` has it, had its voltage move
    // toward the edge in time for a neighbour that went off at ta.
    bool passes(const CascadePlace& b, const CascadeInput& in, u64 t, u64 ta) const;
};

// A respawn (7.8), as a fact (13.1): its tick, its place, the place's new
// incarnation (its respawns so far, this one among them), its new point,
// whether it found one (a place that found none stays where it was), and
// the landmass it stood on before: a new point may be on another landmass,
// and the straits section reads each place-window by where its place stood
// then (build-plan-gate5.md 11, 2026-11-23).
struct RespawnRec {
    u64 tick;
    double x, y;
    u32 place, incarnation, left_mass;
    bool moved;
};

// A fact for the views' events timeline (design.md 13.4; build-plan-gate5.md
// step 13) that no table above keeps: a device failed (`detail`: its
// HeatKind), a program ended by the killer or by the watchdog, or a strait
// parted (`place`: the strait's number). The observers keep the latest
// kEventsKept.
enum class EventKind : u8 { failure = 0, kill, watchdog, parted };
inline constexpr u32 kEventsKept = 4096;
struct EventRec {
    u64 tick;
    u32 place;
    u8 kind, detail;
    u16 pad;
};

// A strait joining again (gap 13): the tick and its two landmasses.
struct JoinRec {
    u64 tick;
    u32 strait, ma, mb;
};

// A strait as the observers last saw it: known by its two landmasses, since
// a respawn builds the list of straits again (Links::relink), perhaps in
// another order; and whether it was parted.
struct StraitSeen {
    u32 ma, mb, parted;
};

// A program made by a static run in bytes the current landed (12.6): where
// it was born, where its parent held the bytes, and its lineage's home.
struct CarriedRec {
    u64 tick, origin;
    u32 place, from, home;
};

// The world's calls by kind as a turnover window ends (running totals, as
// the samples have them), with the program-samples so far: era by era.
struct CallsRow {
    u64 tick, program_samples;
    u64 calls[kCallCount];
};

// A lineage's hold of a place (stage 4's hold, 13.3), as the observers
// keep it for worn ground: whose, where, and the sample it began at (~0:
// not holding).
struct HoldClock {
    u64 origin = 0, since = ~0ull;
    u32 place = 0;
};

// A program's size at birth (its allocation): under 256 bytes, 1 KB, 4 KB,
// 16 KB, and more. A hold's age: under 1 million ticks, 4 million, 16
// million, and more.
inline constexpr u32 kSizeBins = 5;
u32 size_bin(u32 bytes);
inline constexpr u32 kAgeBins = 4;
u32 age_bin(u64 ticks);

// The totals the report reads (all running).
struct WatchTotals {
    // The out-of-memory killer [0] and the watchdog [1] (gap 14): the
    // programs each ended, by size at birth; of those, how many were of
    // their place's leading lineage (by its programs there as its window
    // began), and that lineage's share of the place's programs then, summed
    // over them (what ends in proportion to programs would give). Births by
    // size, for the comparison.
    u64 ended[2][kSizeBins] = {};
    u64 ended_leader[2] = {};
    double leader_share[2] = {};
    u64 births_by_size[kSizeBins] = {};
    // Flaky parts (gap 12): at places with no flaky part [0] and with one
    // [1], the births made there by a parent (not a hand-over), those whose
    // genome differs from the parent's, and the place-ticks.
    u64 flaky_births[2] = {}, flaky_mutations[2] = {}, flaky_ticks[2] = {};
    // Patches formed, by PatchType (12.3), and place-ticks with a device
    // failed and waiting for its visit (scars, 7.8).
    u64 patches[8] = {};
    u64 scarred_ticks = 0;
};

} // namespace nova2
