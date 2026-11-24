#include "nova2/map.h"

#include <cmath>   // floor, ceil, sqrt, pow, cos, sin, exp
#include <cstdio>  // snprintf
#include <cstring> // memcpy

#include "nova2/archive.h"
#include "nova2/hash.h"
#include "nova2/hashmap.h"
#include "nova2/keys.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

constexpr u32 kNo = 0xFFFFFFFFu;
constexpr double kPi = 3.14159265358979323846;

// The map's jobs, each its own stream (kPurposeMap).
constexpr u32 kJobDarts = 0;
constexpr u32 kJobIds = 1;
constexpr u32 kJobBridges = 2;

u64 pair_key(u32 a, u32 b) { return a < b ? (static_cast<u64>(a) << 32) | b : (static_cast<u64>(b) << 32) | a; }

u64 bits_of(double d) {
    u64 b;
    memcpy(&b, &d, sizeof b);
    return b;
}

double dist(const Array<double>& x, const Array<double>& y, u32 a, u32 b) {
    const double dx = x[a] - x[b], dy = y[a] - y[b];
    return sqrt(dx * dx + dy * dy);
}

u32 find_root(Array<u32>& parent, u32 v) {
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
    }
    return v;
}

// --- the triangulation's parts ------------------------------------------------

struct Tri {
    u32 v[3];  // counter-clockwise
    u32 nb[3]; // the neighbour opposite v[i], or kNo
    u32 mark;
    u32 alive;
};

struct CavityEdge {
    u32 a, b, out, from;
};

double orient(const Array<double>& X, const Array<double>& Y, u32 a, u32 b, u32 c) {
    return (X[b] - X[a]) * (Y[c] - Y[a]) - (Y[b] - Y[a]) * (X[c] - X[a]);
}

bool incircle(const Array<double>& X, const Array<double>& Y, const Tri& t, u32 p) {
    const double ax = X[t.v[0]] - X[p], ay = Y[t.v[0]] - Y[p];
    const double bx = X[t.v[1]] - X[p], by = Y[t.v[1]] - Y[p];
    const double qx = X[t.v[2]] - X[p], qy = Y[t.v[2]] - Y[p];
    const double det = (ax * ax + ay * ay) * (bx * qy - qx * by) - (bx * bx + by * by) * (ax * qy - qx * ay) +
                       (qx * qx + qy * qy) * (ax * by - bx * ay);
    return det > 0;
}

// How far a field's scatter reaches either way: `scatter` of its values'
// range.
double scatter_span(const Array<double>& v, double scatter) {
    if (v.size() == 0 || scatter <= 0)
        return 0;
    double lo = v[0], hi = v[0];
    for (usize i = 1; i < v.size(); ++i) {
        lo = v[i] < lo ? v[i] : lo;
        hi = v[i] > hi ? v[i] : hi;
    }
    return scatter * (hi - lo);
}

// A field's values nudged by up to `scatter` of their range, either way (7.3).
void scatter_values(Array<double>& v, double scatter, u64 seed, Field f) {
    if (v.size() == 0 || scatter <= 0)
        return;
    double lo = v[0], hi = v[0];
    for (usize i = 1; i < v.size(); ++i) {
        lo = v[i] < lo ? v[i] : lo;
        hi = v[i] > hi ? v[i] : hi;
    }
    const double span = scatter * (hi - lo);
    Rng r(seed, Rng::kWorldPlace, kPurposeScatter, static_cast<u32>(f));
    for (usize i = 0; i < v.size(); ++i)
        v[i] += span * (2.0 * r.unit() - 1.0);
}

double of_bits(u64 b) {
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

// Values by rank, spread evenly on a log scale from lo to hi: the lowest
// value gets lo and the highest hi (one alone gets their middle).
void spread_by_rank(const Array<double>& v, double lo, double hi, Array<double>* out) {
    const usize n = v.size();
    NOVA2_ASSERT(n < (1u << 24));
    Array<u64> rank;
    for (usize i = 0; i < n; ++i)
        rank.push((order_key(v[i]) & ~0xFFFFFFull) | i);
    sort_u64(rank.data(), n);
    out->resize(n);
    for (usize r = 0; r < n; ++r) {
        const double t = n > 1 ? static_cast<double>(r) / static_cast<double>(n - 1) : 0.5;
        (*out)[rank[r] & 0xFFFFFF] = lo * pow(hi / lo, t);
    }
}

} // namespace

void read_map_settings(Config& cfg, MapSettings* ms) {
    MapSettings& m = *ms;
    const Str kind = key_choice(cfg, "world.map");
    m.kind = kind.eq("none")   ? MapKind::none
             : kind.eq("grid") ? MapKind::grid
             : kind.eq("ring") ? MapKind::ring
                               : MapKind::landscape;
    m.aspect = key_f64(cfg, "map.land.aspect");
    m.land = key_f64(cfg, "map.land.share");
    m.land_scale = key_f64(cfg, "map.land.scale");
    m.warp = key_f64(cfg, "map.land.warp");
    m.border = key_f64(cfg, "map.land.border");
    m.resolution = key_u32(cfg, "map.land.resolution");
    m.density = key_f64(cfg, "map.density");
    m.bridges = key_u32(cfg, "map.bridges");
    m.bridge_longest = key_f64(cfg, "map.bridge.longest");
    m.unbridged = key_f64(cfg, "map.unbridged");
    m.bandwidth_min = key_u32(cfg, "map.bandwidth.min");
    m.bandwidth_max = key_u32(cfg, "map.bandwidth.max");
    if (m.bandwidth_max < m.bandwidth_min)
        cfg.fail("map.bandwidth.max", "must be at least map.bandwidth.min");
    m.bridge_bandwidth = key_u32(cfg, "map.bridge.bandwidth");
    m.delay_hop = key_u32(cfg, "map.delay.hop");
    m.delay_length = key_f64(cfg, "map.delay.length");
    m.fields = key_flag(cfg, "map.fields");
    m.field_scale = key_f64(cfg, "map.field.scale");
    m.scatter = key_f64(cfg, "map.field.scatter");
    m.resistance_min = key_f64(cfg, "map.resistance.min");
    m.resistance_max = key_f64(cfg, "map.resistance.max");
    if (m.resistance_max < m.resistance_min)
        cfg.fail("map.resistance.max", "must be at least map.resistance.min");
    m.reach = key_f64(cfg, "map.water.reach");
    m.border_strength = key_f64(cfg, "map.border.strength");
    m.cooling_min = key_f64(cfg, "map.cooling.min");
    m.cooling_max = key_f64(cfg, "map.cooling.max");
    if (m.cooling_max < m.cooling_min)
        cfg.fail("map.cooling.max", "must be at least map.cooling.min");
    char key[48];
    for (u32 f = 0; f < kFieldCount; ++f) {
        snprintf(key, sizeof key, "map.%s.edge", field_name(static_cast<Field>(f)));
        m.shape[f].edge = key_f64(cfg, key);
        snprintf(key, sizeof key, "map.%s.sharpness", field_name(static_cast<Field>(f)));
        m.shape[f].sharpness = key_f64(cfg, key);
    }
}

// --- the triangulation (Bowyer-Watson, from Nova 1) ------------------------------

void delaunay(const double* px, const double* py, u32 n, Array<u32>* tris) {
    tris->clear();
    if (n < 3)
        return;
    double minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for (u32 i = 1; i < n; ++i) {
        minx = px[i] < minx ? px[i] : minx;
        maxx = px[i] > maxx ? px[i] : maxx;
        miny = py[i] < miny ? py[i] : miny;
        maxy = py[i] > maxy ? py[i] : maxy;
    }
    const double cx = 0.5 * (minx + maxx), cy = 0.5 * (miny + maxy);
    double span = maxx - minx > maxy - miny ? maxx - minx : maxy - miny;
    span = span > 1e-9 ? span : 1.0;
    const double S = 64.0 * span;
    Array<double> X, Y;
    X.assign(px, n);
    Y.assign(py, n);
    // A super triangle far around every point (counter-clockwise).
    X.push(cx - S);
    Y.push(cy - S);
    X.push(cx + S);
    Y.push(cy - S);
    X.push(cx);
    Y.push(cy + S);
    Array<Tri> T;
    Array<u32> free_slots, bad, stack, made;
    Array<CavityEdge> boundary;
    T.push(Tri{{n, n + 1, n + 2}, {kNo, kNo, kNo}, 0, 1});
    u32 last = 0, stamp = 0;
    for (u32 p = 0; p < n; ++p) {
        // 1. Walk from the last new triangle to the one holding p.
        u32 t = last;
        for (u32 step = 0; step < 4 * T.size() + 64; ++step) {
            const Tri& tr = T[t];
            u32 next = kNo;
            for (u32 k = 0; k < 3; ++k) {
                const u32 i = (k + step) % 3; // a varying start: no cycling
                if (orient(X, Y, tr.v[(i + 1) % 3], tr.v[(i + 2) % 3], p) < 0) {
                    next = tr.nb[i];
                    break;
                }
            }
            if (next == kNo)
                break;
            t = next;
        }
        // 2. The cavity: every triangle connected to t whose circumcircle holds p.
        ++stamp;
        bad.clear();
        stack.clear();
        T[t].mark = stamp;
        stack.push(t);
        while (!stack.empty()) {
            const u32 u = stack.back();
            stack.pop();
            bad.push(u);
            for (u32 k = 0; k < 3; ++k) {
                const u32 nb = T[u].nb[k];
                if (nb != kNo && T[nb].mark != stamp && incircle(X, Y, T[nb], p)) {
                    T[nb].mark = stamp;
                    stack.push(nb);
                }
            }
        }
        // 3. Its boundary, each edge with the triangle outside it.
        boundary.clear();
        for (usize i = 0; i < bad.size(); ++i) {
            const Tri& b = T[bad[i]];
            for (u32 k = 0; k < 3; ++k) {
                const u32 nb = b.nb[k];
                if (nb == kNo || T[nb].mark != stamp)
                    boundary.push(CavityEdge{b.v[(k + 1) % 3], b.v[(k + 2) % 3], nb, bad[i]});
            }
        }
        // 4. A fan of new triangles from p to the boundary. (This insertion's
        //    own slots are reused only by later ones: an outside triangle may
        //    still point at several of them while the fan is linked.)
        made.clear();
        for (usize i = 0; i < boundary.size(); ++i) {
            const CavityEdge& e = boundary[i];
            u32 slot;
            if (!free_slots.empty()) {
                slot = free_slots.back();
                free_slots.pop();
            } else {
                slot = static_cast<u32>(T.size());
                T.push(Tri{});
            }
            T[slot] = Tri{{p, e.a, e.b}, {e.out, kNo, kNo}, 0, 1};
            if (e.out != kNo)
                for (u32 j = 0; j < 3; ++j)
                    if (T[e.out].nb[j] == e.from) {
                        T[e.out].nb[j] = slot;
                        break;
                    }
            made.push(slot);
        }
        for (usize i = 0; i < made.size(); ++i) {
            Tri& ti = T[made[i]];
            for (usize j = 0; j < made.size(); ++j) {
                const Tri& tj = T[made[j]];
                if (tj.v[1] == ti.v[2])
                    ti.nb[1] = made[j]; // across (b, p)
                if (tj.v[2] == ti.v[1])
                    ti.nb[2] = made[j]; // across (p, a)
            }
        }
        for (usize i = 0; i < bad.size(); ++i) {
            T[bad[i]].alive = 0;
            free_slots.push(bad[i]);
        }
        last = made[0];
    }
    for (usize i = 0; i < T.size(); ++i) {
        const Tri& t = T[i];
        if (t.alive && t.v[0] < n && t.v[1] < n && t.v[2] < n)
            tris->append(t.v, 3);
    }
}

// --- the map ----------------------------------------------------------------------

bool Map::build(u64 seed, u32 places, const MapSettings& s, TextBuf* why) {
    kind_ = s.kind;
    n_ = places;
    width_ = 1;
    x_.clear();
    y_.clear();
    mass_.clear();
    masses_ = 0;
    links_.clear();
    begin_.clear();
    adj_.clear();
    spacing_ = 0;
    mean_length_ = 1;
    bridges_ = joins_ = patches_ = peopled_ = unbridged_ = 0;
    raster_w_ = raster_h_ = 0;
    land_.clear();
    cell_mass_.clear();
    plinks_.clear();
    padj_.clear();
    cell_place_.clear();
    power_wet_ = power_only_ = data_only_ = borders_ = 0;
    border_strength_ = s.border_strength;
    seed_ = seed;
    s_ = s;
    lonely_.clear();
    bandwidth_table_ = NearTable{};
    resistance_table_ = NearTable{};
    resistance_dealt_.clear();
    ready_ = false;
    land_list_.clear();
    dens_.clear();
    cum_.clear();
    radius_.clear();
    setup_data_.clear();
    setup_power_.clear();
    cooling_dealt_.clear();
    inc_.clear();
    respawned_ = respawns_ = 0;
    anchors_.clear();
    data_moved_.clear();
    power_moved_.clear();
    pbegin_.resize(static_cast<usize>(n_) + 1);
    pbegin_.fill(0);
    catchment_.resize(n_);
    catchment_.fill(1);
    water_share_.resize(n_);
    water_share_.fill(0);
    cooling_.resize(n_);
    cooling_.fill(1);
    if (kind_ == MapKind::none) {
        begin_.resize(static_cast<usize>(n_) + 1);
        return true;
    }
    x_.resize(n_);
    y_.resize(n_);
    mass_.resize(n_);
    if (kind_ == MapKind::grid)
        grid();
    else if (kind_ == MapKind::ring)
        ring();
    else if (!landscape(seed, s, why))
        return false;
    finish(seed, s);
    power(seed, s);
    return true;
}

void Map::add_link(u32 a, u32 b, bool bridge) {
    MapLink l;
    l.a = a < b ? a : b;
    l.b = a < b ? b : a;
    l.length = dist(x_, y_, a, b);
    l.bridge = bridge;
    links_.push(l);
}

void Map::grid() {
    u32 cols = static_cast<u32>(ceil(sqrt(static_cast<double>(n_))));
    while (static_cast<u64>(cols) * cols < n_)
        ++cols;
    cols = cols ? cols : 1;
    const u32 rows = (n_ + cols - 1) / cols ? (n_ + cols - 1) / cols : 1;
    const double cell = 1.0 / rows;
    width_ = cols * cell;
    spacing_ = cell;
    masses_ = 1;
    peopled_ = n_ ? 1 : 0;
    for (u32 i = 0; i < n_; ++i) {
        x_[i] = (i % cols + 0.5) * cell;
        y_[i] = (i / cols + 0.5) * cell;
        mass_[i] = 0;
    }
    for (u32 i = 0; i < n_; ++i) {
        if (i % cols + 1 < cols && i + 1 < n_)
            add_link(i, i + 1, false);
        if (i + cols < n_)
            add_link(i, i + cols, false);
    }
}

void Map::ring() {
    width_ = 1;
    masses_ = 1;
    peopled_ = n_ ? 1 : 0;
    spacing_ = n_ > 1 ? 0.8 * sin(kPi / n_) : 0;
    for (u32 i = 0; i < n_; ++i) {
        const double a = 2.0 * kPi * i / n_;
        x_[i] = 0.5 + 0.4 * cos(a);
        y_[i] = 0.5 + 0.4 * sin(a);
        mass_[i] = 0;
    }
    for (u32 i = 0; i + 1 < n_; ++i)
        add_link(i, i + 1, false);
    if (n_ >= 3)
        add_link(n_ - 1, 0, false);
}

bool Map::landscape(u64 seed, const MapSettings& s, TextBuf* why) {
    const u32 N = n_;
    const u32 h = s.resolution < 16 ? 16 : s.resolution;
    u32 w = static_cast<u32>(floor(h * s.aspect + 0.5));
    w = w < 1 ? 1 : w;
    raster_w_ = w;
    raster_h_ = h;
    width_ = static_cast<double>(w) / h;
    const double W = width_;
    const usize cells = static_cast<usize>(w) * h;
    NOVA2_ASSERT(cells < (1ull << 28)); // a cell's index fits the rank keys' low 28 bits

    // --- land and water: warped terrain, sinking towards the edges ------------
    Noise2 terrain, wx, wy;
    terrain.init(seed, kNoiseTerrain);
    wx.init(seed, kNoiseWarpX);
    wy.init(seed, kNoiseWarpY);
    Array<u64> keys, sorted;
    keys.resize(cells);
    for (u32 j = 0; j < h; ++j) {
        for (u32 i = 0; i < w; ++i) {
            const double x = (i + 0.5) / h, y = (j + 0.5) / h;
            const double qx = x + s.warp * wx.fbm(x, y, 3, s.land_scale);
            const double qy = y + s.warp * wy.fbm(x, y, 3, s.land_scale);
            double v = terrain.fbm(qx, qy, 5, s.land_scale);
            double e = x;
            e = W - x < e ? W - x : e;
            e = y < e ? y : e;
            e = 1 - y < e ? 1 - y : e;
            e /= 0.2; // 0 at the edge, 1 a fifth of the way in
            const double f = e < 1 ? 1 - e : 0;
            v -= s.border * f * f;
            keys[static_cast<usize>(j) * w + i] = order_key(v);
        }
    }
    sorted.assign(keys.data(), cells);
    sort_u64(sorted.data(), cells);
    const double share = s.land < 0 ? 0 : (s.land > 1 ? 1 : s.land);
    const usize cut = static_cast<usize>(floor((1.0 - share) * static_cast<double>(cells) + 0.5));
    const u64 thr = cut < cells ? sorted[cut] : ~0ull;
    land_.resize(cells);
    Array<u32> land_list;
    for (usize c = 0; c < cells; ++c) {
        land_[c] = cut < cells && keys[c] >= thr ? 1 : 0;
        if (land_[c])
            land_list.push(static_cast<u32>(c));
    }
    const u32 L = static_cast<u32>(land_list.size());
    if (L == 0) {
        why->append("map.land.share: the map has no land");
        return false;
    }

    // --- landmasses: a flood fill over 4 neighbours ------------------------------
    cell_mass_.resize(cells);
    cell_mass_.fill(kNo);
    Array<u32> st;
    for (usize c0 = 0; c0 < cells; ++c0) {
        if (!land_[c0] || cell_mass_[c0] != kNo)
            continue;
        const u32 m = masses_++;
        st.clear();
        st.push(static_cast<u32>(c0));
        cell_mass_[c0] = m;
        while (!st.empty()) {
            const u32 c = st.back();
            st.pop();
            const u32 ci = c % w, cj = c / w;
            const u32 nbs[4] = {ci > 0 ? c - 1 : kNo, ci + 1 < w ? c + 1 : kNo, cj > 0 ? c - w : kNo,
                                cj + 1 < h ? c + w : kNo};
            for (u32 k = 0; k < 4; ++k) {
                const u32 q = nbs[k];
                if (q != kNo && land_[q] && cell_mass_[q] == kNo) {
                    cell_mass_[q] = m;
                    st.push(q);
                }
            }
        }
    }

    // --- the density field over the land: d = density^rank, 1 to density ----------
    Array<double> d, cum;
    density_over_land(land_list, &d, &cum);
    const double dmax = s.density > 1 ? s.density : 1;
    const double total = cum[L - 1];

    // --- places: darts on land, chosen by density, none closer than their spacing ---
    // A dart's spacing is r / sqrt(d) at its cell; two darts keep the mean of
    // theirs apart. r starts loose and shrinks by 0.85 until every place fits.
    double r = sqrt(0.5 * total / (static_cast<double>(h) * h) / (N ? N : 1));
    Rng rng(seed, Rng::kWorldPlace, kPurposeMap, kJobDarts);
    Array<double> px, py, pr;
    Array<u32> head, next;
    for (;;) {
        // Places closer than a quarter of a raster cell would sit on coastlines
        // the raster can't draw.
        if (r / sqrt(dmax) < 0.25 / h) {
            why->append("world.places: they don't fit on the land; lower world.places or map.density, or raise "
                        "map.land.share or map.land.resolution");
            return false;
        }
        const double gs = r;
        const u32 gw = static_cast<u32>(ceil(W / gs)) + 1, gh = static_cast<u32>(ceil(1.0 / gs)) + 1;
        head.resize(static_cast<usize>(gw) * gh);
        head.fill(kNo);
        next.resize(px.size());
        for (usize i = 0; i < px.size(); ++i) {
            u32 gx = static_cast<u32>(px[i] / gs), gy = static_cast<u32>(py[i] / gs);
            gx = gx >= gw ? gw - 1 : gx;
            gy = gy >= gh ? gh - 1 : gy;
            const usize g = static_cast<usize>(gy) * gw + gx;
            next[i] = head[g];
            head[g] = static_cast<u32>(i);
        }
        const u64 budget = 60ull * N + 1000;
        for (u64 a = 0; a < budget && px.size() < N; ++a) {
            // The land cell, by density: the first whose running total passes u.
            const double u = rng.unit() * total;
            u32 lo = 0, hi = L - 1;
            while (lo < hi) {
                const u32 mid = lo + (hi - lo) / 2;
                if (cum[mid] > u)
                    hi = mid;
                else
                    lo = mid + 1;
            }
            const u32 c = land_list[lo];
            const double x = (c % w + rng.unit()) / h, y = (c / w + rng.unit()) / h;
            const double rp = r / sqrt(d[lo]);
            u32 gx = static_cast<u32>(x / gs), gy = static_cast<u32>(y / gs);
            gx = gx >= gw ? gw - 1 : gx;
            gy = gy >= gh ? gh - 1 : gy;
            bool ok = true;
            for (i32 dy = -1; dy <= 1 && ok; ++dy) {
                for (i32 dx = -1; dx <= 1 && ok; ++dx) {
                    const i32 qx = static_cast<i32>(gx) + dx, qy = static_cast<i32>(gy) + dy;
                    if (qx < 0 || qy < 0 || qx >= static_cast<i32>(gw) || qy >= static_cast<i32>(gh))
                        continue;
                    for (u32 o = head[static_cast<usize>(qy) * gw + qx]; o != kNo && ok; o = next[o]) {
                        const double ex = px[o] - x, ey = py[o] - y, lim = 0.5 * (rp + pr[o]);
                        ok = ex * ex + ey * ey >= lim * lim;
                    }
                }
            }
            if (!ok)
                continue;
            const usize g = static_cast<usize>(gy) * gw + gx;
            next.push(head[g]);
            head[g] = static_cast<u32>(px.size());
            px.push(x);
            py.push(y);
            pr.push(rp);
        }
        if (px.size() >= N)
            break;
        r *= 0.85;
        for (usize i = 0; i < pr.size(); ++i)
            pr[i] *= 0.85;
    }
    spacing_ = r;

    // --- ids, shuffled ----------------------------------------------------------
    Array<u32> perm;
    perm.resize(N);
    for (u32 i = 0; i < N; ++i)
        perm[i] = i;
    Rng ids(seed, Rng::kWorldPlace, kPurposeMap, kJobIds);
    for (u32 i = N; i > 1; --i) {
        const u32 j = static_cast<u32>(ids.below(i));
        const u32 t = perm[i - 1];
        perm[i - 1] = perm[j];
        perm[j] = t;
    }
    Array<u32> count;
    count.resize(masses_);
    for (u32 k = 0; k < N; ++k) {
        x_[k] = px[perm[k]];
        y_[k] = py[perm[k]];
        u32 ci = static_cast<u32>(x_[k] * h), cj = static_cast<u32>(y_[k] * h);
        ci = ci >= w ? w - 1 : ci;
        cj = cj >= h ? h - 1 : cj;
        mass_[k] = cell_mass_[static_cast<usize>(cj) * w + ci];
        NOVA2_ASSERT(mass_[k] != kNo);
        ++count[mass_[k]];
    }
    for (u32 m = 0; m < masses_; ++m)
        peopled_ += count[m] ? 1 : 0;

    // --- data links: each landmass's Gabriel graph ---------------------------------
    land_links();
    Array<u32> tris, parent;

    // --- bridges: crossings from the triangulation of every place ------------------
    lonely_.resize(0);
    lonely_.resize(masses_);
    Rng br(seed, Rng::kWorldPlace, kPurposeMap, kJobBridges);
    for (u32 m = 0; m < masses_; ++m) {
        lonely_[m] = br.chance(s.unbridged) ? 1 : 0;
        unbridged_ += lonely_[m] && count[m] ? 1 : 0;
    }
    Array<u64> edges;
    delaunay(x_.data(), y_.data(), N, &tris);
    for (usize t = 0; t + 2 < tris.size(); t += 3)
        for (u32 e = 0; e < 3; ++e) {
            const u32 a = tris[t + e], b = tris[t + (e + 1) % 3];
            if (mass_[a] != mass_[b])
                edges.push(pair_key(a, b));
        }
    sort_u64(edges.data(), edges.size());
    {
        usize u = 0;
        for (usize i = 0; i < edges.size(); ++i)
            if (u == 0 || edges[i] != edges[u - 1])
                edges[u++] = edges[i];
        edges.resize(u);
    }
    NOVA2_ASSERT(edges.size() < (1u << 24));
    Array<u64> cross; // by length, then key
    for (usize i = 0; i < edges.size(); ++i) {
        const u32 a = static_cast<u32>(edges[i] >> 32), b = static_cast<u32>(edges[i]);
        cross.push((order_key(dist(x_, y_, a, b)) & ~0xFFFFFFull) | i);
    }
    sort_u64(cross.data(), cross.size());
    parent.resize(masses_);
    for (u32 m = 0; m < masses_; ++m)
        parent[m] = m;
    // The shortest crossings between each pair, up to map.bridges a pair, no
    // longer than the longest, no place at the end of two.
    HashMap<u32> per_pair;
    Array<u8> used;
    used.resize(N);
    for (usize i = 0; i < cross.size() && s.bridges > 0; ++i) {
        const u64 e = edges[cross[i] & 0xFFFFFF];
        const u32 a = static_cast<u32>(e >> 32), b = static_cast<u32>(e);
        const u32 ma = mass_[a], mb = mass_[b];
        if (lonely_[ma] || lonely_[mb] || used[a] || used[b] || dist(x_, y_, a, b) > s.bridge_longest)
            continue;
        u32& got = per_pair.get_or_insert(pair_key(ma, mb));
        if (got >= s.bridges)
            continue;
        ++got;
        used[a] = used[b] = 1;
        add_link(a, b, true);
        ++bridges_;
        parent[find_root(parent, ma)] = find_root(parent, mb);
    }
    // Then every landmass joined to the rest by the shortest crossings,
    // whatever their length.
    for (usize i = 0; i < cross.size(); ++i) {
        const u64 e = edges[cross[i] & 0xFFFFFF];
        const u32 a = static_cast<u32>(e >> 32), b = static_cast<u32>(e);
        const u32 ma = mass_[a], mb = mass_[b];
        if (lonely_[ma] || lonely_[mb] || find_root(parent, ma) == find_root(parent, mb))
            continue;
        add_link(a, b, true);
        ++bridges_;
        ++joins_;
        parent[find_root(parent, ma)] = find_root(parent, mb);
    }
    // (Landmasses the triangulation reaches only through an unbridged one:
    // the closest pair of places across, by brute force. Rare.)
    u32 apart = 0;
    for (u32 m = 0; m < masses_; ++m)
        apart += count[m] && !lonely_[m] && find_root(parent, m) == m ? 1 : 0;
    for (; apart > 1; --apart) {
        u32 ba = kNo, bb = kNo;
        double best = 0;
        for (u32 a = 0; a < N; ++a) {
            if (lonely_[mass_[a]])
                continue;
            for (u32 b = a + 1; b < N; ++b) {
                if (lonely_[mass_[b]] || find_root(parent, mass_[a]) == find_root(parent, mass_[b]))
                    continue;
                const double dd = dist(x_, y_, a, b);
                if (ba == kNo || dd < best) {
                    ba = a;
                    bb = b;
                    best = dd;
                }
            }
        }
        if (ba == kNo)
            break;
        add_link(ba, bb, true);
        ++bridges_;
        ++joins_;
        parent[find_root(parent, mass_[ba])] = find_root(parent, mass_[bb]);
    }
    return true;
}

void Map::finish(u64 seed, const MapSettings& s) {
    sort_links();
    const u32 E = link_count();

    // The average link length, of the links on land (6.1), and the delays.
    double sum = 0;
    u32 on_land = 0;
    for (u32 k = 0; k < E; ++k)
        if (!links_[k].bridge) {
            sum += links_[k].length;
            ++on_land;
        }
    if (on_land == 0)
        for (u32 k = 0; k < E; ++k) {
            sum += links_[k].length;
            ++on_land;
        }
    mean_length_ = on_land && sum > 0 ? sum / on_land : 1;
    set_delays();

    // Bandwidth: the bandwidth field at each link's middle, ranked among the
    // links on land, spread evenly on a log scale from the least to the most.
    FieldNoise bwf;
    bwf.init(seed, Field::bandwidth, s.shape[static_cast<u32>(Field::bandwidth)], s.field_scale);
    Array<double> v;
    Array<u32> which;
    for (u32 k = 0; k < E; ++k) {
        if (links_[k].bridge) {
            links_[k].bandwidth = s.bridge_bandwidth;
            continue;
        }
        const MapLink& l = links_[k];
        v.push(bwf.at(0.5 * (x_[l.a] + x_[l.b]), 0.5 * (y_[l.a] + y_[l.b])));
        which.push(k);
    }
    const double span = scatter_span(v, s.scatter);
    scatter_values(v, s.scatter, seed, Field::bandwidth);
    NOVA2_ASSERT(v.size() < (1u << 24));
    Array<u64> rank;
    for (usize i = 0; i < v.size(); ++i)
        rank.push((order_key(v[i]) & ~0xFFFFFFull) | i);
    sort_u64(rank.data(), rank.size());
    const double lo = s.bandwidth_min, ratio = static_cast<double>(s.bandwidth_max) / s.bandwidth_min;
    for (usize r = 0; r < rank.size(); ++r) {
        const double t = rank.size() > 1 ? static_cast<double>(r) / static_cast<double>(rank.size() - 1) : 0.5;
        const double bw = floor(lo * pow(ratio, t) + 0.5);
        links_[which[rank[r] & 0xFFFFFF]].bandwidth = bw < 1 ? 1 : static_cast<u32>(bw);
    }

    // The field's values and what each was dealt, for the links a respawn
    // makes (7.8).
    bandwidth_table_.build(v, span);
    for (usize i = 0; i < bandwidth_table_.who.size(); ++i)
        bandwidth_table_.who[i] = links_[which[bandwidth_table_.who[i]]].bandwidth;
    index_links();
}

// --- the parts of setup a respawn runs again (7.8) ---------------------------------

// The density over the land cells given, in their order: d = density^rank, 1
// to density, by the density field at each cell's middle; and their running
// total.
void Map::land_table(Array<u32>* cells, Array<double>* cum) const {
    cells->clear();
    cum->clear();
    if (kind_ != MapKind::landscape)
        return;
    for (usize c = 0; c < land_.size(); ++c)
        if (land_[c])
            cells->push(static_cast<u32>(c));
    Array<double> d;
    density_over_land(*cells, &d, cum);
}

void Map::density_over_land(const Array<u32>& land_list, Array<double>* dp, Array<double>* cump) const {
    const u32 w = raster_w_, h = raster_h_;
    const u32 L = static_cast<u32>(land_list.size());
    Array<double>& d = *dp;
    Array<double>& cum = *cump;
    FieldNoise dens;
    dens.init(seed_, Field::density, s_.shape[static_cast<u32>(Field::density)], s_.field_scale);
    Array<u64> dkeys;
    dkeys.resize(L);
    for (u32 k = 0; k < L; ++k) {
        const u32 c = land_list[k];
        const double v = dens.at((c % w + 0.5) / h, (c / w + 0.5) / h);
        dkeys[k] = (order_key(v) & ~0xFFFFFFFull) | k;
    }
    sort_u64(dkeys.data(), L);
    d.resize(L);
    cum.resize(L);
    const double dmax = s_.density > 1 ? s_.density : 1;
    for (u32 r = 0; r < L; ++r)
        d[static_cast<u32>(dkeys[r] & 0xFFFFFFF)] = L > 1 ? pow(dmax, static_cast<double>(r) / (L - 1)) : 1.0;
    double total = 0;
    for (u32 k = 0; k < L; ++k) {
        total += d[k];
        cum[k] = total;
    }
}

// The links on land: each landmass's Gabriel graph, from its places as they
// stand.
void Map::land_links() {
    const u32 N = n_;
    Array<u32> count;
    count.resize(masses_);
    for (u32 k = 0; k < N; ++k)
        ++count[mass_[k]];
    Array<u32> mbegin, mplaces, fill;
    mbegin.resize(static_cast<usize>(masses_) + 1);
    for (u32 m = 0; m < masses_; ++m)
        mbegin[m + 1] = mbegin[m] + count[m];
    fill.assign(mbegin.data(), masses_);
    mplaces.resize(N);
    for (u32 k = 0; k < N; ++k)
        mplaces[fill[mass_[k]]++] = k;
    Array<double> lx, ly;
    Array<u32> tris, parent;
    Array<u64> kept;
    HashMap<u8> seen;
    for (u32 m = 0; m < masses_; ++m) {
        const u32 k = count[m];
        if (k < 2)
            continue;
        const u32* ids_m = mplaces.data() + mbegin[m];
        lx.resize(k);
        ly.resize(k);
        for (u32 i = 0; i < k; ++i) {
            lx[i] = x_[ids_m[i]];
            ly[i] = y_[ids_m[i]];
        }
        // An edge of the triangulation is Gabriel if both angles facing it
        // are acute: no point lies in the circle on it.
        delaunay(lx.data(), ly.data(), k, &tris);
        seen.clear();
        for (usize t = 0; t + 2 < tris.size(); t += 3) {
            for (u32 e = 0; e < 3; ++e) {
                const u32 p = tris[t + e], q = tris[t + (e + 1) % 3], o = tris[t + (e + 2) % 3];
                const double dot = (lx[p] - lx[o]) * (lx[q] - lx[o]) + (ly[p] - ly[o]) * (ly[q] - ly[o]);
                u8& f = seen.get_or_insert(pair_key(p, q));
                f = static_cast<u8>(f | (dot > 0 ? 1 : 3)); // 1 seen, 2 an angle facing it isn't acute
            }
        }
        kept.clear();
        for (usize i = 0; i < seen.slot_count(); ++i)
            if (seen.slot_used(i) && seen.slot_value(i) == 1)
                kept.push(seen.slot_key(i));
        sort_u64(kept.data(), kept.size());
        parent.resize(k);
        for (u32 i = 0; i < k; ++i)
            parent[i] = i;
        u32 pieces = k;
        for (usize i = 0; i < kept.size(); ++i) {
            const u32 p = static_cast<u32>(kept[i] >> 32), q = static_cast<u32>(kept[i]);
            add_link(ids_m[p], ids_m[q], false);
            const u32 rp = find_root(parent, p), rq = find_root(parent, q);
            if (rp != rq) {
                parent[rp] = rq;
                --pieces;
            }
        }
        // A degenerate landmass (two places, or all on a line) has pieces
        // the triangulation didn't join: each joined by its shortest gap.
        while (pieces > 1) {
            u32 bp = kNo, bq = kNo;
            double best = 0;
            for (u32 p = 0; p < k; ++p)
                for (u32 q = p + 1; q < k; ++q) {
                    if (find_root(parent, p) == find_root(parent, q))
                        continue;
                    const double dd = dist(lx, ly, p, q);
                    if (bp == kNo || dd < best) {
                        bp = p;
                        bq = q;
                        best = dd;
                    }
                }
            add_link(ids_m[bp], ids_m[bq], false);
            parent[find_root(parent, bp)] = find_root(parent, bq);
            --pieces;
            patches_ += k > 2 ? 1 : 0;
        }
    }
}

// The links in order of their places.
void Map::sort_links() {
    const u32 E = link_count();
    Array<u64> order;
    HashMap<u32> at;
    for (u32 k = 0; k < E; ++k) {
        const u64 key = pair_key(links_[k].a, links_[k].b);
        order.push(key);
        at.get_or_insert(key) = k;
    }
    sort_u64(order.data(), E);
    Array<MapLink> sorted;
    for (u32 k = 0; k < E; ++k)
        sorted.push(links_[*at.find(order[k])]);
    links_.clear();
    links_.append(sorted.data(), E);
}

// Each link's delay from its length (11.3), in whole ticks.
void Map::set_delays() {
    for (u32 k = 0; k < link_count(); ++k) {
        const double ticks = s_.delay_hop + s_.delay_length * links_[k].length / mean_length_;
        const double up = ceil(ticks - 1e-9);
        links_[k].delay = up < 1 ? 1 : static_cast<u32>(up);
    }
}

// Each place's links, in order of the place at their other end.
void Map::index_links() {
    const u32 E = link_count();
    begin_.resize(static_cast<usize>(n_) + 1);
    begin_.fill(0);
    for (u32 k = 0; k < E; ++k) {
        ++begin_[links_[k].a + 1];
        ++begin_[links_[k].b + 1];
    }
    for (u32 i = 0; i < n_; ++i)
        begin_[i + 1] += begin_[i];
    Array<u32> next;
    next.assign(begin_.data(), n_);
    Array<u64> ends;
    ends.resize(begin_[n_]);
    for (u32 k = 0; k < E; ++k) {
        ends[next[links_[k].a]++] = (static_cast<u64>(links_[k].b) << 32) | k;
        ends[next[links_[k].b]++] = (static_cast<u64>(links_[k].a) << 32) | k;
    }
    adj_.resize(begin_[n_]);
    for (u32 i = 0; i < n_; ++i) {
        sort_u64(ends.data() + begin_[i], begin_[i + 1] - begin_[i]);
        for (u32 j = begin_[i]; j < begin_[i + 1]; ++j)
            adj_[j] = static_cast<u32>(ends[j]);
    }
}

// The Gabriel graph over all the places, as pairs: an edge of their
// triangulation both of whose facing angles are acute.
void Map::gabriel_all(Array<u64>* keptp) const {
    const u32 N = n_;
    Array<u64>& kept = *keptp;
    Array<u32> tris;
    delaunay(x_.data(), y_.data(), N, &tris);
    if (tris.size() > 0) {
        // As a landmass's data links are: an edge of the triangulation
        // both of whose facing angles are acute.
        HashMap<u8> seen;
        for (usize t = 0; t + 2 < tris.size(); t += 3) {
            for (u32 e = 0; e < 3; ++e) {
                const u32 p = tris[t + e], q = tris[t + (e + 1) % 3], o = tris[t + (e + 2) % 3];
                const double dot = (x_[p] - x_[o]) * (x_[q] - x_[o]) + (y_[p] - y_[o]) * (y_[q] - y_[o]);
                u8& f = seen.get_or_insert(pair_key(p, q));
                f = static_cast<u8>(f | (dot > 0 ? 1 : 3));
            }
        }
        for (usize i = 0; i < seen.slot_count(); ++i)
            if (seen.slot_used(i) && seen.slot_value(i) == 1)
                kept.push(seen.slot_key(i));
    } else {
        // Two places, or all on a line: nothing to triangulate, so by the
        // rule itself.
        for (u32 a = 0; a < N; ++a)
            for (u32 b = a + 1; b < N; ++b) {
                bool clear = true;
                for (u32 o = 0; o < N && clear; ++o)
                    clear = o == a || o == b ||
                            (x_[a] - x_[o]) * (x_[b] - x_[o]) + (y_[a] - y_[o]) * (y_[b] - y_[o]) > 0;
                if (clear)
                    kept.push(pair_key(a, b));
            }
    }
}

// A proximity link's resistance, in average links of the usual resistance:
// its length x its resistance a length x e^(its water / the reach), and the
// border strength more if it crosses one.
void Map::set_resistance(PowerLink& l) const {
    l.resistance = l.length / mean_length_ * l.per_length * exp(l.water / mean_length_ / s_.reach) +
                   (l.border ? s_.border_strength : 0);
}

// Each place's proximity links, in order of the place at their other end.
void Map::index_power() {
    const u32 N = n_;
    const u32 E = power_link_count();
    pbegin_.resize(static_cast<usize>(N) + 1);
    pbegin_.fill(0);
    for (u32 k = 0; k < E; ++k) {
        ++pbegin_[plinks_[k].a + 1];
        ++pbegin_[plinks_[k].b + 1];
    }
    for (u32 i = 0; i < N; ++i)
        pbegin_[i + 1] += pbegin_[i];
    Array<u32> next;
    next.assign(pbegin_.data(), N);
    Array<u64> ends;
    ends.resize(pbegin_[N]);
    for (u32 k = 0; k < E; ++k) {
        ends[next[plinks_[k].a]++] = (static_cast<u64>(plinks_[k].b) << 32) | k;
        ends[next[plinks_[k].b]++] = (static_cast<u64>(plinks_[k].a) << 32) | k;
    }
    padj_.resize(pbegin_[N]);
    for (u32 i = 0; i < N; ++i) {
        sort_u64(ends.data() + pbegin_[i], pbegin_[i + 1] - pbegin_[i]);
        for (u32 j = pbegin_[i]; j < pbegin_[i + 1]; ++j)
            padj_[j] = static_cast<u32>(ends[j]);
    }
}

// --- the power map (6.1, 6.2; build-plan-gate4.md step 2) --------------------------
// Worked out after the data map, from the places it left, and changing none
// of it.

void Map::power(u64 seed, const MapSettings& s) {
    // --- proximity links: the Gabriel graph over all the places ------------------
    Array<u64> kept;
    if (kind_ == MapKind::landscape) {
        gabriel_all(&kept);
    } else {
        // A test map's are its data links.
        for (usize k = 0; k < links_.size(); ++k)
            kept.push(pair_key(links_[k].a, links_[k].b));
    }
    sort_u64(kept.data(), kept.size());
    const u32 E = static_cast<u32>(kept.size());

    // --- each link's length, water, and the data link beside it -------------------
    // A bridge between the same two places is another thing (6.1: a bridge
    // carries no power), so the proximity link there is power only.
    HashMap<u32> data_at; // a pair's data link on land, + 1
    u32 on_land = 0;
    for (u32 k = 0; k < link_count(); ++k)
        if (!links_[k].bridge) {
            data_at.get_or_insert(pair_key(links_[k].a, links_[k].b)) = k + 1;
            ++on_land;
        }
    FieldNoise rf;
    rf.init(seed, Field::resistance, s.shape[static_cast<u32>(Field::resistance)], s.field_scale);
    Array<double> v, per;
    u32 both = 0;
    for (u32 k = 0; k < E; ++k) {
        PowerLink l;
        l.a = static_cast<u32>(kept[k] >> 32);
        l.b = static_cast<u32>(kept[k]);
        l.length = dist(x_, y_, l.a, l.b);
        const u32* d = data_at.find(kept[k]);
        l.data = d ? *d - 1 : kNoLink;
        both += d ? 1 : 0;
        l.water = kind_ == MapKind::landscape ? water_under(l.a, l.b) : 0;
        power_wet_ += l.water > 0 ? 1 : 0;
        l.border = rf.high(x_[l.a], y_[l.a]) != rf.high(x_[l.b], y_[l.b]);
        borders_ += l.border ? 1 : 0;
        v.push(rf.at(0.5 * (x_[l.a] + x_[l.b]), 0.5 * (y_[l.a] + y_[l.b])));
        plinks_.push(l);
    }
    power_only_ = E - both;
    data_only_ = on_land - both;

    // --- resistance: by the field's rank, the water's width, and borders ------------
    const double span = scatter_span(v, s.scatter);
    scatter_values(v, s.scatter, seed, Field::resistance);
    spread_by_rank(v, s.resistance_min, s.resistance_max, &per);
    for (u32 k = 0; k < E; ++k) {
        PowerLink& l = plinks_[k];
        l.per_length = per[k];
        set_resistance(l);
    }
    // The field's values and what each was dealt, for the links a respawn
    // makes (7.8).
    resistance_table_.build(v, span);
    resistance_dealt_.assign(per.data(), per.size());

    // --- each place's proximity links, in order of the place at their other end ----
    index_power();

    // --- catchments and water shares, and each place's cooling ----------------------
    if (kind_ == MapKind::landscape)
        catchments();
    field_values(seed, Field::cooling, s, &v);
    spread_by_rank(v, s.cooling_min, s.cooling_max, &cooling_);
}

// The part of the line from place a to place b that lies over water cells,
// in the map's height. The line is cut wherever it passes from one raster
// cell to the next, and each piece is its middle's cell's: land or water.
double Map::water_under(u32 a, u32 b) const {
    const double h = raster_h_;
    const double ax = x_[a] * h, ay = y_[a] * h, dx = x_[b] * h - ax, dy = y_[b] * h - ay; // in cells
    Array<u64> cuts; // shares of the way along, none below 0, so their bits sort as they do
    cuts.push(bits_of(0.0));
    cuts.push(bits_of(1.0));
    for (u32 axis = 0; axis < 2; ++axis) {
        const double from = axis ? ay : ax, d = axis ? dy : dx;
        if (d == 0)
            continue;
        const double lo = d > 0 ? from : from + d, hi = d > 0 ? from + d : from;
        for (double g = floor(lo) + 1; g < hi; g += 1) {
            const double t = (g - from) / d;
            cuts.push(bits_of(t < 1 ? t : 1.0));
        }
    }
    sort_u64(cuts.data(), cuts.size());
    double wet = 0;
    for (usize i = 0; i + 1 < cuts.size(); ++i) {
        const double t0 = of_bits(cuts[i]), t1 = of_bits(cuts[i + 1]);
        if (t1 <= t0)
            continue;
        const double tm = 0.5 * (t0 + t1), cx = floor(ax + tm * dx), cy = floor(ay + tm * dy);
        const bool in = cx >= 0 && cy >= 0 && cx < raster_w_ && cy < raster_h_;
        if (!in || !land_[static_cast<usize>(cy) * raster_w_ + static_cast<usize>(cx)])
            wet += t1 - t0;
    }
    return wet * dist(x_, y_, a, b);
}

// Each land cell to the place nearest its middle (the lower id of two as
// near), and each place's share of water among the cells bordering its
// catchment.
void Map::catchments() {
    const u32 N = n_, w = raster_w_, h = raster_h_;
    const usize cells = static_cast<usize>(w) * h;
    cell_place_.resize(cells);
    cell_place_.fill(kNoPlace);
    catchment_.fill(0);
    if (N == 0)
        return;
    // The places in buckets, about one each; a cell looks through the rings
    // of buckets round its own until none further out could hold a nearer.
    const double gs = sqrt(width_ / N);
    const u32 gw = static_cast<u32>(floor(width_ / gs)) + 1, gh = static_cast<u32>(floor(1.0 / gs)) + 1;
    Array<u32> head, next;
    head.resize(static_cast<usize>(gw) * gh);
    head.fill(kNo);
    next.resize(N);
    for (u32 i = 0; i < N; ++i) {
        u32 gx = static_cast<u32>(x_[i] / gs), gy = static_cast<u32>(y_[i] / gs);
        gx = gx >= gw ? gw - 1 : gx;
        gy = gy >= gh ? gh - 1 : gy;
        next[i] = head[static_cast<usize>(gy) * gw + gx];
        head[static_cast<usize>(gy) * gw + gx] = i;
    }
    const i32 far = static_cast<i32>(gw > gh ? gw : gh);
    for (usize c = 0; c < cells; ++c) {
        if (!land_[c])
            continue;
        const double px = (c % w + 0.5) / h, py = (c / w + 0.5) / h;
        i32 bx = static_cast<i32>(px / gs), by = static_cast<i32>(py / gs);
        bx = bx >= static_cast<i32>(gw) ? static_cast<i32>(gw) - 1 : bx;
        by = by >= static_cast<i32>(gh) ? static_cast<i32>(gh) - 1 : by;
        u32 best = kNo;
        double bd = 0;
        for (i32 r = 0; r <= far; ++r) {
            for (i32 gy = by - r; gy <= by + r; ++gy) {
                if (gy < 0 || gy >= static_cast<i32>(gh))
                    continue;
                // The ring's top and bottom rows whole, the rest by their two ends.
                const i32 step = (gy == by - r || gy == by + r || r == 0) ? 1 : 2 * r;
                for (i32 gx = bx - r; gx <= bx + r; gx += step) {
                    if (gx < 0 || gx >= static_cast<i32>(gw))
                        continue;
                    for (u32 o = head[static_cast<usize>(gy) * gw + gx]; o != kNo; o = next[o]) {
                        const double ex = x_[o] - px, ey = y_[o] - py, d2 = ex * ex + ey * ey;
                        if (best == kNo || d2 < bd || (d2 == bd && o < best)) {
                            best = o;
                            bd = d2;
                        }
                    }
                }
            }
            // A bucket of a later ring is at least r buckets away.
            if (best != kNo && bd <= r * gs * r * gs)
                break;
        }
        cell_place_[c] = best;
        ++catchment_[best];
    }

    // Each place's cells together, then the cells bordering them, each
    // counted once: those beside a cell of the catchment and not in it.
    Array<u32> cbegin, fill, list, mark;
    cbegin.resize(static_cast<usize>(N) + 1);
    cbegin.fill(0);
    for (u32 i = 0; i < N; ++i)
        cbegin[i + 1] = cbegin[i] + catchment_[i];
    fill.assign(cbegin.data(), N);
    list.resize(cbegin[N]);
    for (usize c = 0; c < cells; ++c)
        if (cell_place_[c] != kNoPlace)
            list[fill[cell_place_[c]]++] = static_cast<u32>(c);
    mark.resize(cells);
    mark.fill(kNo);
    for (u32 i = 0; i < N; ++i) {
        u32 around = 0, water = 0;
        u32 own = kNo; // a place with no catchment: round its own cell
        if (catchment_[i] == 0) {
            u32 ci = static_cast<u32>(x_[i] * h), cj = static_cast<u32>(y_[i] * h);
            ci = ci >= w ? w - 1 : ci;
            cj = cj >= h ? h - 1 : cj;
            own = cj * w + ci;
        }
        const u32 count = own != kNo ? 1 : catchment_[i];
        for (u32 j = 0; j < count; ++j) {
            const u32 c = own != kNo ? own : list[cbegin[i] + j];
            const u32 ci = c % w, cj = c / w;
            const u32 nbs[4] = {ci > 0 ? c - 1 : kNo, ci + 1 < w ? c + 1 : kNo, cj > 0 ? c - w : kNo,
                                cj + 1 < h ? c + w : kNo};
            for (u32 k = 0; k < 4; ++k) {
                const u32 q = nbs[k];
                if (q == kNo) { // past the map's edge: water
                    ++around;
                    ++water;
                } else if (own != kNo || (cell_place_[q] != i && mark[q] != i)) {
                    mark[q] = i;
                    ++around;
                    water += land_[q] ? 0 : 1;
                }
            }
        }
        water_share_[i] = around ? static_cast<double>(water) / around : 0;
    }
}

void Map::field_values(u64 seed, Field f, const MapSettings& s, Array<double>* out) const {
    out->resize(n_);
    if (!placed())
        return;
    FieldNoise fn;
    fn.init(seed, f, s.shape[static_cast<u32>(f)], s.field_scale);
    for (u32 i = 0; i < n_; ++i)
        (*out)[i] = fn.at(x_[i], y_[i]);
    scatter_values(*out, s.scatter, seed, f);
}

// --- respawning (7.8; build-plan-gate5.md step 6; map.h's head) ------------------

void NearTable::build(const Array<double>& values, double scatter_span) {
    const usize n = values.size();
    NOVA2_ASSERT(n < (1u << 24));
    span = scatter_span;
    Array<u64> keys;
    for (usize i = 0; i < n; ++i)
        keys.push((order_key(values[i]) & ~0xFFFFFFull) | i);
    sort_u64(keys.data(), n);
    v.resize(n);
    who.resize(n);
    for (usize r = 0; r < n; ++r) {
        who[r] = static_cast<u32>(keys[r] & 0xFFFFFF);
        v[r] = values[who[r]];
    }
    // The keys' low bits went to the index: put right any the sort left out
    // of order by them.
    for (usize r = 1; r < n; ++r)
        for (usize j = r; j > 0 && (v[j - 1] > v[j] || (v[j - 1] == v[j] && who[j - 1] > who[j])); --j) {
            const double tv = v[j];
            v[j] = v[j - 1];
            v[j - 1] = tv;
            const u32 tw = who[j];
            who[j] = who[j - 1];
            who[j - 1] = tw;
        }
}

u32 NearTable::nearest(double x, const u8* only) const {
    const usize n = v.size();
    usize lo = 0, hi = n; // the first at or above x
    while (lo < hi) {
        const usize mid = lo + (hi - lo) / 2;
        if (v[mid] < x)
            lo = mid + 1;
        else
            hi = mid;
    }
    usize below = n, above = n;
    for (usize j = lo; j > 0 && below == n; --j)
        if (!only || only[who[j - 1]])
            below = j - 1;
    for (usize j = lo; j < n && above == n; ++j)
        if (!only || only[who[j]])
            above = j;
    if (below == n && above == n)
        return kNoPlace;
    if (above == n)
        return who[below];
    if (below == n)
        return who[above];
    return x - v[below] <= v[above] - x ? who[below] : who[above];
}

// A field's values over the places as they stand, scattered as setup
// scatters them, with the scatter's reach.
void Map::place_table(Field f, NearTable* t) const {
    Array<double> raw, vals;
    raw.resize(n_);
    FieldNoise fn;
    fn.init(seed_, f, s_.shape[static_cast<u32>(f)], s_.field_scale);
    for (u32 i = 0; i < n_; ++i)
        raw[i] = fn.at(x_[i], y_[i]);
    field_values(seed_, f, s_, &vals);
    t->build(vals, scatter_span(raw, s_.scatter));
}

void Map::prepare_respawns() {
    if (ready_)
        return;
    ready_ = true;
    inc_.resize(0);
    inc_.resize(n_);
    respawned_ = respawns_ = 0;
    anchors_.clear();
    setup_data_.clear();
    setup_power_.clear();
    land_list_.clear();
    radius_.resize(0);
    radius_.resize(n_);
    radius_.fill(spacing_);
    cooling_dealt_.assign(cooling_.data(), cooling_.size());
    if (!placed())
        return;
    for (u32 k = 0; k < link_count(); ++k) {
        const MapLink& l = links_[k];
        if (l.bridge) {
            BridgeAnchor t;
            t.ax = x_[l.a];
            t.ay = y_[l.a];
            t.bx = x_[l.b];
            t.by = y_[l.b];
            t.ma = mass_[l.a];
            t.mb = mass_[l.b];
            anchors_.push(t);
        } else {
            setup_data_.get_or_insert(pair_key(l.a, l.b)) = l.bandwidth;
        }
    }
    for (u32 k = 0; k < power_link_count(); ++k)
        setup_power_.get_or_insert(pair_key(plinks_[k].a, plinks_[k].b)) = k + 1;
    for (u32 f = 0; f < kFieldCount; ++f)
        place_table(static_cast<Field>(f), &field_[f]);
    if (kind_ != MapKind::landscape)
        return;
    // The density over the land, as the darts had it, and each place's
    // spacing from its cell's.
    const u32 w = raster_w_, h = raster_h_;
    for (usize c = 0; c < land_.size(); ++c)
        if (land_[c])
            land_list_.push(static_cast<u32>(c));
    density_over_land(land_list_, &dens_, &cum_);
    for (u32 i = 0; i < n_; ++i) {
        u32 ci = static_cast<u32>(x_[i] * h), cj = static_cast<u32>(y_[i] * h);
        ci = ci >= w ? w - 1 : ci;
        cj = cj >= h ? h - 1 : cj;
        const u32 c = cj * w + ci;
        usize lo = 0, hi = land_list_.size();
        while (lo < hi) {
            const usize mid = lo + (hi - lo) / 2;
            if (land_list_[mid] < c)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < land_list_.size() && land_list_[lo] == c)
            radius_[i] = spacing_ / sqrt(dens_[lo]);
    }
}

double Map::field_now(Field f, u32 i) const {
    FieldNoise fn;
    fn.init(seed_, f, s_.shape[static_cast<u32>(f)], s_.field_scale);
    const u64 draw = Rng(seed_, i, kPurposeRespawnField, incarnation(i)).at(static_cast<u32>(f));
    const double u = static_cast<double>(draw >> 11) * 0x1.0p-53;
    return fn.at(x_[i], y_[i]) + field_[static_cast<u32>(f)].span * (2.0 * u - 1.0);
}

// A new link's scatter, -1 to 1: by its places and their incarnations.
// which: 0 its bandwidth's, 1 its resistance's.
double Map::link_scatter(u32 a, u32 b, u32 which) const {
    const u64 key = (static_cast<u64>(b) << 32) | (inc_[a] * 65537u + inc_[b]);
    const u64 draw = Rng(seed_, a, kPurposeRespawnLink, key).at(which);
    return 2.0 * (static_cast<double>(draw >> 11) * 0x1.0p-53) - 1.0;
}

// The place of a landmass nearest a point (the lower id of two as near), or
// kNo with nobody on it.
u32 Map::nearest_on(u32 mass, double x, double y) const {
    u32 best = kNo;
    double bd = 0;
    for (u32 i = 0; i < n_; ++i) {
        if (mass_[i] != mass)
            continue;
        const double ex = x_[i] - x, ey = y_[i] - y, d2 = ex * ex + ey * ey;
        if (best == kNo || d2 < bd) {
            best = i;
            bd = d2;
        }
    }
    return best;
}

// The bridges, from their anchors: each end at the place of its landmass
// nearest its anchor. Then every landmass with places that isn't left alone
// is joined to the rest by its shortest crossing, as setup's joins; such a
// crossing's ends are a new bridge's anchors.
void Map::bridge_links() {
    const u32 N = n_;
    Array<u32> count, parent;
    count.resize(masses_);
    for (u32 k = 0; k < N; ++k)
        ++count[mass_[k]];
    parent.resize(masses_);
    for (u32 m = 0; m < masses_; ++m)
        parent[m] = m;
    peopled_ = unbridged_ = 0;
    for (u32 m = 0; m < masses_; ++m) {
        peopled_ += count[m] ? 1 : 0;
        unbridged_ += lonely_[m] && count[m] ? 1 : 0;
    }
    bridges_ = 0;
    HashMap<u8> have;
    for (usize t = 0; t < anchors_.size(); ++t) {
        const BridgeAnchor& an = anchors_[t];
        const u32 a = nearest_on(an.ma, an.ax, an.ay), b = nearest_on(an.mb, an.bx, an.by);
        if (a == kNo || b == kNo)
            continue; // nobody on one of its landmasses now
        bool fresh = false;
        have.get_or_insert(pair_key(a, b), &fresh) = 1;
        if (!fresh)
            continue; // two bridges' ends have come to the same two places: one link
        add_link(a, b, true);
        ++bridges_;
        parent[find_root(parent, an.ma)] = find_root(parent, an.mb);
    }
    u32 apart = 0;
    for (u32 m = 0; m < masses_; ++m)
        apart += count[m] && !lonely_[m] && find_root(parent, m) == m ? 1 : 0;
    for (; apart > 1; --apart) {
        u32 ba = kNo, bb = kNo;
        double best = 0;
        for (u32 a = 0; a < N; ++a) {
            if (lonely_[mass_[a]])
                continue;
            for (u32 b = a + 1; b < N; ++b) {
                if (lonely_[mass_[b]] || find_root(parent, mass_[a]) == find_root(parent, mass_[b]))
                    continue;
                const double dd = dist(x_, y_, a, b);
                if (ba == kNo || dd < best) {
                    ba = a;
                    bb = b;
                    best = dd;
                }
            }
        }
        if (ba == kNo)
            break;
        BridgeAnchor an;
        an.ax = x_[ba];
        an.ay = y_[ba];
        an.bx = x_[bb];
        an.by = y_[bb];
        an.ma = mass_[ba];
        an.mb = mass_[bb];
        anchors_.push(an);
        add_link(ba, bb, true);
        ++bridges_;
        ++joins_;
        parent[find_root(parent, an.ma)] = find_root(parent, an.mb);
    }
}

// Both link sets, the catchments and the counts worked out again from the
// places as they stand, their incarnations and the bridges' anchors.
// `moved`: the place that respawned, or kNoPlace (a snapshot's load).
void Map::relink(u32 moved) {
    Array<MapLink> old;
    old.assign(links_.data(), links_.size());
    Array<PowerLink> pold;
    pold.assign(plinks_.data(), plinks_.size());
    const bool land = kind_ == MapKind::landscape;

    // --- the data links ---------------------------------------------------------
    if (land) {
        links_.clear();
        patches_ = 0;
        land_links();
        bridge_links();
        sort_links();
    }
    set_delays();
    FieldNoise bwf;
    bwf.init(seed_, Field::bandwidth, s_.shape[static_cast<u32>(Field::bandwidth)], s_.field_scale);
    for (u32 k = 0; k < link_count(); ++k) {
        MapLink& l = links_[k];
        if (l.bridge) {
            l.bandwidth = s_.bridge_bandwidth;
            continue;
        }
        const u32* dealt = !inc_[l.a] && !inc_[l.b] ? setup_data_.find(pair_key(l.a, l.b)) : nullptr;
        if (dealt) {
            l.bandwidth = *dealt;
            continue;
        }
        const double v = bwf.at(0.5 * (x_[l.a] + x_[l.b]), 0.5 * (y_[l.a] + y_[l.b])) +
                         bandwidth_table_.span * link_scatter(l.a, l.b, 0);
        const u32 bw = bandwidth_table_.nearest(v);
        l.bandwidth = bw != kNoPlace ? bw : (s_.bandwidth_min ? s_.bandwidth_min : 1);
    }
    index_links();

    // --- the proximity links ------------------------------------------------------
    Array<u64> kept;
    if (land) {
        gabriel_all(&kept);
    } else {
        for (usize k = 0; k < links_.size(); ++k)
            kept.push(pair_key(links_[k].a, links_[k].b));
    }
    sort_u64(kept.data(), kept.size());
    const u32 E = static_cast<u32>(kept.size());
    HashMap<u32> data_at; // a pair's data link on land, + 1
    u32 on_land = 0;
    for (u32 k = 0; k < link_count(); ++k)
        if (!links_[k].bridge) {
            data_at.get_or_insert(pair_key(links_[k].a, links_[k].b)) = k + 1;
            ++on_land;
        }
    FieldNoise rf;
    rf.init(seed_, Field::resistance, s_.shape[static_cast<u32>(Field::resistance)], s_.field_scale);
    plinks_.clear();
    power_wet_ = borders_ = 0;
    u32 both = 0;
    for (u32 k = 0; k < E; ++k) {
        PowerLink l;
        l.a = static_cast<u32>(kept[k] >> 32);
        l.b = static_cast<u32>(kept[k]);
        l.length = dist(x_, y_, l.a, l.b);
        const u32* d = data_at.find(kept[k]);
        l.data = d ? *d - 1 : kNoLink;
        both += d ? 1 : 0;
        l.water = land ? water_under(l.a, l.b) : 0;
        power_wet_ += l.water > 0 ? 1 : 0;
        l.border = rf.high(x_[l.a], y_[l.a]) != rf.high(x_[l.b], y_[l.b]);
        borders_ += l.border ? 1 : 0;
        const u32* dealt = !inc_[l.a] && !inc_[l.b] ? setup_power_.find(kept[k]) : nullptr;
        if (dealt) {
            l.per_length = resistance_dealt_[*dealt - 1];
        } else {
            const double v = rf.at(0.5 * (x_[l.a] + x_[l.b]), 0.5 * (y_[l.a] + y_[l.b])) +
                             resistance_table_.span * link_scatter(l.a, l.b, 1);
            const u32 who = resistance_table_.nearest(v);
            l.per_length = who != kNoPlace ? resistance_dealt_[who] : 1;
        }
        set_resistance(l);
        plinks_.push(l);
    }
    power_only_ = E - both;
    data_only_ = on_land - both;
    index_power();
    if (land)
        catchments();

    // --- where each link the map had is now ------------------------------------------
    HashMap<u32> now;
    for (u32 k = 0; k < link_count(); ++k)
        now.get_or_insert(pair_key(links_[k].a, links_[k].b)) = k + 1;
    data_moved_.resize(old.size());
    for (usize k = 0; k < old.size(); ++k) {
        const MapLink& o = old[k];
        const u32* at = o.a == moved || o.b == moved ? nullptr : now.find(pair_key(o.a, o.b));
        data_moved_[k] = at && links_[*at - 1].bridge == o.bridge ? *at - 1 : kNoLink;
    }
    now.clear();
    for (u32 k = 0; k < power_link_count(); ++k)
        now.get_or_insert(pair_key(plinks_[k].a, plinks_[k].b)) = k + 1;
    power_moved_.resize(pold.size());
    for (usize k = 0; k < pold.size(); ++k) {
        const PowerLink& o = pold[k];
        const u32* at = o.a == moved || o.b == moved ? nullptr : now.find(pair_key(o.a, o.b));
        power_moved_[k] = at ? *at - 1 : kNoLink;
    }
}

bool Map::respawn(u32 i) {
    prepare_respawns();
    respawned_ += inc_[i] ? 0 : 1;
    ++respawns_;
    const u32 inc = ++inc_[i];
    bool moved = false;
    if (kind_ == MapKind::landscape && !cum_.empty()) {
        const u32 w = raster_w_, h = raster_h_;
        const u32 L = static_cast<u32>(land_list_.size());
        const double total = cum_[L - 1];
        Rng r(seed_, i, kPurposeRespawn, inc);
        for (u32 t = 0; t < kRespawnTries && !moved; ++t) {
            // A land cell by density, and a point in it, as a dart's.
            const double u = r.unit() * total;
            u32 lo = 0, hi = L - 1;
            while (lo < hi) {
                const u32 mid = lo + (hi - lo) / 2;
                if (cum_[mid] > u)
                    hi = mid;
                else
                    lo = mid + 1;
            }
            const u32 c = land_list_[lo];
            const double ux = r.unit();
            const double uy = r.unit();
            const double x = (c % w + ux) / h, y = (c / w + uy) / h;
            const double rp = spacing_ / sqrt(dens_[lo]);
            bool ok = true;
            for (u32 o = 0; o < n_ && ok; ++o) {
                if (o == i)
                    continue;
                const double ex = x_[o] - x, ey = y_[o] - y, lim = 0.5 * (rp + radius_[o]);
                ok = ex * ex + ey * ey >= lim * lim;
            }
            if (!ok)
                continue;
            u32 ci = static_cast<u32>(x * h), cj = static_cast<u32>(y * h);
            ci = ci >= w ? w - 1 : ci;
            cj = cj >= h ? h - 1 : cj;
            const u32 m = cell_mass_[static_cast<usize>(cj) * w + ci];
            x_[i] = x;
            y_[i] = y;
            mass_[i] = m != kNo ? m : cell_mass_[c];
            radius_[i] = rp;
            moved = true;
        }
    }
    relink(i);
    // Its cooling: the one setup dealt to the cooling field value nearest its
    // own (7.3).
    if (placed()) {
        const u32 who = nearest_dealt(Field::cooling, field_now(Field::cooling, i));
        if (who != kNoPlace)
            cooling_[i] = cooling_dealt_[who];
    }
    return moved;
}

void Map::serialize_respawns(Archive& ar) {
    ar.tag(0x52535031); // "RSP1"
    u32 count = respawned_;
    ar.u32_(count);
    if (!ar.loading()) {
        for (u32 i = 0; i < n_ && count; ++i) {
            if (!inc_[i])
                continue;
            u32 id = i, inc = inc_[i];
            u64 bx = placed() ? bits_of(x_[i]) : 0, by = placed() ? bits_of(y_[i]) : 0; // no map: no points
            ar.u32_(id);
            ar.u32_(inc);
            ar.u64_(bx);
            ar.u64_(by);
        }
        u32 bridges = count ? static_cast<u32>(anchors_.size()) : 0;
        if (count)
            ar.u32_(bridges);
        for (u32 t = 0; t < bridges; ++t) {
            BridgeAnchor an = anchors_[t];
            u64 q[4] = {bits_of(an.ax), bits_of(an.ay), bits_of(an.bx), bits_of(an.by)};
            for (u32 j = 0; j < 4; ++j)
                ar.u64_(q[j]);
            ar.u32_(an.ma);
            ar.u32_(an.mb);
        }
        ar.mark("map: respawns (gate 5)");
        return;
    }
    // Loading: onto the map as setup left it.
    if (!ar.ok() || respawned_ != 0 || count > n_) {
        ar.fail();
        return;
    }
    if (count == 0) {
        ar.mark("map: respawns (gate 5)");
        return;
    }
    prepare_respawns();
    const u32 w = raster_w_, h = raster_h_;
    u32 last = 0;
    for (u32 j = 0; j < count && ar.ok(); ++j) {
        u32 id = 0, inc = 0;
        u64 bx = 0, by = 0;
        ar.u32_(id);
        ar.u32_(inc);
        ar.u64_(bx);
        ar.u64_(by);
        const double x = of_bits(bx), y = of_bits(by);
        if (!ar.ok() || id >= n_ || (j && id <= last) || inc == 0 ||
            (placed() ? !(x >= 0) || !(y >= 0) || !(x <= width_) || !(y <= 1) : (bx | by) != 0)) {
            ar.fail();
            return;
        }
        last = id;
        inc_[id] = inc;
        respawns_ += inc;
        if (!placed())
            continue;
        if (kind_ == MapKind::landscape && (x != x_[id] || y != y_[id])) {
            u32 ci = static_cast<u32>(x * h), cj = static_cast<u32>(y * h);
            ci = ci >= w ? w - 1 : ci;
            cj = cj >= h ? h - 1 : cj;
            const u32 c = cj * w + ci;
            usize lo = 0, hi = land_list_.size();
            while (lo < hi) {
                const usize mid = lo + (hi - lo) / 2;
                if (land_list_[mid] < c)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo >= land_list_.size() || land_list_[lo] != c) {
                ar.fail(); // not on land
                return;
            }
            x_[id] = x;
            y_[id] = y;
            mass_[id] = cell_mass_[c];
            radius_[id] = spacing_ / sqrt(dens_[lo]);
        } else if (x != x_[id] || y != y_[id]) {
            ar.fail(); // a test map's places don't move
            return;
        }
    }
    u32 bridges = 0;
    ar.u32_(bridges);
    if (!ar.ok() || bridges < anchors_.size() || bridges > anchors_.size() + count * 4ull + masses_) {
        ar.fail();
        return;
    }
    anchors_.clear();
    for (u32 t = 0; t < bridges && ar.ok(); ++t) {
        BridgeAnchor an;
        u64 q[4] = {};
        for (u32 j = 0; j < 4; ++j)
            ar.u64_(q[j]);
        ar.u32_(an.ma);
        ar.u32_(an.mb);
        an.ax = of_bits(q[0]);
        an.ay = of_bits(q[1]);
        an.bx = of_bits(q[2]);
        an.by = of_bits(q[3]);
        if (!ar.ok() || an.ma >= masses_ || an.mb >= masses_ || an.ma == an.mb) {
            ar.fail();
            return;
        }
        anchors_.push(an);
    }
    ar.mark("map: respawns (gate 5)");
    if (!ar.ok())
        return;
    respawned_ = count;
    relink(kNoPlace);
    for (u32 i = 0; i < n_ && placed(); ++i) {
        if (!inc_[i])
            continue;
        const u32 who = nearest_dealt(Field::cooling, field_now(Field::cooling, i));
        if (who != kNoPlace)
            cooling_[i] = cooling_dealt_[who];
    }
}

u64 Map::respawn_hash() const {
    Hasher h(0x72657370ull); // "resp"
    h.word(respawned_);
    if (!respawned_)
        return h.finish();
    for (u32 i = 0; i < n_; ++i) {
        if (!inc_[i])
            continue;
        h.word((static_cast<u64>(i) << 32) | inc_[i]);
        if (!placed())
            continue; // no map: no points
        h.word(bits_of(x_[i]));
        h.word(bits_of(y_[i]));
    }
    h.word(anchors_.size());
    for (usize t = 0; t < anchors_.size(); ++t) {
        const BridgeAnchor& an = anchors_[t];
        h.word(bits_of(an.ax));
        h.word(bits_of(an.ay));
        h.word(bits_of(an.bx));
        h.word(bits_of(an.by));
        h.word((static_cast<u64>(an.ma) << 32) | an.mb);
    }
    return h.finish();
}

namespace {

bool map_broken(TextBuf* why, const char* what, u32 a, u32 b) {
    if (why) {
        why->append(what);
        why->append(": places ");
        why->append_u64(a);
        why->append(" and ");
        why->append_u64(b);
    }
    return false;
}

// Whether two links cross, other than at a place they share.
bool links_cross(const Array<double>& X, const Array<double>& Y, u32 pa, u32 pb, u32 qa, u32 qb) {
    if (pa == qa || pa == qb || pb == qa || pb == qb)
        return false;
    const double o1 = orient(X, Y, pa, pb, qa), o2 = orient(X, Y, pa, pb, qb);
    const double o3 = orient(X, Y, qa, qb, pa), o4 = orient(X, Y, qa, qb, pb);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

} // namespace

bool Map::check_links(bool brute, TextBuf* why) const {
    if (kind_ != MapKind::landscape)
        return true;
    const u32 N = n_;
    // No other place (of the same landmass, with `mass`) in the closed circle on a and b.
    struct Rule {
        const Map* m;
        bool clear(u32 a, u32 b, bool mass) const {
            for (u32 c = 0; c < m->n_; ++c) {
                if (c == a || c == b || (mass && m->mass_[c] != m->mass_[a]))
                    continue;
                if ((m->x_[a] - m->x_[c]) * (m->x_[b] - m->x_[c]) + (m->y_[a] - m->y_[c]) * (m->y_[b] - m->y_[c]) <= 0)
                    return false;
            }
            return true;
        }
    };
    const Rule rule{this};
    HashMap<u8> have; // 1: a proximity link, 2: a link on land, 4: a bridge
    for (u32 k = 0; k < power_link_count(); ++k) {
        const PowerLink& l = plinks_[k];
        if (l.a >= l.b || l.b >= N)
            return map_broken(why, "the map: a proximity link's places are out of order", l.a, l.b);
        u8& f = have.get_or_insert(pair_key(l.a, l.b));
        if (f & 1)
            return map_broken(why, "the map: two proximity links between", l.a, l.b);
        f |= 1;
        if (!rule.clear(l.a, l.b, false))
            return map_broken(why, "the map: a proximity link with a place in its circle", l.a, l.b);
    }
    u32 off_rule = 0;
    for (u32 k = 0; k < link_count(); ++k) {
        const MapLink& l = links_[k];
        if (l.a >= l.b || l.b >= N)
            return map_broken(why, "the map: a link's places are out of order", l.a, l.b);
        u8& f = have.get_or_insert(pair_key(l.a, l.b));
        if (f & 6)
            return map_broken(why, "the map: two data links between", l.a, l.b);
        f |= l.bridge ? 4 : 2;
        if (l.bridge != (mass_[l.a] != mass_[l.b]))
            return map_broken(why, "the map: a link on land between landmasses, or a bridge within one", l.a, l.b);
        if (l.bridge && (lonely_[mass_[l.a]] || lonely_[mass_[l.b]]))
            return map_broken(why, "the map: a bridge to a landmass left alone", l.a, l.b);
        if (!l.bridge && !rule.clear(l.a, l.b, true))
            ++off_rule; // a degenerate landmass's patch
    }
    if (off_rule > patches_)
        return map_broken(why, "the map: links on land with a place of their landmass in their circle", off_rule,
                          patches_);
    if (brute) {
        for (u32 a = 0; a < N; ++a)
            for (u32 b = a + 1; b < N; ++b) {
                const u8* f = have.find(pair_key(a, b));
                const u8 got = f ? *f : 0;
                if (rule.clear(a, b, false) != ((got & 1) != 0))
                    return map_broken(why, "the map: the proximity links aren't the Gabriel graph", a, b);
                if (mass_[a] == mass_[b] && rule.clear(a, b, true) && !(got & 2))
                    return map_broken(why, "the map: a landmass's links miss a Gabriel pair", a, b);
            }
        for (u32 k = 0; k < link_count(); ++k)
            for (u32 q = k + 1; q < link_count(); ++q)
                if (!links_[k].bridge && !links_[q].bridge &&
                    links_cross(x_, y_, links_[k].a, links_[k].b, links_[q].a, links_[q].b))
                    return map_broken(why, "the map: two links on land cross", links_[k].a, links_[q].a);
        for (u32 k = 0; k < power_link_count(); ++k)
            for (u32 q = k + 1; q < power_link_count(); ++q)
                if (links_cross(x_, y_, plinks_[k].a, plinks_[k].b, plinks_[q].a, plinks_[q].b))
                    return map_broken(why, "the map: two proximity links cross", plinks_[k].a, plinks_[q].a);
    }
    // Every landmass one piece by its links on land; and every landmass with
    // places that isn't left alone joined to the rest.
    Array<u32> parent, count;
    parent.resize(N);
    for (u32 i = 0; i < N; ++i)
        parent[i] = i;
    for (u32 k = 0; k < link_count(); ++k)
        if (!links_[k].bridge)
            parent[find_root(parent, links_[k].a)] = find_root(parent, links_[k].b);
    Array<u32> first; // a landmass's first place
    first.resize(masses_);
    first.fill(kNo);
    count.resize(masses_);
    for (u32 i = 0; i < N; ++i) {
        const u32 m = mass_[i];
        if (m >= masses_)
            return map_broken(why, "the map: a place on no landmass", i, m);
        ++count[m];
        if (first[m] == kNo)
            first[m] = i;
        else if (find_root(parent, i) != find_root(parent, first[m]))
            return map_broken(why, "the map: a landmass in two pieces", first[m], i);
    }
    for (u32 k = 0; k < link_count(); ++k)
        if (links_[k].bridge)
            parent[find_root(parent, links_[k].a)] = find_root(parent, links_[k].b);
    u32 joined = kNo;
    for (u32 m = 0; m < masses_; ++m) {
        if (!count[m] || lonely_[m])
            continue;
        if (joined == kNo)
            joined = first[m];
        else if (find_root(parent, first[m]) != find_root(parent, joined))
            return map_broken(why, "the map: a landmass with no bridge to the rest", joined, first[m]);
    }
    // The places at each link's ends are each other's, and in order.
    for (u32 i = 0; i < N; ++i) {
        for (u32 j = 0; j < degree(i); ++j) {
            const u32 k = link_at(i, j);
            if (k >= link_count() || (links_[k].a != i && links_[k].b != i) ||
                (j && other(link_at(i, j - 1), i) >= other(k, i)))
                return map_broken(why, "the map: a place's links are out of order", i, j);
        }
        for (u32 j = 0; j < power_degree(i); ++j) {
            const u32 k = power_link_at(i, j);
            if (k >= power_link_count() || (plinks_[k].a != i && plinks_[k].b != i) ||
                (j && power_other(power_link_at(i, j - 1), i) >= power_other(k, i)))
                return map_broken(why, "the map: a place's proximity links are out of order", i, j);
        }
    }
    return true;
}

u64 Map::hash() const {
    Hasher h(0x6D6170ull); // "map"
    h.word(static_cast<u64>(kind_));
    h.word(n_);
    h.word(bits_of(width_));
    for (u32 i = 0; i < x_.size(); ++i) {
        h.word(bits_of(x_[i]));
        h.word(bits_of(y_[i]));
        h.word(mass_[i]);
    }
    h.word(links_.size());
    for (usize k = 0; k < links_.size(); ++k) {
        const MapLink& l = links_[k];
        h.word((static_cast<u64>(l.a) << 32) | l.b);
        h.word(bits_of(l.length));
        h.word((static_cast<u64>(l.bandwidth) << 32) | l.delay);
        h.word(l.bridge ? 1 : 0);
    }
    return h.finish();
}

u64 Map::power_hash() const {
    Hasher h(0x706F776572ull); // "power"
    h.word(plinks_.size());
    for (usize k = 0; k < plinks_.size(); ++k) {
        const PowerLink& l = plinks_[k];
        h.word((static_cast<u64>(l.a) << 32) | l.b);
        h.word((static_cast<u64>(l.data) << 1) | (l.border ? 1 : 0));
        h.word(bits_of(l.length));
        h.word(bits_of(l.water));
        h.word(bits_of(l.per_length));
        h.word(bits_of(l.resistance));
    }
    h.word(catchment_.size());
    for (usize i = 0; i < catchment_.size(); ++i) {
        h.word(catchment_[i]);
        h.word(bits_of(water_share_[i]));
        h.word(bits_of(cooling_[i]));
    }
    return h.finish();
}

void Map::describe_power(TextBuf* t) const {
    char line[400];
    if (!placed()) {
        t->append("power map: none");
        return;
    }
    const u32 E = power_link_count();
    u32 lo = n_ ? kNo : 0, hi = 0, bare = 0;
    double shore = 0;
    for (u32 i = 0; i < n_; ++i) {
        lo = power_degree(i) < lo ? power_degree(i) : lo;
        hi = power_degree(i) > hi ? power_degree(i) : hi;
        bare += catchment_[i] ? 0 : 1;
        shore += water_share_[i];
    }
    double least = 0, most = 0;
    for (u32 k = 0; k < E; ++k) {
        least = k == 0 || plinks_[k].resistance < least ? plinks_[k].resistance : least;
        most = k == 0 || plinks_[k].resistance > most ? plinks_[k].resistance : most;
    }
    char borders[64];
    if (border_strength_ > 0)
        snprintf(borders, sizeof borders, "strength %g links", border_strength_);
    else
        snprintf(borders, sizeof borders, "borders off");
    snprintf(line, sizeof line,
             "power map: %u proximity links, %.2f a place (%u to %u); %u over water, %u power only, %u data links "
             "on land with none; %u cross a grid border (%s); a link's resistance %.3g to %.3g links; water is "
             "%.0f%% of what borders a catchment, %u places with none",
             E, n_ ? 2.0 * E / n_ : 0.0, lo, hi, power_wet_, power_only_, data_only_, borders_, borders, least, most,
             n_ ? 100.0 * shore / n_ : 0.0, bare);
    t->append(line);
}

void Map::describe(TextBuf* t) const {
    static const char* const kKinds[4] = {"none", "landscape", "grid", "ring"};
    char line[320];
    if (!placed()) {
        snprintf(line, sizeof line, "map: none, %u places with no links", n_);
        t->append(line);
        return;
    }
    u32 lo = n_ ? kNo : 0, hi = 0;
    for (u32 i = 0; i < n_; ++i) {
        lo = degree(i) < lo ? degree(i) : lo;
        hi = degree(i) > hi ? degree(i) : hi;
    }
    u32 slowest = 0, narrowest = kNo, widest = 0;
    for (usize k = 0; k < links_.size(); ++k) {
        slowest = links_[k].delay > slowest ? links_[k].delay : slowest;
        narrowest = links_[k].bandwidth < narrowest ? links_[k].bandwidth : narrowest;
        widest = links_[k].bandwidth > widest ? links_[k].bandwidth : widest;
    }
    snprintf(line, sizeof line,
             "map: %s, %u places on %u landmasses (of %u), %u links, %.2f a place (%u to %u); %u bridges (%u joins), "
             "%u landmasses unbridged; average link length %.4f, delays up to %u ticks, bandwidth %u to %u",
             kKinds[static_cast<u32>(kind_)], n_, peopled_, masses_, link_count(),
             n_ ? 2.0 * link_count() / n_ : 0.0, lo, hi, bridges_, joins_, unbridged_, mean_length_, slowest,
             links_.size() ? narrowest : 0, widest);
    t->append(line);
}

} // namespace nova2
