#pragma once

// The replication current (design.md 12.6; build-plan-gate5.md 2, gap 15,
// and step 9): background copying that no program asks for, as real
// networks have. Each place now and then copies a block of its memory down
// one of its data links, and the block is written into the next place's
// reset region. It carries whatever lies in its path, and never reacts to
// the world: its schedule is a function of the seed and the tick alone, as
// the supply's is, so a snapshot saves nothing of it.
// - Its heading and strength are the world's, one of each. A season
//   (current.season ticks) begins at a knot: knot 0's heading is drawn
//   evenly over the full turn, and each later knot's is the one before it
//   turned by a draw of up to current.turn either way; each knot's strength
//   is drawn evenly on a log scale between current.strength.min and .max,
//   as a share of the mean. Between two knots both ease from one to the
//   next by the smoothstep the sources' swings use (supply.cpp).
// - The heading is in turns, in the map's own axes: 0 points along the
//   map's x, a quarter along its y.
// - With the day (current.night under 1) the strength at a place follows
//   its day, by the supply's law (Bhaskara's sine, squared): 1 at its noon,
//   current.night at its midnight. The place's day is its supply's.
// - Gusts are a patch type (events.h): a gust over a place multiplies the
//   strength there.
// - So the chance, each tick, that a place copies a block is
//     current.strength x the season's strength x the day's share x the
//     gusts' factor / the day's length in ticks,
//   drawn from the seed, the place and the tick. current.strength is in
//   blocks a place a day at its noon, at the mean strength.
// - A block that is drawn takes three more draws of the same stream: the
//   address it is copied from, evenly over every address a whole block
//   fits at; the link it goes down; and the spot it lands at, as a share
//   of the next place's region.
// - The link: each of the place's data links has a weight, the cosine of
//   the angle between the heading and the straight line to the place at
//   its other end, or nothing if that is 0 or less (across, or upstream).
//   The block goes down one link, drawn by the weights; a place with none
//   downstream sends nothing. The choice doesn't look at what state the
//   links are in.
// - What happens to it then is the world's (world.cpp) and the network's
//   (net.h): it is dropped, and counted, if the place's network device is
//   off, the memory it lay in is off or failed, the link is failed or
//   parted, the next place's network device is off, or the link's queue
//   has no room for it; otherwise it is a message of its size to the next
//   place, queued behind the tick's SENDs, and written into that place's
//   region when it is delivered there (region.h).
#include "nova2/config.h"
#include "nova2/types.h"

namespace nova2 {

struct CurrentSettings {
    double strength = 0; // blocks a place a day at its noon, at the mean strength; 0: off
    u32 block = 256;     // bytes
    u64 season = 500000; // ticks
    double turn = 0.125; // the most the heading turns from one season to the next, either way, in turns
    double strength_min = 0.5, strength_max = 2; // a season's strength, as a share of the mean
    double night = 0.2;  // the strength at a place's midnight, as a share of its noon's (1: no day)
    u64 day = 2000;      // ticks (supply.day)
};

void read_current_settings(Config& cfg, CurrentSettings* s);

// What the current did, for reports (never state): the blocks its schedule
// drew, and what became of each.
struct CurrentCounts {
    u64 drawn = 0;
    u64 becalmed = 0; // no link downstream
    u64 unlit = 0;    // the place's own network device was off
    u64 unread = 0;   // the memory it lay in was off or failed
    u64 down = 0;     // its link was failed, or its strait parted
    u64 dark = 0;     // the next place's network device was off
    u64 full = 0;     // no room in the link's queue
    u64 sent = 0, sent_bytes = 0; // queued as messages (the network counts those that arrive, and those lost)
    u64 held = 0;     // of those sent, the blocks a living program held most of
    u64 landed = 0, landed_bytes = 0; // written into the next place's region (cut short at its end)
    u64 unlanded = 0; // arrived at a place whose region's memory device was off or failed
};

class Current {
  public:
    void init(const CurrentSettings& s, u64 seed);
    bool on() const { return s_.strength > 0; }
    const CurrentSettings& settings() const { return s_; }

    // The heading and the season's strength at a tick.
    void seek(u64 tick);
    double heading() const { return heading_; } // turns; any whole number of turns over
    double dx() const { return dx_; }           // the heading as a unit vector, in the map's axes
    double dy() const { return dy_; }
    double strength() const { return factor_; } // the season's, as a share of the mean
    // Knot k's heading, in turns, and its strength (the comment above).
    double knot_heading(u64 k) const;
    double knot_strength(u64 k) const;
    // The day's share at a tick, at a place whose day runs `phase` ticks
    // ahead: current.night at its midnight to 1 at its noon.
    double day_at(u64 tick, u64 phase) const;
    // The chance a tick that a place copies a block, at the tick sought:
    // `gust` is the gusts' factor there (1: none).
    double chance(u64 phase, double gust) const;
    // Whether place `place` copies a block at the tick sought, given that
    // chance: drawn from the seed, the place and the tick.
    bool copies(u32 place, double chance) const;
    // A copied block's own draws: where it is copied from, of `span`
    // addresses (0 .. span - 1); a number in [0, 1) that picks its link by
    // the weights; and where it lands, as a share of 2^32 of the region.
    struct Draw {
        u32 addr = 0;
        double link = 0;
        u32 spot = 0;
    };
    Draw draw(u32 place, u32 span) const;
    // A link's weight: the cosine between the heading sought and the line
    // from (x0, y0) to (x1, y1), or 0 if that is 0 or less.
    double weight(double x0, double y0, double x1, double y1) const;

  private:
    double knot_turn(u64 k) const; // knot k's heading less the one before's (knot 0: its heading)

    CurrentSettings s_;
    u64 seed_ = 0;
    u64 tick_ = 0;
    // The knots either side of the tick sought, kept from seek to seek.
    bool have_ = false;
    u64 knot_ = 0;
    double h0_ = 0, h1_ = 0, f0_ = 1, f1_ = 1;
    double heading_ = 0, dx_ = 1, dy_ = 0, factor_ = 1;
};

} // namespace nova2
