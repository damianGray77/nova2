#pragma once

// Ramps, regional events and strikes (design.md 12.2 to 12.4;
// build-plan-gate5.md step 7).
//
// - A ramp (12.4) is how an outside event starts and ends: its strength
//   rises in a straight line over a lead drawn for the event, holds for the
//   event's own length, and eases over as long again. A drawn share of
//   events are false alarms: they rise over the lead to a drawn fraction of
//   their strength and ease at once; the event never comes. Leads are drawn
//   evenly on a log scale between the shortest and the longest. With the
//   longest at 0 nothing ramps and nothing is a false alarm: gate 4's
//   events, which start and end at once.
//   - The sources' drop-outs and swells ramp (sources.h), and so do patches.
//     A strike and a cut don't.
// - A patch (12.3) is a region of the map that scales one quantity inside
//   it for a while. Everything about a patch is a function of the seed and
//   its number, and where it is and how strong a function of those and the
//   tick: nothing the world does changes one (principle 3), and a snapshot
//   saves nothing of them.
//   - Each type forms at its own rate, in proportion to the world's places.
//     In each stretch of ticks (the smallest power of two at least the
//     longest a patch of the type can last) a fixed number of slots each
//     hold a patch with the chance that gives the rate, starting at a tick
//     drawn evenly in the stretch.
//   - It forms at a point on land, drawn by the density field as the places
//     were (a place drawn evenly on a map with no raster), with a radius, a
//     life and a strength drawn evenly between its type's least and most
//     (the life on a log scale, between the least and most all types share).
//   - A drawn share stay put. The rest move at a speed drawn evenly up to
//     the most, on a heading that turns by a drawn amount each day.
//   - Its strength at a point is full at its centre and falls in a straight
//     line to nothing at its radius, times its ramp's level. A source is
//     inside by its landing point, a place by its position, a link by its
//     middle.
//   - Radii and speeds are in links: multiples of the map's average link
//     length as it was set up.
// - The types, and what each scales at its centre at full strength:
//   - storm: the chance a tick that a strike lands on a place (the chances
//     of storms that overlap add);
//   - swell: the sources' voltage, times its strength (1 or more);
//   - brownout: the sources' capacity, times its strength (under 1);
//   - heat wave: the outside temperature its places cool toward, plus its
//     strength in degrees (heat waves that overlap add);
//   - outage: the chance a link is cut, times its strength;
//   - gust (12.6): the replication current's strength at a place, times
//     its strength.
//   Toward a patch's edge the factor goes to 1 in a straight line, and
//   patches that overlap multiply.
// - A strike (12.2) is a source of its own type for one tick: a very high
//   voltage behind a small resistance. It joins that tick's solve at its
//   place like any source, so its spike falls off along the links, and its
//   own loss heats the place it struck.
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/map.h"
#include "nova2/str.h"
#include "nova2/supply.h"
#include "nova2/types.h"

namespace nova2 {

struct RampSettings {
    u64 lead_min = 20, lead_max = 2000; // ticks; a longest of 0: no ramps
    double false_share = 0.25;          // the share of events that are false alarms
    double false_min = 0.1, false_max = 0.5; // what a false alarm peaks at, a share of the event's strength
};

// Reads them through the registry; bad values fail through cfg.
void read_ramp_settings(Config& cfg, RampSettings* s);

// An event's ramp: its lead, and what it peaks at in 2^-20ths (all of its
// strength, or a false alarm's share).
struct Ramp {
    u64 lead = 0;
    u32 peak = kCapacityOne;
    bool false_alarm = false;
};

class RampLaw {
  public:
    void init(const RampSettings& s);
    bool on() const { return on_; }
    u64 longest() const { return on_ ? s_.lead_max : 0; }
    // An event's ramp, from a stream of its own. With ramps off: no lead,
    // the full strength.
    Ramp draw(u64 seed, u32 stream, u32 purpose, u64 key) const;
    // How long the event lasts from its first tick: its rise, its hold of
    // `hold` ticks (none for a false alarm) and its ease.
    static u64 span(const Ramp& r, u64 hold) { return (r.false_alarm ? 0 : hold) + 2 * r.lead; }
    // Its level `age` ticks after its first, in 2^-20ths of its strength.
    static u32 level(const Ramp& r, u64 age, u64 hold);

  private:
    RampSettings s_;
    bool on_ = false;
    u64 false_cut_ = 0; // a 64-bit draw under it: a false alarm
    u32 peak_lo_ = kCapacityOne, peak_hi_ = kCapacityOne;
    u32 leads_[kDropoutLengths] = {};
};

inline constexpr u32 kPatchTypes = 6;
enum class PatchType : u8 { storm, swell, brownout, heatwave, outage, gust };
extern const char* const kPatchTypeNames[kPatchTypes];

// A row of the catalogue (12.3).
struct PatchKind {
    double rate = 0.5;                         // forming per million ticks per 100 places; 0: none
    double radius_min = 1, radius_max = 3;     // links
    double strength_min = 1, strength_max = 1; // what it scales by at its centre (the comment above)
};

struct EventSettings {
    RampSettings ramp;
    double speed = 2;       // links a day, at most
    double still = 1.0 / 3; // the share that stay put
    double turn = 0.125;    // the most a heading turns a day, either way, in turns
    u64 day = 2000;         // ticks (supply.day)
    u64 life_min = 2000, life_max = 50000; // ticks at its peak, between its rise and its ease
    i32 strike_mv = 20000;  // a strike's voltage
    double strike_mohm = 3; // and its internal resistance, in milliohms
    PatchKind kind[kPatchTypes];
};

void read_event_settings(Config& cfg, EventSettings* s);

// A patch: what the seed fixes of it.
struct Patch {
    u8 type = 0;
    u64 block = 0; // the stretch it starts in, and its slot there
    u32 slot = 0;
    u64 start = 0, life = 0;
    Ramp ramp;
    double x = 0, y = 0;          // where it forms, in the map's height
    double radius = 0;            // in the map's height
    double speed = 0;             // the map's height a day
    double heading = 0, turn = 0; // radians, and radians a day
    double strength = 1;
    u64 end() const { return start + RampLaw::span(ramp, life); } // the first tick it's gone
};

// A patch that is there at the tick asked for: where its centre is, and its
// ramp's level, 0 to 1.
struct PatchNow {
    u8 type = 0;
    u32 index = 0; // among its type's candidates (Events::patch)
    double x = 0, y = 0, radius = 0;
    double level = 0;
    double strength = 1;
};

class Events {
  public:
    // Works out the laws. False, with the reason, if the settings can't make
    // them. The map must outlive it.
    bool init(const EventSettings& s, const Map* map, u64 seed, TextBuf* why);
    // Whether any patch can form here: a type with a rate, on a map with
    // positions.
    bool on() const { return on_; }
    const EventSettings& settings() const { return s_; }

    // The patches there at a tick.
    void seek(u64 tick);
    u64 tick() const { return tick_; }
    u32 count() const { return static_cast<u32>(now_.size()); }
    const PatchNow& now(u32 k) const { return now_[k]; }
    const Patch& patch(u32 k) const { return cand_[now_[k].type][now_[k].index]; }
    bool any(PatchType t) const { return n_[static_cast<u32>(t)] != 0; }
    // What the patches there now do at a point (the comment above): the
    // factor on sources' voltage, on their capacity, on a link's cut
    // chance, and on the current's strength; the degrees added to the
    // outside temperature; and the chance a tick of a strike.
    double swell_at(double x, double y) const { return factor(PatchType::swell, x, y); }
    double brownout_at(double x, double y) const { return factor(PatchType::brownout, x, y); }
    double outage_at(double x, double y) const { return factor(PatchType::outage, x, y); }
    double gust_at(double x, double y) const { return factor(PatchType::gust, x, y); }
    double heat_at(double x, double y) const { return sum(PatchType::heatwave, x, y); }
    double storm_at(double x, double y) const;

    // The patch in slot `slot` of stretch `block` of a type's, if there is
    // one; and where its centre is at a tick.
    bool make(PatchType t, u64 block, u32 slot, Patch* p) const;
    void where(const Patch& p, u64 tick, double* x, double* y) const;
    u64 block(PatchType t) const { return block_[static_cast<u32>(t)]; }
    u32 slots(PatchType t) const { return slots_[static_cast<u32>(t)]; }

    // A strike: its voltage in microvolts and its conductance in 2^-20 S;
    // and whether one lands on a place at a tick, given the chance there:
    // drawn from the seed, the place and the tick.
    i64 strike_uv() const { return strike_uv_; }
    u64 strike_g() const { return strike_g_; }
    bool strike(u32 place, u64 tick, double chance) const;
    // An outage patch's cut of a link at a tick, given the chance: drawn
    // from the seed, the link's places and the tick.
    bool patch_cut(u32 a, u32 b, u64 tick, double chance) const;

  private:
    double strength_at(const PatchNow& p, double x, double y) const;
    double factor(PatchType t, double x, double y) const;
    double sum(PatchType t, double x, double y) const;

    EventSettings s_;
    const Map* map_ = nullptr;
    u64 seed_ = 0;
    bool on_ = false;
    RampLaw ramp_;
    double link_ = 1; // the map's average link length as set up
    u64 block_[kPatchTypes] = {};
    u32 slots_[kPatchTypes] = {};
    u64 chance_[kPatchTypes] = {}; // a 64-bit draw under it: the slot holds a patch
    Array<u32> land_;              // a landscape's land cells, and their densities summed
    Array<double> cum_;
    i64 strike_uv_ = 0;
    u64 strike_g_ = 0;
    // The walk: each type's candidates (the patches of the stretch the tick
    // is in and the one before), and those there now.
    bool sought_ = false;
    u64 tick_ = 0;
    u64 cand_block_[kPatchTypes] = {};
    Array<Patch> cand_[kPatchTypes];
    Array<PatchNow> now_;
    u32 n_[kPatchTypes] = {};
};

} // namespace nova2
