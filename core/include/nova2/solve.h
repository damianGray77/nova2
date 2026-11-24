#pragma once

// The voltage solve (design.md 4.1, 6.1, appendix C steps 2 and 3;
// build-plan-gate4.md step 4): one solve a tick for every place's voltage.
//
// - The network is the map's proximity links (map.h), each one conductance:
//   grid.link_resistance, a usual link's ohms, times the link's resistance
//   in usual links (its length, its field, its water, a border). A link too
//   weak to matter (under kLeastLink) is left out at setup, so the integers
//   keep their precision; what's left falls into parts, and a part with no
//   source this tick sits at 0 V.
// - Each place hangs three things on its node: its devices, as one
//   conductance worked from its last tick, G = P / V^2 (the one-tick lag,
//   4.1); its shore drain to ground, grid.drain at full voltage times its
//   share of water (6.1); and each source there, as a Norton equivalent: its
//   voltage behind its conductance at this tick's capacity (sources.h).
// - Nodal analysis by Gauss-Seidel: sweeps in place order, each place's
//   voltage set to what its neighbours' and its sources' give,
//   V = (sum Vs x Gs + sum Gij x Vj) / (Gload + Gdrain + sum Gs + sum Gij),
//   started from the last tick's voltages, until no place moves by more
//   than the tolerance or the sweep cap is reached. A capped solve leaves
//   what its last sweep gave, and is counted. One host thread, so the
//   order is the same whatever the pool.
// - All in integers: voltages in microvolts, conductances in 2^-20 siemens,
//   64-bit with 128-bit products. A place reads its voltage rounded to the
//   millivolt.
// - What each part lost, for heat (5): a link's loss is split between the
//   places at its two ends, but for the share its water takes, which goes
//   into the sea (the share of its resistance that's water's doing); a
//   shore drain's goes into the sea; a source's internal loss heats its
//   place.
//
// From gate 5 (build-plan-gate5.md step 5) a link's conductance can change:
// it falls as the link wears, and a failed link has none (set_link), until
// it is mended. The parts are worked out again when a link fails or comes
// back (repart). Which links are worn or failed is the link table's state
// (links.h), not the solve's.
//
// The voltages are the solve's state: a snapshot saves them, so a resumed
// world starts its next solve where the saved one would have.
#include "nova2/archive.h"
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/map.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

inline constexpr u64 kSiemens = 1ull << 20; // a conductance of 1 S
inline constexpr u64 kLeastLink = 105;      // about 10^-4 S: 100 microwatts with a volt across it

struct SolveSettings {
    double link_mohm = 2.5;      // a usual link's resistance, in milliohms
    double drain_w = 0.3;      // a shore drain's draw at 1,000 mV, for a place wholly on the shore
    double tolerance_mv = 0.1; // a sweep that moves no place by more ends the solve
    u32 sweeps = 64;           // at most, a tick
};

void read_solve_settings(Config& cfg, SolveSettings* s);

// A load's conductance, in 2^-20 S, from what it drew and the voltage it
// drew it at: G = P / V^2 (appendix C 2). 0 with no draw or no voltage.
u64 load_conductance(u64 draw_uw, i32 mv);
// The power a conductance takes with duv microvolts across it, in
// microwatts: V^2 x G.
u64 power_uw(i64 duv, u64 g);

class Grid {
  public:
    // Works out the links' conductances, the drains and the parts. The map
    // must outlive it. False, with the reason, if it can't.
    bool init(const SolveSettings& s, const Map* map, TextBuf* why);

    // The map's proximity links changed (a place respawned, 7.8): the
    // links' conductances, the drains and the parts from the map as it
    // stands, every link new. Each place's voltage, and the counts, go on.
    void remap();

    // A tick's inputs: begin clears them; then each place's load, and each
    // source at its place.
    void begin();
    void load(u32 place, u64 g) { load_[place] = g; }
    void source(u32 place, i64 uv, u64 g);
    // The sources given since begin, for tests and reports: each one's
    // place, voltage in microvolts and conductance.
    u32 source_count() const { return static_cast<u32>(src_.size()); }
    u32 source_place(u32 k) const { return src_[k].place; }
    i64 source_uv(u32 k) const { return src_[k].uv; }
    u64 source_g(u32 k) const { return src_[k].g; }
    // Solves, with at most `cap` sweeps (0: the setting's). Returns the
    // sweeps it took.
    u32 solve(u32 cap = 0);

    u32 place_count() const { return n_; }
    i64 uv(u32 place) const { return uv_[place]; }
    i32 mv(u32 place) const { return static_cast<i32>((uv_[place] + 500) / 1000); }
    // Tests and setup: sets a voltage outright.
    void set_uv(u32 place, i64 uv) { uv_[place] = uv; }

    // A link's conductance now (0: failed), for a link the setup kept; and
    // the parts worked out again, after any link went to 0 or came back.
    void set_link(u32 k, u64 g);
    void repart();
    u64 link_g0(u32 k) const { return link_g0_[k]; } // as set up, new: 0 if left out
    // Each link's loss in the last solve, in microwatts.
    u64 last_loss(u32 k) const { return loss_[k]; }

    // The network as the solve has it.
    u64 link_g(u32 k) const { return link_g_[k]; }       // 0: left out, or failed
    u64 drain_g(u32 place) const { return drain_[place]; }
    u32 part(u32 place) const { return part_[place]; }
    u32 parts() const { return parts_; }
    bool lit(u32 place) const { return lit_[part_[place]] != 0; } // its part has a source this tick
    u32 dropped() const { return dropped_; }              // links left out as too weak

    // After a solve, in microwatts. A link's loss, and the microwatts it
    // carries from its place a to its place b (negative: the other way).
    u64 link_loss(u32 k) const;
    i64 link_flow(u32 k) const;
    // What heats a place besides its devices: its halves of its links'
    // losses on land, and its sources' internal losses.
    u64 place_heat(u32 place) const { return heat_[place]; }
    // The totals: what the sources gave the network, what the loads took,
    // what the links lost (the sea's share of that), what the drains took,
    // and what the sources lost inside.
    // (What the sources gave is signed: a source below its place's voltage
    // takes power.)
    i64 given_uw = 0;
    u64 loads_uw = 0, links_uw = 0, links_sea_uw = 0, drains_uw = 0, inside_uw = 0;

    // Counters for reports (not state): solves, their sweeps, those that
    // hit the cap, and the most any place moved in the last sweep of the
    // last solve, in microvolts.
    u64 solves = 0, sweeps = 0, capped = 0;
    i64 last_move = 0;

    void serialize(Archive& ar);
    u64 hash() const;
    const SolveSettings& settings() const { return s_; }

  private:
    void account();

    struct Src {
        u32 place;
        i64 uv;
        u64 g;
    };
    SolveSettings s_;
    const Map* map_ = nullptr;
    u32 n_ = 0;
    i64 tolerance_ = 100;
    Array<u64> link_g_, link_g0_, loss_;
    Array<u32> link_at_; // where a kept link sits in its two places' lists (2 a link), or ~0
    Array<u32> link_land_; // the share of a link's loss that heats its ends, in 2^-20ths
    // Each place's kept links, by the place at the other end: where they
    // start, then (other place, conductance, link).
    Array<u32> begin_, other_, link_;
    Array<u64> g_;
    Array<u64> drain_, load_;
    Array<u32> part_;
    u32 parts_ = 0, dropped_ = 0;
    Array<u8> lit_;
    // Each place's sources this tick, summed: sum Gs, and sum Vs x Gs (128 bits).
    Array<u64> src_g_, src_lo_, src_hi_;
    Array<Src> src_;
    Array<i64> uv_;
    Array<u64> heat_;
};

} // namespace nova2
