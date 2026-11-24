#include "nova2/solve.h"

#include <cmath>    // setup only: a link's conductance from its resistance
#include <intrin.h> // _umul128, _udiv128, _addcarry_u64

#include "nova2/hash.h"
#include "nova2/keys.h"
#include "nova2/supply.h" // mul_div_u64

namespace nova2 {

void read_solve_settings(Config& cfg, SolveSettings* s) {
    s->link_mohm = key_f64(cfg, "grid.link_resistance");
    s->drain_w = key_f64(cfg, "grid.drain");
    s->tolerance_mv = key_f64(cfg, "grid.tolerance");
    s->sweeps = key_u32(cfg, "grid.sweeps");
}

u64 load_conductance(u64 draw_uw, i32 mv) {
    if (mv <= 0 || draw_uw == 0)
        return 0;
    return mul_div_u64(draw_uw, kSiemens, static_cast<u64>(mv) * static_cast<u64>(mv));
}

u64 power_uw(i64 duv, u64 g) {
    const u64 d = static_cast<u64>(duv < 0 ? -duv : duv);
    // (uV x 10^-6)^2 x (g x 2^-20) W, in microwatts.
    return mul_div_u64(d * d, g, 1000000ull << 20);
}

bool Grid::init(const SolveSettings& s, const Map* map, TextBuf* why) {
    s_ = s;
    map_ = map;
    n_ = map->place_count();
    if (!(s.link_mohm > 0) || !(s.tolerance_mv >= 0) || s.sweeps == 0) {
        if (why)
            why->append("grid: the link resistance must be above nothing, and the solve needs a sweep");
        return false;
    }
    tolerance_ = static_cast<i64>(s.tolerance_mv * 1000.0 + 0.5);
    const u32 links = map->power_link_count();
    link_g_.resize(0);
    link_g_.resize(links);
    link_land_.resize(0);
    link_land_.resize(links);
    dropped_ = 0;
    const double ohm = s.link_mohm / 1000.0;
    for (u32 k = 0; k < links; ++k) {
        const PowerLink& l = map->power_link(k);
        const double g = static_cast<double>(kSiemens) / (ohm * l.resistance);
        if (!(l.resistance > 0) || !(g >= static_cast<double>(kLeastLink)) || g > 9e18) {
            ++dropped_;
            continue;
        }
        link_g_[k] = static_cast<u64>(g);
        // The share of its resistance that isn't water's doing: its length on
        // the field, and a border.
        const double dry = l.length / map->mean_length() * l.per_length + (l.border ? map->border_strength() : 0);
        double share = l.water > 0 ? dry / l.resistance : 1;
        share = share < 0 ? 0 : (share > 1 ? 1 : share);
        link_land_[k] = static_cast<u32>(share * 1048576.0 + 0.5);
    }
    // Each place's kept links, in order of the place at their other end.
    begin_.resize(0);
    begin_.resize(n_ + 1);
    other_.resize(0);
    link_.resize(0);
    g_.resize(0);
    link_g0_.assign(link_g_.data(), link_g_.size());
    loss_.resize(0);
    loss_.resize(links);
    link_at_.resize(0);
    link_at_.resize(static_cast<usize>(links) * 2);
    link_at_.fill(~0u);
    for (u32 i = 0; i < n_; ++i) {
        begin_[i] = static_cast<u32>(other_.size());
        for (u32 j = 0; j < map->power_degree(i); ++j) {
            const u32 k = map->power_link_at(i, j);
            if (!link_g_[k])
                continue;
            link_at_[2 * k + (map->power_link(k).a == i ? 0 : 1)] = static_cast<u32>(other_.size());
            other_.push(map->power_other(k, i));
            link_.push(k);
            g_.push(link_g_[k]);
        }
    }
    begin_[n_] = static_cast<u32>(other_.size());
    // The shore drains (6.1): a conductance to ground.
    drain_.resize(0);
    drain_.resize(n_);
    for (u32 i = 0; i < n_; ++i)
        drain_[i] = static_cast<u64>(s.drain_w * map->water_share(i) * static_cast<double>(kSiemens) + 0.5);
    repart();
    load_.resize(0);
    load_.resize(n_);
    src_g_.resize(0);
    src_g_.resize(n_);
    src_lo_.resize(0);
    src_lo_.resize(n_);
    src_hi_.resize(0);
    src_hi_.resize(n_);
    src_.resize(0);
    uv_.resize(0);
    uv_.resize(n_);
    heat_.resize(0);
    heat_.resize(n_);
    solves = sweeps = capped = 0;
    last_move = 0;
    given_uw = 0;
    loads_uw = links_uw = links_sea_uw = drains_uw = inside_uw = 0;
    return true;
}

void Grid::remap() {
    Array<i64> uv;
    uv.assign(uv_.data(), uv_.size());
    const u64 a = solves, b = sweeps, c = capped;
    const i64 move = last_move;
    init(s_, map_, nullptr);
    for (usize i = 0; i < uv.size() && i < uv_.size(); ++i)
        uv_[i] = uv[i];
    solves = a;
    sweeps = b;
    capped = c;
    last_move = move;
}

// The parts the links with any conductance leave: the kept links, less the
// failed ones.
void Grid::repart() {
    part_.resize(0);
    part_.resize(n_);
    part_.fill(~0u);
    parts_ = 0;
    Array<u32> stack;
    for (u32 i = 0; i < n_; ++i) {
        if (part_[i] != ~0u)
            continue;
        part_[i] = parts_;
        stack.push(i);
        while (!stack.empty()) {
            const u32 a = stack[stack.size() - 1];
            stack.resize(stack.size() - 1);
            for (u32 e = begin_[a]; e < begin_[a + 1]; ++e)
                if (g_[e] && part_[other_[e]] == ~0u) {
                    part_[other_[e]] = parts_;
                    stack.push(other_[e]);
                }
        }
        ++parts_;
    }
    lit_.resize(0);
    lit_.resize(parts_);
}

void Grid::set_link(u32 k, u64 g) {
    if (k >= link_g_.size() || !link_g0_[k])
        return; // one the setup left out stays out
    link_g_[k] = g;
    g_[link_at_[2 * k]] = g;
    g_[link_at_[2 * k + 1]] = g;
}

void Grid::begin() {
    load_.fill(0);
    src_g_.fill(0);
    src_lo_.fill(0);
    src_hi_.fill(0);
    src_.resize(0);
}

void Grid::source(u32 place, i64 uv, u64 g) {
    if (!g || uv <= 0)
        return;
    Src s;
    s.place = place;
    s.uv = uv;
    s.g = g;
    src_.push(s);
    src_g_[place] += g;
    u64 hi;
    const u64 lo = _umul128(static_cast<u64>(uv), g, &hi);
    const unsigned char c = _addcarry_u64(0, src_lo_[place], lo, &src_lo_[place]);
    _addcarry_u64(c, src_hi_[place], hi, &src_hi_[place]);
}

u32 Grid::solve(u32 cap) {
    const u32 most = cap ? cap : s_.sweeps;
    lit_.fill(0);
    for (usize k = 0; k < src_.size(); ++k)
        lit_[part_[src_[k].place]] = 1;
    // A part with no source sits at 0 V (4.1).
    for (u32 i = 0; i < n_; ++i)
        if (!lit_[part_[i]])
            uv_[i] = 0;
    u32 count = 0;
    i64 move = 0;
    while (!src_.empty() && count < most) {
        move = 0;
        for (u32 i = 0; i < n_; ++i) {
            if (!lit_[part_[i]])
                continue;
            u64 den = load_[i] + drain_[i] + src_g_[i];
            u64 lo = src_lo_[i], hi = src_hi_[i];
            for (u32 e = begin_[i]; e < begin_[i + 1]; ++e) {
                den += g_[e];
                u64 h;
                const u64 l = _umul128(static_cast<u64>(uv_[other_[e]]), g_[e], &h);
                const unsigned char c = _addcarry_u64(0, lo, l, &lo);
                _addcarry_u64(c, hi, h, &hi);
            }
            i64 v = 0;
            if (den) {
                // Rounded down to the microvolt: the millivolt a place then
                // reads (mv) is the exact answer's, rounded once, so one
                // source at one place gives gate 2's voltage exactly.
                u64 rem;
                v = static_cast<i64>(_udiv128(hi, lo, den, &rem));
            }
            const i64 d = v > uv_[i] ? v - uv_[i] : uv_[i] - v;
            move = d > move ? d : move;
            uv_[i] = v;
        }
        ++count;
        if (move <= tolerance_)
            break;
    }
    ++solves;
    sweeps += count;
    capped += count == most && move > tolerance_ ? 1 : 0;
    last_move = move;
    account();
    return count;
}

u64 Grid::link_loss(u32 k) const {
    const PowerLink& l = map_->power_link(k);
    return power_uw(uv_[l.a] - uv_[l.b], link_g_[k]);
}

i64 Grid::link_flow(u32 k) const {
    const PowerLink& l = map_->power_link(k);
    const i64 va = uv_[l.a], vb = uv_[l.b];
    if (va == vb || !link_g_[k])
        return 0;
    // What enters it at its higher end: (Vhi - Vlo) x Vhi x G.
    const u64 d = static_cast<u64>(va > vb ? va - vb : vb - va), top = static_cast<u64>(va > vb ? va : vb);
    const i64 p = static_cast<i64>(mul_div_u64(d * top, link_g_[k], 1000000ull << 20));
    return va > vb ? p : -p;
}

// What every part lost in the solve just made, and what heats each place
// besides its devices.
void Grid::account() {
    heat_.fill(0);
    given_uw = 0;
    loads_uw = links_uw = links_sea_uw = drains_uw = inside_uw = 0;
    for (u32 k = 0; k < link_g_.size(); ++k) {
        loss_[k] = 0;
        if (!link_g_[k])
            continue;
        const PowerLink& l = map_->power_link(k);
        const u64 p = power_uw(uv_[l.a] - uv_[l.b], link_g_[k]);
        loss_[k] = p;
        if (!p)
            continue;
        const u64 land = link_land_[k] >= (1u << 20) ? p : mul_div_u64(p, link_land_[k], 1ull << 20);
        links_uw += p;
        links_sea_uw += p - land;
        heat_[l.a] += land / 2;
        heat_[l.b] += land - land / 2;
    }
    for (u32 i = 0; i < n_; ++i) {
        drains_uw += power_uw(uv_[i], drain_[i]);
        loads_uw += power_uw(uv_[i], load_[i]);
    }
    for (usize k = 0; k < src_.size(); ++k) {
        const Src& s = src_[k];
        const i64 v = uv_[s.place], d = s.uv - v;
        const u64 inside = power_uw(d, s.g);
        inside_uw += inside;
        heat_[s.place] += inside;
        // What it gives its place: (Vs - V) x V x Gs. A source below its
        // place's voltage takes power, which leaves the world.
        const u64 mag = mul_div_u64(static_cast<u64>(d < 0 ? -d : d) * static_cast<u64>(v), s.g, 1000000ull << 20);
        given_uw += d < 0 ? -static_cast<i64>(mag) : static_cast<i64>(mag);
    }
}

void Grid::serialize(Archive& ar) {
    ar.tag(0x47524431); // "GRD1"
    u64 n = uv_.size();
    ar.u64_(n);
    if (n != uv_.size()) {
        ar.fail();
        return;
    }
    for (usize i = 0; i < uv_.size(); ++i)
        ar.i64_(uv_[i]);
}

u64 Grid::hash() const {
    Hasher h(0x67726964ull); // "grid"
    h.word(uv_.size());
    for (usize i = 0; i < uv_.size(); ++i)
        h.word(static_cast<u64>(uv_[i]));
    return h.finish();
}

} // namespace nova2
