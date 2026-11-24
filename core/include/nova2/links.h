#pragma once

// The links' state (design.md 7.4, 7.6, 11.6; build-plan-gate5.md step 5):
// which links have failed, and how worn the ones that carry current are.
//
// - A link here is one pair of places joined on the map: a data link, a
//   proximity link (which carries power, and heat over land), or both. One
//   that is both fails as one thing (11.6). They are numbered with the data
//   links first, in the map's order, then the proximity links that are no
//   data link, in theirs.
// - A link that carries current wears by its loss (7.4): a tick's wear is
//   its loss in that tick's solve over its loss at the reference current
//   when new, which is (I / Iref)^2 x (R / R0), times the wear rate. Its
//   resistance rises in proportion to its wear, to wear.link.rise times its
//   new value at the end of its life, so it loses more and wears faster.
//   (The rise is taken in 4,096 steps over a life, so the solve's
//   conductances change at most that often.) Its endurance is drawn from
//   half to twice wear.link.endurance, evenly on a log scale, as a device's
//   is, from the seed, its places, and how many times it has been replaced.
//   At its endurance it fails: after the tick's solve, with the devices'
//   failures (appendix C 4), in link order.
// - Cuts come from outside (11.6): each tick a link is cut with the chance
//   link.cuts x its length / the average link length (at most a half),
//   drawn as gaps from the seed, its places and the cut's number, so the
//   schedule is fixed before the run and nothing in it can move it. A cut
//   that finds its link already failed does nothing. Cuts land with the
//   tick's boundary inputs (appendix C 1), before the solve.
// - A failed link carries nothing: no power (the solve gives it no
//   conductance, and works its parts out again), no heat between its
//   places, and no data (the network is told at the barrier; net.h).
// - A visit to either of its places mends it (7.8): a new link, with no
//   wear and an endurance drawn again. Visits come first among a tick's
//   inputs, so a cut in a visit's tick waits for the next.
//
// - Straits (11.6; build-plan-gate5.md step 9, gap 13). A strait is all the
//   bridges between one pair of landmasses. Each is drawn to part or not,
//   with strait.share as the chance. One that parts is joined and parted by
//   turns, joined first, each spell's length drawn evenly on a log scale
//   between strait.spell.min and .max, from the seed and the two
//   landmasses: a function of the seed and the tick, so nothing of it is
//   saved, and nothing in the world can move it. While parted its bridges
//   carry nothing, as failed links; a visit doesn't mend them, a cut that
//   finds one does nothing, and the messages crossing one as it parts are
//   lost (the network is told which links are parted, not just failed). A
//   strait that a respawn first gives bridges to is drawn the same way, by
//   its landmasses, with its spells from tick 0.
//
// It belongs to the world, and is worked on one host thread. Its state is
// saved in a world's snapshot, and hashed, only when links can fail: with
// wear.link.endurance (or wear.rate), link.cuts and strait.share at 0 there
// is none.
#include "nova2/archive.h"
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/map.h"
#include "nova2/noise.h" // GapLog
#include "nova2/solve.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

struct LinkSettings {
    u64 endurance = 200000000; // ticks at the reference current; 0: links don't wear
    double current = 30;       // the reference current, in amps
    double rise = 2;           // a link's resistance at the end of its life, over its new one
    double cuts = 5e-8;        // the chance a tick that a link of the average length is cut; 0: none
    // Straits: the share that part (0: none), and a spell's shortest and longest, in ticks.
    double strait_share = 0;
    u64 strait_min = 2000000, strait_max = 20000000;
};

void read_link_settings(Config& cfg, LinkSettings* s);

enum class LinkFail : u8 { none = 0, worn, cut };

struct Line {
    u32 a = 0, b = 0;                    // its places, a < b
    u32 data = kNoLink, power = kNoLink; // the map's data link and proximity link it is
    // Its state.
    u64 wear = 0;       // in wear units (wear.h)
    u64 cut_at = ~0ull; // the tick of its next cut (~0: never)
    u64 cut_count = 0;  // cuts drawn so far
    u32 fresh = 0;      // which link this is: 0 the first, one more at each mend
    LinkFail failed = LinkFail::none;
    // Its strait, if it is a bridge between two landmasses, and whether
    // that is parted now: the seed's and the tick's, not saved.
    u32 strait = kNoLink;
    bool parted = false;
    bool down = false; // failed or parted: what the network and the solve are shown
    // Worked out from it and the settings.
    u64 life = 0;       // its endurance as drawn, in wear units; 0: it doesn't wear
    u64 per_uw = 0;     // the wear a microwatt of loss gives, in 2^-32 wear units
    u64 next_rise = ~0ull; // the wear at which its conductance next falls
    u64 cut_p64 = 0; // its chance of a cut a tick
    GapLog cut_log;  // and gap_log of it
};

// A strait: its two landmasses, whether it ever parts, and the spell it is
// in (even: joined; odd: parted), which runs over the ticks [from, until).
struct Strait {
    u32 ma = 0, mb = 0;
    bool parts = false;
    u64 spell = 0;
    u64 from = 0, until = ~0ull;
    u32 bridges = 0;
    bool parted() const { return (spell & 1) != 0; }
};

class Events;

class Links {
  public:
    // The lines from the map, all new. `grid`: the solve, or nullptr in a
    // world with no sources (nothing then carries current). `wear_rate`:
    // wear.rate. The map and the grid must outlive it.
    void init(const LinkSettings& s, double wear_rate, const Map* map, Grid* grid, u64 seed);
    // Whether links can fail in this world, and whether any wears.
    bool on() const { return wears_ || cuts_ || straits_; }
    bool wears() const { return wears_; }
    // Straits: whether any can part; each one; and the first of a tick's
    // boundary inputs, each strait's state at the tick, with its bridges
    // parted or joined to match. `count`: a run's tick, whose partings and
    // joinings are counted; not a load or a rebuilt map catching up.
    bool straits() const { return straits_; }
    u32 strait_count() const { return static_cast<u32>(strait_.size()); }
    const Strait& strait(u32 i) const { return strait_[i]; }
    u32 parted_straits() const;
    void straits_due(u64 tick, bool count = true);
    // A strait's spell j, in ticks.
    u64 spell_length(const Strait& s, u64 j) const;

    u32 count() const { return static_cast<u32>(lines_.size()); }
    const Line& line(u32 i) const { return lines_[i]; }
    u32 of_data(u32 k) const { return k; }
    u32 of_power(u32 pk) const { return of_power_[pk]; }
    // The failed links, by data link and by proximity link (1: failed).
    const u8* data_down() const { return down_.data(); }
    const u8* power_down() const { return pdown_.data(); }
    u32 failed_count() const { return failed_; }
    // The links that carry nothing now: failed, or parted with their strait.
    // A parted data link is 2 in data_down, a failed one 1.
    u32 down_count() const { return down_n_; }

    // A tick's boundary inputs, in this order (appendix C 1): a visit at a
    // place mends its failed links; then the cuts due at the tick. Then
    // sync, before the solve: the solve's parts, if a link failed or came
    // back since the last.
    void visit(u32 place);
    void cuts_due(u64 tick);
    // The cuts of the outage patches there now (events.h; 12.3): a link
    // inside one, by its middle, has its chance of a cut a tick times the
    // patch's factor there. The schedule's own cuts are the chance as it
    // is; the rest are drawn here, a link a tick, while a patch is over it.
    void patch_cuts(u64 tick, const Events& events);
    void sync();
    // After a tick's solve: the links at their endurance fail (appendix C
    // 4), and the rest take the tick's wear from their losses (C 8).
    void wear_step();

    // The map's links changed (a place respawned, 7.8), after the solve was
    // told (Grid::remap): `data_to` and `power_to` say where each data link
    // and each proximity link of before is now, or kNoLink. A link that is
    // still there keeps its state; its wear goes on if it carried current
    // before and does now. Every other is a new link: no wear, its
    // endurance and its cuts drawn by `era` (the respawns the world has
    // had), its first cut's trial at `tick`.
    void relink(const u32* data_to, u32 old_data, const u32* power_to, u32 old_power, u64 tick, u32 era);

    // Tests: a link failed or mended outright, and its wear set.
    void fail(u32 i, LinkFail why);
    void mend(u32 i);
    void set_wear(u32 i, u64 wear);

    // For reports (not state): links failed by wear and by a cut, cuts that
    // found their link already failed, and links mended.
    u64 worn = 0, cut = 0, missed = 0, mended = 0;
    // The same for the cuts an outage patch made, which are counted apart.
    u64 patch_cut = 0, patch_missed = 0;
    // Straits' partings and joinings over the run.
    u64 partings = 0, joinings = 0;

    void serialize(Archive& ar);
    void counters(Archive& ar);
    u64 hash();
    // The invariant check, after a tick (`tick`: the next to run): the
    // failed links are counted, the network's and the solve's views match
    // each link's state, and no cut is overdue. False, with the first
    // broken in why.
    bool check(u64 tick, TextBuf* why) const;

  private:
    void apply_state(Archive* ar);
    void show(Line& l);
    void build_straits();
    bool straits_ = false;
    Array<Strait> strait_;
    u64 next_strait_ = 0, strait_tick_ = 0; // the soonest change, and the tick last looked at
    u32 down_n_ = 0;
    double wear_rate_ = 0;
    u64 draw_life(const Line& l) const;
    u64 draw_gap(const Line& l) const;
    u64 worn_g(const Line& l) const;
    void set_rise(Line& l);

    LinkSettings s_;
    const Map* map_ = nullptr;
    Grid* grid_ = nullptr;
    u64 seed_ = 0;
    bool wears_ = false, cuts_ = false;
    u64 rise16_ = 65536; // wear.link.rise, in 2^-16ths
    Array<Line> lines_;
    Array<u32> of_power_;   // each proximity link's line
    Array<u32> wearing_;    // the lines that carry current, in order
    Array<u8> down_, pdown_;
    u32 failed_ = 0;
    u64 next_cut_ = ~0ull;  // the soonest cut_at
    bool dirty_ = false;    // a link failed or came back since the solve's parts were worked out
};

} // namespace nova2
