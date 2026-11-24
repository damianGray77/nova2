#pragma once

// The sources on the map (design.md 4.2, 4.3, 6.1; build-plan-gate4.md step
// 3): from gate 4 a place has no source of its own. Sources land on the map,
// each at a place, and the voltage solve (solve.h) shares them out along the
// proximity links.
//
// - A source is a place, a voltage and an internal resistance (its
//   stiffness). Its type is a row of the catalogue: the voltage, the range
//   its stiffness is drawn from, how deep its night is, the size and pace of
//   its swings, and how often it drops out and swells.
// - Everything about a source is a function of the seed and its number in
//   the seed's sequence, and its state a function of those and the tick. So
//   nothing the world does changes a source (principle 3), and a snapshot
//   saves nothing of them.
//   - The sequence: first the sources placed by hand (sources.fixed), which
//     last the whole run at their full size; then those there at the start,
//     each as old as a draw puts it, evenly within its life; then one for
//     each birth, the gaps between births drawn at the chance of a new one a
//     tick.
//   - Its landing: a place, drawn with a chance in proportion to its
//     catchment's area raised to sources.area_power, then a land cell of
//     that catchment, drawn evenly. The point is kept, for gate 5's
//     respawning (7.8). On a map with no raster every place has the same
//     chance, and the point is the place.
//   - Its size is the load it's rated for, drawn skewed: the least × (the
//     most ÷ the least)^(u^skew), u drawn evenly, so most are small and a
//     few large. Its stiffness is its sag at that load, drawn evenly on a log
//     scale within its type's range; its internal conductance follows
//     (supply.h's source_conductance).
//   - Its life is drawn evenly on a log scale from a quarter of sources.life
//     to four times it. It grows over the first tenth of its life and fades
//     over the last tenth, by its capacity.
// - Its capacity at a tick, a share of its full conductance, is the product
//   of that growing and fading, the day, its swings and its drop-outs, by
//   the law a place's own source had at gate 2 (supply.h's CapacityLaw),
//   each source on streams of its own. The day's phase is its place's: the
//   day sweeps the map from east to west, sources.day_span days from one
//   edge to the other, so a place further east has its noon sooner.
// - A swell multiplies its voltage for a while, by a factor drawn evenly
//   between sources.swell.min and .max. In each stretch of ticks (the
//   smallest power of two at least the longest swell) one starts with a
//   chance set by the type's rate, at a tick drawn evenly in the stretch,
//   with a length drawn evenly on a log scale. If two overlap, the higher
//   counts.
// - From gate 5 drop-outs and swells ramp (events.h; 12.4): each rises over
//   a lead of its own, holds for its length, and eases over as long again,
//   and a share are false alarms. A drop-out's capacity and a swell's
//   factor move in a straight line between 1 and the event's own. With no
//   ramps (ramp.lead.max = 0) they start and end at once, as at gate 4.
//
// The sources alive at a tick are worked by walking the sequence, so asking
// for a tick before the last one asked for starts the walk again from the
// run's start: a few thousand sources at most, each a handful of draws.
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/events.h"
#include "nova2/map.h"
#include "nova2/rng.h"
#include "nova2/str.h"
#include "nova2/supply.h"
#include "nova2/types.h"

namespace nova2 {

inline constexpr u32 kSourceTypes = 3;
extern const char* const kSourceTypeNames[kSourceTypes]; // "steady", "daylight", "gusty": for the viewer only
inline constexpr u32 kMaxFixedSources = 256;

// A catalogue row (4.2).
struct SourceType {
    double share = 1;         // of the sources drawn, against the other types' shares
    i32 voltage = 1000;       // mV
    i32 sag_min = 50, sag_max = 50; // mV at the rated load: its stiffness
    double night = 1;         // capacity at midnight, a share of noon's; 1: it ignores the day
    double swing = 0;         // how far below full its swings reach, at most
    u64 swing_period = 20000; // ticks from one swing level to the next
    double dropouts = 0;      // drop-outs starting per source per million ticks
    double swells = 0;        // swells starting per source per million ticks
};

// A source placed by hand (sources.fixed).
struct FixedSource {
    u32 place = 0, type = 0;
    double watts = 0;
    i32 sag = 0;
};

struct SourceSettings {
    bool on = true;         // supply.model = sources
    double start = 25;      // sources at the start, per 100 places
    double chance = 0;      // new ones per 100 places per million ticks
    u64 life = 20000000;    // the middle of their lifetimes, in ticks
    double area_power = 1;  // a place's chance of one: its catchment's area to this power
    double size_min = 100, size_max = 800, size_skew = 3; // the rated load, in watts
    u64 day = 2000;         // ticks
    double day_span = 1;    // days between the map's east and west edges
    // Drop-outs' lengths and depths, for every type (gate 2's).
    u64 dropout_min = 10, dropout_max = 1000;
    double blackout_share = 0.5, keep_min = 0, keep_max = 0.5;
    // Swells' factors and lengths, for every type.
    double swell_min = 1.1, swell_max = 1.8;
    u64 swell_shortest = 10, swell_longest = 1000;
    RampSettings ramp; // drop-outs' and swells' ramps (ramp.*)
    SourceType type[kSourceTypes];
    FixedSource fixed[kMaxFixedSources];
    u32 fixed_count = 0;
};

// Reads them through the registry; bad values fail through cfg.
void read_source_settings(Config& cfg, SourceSettings* s);
// The average of lifetimes drawn evenly on a log scale from a quarter of
// `life` to four times it: life × 3.75 ÷ ln 16.
double source_mean_life(u64 life);

// A source: what the seed fixes of it.
struct Source {
    u32 index = 0;      // its number in the seed's sequence
    u32 place = 0;
    u32 cell = 0;       // the raster cell it landed on (a landscape's only)
    double x = 0, y = 0; // the landing point, in the map's height
    u8 type = 0;
    bool fixed = false; // placed by hand: there for the whole run at its full size
    i32 voltage = 0;    // mV
    i32 sag = 0;        // mV at its rated load
    u64 rated_uw = 0;   // the load it's rated for
    u64 gs = 0;         // its conductance at full capacity, 2^-20 S
    i64 birth = 0;      // the tick it began (before 0 for one older than the run)
    u64 life = 0;       // ticks
    u64 phase = 0;      // its place's day: ticks past midnight at tick 0
};

// A source's state at a tick.
struct SourceState {
    u32 capacity = 0; // its share of gs, in 2^-20ths
    u32 swell = kCapacityOne; // its voltage's factor, in 2^-20ths (1 or more)
    u64 g = 0;        // its conductance now, 2^-20 S
    i64 uv = 0;       // its voltage now, in microvolts
};

// A swell: when it starts, how long it lasts, and its factor in 2^-20ths.
struct Swell {
    u64 start = 0, length = 0;
    u32 factor = kCapacityOne;
};

class Sources {
  public:
    // Works out the laws and the landing table. False, with the reason, if
    // the settings can't make them. The map must outlive it.
    bool init(const SourceSettings& s, const Map* map, u64 seed, TextBuf* why);

    // The sources alive at a tick, in sequence order, with their states.
    void seek(u64 tick);
    u64 tick() const { return tick_; }
    u32 count() const { return static_cast<u32>(live_.size()); }
    const Source& source(u32 k) const { return live_[k]; }
    const SourceState& state(u32 k) const { return state_[k]; }

    // Source `index` of the sequence, whenever it lives. `birth` is its
    // birth as the walk gives it (a fixed one's and a starting one's are
    // worked here, and `birth` ignored).
    Source make(u32 index, i64 birth) const;
    // Its state at a tick it's alive at, and the factors behind it.
    SourceState state_at(const Source& s, u64 tick) const;
    u32 envelope(const Source& s, u64 tick) const; // its growing and fading
    u32 day(const Source& s, u64 tick) const;
    u32 swing(const Source& s, u64 tick) const;
    u32 dropout(const Source& s, u64 tick, Dropout* which = nullptr) const;
    u32 swell(const Source& s, u64 tick, Swell* which = nullptr) const;
    bool swell_in(const Source& s, u64 j, Swell* w) const;
    // The drop-out that starts in stretch j of the source's, if one does; and
    // a stretch's length in ticks.
    bool dropout_in(const Source& s, u64 j, Dropout* d) const;
    u64 dropout_stretch(const Source& s) const;
    u64 swell_block() const { return swell_block_; }
    // A place's day: ticks past its midnight at tick 0.
    u64 phase_of(u32 place) const;

    // A place respawned (7.8): each source joins the place nearest its
    // landing point again (its cell's catchment's; a fixed source's by its
    // point). Its day stays its first place's as the map was set up, so the
    // sources are a function of the seed and the tick still (principle 3).
    void reattach();

    // How many of the sequence have begun and ended by the tick last asked
    // for, for reports.
    u64 begun() const { return begun_; }
    u64 ended() const { return ended_; }
    u32 fixed_count() const { return s_.fixed_count; }
    u32 start_count() const { return start_count_; }
    const SourceSettings& settings() const { return s_; }
    // One line on the sources alive now.
    void describe(TextBuf* t) const;

  private:
    void restart();
    u32 land(Rng& r, u32* cell) const;
    void attach(Source* s) const;
    Array<double> x0_, y0_; // the places' points as the map was set up

    SourceSettings s_;
    const Map* map_ = nullptr;
    u64 seed_ = 0;
    CapacityLaw law_[kSourceTypes];
    u64 type_cut_[kSourceTypes] = {}; // thresholds for a 64-bit draw
    u64 birth_p64_ = 0;               // the chance of a new one a tick
    u32 start_count_ = 0;
    // Landing: the places with a catchment, their chances summed, and each
    // one's cells (a landscape's only).
    Array<u32> land_place_;
    Array<double> land_sum_;
    Array<u32> cell_begin_, cells_;
    // Ramps (events.h): the law, and how many stretches back an event that
    // still counts can have started.
    RampLaw ramp_;
    u64 drop_back_ = 1, swell_back_ = 1;
    // Swells.
    u64 swell_block_ = 1;
    u64 swell_start_[kSourceTypes] = {};
    u32 swell_lo_ = kCapacityOne, swell_hi_ = kCapacityOne;
    u32 swell_len_[kDropoutLengths] = {};
    // The walk.
    u64 tick_ = 0;
    bool walked_ = false;
    u32 next_index_ = 0;
    i64 next_birth_ = 0;
    u64 begun_ = 0, ended_ = 0;
    Array<Source> live_;
    Array<SourceState> state_;
};

} // namespace nova2
