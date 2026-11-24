// The links' state (links.h; design.md 7.4, 11.6; build-plan-gate5.md step 5).
#include "nova2/links.h"

#include <cmath>    // setup only: a link's endurance, its chance of a cut
#include <intrin.h> // _umul128

#include "nova2/events.h"
#include "nova2/hash.h"
#include "nova2/keys.h"
#include "nova2/noise.h" // to_p64, keep_gap_log, geometric_gap
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/supply.h" // mul_div_u64
#include "nova2/wear.h"

namespace nova2 {

void read_link_settings(Config& cfg, LinkSettings* s) {
    s->endurance = key_u64(cfg, "wear.link.endurance");
    s->current = key_f64(cfg, "wear.link.current");
    s->rise = key_f64(cfg, "wear.link.rise");
    s->cuts = key_f64(cfg, "link.cuts");
    s->strait_share = key_f64(cfg, "strait.share");
    s->strait_min = key_u64(cfg, "strait.spell.min");
    s->strait_max = key_u64(cfg, "strait.spell.max");
    if (s->strait_max < s->strait_min)
        cfg.fail("strait.spell.max", "must be at least strait.spell.min");
}

namespace {

constexpr u64 kRiseSteps = 4096; // a life's rise in resistance, in steps

bool links_broken(TextBuf* why, const char* what, u64 n) {
    if (why) {
        why->append(what);
        why->append_char(' ');
        why->append_u64(n);
    }
    return false;
}

} // namespace

void Links::init(const LinkSettings& s, double wear_rate, const Map* map, Grid* grid, u64 seed) {
    s_ = s;
    map_ = map;
    grid_ = grid;
    seed_ = seed;
    wear_rate_ = wear_rate;
    cuts_ = s.cuts > 0 && map->placed();
    wears_ = grid != nullptr && wear_rate > 0 && s.endurance > 0 && map->placed();
    const double rise = s.rise < 1 ? 1 : (s.rise > 1000 ? 1000 : s.rise);
    rise16_ = static_cast<u64>(rise * 65536.0 + 0.5);
    lines_.clear();
    of_power_.resize(0);
    of_power_.resize(map->power_link_count());
    wearing_.clear();
    worn = cut = missed = mended = patch_cut = patch_missed = 0;
    partings = joinings = 0;
    failed_ = 0;
    down_n_ = 0;
    next_cut_ = ~0ull;
    dirty_ = false;
    const u32 data = map->placed() ? map->link_count() : 0;
    const u32 power = map->placed() ? map->power_link_count() : 0;
    for (u32 k = 0; k < data; ++k) {
        Line l;
        l.a = map->link(k).a;
        l.b = map->link(k).b;
        l.data = k;
        lines_.push(l);
    }
    for (u32 pk = 0; pk < power; ++pk) {
        const PowerLink& p = map->power_link(pk);
        if (p.data != kNoLink) {
            lines_[p.data].power = pk;
            of_power_[pk] = p.data;
            continue;
        }
        Line l;
        l.a = p.a;
        l.b = p.b;
        l.power = pk;
        of_power_[pk] = static_cast<u32>(lines_.size());
        lines_.push(l);
    }
    down_.resize(0);
    down_.resize(data);
    pdown_.resize(0);
    pdown_.resize(power);
    const double amps2 = s.current * s.current;
    for (u32 i = 0; i < lines_.size(); ++i) {
        Line& l = lines_[i];
        const double length = l.data != kNoLink ? map->link(l.data).length : map->power_link(l.power).length;
        if (cuts_) {
            double p = s.cuts * length / map->mean_length();
            p = p > 0.5 ? 0.5 : (p > 0 ? p : 0);
            l.cut_p64 = to_p64(p);
            l.cut_log = l.cut_p64 ? keep_gap_log(l.cut_p64) : GapLog();
            // The first trial is tick 0's.
            const u64 gap = draw_gap(l);
            l.cut_at = gap == ~0ull ? ~0ull : gap - 1;
            next_cut_ = l.cut_at < next_cut_ ? l.cut_at : next_cut_;
        }
        if (wears_ && l.power != kNoLink && grid->link_g0(l.power)) {
            // Its loss at the reference current when new is I^2 x R0, with
            // R0 = 2^20 / g0 ohms; a microwatt of loss is that share of a
            // tick's wear at the reference, times the rate.
            const double per = wear_rate * 4294967296.0 * static_cast<double>(grid->link_g0(l.power)) /
                               (amps2 * 1000000.0);
            l.per_uw = per >= 9e18 ? static_cast<u64>(9e18) : (per < 1 ? 1 : static_cast<u64>(per + 0.5));
            l.life = draw_life(l);
            set_rise(l);
            wearing_.push(i);
        }
    }
    wears_ = wears_ && !wearing_.empty();
    build_straits();
}

// Evenly on a log scale from half to twice the endurance, as a device's
// (wear.cpp), by the link's places and which link there this is.
u64 Links::draw_life(const Line& l) const {
    const Rng r(seed_, l.a, kPurposeLinkLife, l.b);
    const double u = static_cast<double>(r.at(l.fresh) >> 11) * 0x1.0p-53;
    double ticks = static_cast<double>(s_.endurance) * pow(2.0, 2.0 * u - 1.0);
    if (ticks < 1)
        ticks = 1;
    const double most = static_cast<double>(kWearMost >> kWearBits);
    const u64 whole = ticks >= most ? kWearMost >> kWearBits : static_cast<u64>(ticks + 0.5);
    return whole << kWearBits;
}

// The ticks from one cut to the next (1, 2, ...), by the link's places and
// the cut's number. ~0: never.
u64 Links::draw_gap(const Line& l) const {
    if (!l.cut_p64)
        return ~0ull;
    const Rng r(seed_, l.a, kPurposeCut, l.b);
    return geometric_gap(l.cut_p64, l.cut_log, r.at(l.cut_count));
}

// Its conductance as worn: its resistance up in proportion to its wear, in
// steps, to the rise at the end of its life.
u64 Links::worn_g(const Line& l) const {
    const u64 g0 = grid_->link_g0(l.power);
    if (!l.life || rise16_ <= 65536)
        return g0;
    u64 q = l.wear / (l.life / kRiseSteps);
    q = q > kRiseSteps ? kRiseSteps : q;
    const u64 g = mul_div_u64(g0, kRiseSteps << 16, (kRiseSteps << 16) + (rise16_ - 65536) * q);
    return g ? g : 1;
}

void Links::set_rise(Line& l) {
    l.next_rise = ~0ull;
    if (!l.life || rise16_ <= 65536)
        return;
    const u64 step = l.life / kRiseSteps;
    const u64 q = l.wear / step;
    if (q < kRiseSteps)
        l.next_rise = (q + 1) * step;
}

void Links::fail(u32 i, LinkFail why) {
    Line& l = lines_[i];
    if (l.failed != LinkFail::none || why == LinkFail::none)
        return;
    l.failed = why;
    ++failed_;
    ++(why == LinkFail::worn ? worn : cut);
    show(l);
}

// A line as the network and the solve see it, after its state changed: down
// if it has failed or its strait is parted.
void Links::show(Line& l) {
    const bool gone = l.failed != LinkFail::none || l.parted;
    if (l.data != kNoLink)
        down_[l.data] = l.parted ? 2 : gone ? 1 : 0;
    if (gone == l.down)
        return;
    l.down = gone;
    down_n_ += gone ? 1 : 0;
    down_n_ -= gone ? 0 : 1;
    if (l.power != kNoLink) {
        pdown_[l.power] = gone ? 1 : 0;
        if (grid_) {
            grid_->set_link(l.power, gone ? 0 : worn_g(l));
            dirty_ = true;
        }
    }
}

// The straits, from the map's bridges as they stand: each pair of
// landmasses with a bridge between them, in the order of their first.
void Links::build_straits() {
    strait_.clear();
    next_strait_ = 0;
    strait_tick_ = 0;
    straits_ = s_.strait_share > 0 && map_->placed();
    if (!straits_)
        return;
    const u32 data = map_->link_count();
    for (u32 k = 0; k < data; ++k) {
        const MapLink& m = map_->link(k);
        const u32 x = map_->landmass(m.a), y = map_->landmass(m.b);
        if (!m.bridge || x == y)
            continue;
        const u32 ma = x < y ? x : y, mb = x < y ? y : x;
        u32 at = 0;
        while (at < strait_.size() && (strait_[at].ma != ma || strait_[at].mb != mb))
            ++at;
        if (at == strait_.size()) {
            Strait s;
            s.ma = ma;
            s.mb = mb;
            const Rng r(seed_, ma, kPurposeStrait, mb);
            s.parts = s_.strait_share >= 1 || static_cast<double>(r.at(0) >> 11) * 0x1.0p-53 < s_.strait_share;
            s.until = s.parts ? spell_length(s, 0) : ~0ull;
            strait_.push(s);
        }
        ++strait_[at].bridges;
        lines_[k].strait = at;
    }
}

u64 Links::spell_length(const Strait& s, u64 j) const {
    const Rng r(seed_, s.ma, kPurposeStrait, s.mb);
    const double u = static_cast<double>(r.at(1 + j) >> 11) * 0x1.0p-53;
    const double lo = static_cast<double>(s_.strait_min), hi = static_cast<double>(s_.strait_max);
    const double len = lo * pow(hi / lo, u);
    return len < 1 ? 1 : static_cast<u64>(len + 0.5);
}

u32 Links::parted_straits() const {
    u32 n = 0;
    for (usize i = 0; i < strait_.size(); ++i)
        n += strait_[i].parted() ? 1 : 0;
    return n;
}

void Links::straits_due(u64 tick, bool count) {
    if (!straits_ || (tick >= strait_tick_ && tick < next_strait_))
        return;
    strait_tick_ = tick;
    next_strait_ = ~0ull;
    for (u32 i = 0; i < strait_.size(); ++i) {
        Strait& s = strait_[i];
        if (!s.parts)
            continue;
        const bool was = s.parted();
        if (tick < s.from) { // a world gone back: from its first spell again
            s.spell = 0;
            s.from = 0;
            s.until = spell_length(s, 0);
        }
        while (tick >= s.until) {
            ++s.spell;
            s.from = s.until;
            const u64 len = spell_length(s, s.spell);
            s.until = len >= ~0ull - s.from ? ~0ull : s.from + len;
            if (count)
                ++(s.parted() ? partings : joinings);
        }
        next_strait_ = s.until < next_strait_ ? s.until : next_strait_;
        const bool now = s.parted();
        for (u32 k = 0; k < lines_.size(); ++k) {
            Line& l = lines_[k];
            if (l.strait != i || (l.parted == now && now == was))
                continue;
            l.parted = now;
            show(l);
        }
    }
}

void Links::mend(u32 i) {
    Line& l = lines_[i];
    if (l.failed == LinkFail::none)
        return;
    l.failed = LinkFail::none;
    --failed_;
    ++mended;
    ++l.fresh;
    l.wear = 0;
    if (l.life) {
        l.life = draw_life(l);
        set_rise(l);
    }
    show(l);
}

void Links::set_wear(u32 i, u64 wear) {
    Line& l = lines_[i];
    l.wear = wear;
    if (!l.life || l.failed != LinkFail::none)
        return;
    set_rise(l);
    if (!l.down)
        grid_->set_link(l.power, worn_g(l));
}

void Links::visit(u32 place) {
    if (!failed_)
        return;
    for (u32 j = 0; j < map_->degree(place); ++j)
        mend(map_->link_at(place, j));
    for (u32 j = 0; j < map_->power_degree(place); ++j)
        mend(of_power_[map_->power_link_at(place, j)]);
}

void Links::cuts_due(u64 tick) {
    if (!cuts_ || tick < next_cut_)
        return;
    next_cut_ = ~0ull;
    for (u32 i = 0; i < lines_.size(); ++i) {
        Line& l = lines_[i];
        while (l.cut_at <= tick) {
            if (l.failed == LinkFail::none && !l.parted)
                fail(i, LinkFail::cut);
            else
                ++missed; // already failed, or parted with its strait: nothing there to cut
            ++l.cut_count;
            const u64 gap = draw_gap(l);
            l.cut_at = gap >= ~0ull - l.cut_at ? ~0ull : l.cut_at + gap;
        }
        next_cut_ = l.cut_at < next_cut_ ? l.cut_at : next_cut_;
    }
}

void Links::patch_cuts(u64 tick, const Events& events) {
    if (!cuts_ || !events.any(PatchType::outage) || !map_->placed())
        return;
    for (u32 i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        if (!l.cut_p64)
            continue;
        const double f = events.outage_at((map_->x(l.a) + map_->x(l.b)) / 2, (map_->y(l.a) + map_->y(l.b)) / 2);
        if (!(f > 1))
            continue;
        const double p = static_cast<double>(l.cut_p64) * 0x1.0p-64 * (f - 1);
        if (!events.patch_cut(l.a, l.b, tick, p))
            continue;
        if (l.failed == LinkFail::none && !l.parted) {
            fail(i, LinkFail::cut);
            ++patch_cut;
        } else {
            ++patch_missed;
        }
    }
}

void Links::sync() {
    if (dirty_ && grid_)
        grid_->repart();
    dirty_ = false;
}

void Links::wear_step() {
    if (!wears_)
        return;
    for (usize w = 0; w < wearing_.size(); ++w) {
        const u32 i = wearing_[w];
        Line& l = lines_[i];
        if (l.failed != LinkFail::none || l.parted)
            continue;
        if (l.wear >= l.life) {
            fail(i, LinkFail::worn);
            continue;
        }
        const u64 loss = grid_->last_loss(l.power);
        if (!loss)
            continue;
        u64 hi;
        const u64 lo = _umul128(loss, l.per_uw, &hi);
        const u64 step = hi >> 31 ? kWearMost : (hi << 32 | lo >> 32);
        l.wear = step >= kWearMost - l.wear ? kWearMost : l.wear + step;
        if (l.wear >= l.next_rise) {
            set_rise(l);
            grid_->set_link(l.power, worn_g(l));
        }
    }
}

void Links::serialize(Archive& ar) {
    ar.tag(0x4C4E4B31); // "LNK1"
    u64 n = lines_.size();
    ar.u64_(n);
    if (n != lines_.size()) {
        ar.fail();
        return;
    }
    for (usize i = 0; i < lines_.size(); ++i) {
        Line& l = lines_[i];
        ar.enum_(l.failed);
        ar.u32_(l.fresh);
        ar.u64_(l.wear);
        ar.u64_(l.cut_count);
        ar.u64_(l.cut_at);
    }
    ar.mark("links (gate 5)");
    if (!ar.loading() || !ar.ok())
        return;
    apply_state(&ar);
}

// What follows from the lines' state: each link's endurance, the failed
// links as the network and the solve see them, and the solve's
// conductances. `ar`: a load's, failed if the state can't be.
void Links::apply_state(Archive* ar) {
    failed_ = 0;
    down_n_ = 0;
    next_cut_ = ~0ull;
    for (usize i = 0; i < lines_.size(); ++i) {
        Line& l = lines_[i];
        if (static_cast<u8>(l.failed) > static_cast<u8>(LinkFail::cut) || (!cuts_ && l.cut_at != ~0ull)) {
            if (ar)
                ar->fail();
            return;
        }
        failed_ += l.failed != LinkFail::none ? 1 : 0;
        const bool gone = l.failed != LinkFail::none || l.parted;
        l.down = gone;
        down_n_ += gone ? 1 : 0;
        next_cut_ = l.cut_at < next_cut_ ? l.cut_at : next_cut_;
        if (l.life) {
            l.life = draw_life(l);
            set_rise(l);
        }
        if (l.data != kNoLink)
            down_[l.data] = l.parted ? 2 : gone ? 1 : 0;
        if (l.power != kNoLink) {
            pdown_[l.power] = gone ? 1 : 0;
            if (grid_)
                grid_->set_link(l.power, gone ? 0 : worn_g(l));
        }
    }
    dirty_ = true;
    sync();
}

void Links::relink(const u32* data_to, u32 old_data, const u32* power_to, u32 old_power, u64 tick, u32 era) {
    Array<Line> old;
    old.assign(lines_.data(), lines_.size());
    Array<u32> old_of_power;
    old_of_power.assign(of_power_.data(), of_power_.size());
    const u64 w = worn, c = cut, mi = missed, me = mended, pc = patch_cut, pm = patch_missed;
    const u64 sp = partings, sj = joinings;
    init(s_, wear_rate_, map_, grid_, seed_); // every line new, on the map as it stands
    partings = sp;
    joinings = sj;
    worn = w;
    cut = c;
    missed = mi;
    mended = me;
    patch_cut = pc;
    patch_missed = pm;
    // The line each was: a data link's by its data link, a power-only one's
    // by its proximity link.
    Array<u32> from;
    from.resize(lines_.size());
    from.fill(kNoLink);
    for (u32 k = 0; k < old_data && k < old.size(); ++k)
        if (data_to[k] != kNoLink)
            from[data_to[k]] = k;
    for (u32 pk = 0; pk < old_power && pk < old_of_power.size() && !old.empty(); ++pk) {
        if (power_to[pk] == kNoLink || old[old_of_power[pk]].data != kNoLink)
            continue;
        const u32 line = of_power_[power_to[pk]];
        if (lines_[line].data == kNoLink)
            from[line] = old_of_power[pk];
    }
    const u32 first = era > 0xFFFF ? 0xFFFF0000u : era << 16;
    for (u32 i = 0; i < lines_.size(); ++i) {
        Line& l = lines_[i];
        if (from[i] != kNoLink) {
            const Line& o = old[from[i]];
            l.failed = o.failed;
            l.fresh = o.fresh;
            l.cut_count = o.cut_count;
            l.cut_at = o.cut_at;
            l.wear = l.life && o.life ? o.wear : 0;
            continue;
        }
        l.fresh = first;
        l.cut_count = static_cast<u64>(era) << 32;
        if (cuts_) {
            const u64 gap = draw_gap(l);
            l.cut_at = gap >= ~0ull - tick ? ~0ull : tick + gap - 1;
        }
    }
    apply_state(nullptr);
    // The straits as the map has them now, each where its spells put it at
    // this tick (a new one's run from tick 0 as any other's).
    straits_due(tick, false);
    sync();
}

void Links::counters(Archive& ar) {
    // "LKC2" adds the cuts outage patches made (build-plan-gate5.md step
    // 7), last. A save of a build before them is still read.
    // "LKC3" adds the straits' partings and joinings (step 9).
    const u32 kLkc1 = 0x4C4B4331, kLkc2 = 0x4C4B4332, kLkc3 = 0x4C4B4333;
    u32 version = kLkc3;
    ar.u32_(version);
    if (version != kLkc1 && version != kLkc2 && version != kLkc3) {
        ar.fail();
        return;
    }
    if (version == kLkc3) {
        ar.u64_(partings);
        ar.u64_(joinings);
    } else if (ar.loading()) {
        partings = joinings = 0;
    }
    ar.u64_(worn);
    ar.u64_(cut);
    ar.u64_(missed);
    ar.u64_(mended);
    if (version >= kLkc2) {
        ar.u64_(patch_cut);
        ar.u64_(patch_missed);
    } else if (ar.loading()) {
        patch_cut = patch_missed = 0;
    }
}

u64 Links::hash() {
    ByteWriter w;
    Archive ar(&w);
    serialize(ar);
    return hash_bytes(w.data(), w.size(), 0x6C696E6B73ull); // "links"
}

bool Links::check(u64 tick, TextBuf* why) const {
    u32 failed = 0, down = 0;
    for (u32 i = 0; i < strait_.size(); ++i) {
        const Strait& s = strait_[i];
        if (s.parts ? (tick && (s.from > tick - 1 || s.until <= tick - 1)) : s.spell != 0)
            return links_broken(why, "links check: the spell of strait", i);
    }
    for (u32 i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        const bool gone = l.failed != LinkFail::none || l.parted;
        failed += l.failed != LinkFail::none ? 1 : 0;
        down += gone ? 1 : 0;
        if (l.parted != (l.strait != kNoLink && strait_[l.strait].parted()) || l.down != gone)
            return links_broken(why, "links check: its strait's state at link", i);
        if (l.data != kNoLink && ((down_[l.data] != 0) != gone || (down_[l.data] == 2) != l.parted))
            return links_broken(why, "links check: the network's view of link", i);
        if (l.power != kNoLink && (pdown_[l.power] != 0) != gone)
            return links_broken(why, "links check: the power map's view of link", i);
        const bool solved = l.power != kNoLink && grid_ && grid_->link_g0(l.power);
        if (solved && grid_->link_g(l.power) != (gone ? 0 : worn_g(l)))
            return links_broken(why, "links check: the solve's conductance of link", i);
        if (cuts_ ? l.cut_at < tick : l.cut_at != ~0ull)
            return links_broken(why, "links check: an overdue cut on link", i);
        if (l.cut_at < next_cut_)
            return links_broken(why, "links check: the soonest cut is not link", i);
    }
    if (failed != failed_)
        return links_broken(why, "links check: failed links counted", failed);
    if (down != down_n_)
        return links_broken(why, "links check: links down counted", down);
    return true;
}

} // namespace nova2
