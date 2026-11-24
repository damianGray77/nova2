// The map (design.md 6.1, 6.2, 7.3; build-plan-gate3.md step 2). A landscape
// map's places all sit on land, apart; its links on land join places of one
// landmass, never cross, and are exactly the Gabriel graph of each landmass
// (checked by brute force against every pair); its bridges join different
// landmasses, and every landmass is joined to the rest. Each link's delay
// and bandwidth follow 11.3. The map is a pure function of the seed, and a
// map the places don't fit is refused. The test maps, grid and ring, are as
// drawn. And with the fields, the world's mix of hardware and OS values is
// gate 2's draws exactly, only placed differently, so neighbours are more
// alike; with no map, or the fields off, each place's values are its own
// draws, as at gate 2.
//
// The power map (build-plan-gate4.md step 2): the proximity links are the
// Gabriel graph over all the places, by brute force, with no crossings and
// in one piece; each has the data link on land beside it or none, and every
// data link on land is a proximity link or counted as a difference; a link's
// water width is what a walk from cell to cell finds, none at all for a link
// wholly over land; its border and resistance are as 6.1 and 6.2 give them;
// every land cell is in the catchment of the place nearest it, and a place's
// water share is counted cell by cell. And gate 3's 20 maps hash as they
// did before the power map was added.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h> // __rdtsc

#include "check.h"
#include "nova2/hash.h"
#include "nova2/map.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

bool build_map(Map& m, u64 seed, u32 places, const char* text, TextBuf* why) {
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(text, &pe));
    MapSettings ms;
    read_map_settings(cfg, &ms);
    CHECK(cfg.ok());
    return m.build(seed, places, ms, why);
}

MapSettings default_settings() {
    Config cfg;
    MapSettings ms;
    read_map_settings(cfg, &ms);
    return ms;
}

double orient(const Map& m, u32 a, u32 b, u32 c) {
    return (m.x(b) - m.x(a)) * (m.y(c) - m.y(a)) - (m.y(b) - m.y(a)) * (m.x(c) - m.x(a));
}

// Whether two links cross, other than at a place they share.
bool crosses(const Map& m, const MapLink& p, const MapLink& q) {
    if (p.a == q.a || p.a == q.b || p.b == q.a || p.b == q.b)
        return false;
    const double o1 = orient(m, p.a, p.b, q.a), o2 = orient(m, p.a, p.b, q.b);
    const double o3 = orient(m, q.a, q.b, p.a), o4 = orient(m, q.a, q.b, p.b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

// The link between two places, or ~0.
u32 link_between(const Map& m, u32 a, u32 b) {
    for (u32 j = 0; j < m.degree(a); ++j) {
        const u32 k = m.link_at(a, j);
        if (m.other(k, a) == b)
            return k;
    }
    return ~0u;
}

u32 root(Array<u32>& parent, u32 v) {
    while (parent[v] != v)
        v = parent[v] = parent[parent[v]];
    return v;
}

u32 pieces(const Map& m) {
    Array<u32> parent;
    parent.resize(m.place_count());
    for (u32 i = 0; i < m.place_count(); ++i)
        parent[i] = i;
    u32 n = m.place_count();
    for (u32 k = 0; k < m.link_count(); ++k) {
        const u32 a = root(parent, m.link(k).a), b = root(parent, m.link(k).b);
        if (a != b) {
            parent[a] = b;
            --n;
        }
    }
    return n;
}

// No other place of the landmass in the closed circle on a and b.
bool gabriel(const Map& m, u32 a, u32 b) {
    for (u32 c = 0; c < m.place_count(); ++c) {
        if (c == a || c == b || m.landmass(c) != m.landmass(a))
            continue;
        if ((m.x(a) - m.x(c)) * (m.x(b) - m.x(c)) + (m.y(a) - m.y(c)) * (m.y(b) - m.y(c)) <= 0)
            return false;
    }
    return true;
}

// Every check on a landscape map, with the default settings.
void check_landscape(u64 seed, u32 places, bool brute) {
    Map m;
    TextBuf why;
    CHECK(build_map(m, seed, places, "", &why));
    const MapSettings s = default_settings();
    const u32 n = m.place_count();
    CHECK(n == places && m.placed() && m.kind() == MapKind::landscape);
    // Places: on land, on their cell's landmass, no two closer than a
    // quarter of a cell.
    const u32 w = m.raster_w(), h = m.raster_h();
    double closest = 1e9;
    u32 off_land = 0;
    for (u32 i = 0; i < n; ++i) {
        u32 cx = static_cast<u32>(m.x(i) * h), cy = static_cast<u32>(m.y(i) * h);
        cx = cx >= w ? w - 1 : cx;
        cy = cy >= h ? h - 1 : cy;
        off_land += m.land(cx, cy) ? 0 : 1;
        CHECK(m.landmass(i) < m.landmasses());
        for (u32 j = i + 1; j < n; ++j) {
            const double dx = m.x(i) - m.x(j), dy = m.y(i) - m.y(j);
            const double d = sqrt(dx * dx + dy * dy);
            closest = d < closest ? d : closest;
        }
    }
    CHECK(off_land == 0);
    CHECK(closest >= 0.25 / h);
    // Links: on land within a landmass, bridges between; none crossing on land.
    u32 bad_ends = 0, crossing = 0, land_links = 0, narrowest = ~0u, widest = 0, bad_delay = 0;
    for (u32 k = 0; k < m.link_count(); ++k) {
        const MapLink& l = m.link(k);
        CHECK(l.a < l.b && l.b < n);
        const bool same = m.landmass(l.a) == m.landmass(l.b);
        bad_ends += l.bridge == same ? 1 : 0;
        const double ticks = ceil(s.delay_hop + s.delay_length * l.length / m.mean_length() - 1e-9);
        bad_delay += l.delay != static_cast<u32>(ticks < 1 ? 1 : ticks) ? 1 : 0;
        if (l.bridge) {
            CHECK(l.bandwidth == s.bridge_bandwidth);
            continue;
        }
        ++land_links;
        narrowest = l.bandwidth < narrowest ? l.bandwidth : narrowest;
        widest = l.bandwidth > widest ? l.bandwidth : widest;
        for (u32 q = k + 1; q < m.link_count() && brute; ++q)
            if (!m.link(q).bridge && crosses(m, l, m.link(q)))
                ++crossing;
    }
    CHECK(bad_ends == 0 && bad_delay == 0);
    CHECK(crossing == 0);
    if (land_links >= 2)
        CHECK(narrowest == s.bandwidth_min && widest == s.bandwidth_max);
    // Each place's links in order of the place at their other end, and each
    // link at both its places.
    u32 ends = 0;
    for (u32 i = 0; i < n; ++i) {
        for (u32 j = 0; j < m.degree(i); ++j) {
            const u32 k = m.link_at(i, j);
            CHECK(m.link(k).a == i || m.link(k).b == i);
            if (j > 0)
                CHECK(m.other(m.link_at(i, j - 1), i) < m.other(k, i));
        }
        ends += m.degree(i);
    }
    CHECK(ends == 2 * m.link_count());
    // One piece: every landmass joined to the rest.
    CHECK(pieces(m) == 1);
    // The links on land are each landmass's Gabriel graph, by brute force:
    // every Gabriel pair linked, and every link on land Gabriel (but those
    // joining a degenerate landmass's pieces).
    u32 missing = 0, extra = 0;
    if (brute) {
        for (u32 a = 0; a < n; ++a)
            for (u32 b = a + 1; b < n; ++b) {
                if (m.landmass(a) != m.landmass(b))
                    continue;
                const u32 k = link_between(m, a, b);
                const bool g = gabriel(m, a, b);
                missing += g && k == ~0u ? 1 : 0;
                extra += !g && k != ~0u ? 1 : 0;
            }
        CHECK(missing == 0);
        CHECK(extra <= m.patches());
    }
    TextBuf t;
    m.describe(&t);
    printf("  map: seed %llu: %.*s\n", static_cast<unsigned long long>(seed), static_cast<int>(t.view().n),
           t.view().p);
}

void test_landscape() {
    for (u64 seed = 1; seed <= 6; ++seed)
        check_landscape(seed, 48, true);
    check_landscape(7, 1000, true);
}

// The same seed, the same map; another seed, another.
void test_pure() {
    Map a, b, c;
    TextBuf why;
    CHECK(build_map(a, 5, 200, "", &why) && build_map(b, 5, 200, "", &why) && build_map(c, 6, 200, "", &why));
    CHECK_EQ_U64(a.hash(), b.hash());
    CHECK(a.hash() != c.hash());
    // A setting that shapes it changes it.
    Map d;
    CHECK(build_map(d, 5, 200, "map.land.share = 0.4\n", &why));
    CHECK(d.hash() != a.hash());
}

// Places that don't fit are refused, with the reason.
void test_refused() {
    Map m;
    TextBuf why;
    CHECK(!build_map(m, 1, 100000, "map.land.resolution = 16\n", &why));
    CHECK(strstr(why.c_str(), "don't fit") != nullptr);
}

void test_grid_and_ring() {
    Map g;
    TextBuf why;
    CHECK(build_map(g, 1, 12, "world.map = grid\n", &why));
    // 4 across, 3 down: 9 links along the rows and 8 down the columns.
    CHECK(g.kind() == MapKind::grid && g.link_count() == 17 && g.landmasses() == 1 && pieces(g) == 1);
    CHECK(g.degree(0) == 2 && g.degree(5) == 4 && g.degree(11) == 2);
    CHECK(link_between(g, 0, 1) != ~0u && link_between(g, 0, 4) != ~0u && link_between(g, 3, 4) == ~0u);
    for (u32 k = 0; k < g.link_count(); ++k)
        CHECK(g.link(k).delay == 2 && !g.link(k).bridge && fabs(g.link(k).length - g.mean_length()) < 1e-12);
    CHECK(g.x(1) > g.x(0) && g.y(4) > g.y(0));
    // A partial last row.
    Map g2;
    CHECK(build_map(g2, 1, 10, "world.map = grid\n", &why));
    CHECK(g2.link_count() == 13 && pieces(g2) == 1);
    Map r;
    CHECK(build_map(r, 1, 10, "world.map = ring\n", &why));
    CHECK(r.kind() == MapKind::ring && r.link_count() == 10 && pieces(r) == 1);
    for (u32 i = 0; i < 10; ++i)
        CHECK(r.degree(i) == 2 && link_between(r, i, (i + 1) % 10) != ~0u);
    Map r2, r1;
    CHECK(build_map(r2, 1, 2, "world.map = ring\n", &why) && r2.link_count() == 1);
    CHECK(build_map(r1, 1, 1, "world.map = ring\n", &why) && r1.link_count() == 0);
    Map none;
    CHECK(build_map(none, 1, 5, "world.map = none\n", &why));
    CHECK(!none.placed() && none.link_count() == 0 && none.degree(4) == 0);
}

// --- the fields (7.3) ---------------------------------------------------------

const char* const kSmallPlaces = "memory.size = 32768\n"
                                 "disk.size = 65536\n";

struct Values {
    u64 cores_kinds;   // the core count and which are E cores
    u64 devices_lats;  // the memory device count and their latencies, in order
    u32 cores, devices, page;
    bool strict;
};

Values values_of(Place& p) {
    Values v{};
    v.cores = p.core_count();
    Hasher hc(1);
    hc.word(v.cores);
    for (u32 c = 0; c < v.cores; ++c)
        hc.word(p.cores[c].kind == DeviceKind::e_core ? 1 : 0);
    v.cores_kinds = hc.finish();
    Hasher hd(2);
    for (usize i = 0; i < p.env.devices.size(); ++i)
        if (p.env.devices[i].kind == DeviceKind::memory) {
            ++v.devices;
            hd.word(p.env.devices[i].speed);
        }
    hd.word(v.devices);
    v.devices_lats = hd.finish();
    v.page = p.mem.page_size();
    v.strict = p.mem.strict();
    return v;
}

bool build_world(World& w, const char* text) {
    Config cfg;
    Config::ParseError pe;
    TextBuf why;
    return cfg.parse(text, &pe) && w.init(cfg, &why);
}

// The sorted values of one kind across the places.
void sorted_of(Array<Values>& v, u32 which, Array<u64>* out) {
    out->clear();
    for (usize i = 0; i < v.size(); ++i) {
        const Values& x = v[i];
        out->push(which == 0   ? x.cores_kinds
                  : which == 1 ? x.devices_lats
                  : which == 2 ? x.page
                               : (x.strict ? 1 : 0));
    }
    sort_u64(out->data(), out->size());
}

void test_fields() {
    char text[512];
    const u32 n = 300;
    snprintf(text, sizeof text, "world.places = %u\n%s", n, kSmallPlaces);
    World on, none;
    CHECK(build_world(on, text));
    snprintf(text, sizeof text, "world.places = %u\nworld.map = none\n%s", n, kSmallPlaces);
    CHECK(build_world(none, text));
    snprintf(text, sizeof text, "world.places = %u\nmap.fields = false\n%s", n, kSmallPlaces);
    World off;
    CHECK(build_world(off, text));
    snprintf(text, sizeof text, "world.places = %u\nworld.map = none\nmap.fields = false\n%s", n, kSmallPlaces);
    World none_off;
    CHECK(build_world(none_off, text));
    Array<Values> a, b, c;
    u32 moved = 0, same_off = 0;
    for (u32 i = 0; i < n; ++i) {
        a.push(values_of(on.place(i)));
        b.push(values_of(none.place(i)));
        c.push(values_of(off.place(i)));
        moved += a[i].cores_kinds != b[i].cores_kinds ? 1 : 0;
        same_off += memcmp(&b[i], &c[i], sizeof(Values)) == 0 ? 1 : 0;
    }
    // With the fields off, or no map, each place has its own draws.
    CHECK(same_off == n);
    CHECK_EQ_U64(none.hash(), none_off.hash());
    // With them, the same mix: the cores with their kinds, the memory
    // devices with their latencies, the page sizes, strict or lax.
    for (u32 which = 0; which < 4; ++which) {
        Array<u64> sa, sb;
        sorted_of(a, which, &sa);
        sorted_of(b, which, &sb);
        u32 differ = 0;
        for (u32 i = 0; i < n; ++i)
            differ += sa[i] != sb[i] ? 1 : 0;
        CHECK(differ == 0);
    }
    // Only placed differently, so places linked on the map are more alike.
    CHECK(moved > 0);
    const Map& m = on.map();
    double gap_on = 0, gap_off = 0;
    u32 strict_on = 0, strict_off = 0;
    for (u32 k = 0; k < m.link_count(); ++k) {
        const MapLink& l = m.link(k);
        gap_on += fabs(static_cast<double>(a[l.a].cores) - a[l.b].cores);
        gap_off += fabs(static_cast<double>(b[l.a].cores) - b[l.b].cores);
        strict_on += a[l.a].strict != a[l.b].strict ? 1 : 0;
        strict_off += b[l.a].strict != b[l.b].strict ? 1 : 0;
    }
    CHECK(gap_on < gap_off && strict_on < strict_off);
    printf("  fields: %u places, the same mix of each value as gate 2's draws; across the map's %u links, core "
           "counts differ by %.2f on average (%.2f drawn independently), and %u links join strict to lax (%u)\n",
           n, m.link_count(), gap_on / m.link_count(), gap_off / m.link_count(), strict_on, strict_off);
}

// The test worlds' sizes (14.1): 1,000 and 10,000 places.
void test_large() {
    const u32 sizes[2] = {1000, 10000};
    for (u32 s = 0; s < 2; ++s) {
        Map m;
        TextBuf why;
        const u64 t0 = __rdtsc();
        CHECK(build_map(m, 3, sizes[s], "", &why));
        const u64 t1 = __rdtsc();
        CHECK(m.place_count() == sizes[s] && pieces(m) == 1);
        u32 bad = 0;
        for (u32 k = 0; k < m.link_count(); ++k)
            bad += m.link(k).bridge == (m.landmass(m.link(k).a) == m.landmass(m.link(k).b)) ? 1 : 0;
        CHECK(bad == 0);
        TextBuf t;
        m.describe(&t);
        printf("  map: %u places, about %.0f M cycles: %.*s\n", sizes[s], static_cast<double>(t1 - t0) / 1e6,
               static_cast<int>(t.view().n), t.view().p);
    }
}

// --- the power map (6.1, 6.2; build-plan-gate4.md step 2) -------------------------

// The proximity link between two places, or ~0.
u32 power_between(const Map& m, u32 a, u32 b) {
    for (u32 j = 0; j < m.power_degree(a); ++j) {
        const u32 k = m.power_link_at(a, j);
        if (m.power_other(k, a) == b)
            return k;
    }
    return ~0u;
}

// No other place at all in the closed circle on a and b.
bool gabriel_all(const Map& m, u32 a, u32 b) {
    for (u32 c = 0; c < m.place_count(); ++c) {
        if (c == a || c == b)
            continue;
        if ((m.x(a) - m.x(c)) * (m.x(b) - m.x(c)) + (m.y(a) - m.y(c)) * (m.y(b) - m.y(c)) <= 0)
            return false;
    }
    return true;
}

bool power_crosses(const Map& m, const PowerLink& p, const PowerLink& q) {
    if (p.a == q.a || p.a == q.b || p.b == q.a || p.b == q.b)
        return false;
    const double o1 = orient(m, p.a, p.b, q.a), o2 = orient(m, p.a, p.b, q.b);
    const double o3 = orient(m, q.a, q.b, p.a), o4 = orient(m, q.a, q.b, p.b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool water_cell(const Map& m, i64 cx, i64 cy) {
    return cx < 0 || cy < 0 || cx >= m.raster_w() || cy >= m.raster_h() ||
           !m.land(static_cast<u32>(cx), static_cast<u32>(cy));
}

// The part of the line between two places that's over water, another way
// than the map's: stepping from each raster cell to the next one the line
// enters.
double water_walk(const Map& m, u32 a, u32 b) {
    const double h = m.raster_h();
    const double ax = m.x(a) * h, ay = m.y(a) * h, dx = m.x(b) * h - ax, dy = m.y(b) * h - ay;
    i64 cx = static_cast<i64>(floor(ax)), cy = static_cast<i64>(floor(ay));
    const i64 sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    double tx = dx != 0 ? (static_cast<double>(cx + (dx > 0 ? 1 : 0)) - ax) / dx : 2.0;
    double ty = dy != 0 ? (static_cast<double>(cy + (dy > 0 ? 1 : 0)) - ay) / dy : 2.0;
    const double ddx = dx != 0 ? fabs(1.0 / dx) : 0, ddy = dy != 0 ? fabs(1.0 / dy) : 0;
    double t = 0, wet = 0;
    while (t < 1) {
        double tn = tx < ty ? tx : ty;
        tn = tn < 1 ? tn : 1;
        if (water_cell(m, cx, cy))
            wet += tn - t;
        if (tx < ty) {
            cx += sx;
            tx += ddx;
        } else {
            cy += sy;
            ty += ddy;
        }
        t = tn;
    }
    const double ex = m.x(a) - m.x(b), ey = m.y(a) - m.y(b);
    return wet * sqrt(ex * ex + ey * ey);
}

bool near(double a, double b) { return fabs(a - b) <= 1e-12 * (fabs(a) > fabs(b) ? fabs(a) : fabs(b)); }

// Every check on a landscape's power map. `brute` checks the Gabriel rule
// and the crossings against every pair.
void check_power(u64 seed, u32 places, const char* text, bool brute) {
    Map m;
    TextBuf why;
    CHECK(build_map(m, seed, places, text, &why));
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(text, &pe));
    MapSettings s;
    read_map_settings(cfg, &s);
    const u32 n = m.place_count(), E = m.power_link_count(), w = m.raster_w(), h = m.raster_h();
    // Each place's proximity links in order of the place at their other
    // end, each link at both its places; and the whole in one piece (the
    // Gabriel graph holds the shortest tree joining every place).
    u32 ends = 0;
    for (u32 i = 0; i < n; ++i) {
        for (u32 j = 0; j < m.power_degree(i); ++j) {
            const u32 k = m.power_link_at(i, j);
            CHECK(m.power_link(k).a == i || m.power_link(k).b == i);
            if (j > 0)
                CHECK(m.power_other(m.power_link_at(i, j - 1), i) < m.power_other(k, i));
        }
        ends += m.power_degree(i);
    }
    CHECK(ends == 2 * E);
    Array<u32> parent;
    parent.resize(n);
    for (u32 i = 0; i < n; ++i)
        parent[i] = i;
    u32 apart = n;
    for (u32 k = 0; k < E; ++k) {
        const u32 a = root(parent, m.power_link(k).a), b = root(parent, m.power_link(k).b);
        if (a != b) {
            parent[a] = b;
            --apart;
        }
    }
    CHECK(apart == (n ? 1u : 0u));
    // The Gabriel rule over all the places, by brute force: every pair with
    // no third place in its circle is linked, and no other. And no two cross.
    if (brute) {
        u32 missing = 0, extra = 0, crossing = 0;
        for (u32 a = 0; a < n; ++a)
            for (u32 b = a + 1; b < n; ++b) {
                const bool g = gabriel_all(m, a, b), linked = power_between(m, a, b) != ~0u;
                missing += g && !linked ? 1 : 0;
                extra += !g && linked ? 1 : 0;
            }
        for (u32 k = 0; k < E; ++k)
            for (u32 q = k + 1; q < E; ++q)
                crossing += power_crosses(m, m.power_link(k), m.power_link(q)) ? 1 : 0;
        CHECK(missing == 0);
        CHECK(extra == 0);
        CHECK(crossing == 0);
    }
    // Each link: the data link on land beside it, or none; its water, walked
    // another way and sampled; its border; its resistance.
    FieldNoise rf;
    rf.init(seed, Field::resistance, s.shape[static_cast<u32>(Field::resistance)], s.field_scale);
    Array<u64> per;
    u32 only = 0, wet = 0, borders = 0, dry_between = 0, dry = 0;
    for (u32 k = 0; k < E; ++k) {
        const PowerLink& l = m.power_link(k);
        CHECK(l.a < l.b && l.b < n);
        const double dx = m.x(l.a) - m.x(l.b), dy = m.y(l.a) - m.y(l.b);
        CHECK(near(l.length, sqrt(dx * dx + dy * dy)));
        const u32 d = link_between(m, l.a, l.b);
        const bool on_land = d != ~0u && !m.link(d).bridge;
        CHECK(l.data == (on_land ? d : kNoLink));
        only += l.data == kNoLink ? 1 : 0;
        // A link wholly over land has no water width, exactly; one between
        // landmasses always has some.
        const double walked = water_walk(m, l.a, l.b);
        CHECK(fabs(l.water - walked) < 1e-9);
        CHECK((l.water == 0) == (walked == 0));
        CHECK(l.water >= 0 && l.water <= l.length * (1 + 1e-12));
        dry_between += l.water == 0 && m.landmass(l.a) != m.landmass(l.b) ? 1 : 0;
        wet += l.water > 0 ? 1 : 0;
        dry += l.water == 0 ? 1 : 0;
        const u32 samples = 2000;
        u32 hits = 0;
        for (u32 i = 0; i < samples; ++i) {
            const double t = (i + 0.5) / samples;
            hits += water_cell(m, static_cast<i64>(floor((m.x(l.a) - t * dx) * h)),
                               static_cast<i64>(floor((m.y(l.a) - t * dy) * h)))
                        ? 1
                        : 0;
        }
        if (l.water == 0)
            CHECK(hits == 0);
        // A sample is wrong only where the line passes from cell to cell.
        CHECK(fabs(l.water - l.length * hits / samples) <=
              l.length * ((fabs(dx) + fabs(dy)) * h + 2) / samples + 1e-12);
        const bool border = rf.high(m.x(l.a), m.y(l.a)) != rf.high(m.x(l.b), m.y(l.b));
        CHECK(l.border == border);
        borders += border ? 1 : 0;
        CHECK(l.per_length >= s.resistance_min * (1 - 1e-12) && l.per_length <= s.resistance_max * (1 + 1e-12));
        CHECK(near(l.resistance, l.length / m.mean_length() * l.per_length *
                                         exp(l.water / m.mean_length() / s.reach) +
                                     (border ? s.border_strength : 0)));
        per.push(order_key(l.per_length));
    }
    CHECK(dry_between == 0);
    CHECK(only == m.power_only() && wet == m.power_wet() && borders == m.borders_crossed());
    // The resistance a length: dealt out by rank, evenly on a log scale.
    sort_u64(per.data(), per.size());
    for (u32 k = 0; k < E && E > 1; ++k) {
        u64 bits = per[k] & 0x7FFFFFFFFFFFFFFFull; // a positive double's key is its bits with the top one set
        double v;
        memcpy(&v, &bits, sizeof v);
        CHECK(near(v, s.resistance_min * pow(s.resistance_max / s.resistance_min, static_cast<double>(k) / (E - 1))));
    }
    // Every data link on land is a proximity link too, or is counted as a
    // difference; and one that isn't has a place of another landmass in its
    // circle, or joins a degenerate landmass.
    u32 lone = 0, unexplained = 0;
    for (u32 k = 0; k < m.link_count(); ++k) {
        const MapLink& l = m.link(k);
        if (l.bridge)
            continue;
        const u32 p = power_between(m, l.a, l.b);
        CHECK(p == ~0u || m.power_link(p).data == k);
        if (p == ~0u) {
            ++lone;
            unexplained += brute && gabriel_all(m, l.a, l.b) ? 1 : 0;
        }
    }
    CHECK(lone == m.data_only_land());
    CHECK(unexplained == 0);
    // Catchments: every land cell in that of the place nearest its middle,
    // water in none. And each place's water share, counted cell by cell:
    // a cell borders every catchment a cell beside it is in and it isn't.
    Array<u32> count, around, water;
    count.resize(n);
    around.resize(n);
    water.resize(n);
    u32 wrong = 0, land_cells = 0;
    for (u32 cy = 0; cy < h; ++cy)
        for (u32 cx = 0; cx < w; ++cx) {
            if (!m.land(cx, cy)) {
                wrong += m.cell_place(cx, cy) != kNoPlace ? 1 : 0;
                continue;
            }
            ++land_cells;
            const double px = (cx + 0.5) / h, py = (cy + 0.5) / h;
            u32 best = 0;
            double bd = 0;
            for (u32 i = 0; i < n; ++i) {
                const double ex = m.x(i) - px, ey = m.y(i) - py, d2 = ex * ex + ey * ey;
                if (i == 0 || d2 < bd) {
                    best = i;
                    bd = d2;
                }
            }
            wrong += m.cell_place(cx, cy) != best ? 1 : 0;
            ++count[best];
        }
    CHECK(wrong == 0);
    u32 held = 0, bare = 0;
    for (u32 i = 0; i < n; ++i) {
        CHECK(m.catchment(i) == count[i]);
        held += count[i];
        bare += count[i] ? 0 : 1;
    }
    CHECK(held == land_cells);
    for (i64 cy = -1; cy <= static_cast<i64>(h); ++cy)
        for (i64 cx = -1; cx <= static_cast<i64>(w); ++cx) {
            const bool in = cx >= 0 && cy >= 0 && cx < w && cy < h;
            const u32 own = in ? m.cell_place(static_cast<u32>(cx), static_cast<u32>(cy)) : kNoPlace;
            const i64 nx[4] = {cx - 1, cx + 1, cx, cx}, ny[4] = {cy, cy, cy - 1, cy + 1};
            u32 seen[4], seen_n = 0;
            for (u32 k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h)
                    continue;
                const u32 q = m.cell_place(static_cast<u32>(nx[k]), static_cast<u32>(ny[k]));
                bool fresh = q != kNoPlace && q != own;
                for (u32 j = 0; j < seen_n; ++j)
                    fresh = fresh && seen[j] != q;
                if (!fresh)
                    continue;
                seen[seen_n++] = q;
                ++around[q];
                water[q] += water_cell(m, cx, cy) ? 1 : 0;
            }
        }
    double shore = 0;
    for (u32 i = 0; i < n; ++i) {
        CHECK(m.water_share(i) >= 0 && m.water_share(i) <= 1);
        shore += m.water_share(i);
        if (count[i]) {
            CHECK(around[i] > 0 && near(m.water_share(i), static_cast<double>(water[i]) / around[i]));
            continue;
        }
        // A place with no catchment: round its own cell.
        const i64 cx = static_cast<i64>(m.x(i) * h), cy = static_cast<i64>(m.y(i) * h);
        const u32 wet4 = (water_cell(m, cx - 1, cy) ? 1 : 0) + (water_cell(m, cx + 1, cy) ? 1 : 0) +
                         (water_cell(m, cx, cy - 1) ? 1 : 0) + (water_cell(m, cx, cy + 1) ? 1 : 0);
        CHECK(near(m.water_share(i), wet4 / 4.0));
    }
    // Cooling: in the cooling field's order, from the least to the most.
    Array<double> v;
    m.field_values(seed, Field::cooling, s, &v);
    double least = 1e300, most = 0;
    u32 order = 0;
    for (u32 i = 0; i < n; ++i) {
        least = m.cooling(i) < least ? m.cooling(i) : least;
        most = m.cooling(i) > most ? m.cooling(i) : most;
        for (u32 j = i + 1; j < n && n <= 1000; ++j)
            order += (v[i] < v[j]) != (m.cooling(i) < m.cooling(j)) ? 1 : 0;
    }
    CHECK(order == 0);
    if (n >= 2)
        CHECK(near(least, s.cooling_min) && near(most, s.cooling_max));
    TextBuf t;
    m.describe_power(&t);
    printf("  power map: seed %llu, %u places: %s (%u links wholly over land, %u places with no catchment)\n",
           static_cast<unsigned long long>(seed), n, t.c_str(), dry, bare);
}

void test_power() {
    for (u64 seed = 1; seed <= 6; ++seed)
        check_power(seed, 48, "", true);
    check_power(7, 1000, "", true);
    check_power(3, 10000, "", false);
    // Too few places to triangulate: one link between two, none at one.
    check_power(1, 2, "", true);
    check_power(1, 1, "", true);
    check_power(2, 3, "", true);
    // Other settings: no borders, a longer reach, another spread.
    check_power(4, 200, "map.border.strength = 0\nmap.water.reach = 2.5\nmap.resistance.min = 0.25\n", true);
}

// The power map is the seed's and the settings' alone, and its settings
// leave the data map alone.
void test_power_settings() {
    Map a, b, c;
    TextBuf why;
    CHECK(build_map(a, 5, 200, "", &why) && build_map(b, 5, 200, "", &why) && build_map(c, 6, 200, "", &why));
    CHECK_EQ_U64(a.power_hash(), b.power_hash());
    CHECK(a.power_hash() != c.power_hash());
    // A longer reach: the same links, those over water with less resistance.
    Map far;
    CHECK(build_map(far, 5, 200, "map.water.reach = 2\n", &why));
    CHECK_EQ_U64(far.hash(), a.hash());
    CHECK(far.power_hash() != a.power_hash() && far.power_link_count() == a.power_link_count());
    u32 less = 0, same = 0;
    for (u32 k = 0; k < a.power_link_count(); ++k) {
        const PowerLink& l = a.power_link(k);
        CHECK(far.power_link(k).a == l.a && far.power_link(k).b == l.b && far.power_link(k).water == l.water);
        less += l.water > 0 && far.power_link(k).resistance < l.resistance ? 1 : 0;
        same += l.water == 0 && far.power_link(k).resistance == l.resistance ? 1 : 0;
    }
    CHECK(less == a.power_wet() && less > 0 && less + same == a.power_link_count());
    // No borders: the same links cross them, and it costs nothing.
    Map open;
    CHECK(build_map(open, 5, 200, "map.border.strength = 0\n", &why));
    CHECK_EQ_U64(open.hash(), a.hash());
    CHECK(open.borders_crossed() == a.borders_crossed() && a.borders_crossed() > 0);
    for (u32 k = 0; k < a.power_link_count(); ++k) {
        const PowerLink& l = a.power_link(k);
        CHECK(open.power_link(k).border == l.border);
        CHECK(near(open.power_link(k).resistance + (l.border ? 10.0 : 0.0), l.resistance));
    }
    // A resistance field with no step has no borders.
    Map smooth;
    CHECK(build_map(smooth, 5, 200, "map.resistance.edge = 0\n", &why));
    CHECK(smooth.borders_crossed() == 0 && smooth.hash() == a.hash());
    // One resistance everywhere, and one cooling.
    Map flat;
    CHECK(build_map(flat, 5, 200,
                    "map.resistance.min = 1\nmap.resistance.max = 1\nmap.cooling.min = 1\nmap.cooling.max = 1\n",
                    &why));
    for (u32 k = 0; k < flat.power_link_count(); ++k)
        CHECK(flat.power_link(k).per_length == 1);
    for (u32 i = 0; i < flat.place_count(); ++i)
        CHECK(flat.cooling(i) == 1);
    // A spread the wrong way round is refused.
    const char* const kBad[2] = {"map.resistance.min = 3\n", "map.cooling.max = 0.1\n"};
    for (u32 i = 0; i < 2; ++i) {
        Config cfg;
        Config::ParseError pe;
        CHECK(cfg.parse(kBad[i], &pe));
        MapSettings ms;
        read_map_settings(cfg, &ms);
        CHECK(!cfg.ok());
    }
    // The test maps: the proximity links are the data links, with no water,
    // every place's catchment the same, and borders where the field puts
    // them.
    const char* const kTests[2] = {"world.map = grid\n", "world.map = ring\n"};
    for (u32 i = 0; i < 2; ++i) {
        Map g;
        CHECK(build_map(g, 1, 30, kTests[i], &why));
        CHECK(g.power_link_count() == g.link_count() && g.power_wet() == 0 && g.power_only() == 0 &&
              g.data_only_land() == 0);
        for (u32 k = 0; k < g.link_count(); ++k) {
            const PowerLink& l = g.power_link(k);
            CHECK(l.a == g.link(k).a && l.b == g.link(k).b && l.data == k && l.water == 0);
            CHECK(l.per_length >= 0.5 * (1 - 1e-12) && l.per_length <= 2 * (1 + 1e-12));
            CHECK(near(l.resistance, l.length / g.mean_length() * l.per_length + (l.border ? 10.0 : 0.0)));
        }
        for (u32 p = 0; p < g.place_count(); ++p)
            CHECK(g.catchment(p) == 1 && g.water_share(p) == 0 && g.power_degree(p) == g.degree(p) &&
                  g.cooling(p) >= 0.5 * (1 - 1e-12) && g.cooling(p) <= 2 * (1 + 1e-12));
        TextBuf t;
        g.describe_power(&t);
        printf("  power map: %s    %s\n", i ? "ring" : "grid", t.c_str());
    }
    // No map: no power map.
    Map none;
    CHECK(build_map(none, 1, 5, "world.map = none\n", &why));
    CHECK(none.power_link_count() == 0 && none.power_degree(4) == 0 && none.catchment(4) == 1 &&
          none.water_share(4) == 0 && none.cooling(4) == 1);
    TextBuf t;
    none.describe_power(&t);
    CHECK(strcmp(t.c_str(), "power map: none") == 0);
}

// Gate 3's 20 maps (build-plan-gate4.md step 2): the power map is worked out
// beside the data map, never in it, so each seed's positions, ids, data
// links and bridges hash as they did before gate 4 touched the map. The
// hashes were taken from gate 3's map code, and the lines these maps
// describe themselves by are those of runs/gate3-2's report.
const u64 kGate3Maps[20] = {
    0x8DDBC5AF53E5BF22ull, 0x7F0426EA915CDB40ull, 0xDA732F87FFBA5D44ull, 0xD691B1794FAD30F8ull,
    0x8F960727891C9724ull, 0xFB2470E70E6C95C7ull, 0x92F9A46AD7C2FEFBull, 0x186D83CA62CA07CDull,
    0x265C57E584DC071Full, 0xD714DAB4243997A4ull, 0xDD98E946FAA6D3D2ull, 0x3E0C6CF29AA4B2B1ull,
    0x84C77B8E0F511D03ull, 0x0A8BF57BBD444CA7ull, 0xD85ABEB2814B585Aull, 0xD4A392755A4973D4ull,
    0x726F03EBF28A8E21ull, 0xFCFEEA45005A9803ull, 0x6F8654A54BE4BA1Bull, 0xC6FD525DB34BD37Full};

void test_gate3_maps() {
    u32 same = 0;
    for (u64 seed = 1; seed <= 20; ++seed) {
        Map m;
        TextBuf why;
        CHECK(build_map(m, seed, 48, "", &why));
        same += m.hash() == kGate3Maps[seed - 1] ? 1 : 0;
        TextBuf t;
        m.describe(&t);
        printf("  gate 3's map, seed %llu: 0x%016llXull %s\n", static_cast<unsigned long long>(seed),
               static_cast<unsigned long long>(m.hash()), t.c_str());
        TextBuf p;
        m.describe_power(&p);
        printf("  gate 3's map, seed %llu: its %s\n", static_cast<unsigned long long>(seed), p.c_str());
    }
    CHECK(same == 20);
}

} // namespace

void suite_map() {
    test_landscape();
    test_pure();
    test_refused();
    test_grid_and_ring();
    test_fields();
    test_large();
    test_power();
    test_power_settings();
    test_gate3_maps();
}
