// Respawning places (design.md 7.8, 7.3; build-plan-gate5.md step 6).
//
// The map: with nobody moved, working the links out again changes nothing.
// A respawned place lands on land, no closer to any other place than the
// spacing allows, and denser ground takes more of them; after each respawn
// both link sets obey the Gabriel rule against every pair by brute force,
// nothing crosses, every landmass is one piece and joined to the rest; the
// links that didn't change keep their numbers' worth (bandwidth, delay,
// resistance) and the map says where each went; a new link's values are ones
// setup dealt. The same respawns from the same seed give the same map, and
// a map loaded from the saved points, incarnations and anchors is the same
// map. A test map's places stay where they are.
//
// The world: a place with every device failed by hand is torn down at its
// visit and built again; with the share at 1 a working device is enough to
// be repaired instead; messages for its id reach the new place and those on
// gone wires are counted lost; the network, the solve, the link table and
// heat follow the new map, and are what building them from nothing on it
// gives; the world is the same on any number of host threads and from a
// snapshot taken after respawns.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/archive.h"
#include "nova2/field.h"
#include "nova2/hash.h"
#include "nova2/io.h"
#include "nova2/map.h"
#include "nova2/observe.h"
#include "nova2/rng.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

MapSettings settings_of(const char* text) {
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(text, &pe));
    MapSettings ms;
    read_map_settings(cfg, &ms);
    CHECK(cfg.ok());
    return ms;
}

bool build_map(Map& m, u64 seed, u32 places, const char* text) {
    TextBuf why;
    const MapSettings ms = settings_of(text);
    const bool ok = m.build(seed, places, ms, &why);
    if (!ok)
        printf("  respawn: no map: %s\n", why.c_str());
    return ok;
}

u64 pair_of(u32 a, u32 b) { return (static_cast<u64>(a) << 32) | b; }

// A map's links, as they are, for comparing after a respawn.
struct Before {
    Array<MapLink> links;
    Array<PowerLink> plinks;
    Array<double> x, y;
    void take(const Map& m) {
        links.clear();
        plinks.clear();
        x.clear();
        y.clear();
        for (u32 k = 0; k < m.link_count(); ++k)
            links.push(m.link(k));
        for (u32 k = 0; k < m.power_link_count(); ++k)
            plinks.push(m.power_link(k));
        for (u32 i = 0; i < m.place_count(); ++i) {
            x.push(m.x(i));
            y.push(m.y(i));
        }
    }
};

bool on_land(const Map& m, u32 i) {
    const u32 w = m.raster_w(), h = m.raster_h();
    u32 cx = static_cast<u32>(m.x(i) * h), cy = static_cast<u32>(m.y(i) * h);
    cx = cx >= w ? w - 1 : cx;
    cy = cy >= h ? h - 1 : cy;
    return m.land(cx, cy);
}

// With nobody moved, everything a respawn works out again comes out as
// setup left it.
void test_unmoved() {
    const u64 seeds[] = {1, 7, 20261120};
    for (u32 si = 0; si < 3; ++si) {
        Map m;
        CHECK(build_map(m, seeds[si], 120, ""));
        const u64 h0 = m.hash(), p0 = m.power_hash();
        const u32 bridges = m.bridges(), joins = m.joins(), peopled = m.peopled(), unbridged = m.unbridged();
        const u32 wet = m.power_wet(), only = m.power_only(), data_only = m.data_only_land();
        const u32 borders = m.borders_crossed();
        Before b;
        b.take(m);
        m.relink_again();
        CHECK(m.hash() == h0);
        CHECK(m.power_hash() == p0);
        CHECK(m.bridges() == bridges && m.joins() == joins && m.peopled() == peopled && m.unbridged() == unbridged);
        CHECK(m.power_wet() == wet && m.power_only() == only && m.data_only_land() == data_only);
        CHECK(m.borders_crossed() == borders);
        CHECK(m.respawned() == 0 && m.anchors().size() == bridges);
        u32 same = 0;
        for (u32 k = 0; k < b.links.size(); ++k)
            same += m.data_moved()[k] == k ? 1 : 0;
        CHECK(same == b.links.size() && m.data_moved().size() == b.links.size());
        same = 0;
        for (u32 k = 0; k < b.plinks.size(); ++k)
            same += m.power_moved()[k] == k ? 1 : 0;
        CHECK(same == b.plinks.size() && m.power_moved().size() == b.plinks.size());
        TextBuf why;
        const bool ok = m.check_links(true, &why);
        if (!ok)
            printf("  respawn: seed %llu: %s\n", static_cast<unsigned long long>(seeds[si]), why.c_str());
        CHECK(ok);
    }
    // With fewer bridges than landmasses want, and some left alone.
    Map m;
    CHECK(build_map(m, 5, 90, "map.land.share = 0.25\nmap.unbridged = 0.3\nmap.bridges = 1\n"));
    const u64 h0 = m.hash(), p0 = m.power_hash();
    m.relink_again();
    CHECK(m.hash() == h0 && m.power_hash() == p0);
    TextBuf why;
    CHECK(m.check_links(true, &why));
}

struct Tally {
    u32 respawns = 0, moved = 0, stayed = 0;
    u32 data_gone = 0, data_new = 0, power_gone = 0, power_new = 0;
    u32 crossings_added = 0, masses_changed = 0, dense = 0;
    double closest = 1e9;
};

// `rounds` respawns on one map, each checked. `pick`: a stream of places.
void respawn_checked(Map& m, const MapSettings& ms, u64 seed, u32 rounds, Tally* t, bool brute) {
    const u32 n = m.place_count();
    // The density field's median over the land, for where the new points fall.
    FieldNoise dens;
    dens.init(seed, Field::density, ms.shape[static_cast<u32>(Field::density)], ms.field_scale);
    Array<u64> keys;
    const u32 w = m.raster_w(), h = m.raster_h();
    for (u32 cy = 0; cy < h; ++cy)
        for (u32 cx = 0; cx < w; ++cx)
            if (m.land(cx, cy))
                keys.push(order_key(dens.at((cx + 0.5) / h, (cy + 0.5) / h)));
    sort_u64(keys.data(), keys.size());
    const u64 median = keys[keys.size() / 2];
    // The values setup dealt.
    HashMap<u8> bandwidths;
    double per_lo = 1e30, per_hi = 0;
    for (u32 k = 0; k < m.link_count(); ++k)
        if (!m.link(k).bridge)
            bandwidths.get_or_insert(m.link(k).bandwidth) = 1;
    for (u32 k = 0; k < m.power_link_count(); ++k) {
        const double p = m.power_link(k).per_length;
        per_lo = p < per_lo ? p : per_lo;
        per_hi = p > per_hi ? p : per_hi;
    }
    const double mean = m.mean_length();
    Rng pick(seed, 0, 999, 0);
    Before b;
    for (u32 round = 0; round < rounds; ++round) {
        const u32 i = static_cast<u32>(pick.below(n));
        b.take(m);
        const u32 inc = m.incarnation(i), mass = m.landmass(i);
        const usize anchors = m.anchors().size();
        const bool moved = m.respawn(i);
        ++t->respawns;
        t->moved += moved ? 1 : 0;
        t->stayed += moved ? 0 : 1;
        CHECK(m.incarnation(i) == inc + 1);
        CHECK(m.mean_length() == mean);
        t->crossings_added += static_cast<u32>(m.anchors().size() - anchors);
        t->masses_changed += m.landmass(i) != mass ? 1 : 0;
        // Only it moved, and onto land, with room.
        u32 others_moved = 0;
        for (u32 o = 0; o < n; ++o) {
            if (o == i)
                continue;
            others_moved += m.x(o) != b.x[o] || m.y(o) != b.y[o] ? 1 : 0;
            const double dx = m.x(o) - m.x(i), dy = m.y(o) - m.y(i), d = sqrt(dx * dx + dy * dy);
            t->closest = d < t->closest ? d : t->closest;
        }
        CHECK(others_moved == 0);
        CHECK(on_land(m, i));
        CHECK(moved == (m.x(i) != b.x[i] || m.y(i) != b.y[i]));
        if (moved) {
            u32 cx = static_cast<u32>(m.x(i) * h), cy = static_cast<u32>(m.y(i) * h);
            cx = cx >= w ? w - 1 : cx;
            cy = cy >= h ? h - 1 : cy;
            t->dense += order_key(dens.at((cx + 0.5) / h, (cy + 0.5) / h)) >= median ? 1 : 0;
        }
        // The rules, against every pair.
        TextBuf why;
        const bool ok = m.check_links(brute, &why);
        if (!ok)
            printf("  respawn: seed %llu, round %u, place %u: %s\n", static_cast<unsigned long long>(seed), round, i,
                   why.c_str());
        CHECK(ok);
        if (!ok)
            return;
        // Where each old link went: the same two places, neither of them
        // the respawned one, with the same numbers.
        HashMap<u8> taken;
        u32 bad = 0, gone = 0;
        CHECK(m.data_moved().size() == b.links.size());
        for (u32 k = 0; k < b.links.size(); ++k) {
            const MapLink& o = b.links[k];
            const u32 to = m.data_moved()[k];
            if (to == kNoLink) {
                ++gone;
                continue;
            }
            const MapLink& l = m.link(to);
            bad += to >= m.link_count() || l.a != o.a || l.b != o.b || o.a == i || o.b == i ? 1 : 0;
            bad += l.bridge != o.bridge || l.bandwidth != o.bandwidth || l.delay != o.delay || l.length != o.length
                       ? 1
                       : 0;
            bool fresh = false;
            taken.get_or_insert(to, &fresh) = 1;
            bad += fresh ? 0 : 1;
        }
        CHECK(bad == 0);
        t->data_gone += gone;
        // Every link there now and not come from an old one is new: at the
        // respawned place, or one the rule's answer changed for.
        u32 fresh_links = 0;
        for (u32 k = 0; k < m.link_count(); ++k) {
            if (taken.find(k))
                continue;
            ++fresh_links;
            const MapLink& l = m.link(k);
            bad += l.delay < 1 ? 1 : 0;
            if (l.bridge)
                bad += l.bandwidth != ms.bridge_bandwidth ? 1 : 0;
            else if (l.a == i || l.b == i || m.incarnation(l.a) || m.incarnation(l.b))
                bad += bandwidths.find(l.bandwidth) ? 0 : 1; // one of the values setup dealt
        }
        CHECK(bad == 0);
        t->data_new += fresh_links;
        // A link gone that didn't touch the place: its pair isn't linked now
        // (the rule's answer changed), or it was a bridge whose end went to
        // another place.
        HashMap<u8> now;
        for (u32 k = 0; k < m.link_count(); ++k)
            now.get_or_insert(pair_of(m.link(k).a, m.link(k).b)) = m.link(k).bridge ? 2 : 1;
        for (u32 k = 0; k < b.links.size(); ++k) {
            const MapLink& o = b.links[k];
            if (m.data_moved()[k] != kNoLink || o.a == i || o.b == i)
                continue;
            const u8* f = now.find(pair_of(o.a, o.b));
            bad += f && (*f == 2) == o.bridge ? 1 : 0;
        }
        CHECK(bad == 0);
        // The proximity links likewise.
        taken.clear();
        gone = 0;
        CHECK(m.power_moved().size() == b.plinks.size());
        for (u32 k = 0; k < b.plinks.size(); ++k) {
            const PowerLink& o = b.plinks[k];
            const u32 to = m.power_moved()[k];
            if (to == kNoLink) {
                ++gone;
                continue;
            }
            const PowerLink& l = m.power_link(to);
            bad += to >= m.power_link_count() || l.a != o.a || l.b != o.b || o.a == i || o.b == i ? 1 : 0;
            bad += l.per_length != o.per_length || l.resistance != o.resistance || l.water != o.water ||
                           l.border != o.border || l.length != o.length
                       ? 1
                       : 0;
            taken.get_or_insert(to) = 1;
        }
        CHECK(bad == 0);
        t->power_gone += gone;
        u32 fresh_power = 0;
        for (u32 k = 0; k < m.power_link_count(); ++k) {
            const PowerLink& l = m.power_link(k);
            // Beside its data link on land, or power only.
            if (l.data != kNoLink)
                bad += l.data >= m.link_count() || m.link(l.data).a != l.a || m.link(l.data).b != l.b ||
                               m.link(l.data).bridge
                           ? 1
                           : 0;
            if (taken.find(k))
                continue;
            ++fresh_power;
            bad += l.per_length < per_lo || l.per_length > per_hi || !(l.resistance > 0) ? 1 : 0;
        }
        CHECK(bad == 0);
        t->power_new += fresh_power;
        // Catchments: every land cell has the place nearest it; they add up.
        u64 cells = 0, land = 0;
        for (u32 o = 0; o < n; ++o)
            cells += m.catchment(o);
        u32 not_nearest = 0;
        for (u32 cy = 0; cy < h; ++cy)
            for (u32 cx = 0; cx < w; ++cx) {
                if (!m.land(cx, cy)) {
                    not_nearest += m.cell_place(cx, cy) != kNoPlace ? 1 : 0;
                    continue;
                }
                ++land;
                if ((cx * 7 + cy * 13) % 31)
                    continue; // one cell in 31 against every place
                const double px = (cx + 0.5) / h, py = (cy + 0.5) / h;
                u32 best = 0;
                double bd = 1e30;
                for (u32 o = 0; o < n; ++o) {
                    const double ex = m.x(o) - px, ey = m.y(o) - py, d2 = ex * ex + ey * ey;
                    if (d2 < bd) {
                        bd = d2;
                        best = o;
                    }
                }
                not_nearest += m.cell_place(cx, cy) != best ? 1 : 0;
            }
        CHECK(cells == land && not_nearest == 0);
        CHECK(m.cooling(i) >= ms.cooling_min && m.cooling(i) <= ms.cooling_max);
    }
}

void test_respawns() {
    const u64 seeds[] = {3, 11, 20261120};
    Tally all;
    for (u32 si = 0; si < 3; ++si) {
        Map m;
        const MapSettings ms = settings_of("");
        CHECK(build_map(m, seeds[si], 80, ""));
        const double least = m.spacing() / sqrt(ms.density);
        Tally t;
        respawn_checked(m, ms, seeds[si], 60, &t, true);
        // No two places closer than the spacing at the densest ground.
        CHECK(t.closest >= least * 0.999);
        CHECK(t.moved >= 55);
        CHECK(m.respawned() > 30 && m.respawned() <= 60);
        all.respawns += t.respawns;
        all.moved += t.moved;
        all.stayed += t.stayed;
        all.data_gone += t.data_gone;
        all.data_new += t.data_new;
        all.power_gone += t.power_gone;
        all.power_new += t.power_new;
        all.crossings_added += t.crossings_added;
        all.masses_changed += t.masses_changed;
        all.dense += t.dense;
        // The same respawns again: the same map.
        Map again;
        CHECK(build_map(again, seeds[si], 80, ""));
        Rng pick(seeds[si], 0, 999, 0);
        for (u32 round = 0; round < 60; ++round)
            again.respawn(static_cast<u32>(pick.below(80)));
        CHECK(again.hash() == m.hash() && again.power_hash() == m.power_hash() &&
              again.respawn_hash() == m.respawn_hash());
        // And from what a snapshot saves of it, onto the map as set up.
        ByteWriter out;
        {
            Archive ar(&out);
            m.serialize_respawns(ar);
        }
        Map loaded;
        CHECK(build_map(loaded, seeds[si], 80, ""));
        CHECK(loaded.hash() != m.hash());
        ByteReader in(out.data(), out.size());
        {
            Archive ar(&in);
            loaded.serialize_respawns(ar);
            CHECK(ar.ok() && in.remaining() == 0);
        }
        CHECK(loaded.hash() == m.hash() && loaded.power_hash() == m.power_hash() &&
              loaded.respawn_hash() == m.respawn_hash());
        u32 differ = 0;
        for (u32 i = 0; i < 80; ++i)
            differ += loaded.x(i) != m.x(i) || loaded.y(i) != m.y(i) || loaded.landmass(i) != m.landmass(i) ||
                              loaded.incarnation(i) != m.incarnation(i) || loaded.cooling(i) != m.cooling(i) ||
                              loaded.catchment(i) != m.catchment(i) || loaded.water_share(i) != m.water_share(i)
                          ? 1
                          : 0;
        CHECK(differ == 0);
        TextBuf why;
        CHECK(loaded.check_links(true, &why));
        // It carries on the same: one more respawn on each.
        m.respawn(17);
        loaded.respawn(17);
        CHECK(loaded.hash() == m.hash() && loaded.power_hash() == m.power_hash() &&
              loaded.respawn_hash() == m.respawn_hash());
    }
    // Denser ground takes more: with the default density of 3, about 63% of
    // the draws fall on the denser half of the land.
    CHECK(all.dense * 100 >= all.moved * 54 && all.dense * 100 <= all.moved * 75);
    CHECK(all.data_new > all.respawns && all.power_new > all.respawns);
    printf("  respawn: %u respawns on 3 maps of 80 places: %u moved, %u stayed; %u data links gone and %u new, %u "
           "proximity links gone and %u new; %u landed on another landmass, %u crossings added; %u%% on the denser "
           "half of the land\n",
           all.respawns, all.moved, all.stayed, all.data_gone, all.data_new, all.power_gone, all.power_new,
           all.masses_changed, all.crossings_added, all.moved ? all.dense * 100 / all.moved : 0);
}

// Islands: few places on many landmasses, so respawns empty landmasses and
// people others, bridges' ends move, and landmasses left with no way to the
// rest get a crossing. With some landmasses left alone, they stay so.
void test_islands() {
    const char* const kTexts[2] = {"map.land.share = 0.2\nmap.land.scale = 6\nmap.bridges = 1\n",
                                   "map.land.share = 0.2\nmap.land.scale = 6\nmap.bridges = 1\nmap.unbridged = 0.4\n"};
    for (u32 v = 0; v < 2; ++v) {
        Map m;
        const MapSettings ms = settings_of(kTexts[v]);
        CHECK(build_map(m, 77 + v, 40, kTexts[v]));
        const u32 masses = m.landmasses(), peopled0 = m.peopled(), bridges0 = m.bridges();
        Tally t;
        respawn_checked(m, ms, 77 + v, 150, &t, true);
        CHECK(t.masses_changed > 10);
        CHECK(v == 1 || t.crossings_added > 0);
        printf("  respawn: islands%s: %u landmasses, %u with places at first and %u after 150 respawns; %u bridges "
               "then %u; %u respawns changed landmass, %u crossings added, %u left alone\n",
               v ? " (some left alone)" : "", masses, peopled0, m.peopled(), bridges0, m.bridges(), t.masses_changed,
               t.crossings_added, m.unbridged());
    }
}

// A test map's places stay where they are; their links are new all the same.
void test_test_maps() {
    Map m;
    CHECK(build_map(m, 9, 12, "world.map = grid\n"));
    const u64 p0 = m.power_hash();
    Before b;
    b.take(m);
    CHECK(!m.respawn(5));
    CHECK(m.incarnation(5) == 1 && m.respawned() == 1);
    CHECK(m.x(5) == b.x[5] && m.y(5) == b.y[5]);
    CHECK(m.link_count() == b.links.size() && m.power_link_count() == b.plinks.size());
    u32 gone = 0, kept = 0;
    for (u32 k = 0; k < b.links.size(); ++k) {
        const bool at = b.links[k].a == 5 || b.links[k].b == 5;
        gone += m.data_moved()[k] == kNoLink ? 1 : 0;
        kept += m.data_moved()[k] == k ? 1 : 0;
        CHECK((m.data_moved()[k] == kNoLink) == at);
        CHECK(m.link(k).a == b.links[k].a && m.link(k).b == b.links[k].b);
    }
    CHECK(gone == m.degree(5) && gone + kept == b.links.size());
    (void)p0;
    Map none;
    CHECK(build_map(none, 9, 4, "world.map = none\n"));
    CHECK(!none.respawn(2));
    CHECK(none.incarnation(2) == 1 && none.link_count() == 0);
}

// --- worlds --------------------------------------------------------------------------

const char* const kLand = "world.places = 40\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "supply.model = sources\n"
                          "heat.on = true\n"
                          "wear.rate = 1\n"
                          "wear.link.endurance = 400\n"
                          "wear.link.current = 12\n"
                          "link.cuts = 0.0006\n"
                          "net.lifetime = 30\n"
                          "visit.mean = 150\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build_world(World& w, Config& cfg, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(Str(kLand, strlen(kLand)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  respawn: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

void crowd_one(World& w, u32 i) {
    Place& p = w.place(i);
    if (!p.mem.strict())
        memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
    place_traffic(p, w.place_count(), i);
}

void crowd(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i)
        crowd_one(w, i);
}

void fail_all(Place& p) {
    for (u32 c = 0; c < p.core_count(); ++c)
        p.fail_core(c);
    for (u32 d = 0; d < p.mem.device_count(); ++d)
        p.fail_memory(d);
    p.fail_disk();
    p.fail_network();
    p.fail_cooler();
}

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

// The invariants, and the power balance, after a tick. The worst the
// balance was out, as a share of what the sources gave. `places`: each
// place's own check too (not between a respawn by hand and its tick, when
// the new place's visit is this tick's, still to run).
double world_ok(World& w, bool brute, bool places = true) {
    TextBuf why;
    bool ok = w.links().check(w.tick_count(), &why) && w.net().check(&why) && w.map().check_links(brute, &why);
    for (u32 i = 0; places && ok && i < w.place_count(); ++i)
        ok = w.place(i).check(&why);
    if (!ok)
        printf("  respawn: at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()), why.c_str());
    CHECK(ok);
    const Grid& g = w.grid();
    const double given = static_cast<double>(g.given_uw);
    const double taken = static_cast<double>(g.loads_uw + g.links_uw + g.drains_uw);
    return given > 1e6 ? fabs(given - taken) / given : 0;
}

// What the engine holds by the map agrees with the map as it stands: a
// world built again from nothing on that map (a snapshot's load does that)
// has the same hash.
bool same_from_nothing(World& w, Config& cfg) {
    ByteWriter saved;
    snapshot(w, cfg, saved);
    SnapshotHeader h;
    TextBuf err, why;
    Config::ParseError pe;
    if (!snapshot_parse(saved.data(), saved.size(), &h, &err))
        return false;
    World v;
    Config c2;
    if (!snapshot_config(h, &c2, &pe) || !v.init(c2, &why))
        return false;
    ByteReader in(saved.data() + h.world_offset, h.world_size);
    if (!v.load(in, h.version))
        return false;
    return v.hash() == w.hash();
}

// The messages, of those followed (by number), delivered at the tick just
// run: each to `to`. Each one found is struck off.
u32 reached(const Network& net, Array<u64>& followed, u32 to) {
    u32 n = 0;
    for (u32 d = 0; d < net.delivered_count(); ++d) {
        const Message& g = net.delivered(d);
        for (usize k = 0; k < followed.size(); ++k)
            if (followed[k] == g.seq) {
                CHECK(g.dst == to);
                followed[k] = ~0ull;
                ++n;
            }
    }
    return n;
}

bool has_tag(const ByteWriter& b, u32 tag) {
    for (usize i = 0; i + 4 <= b.size(); ++i)
        if (memcmp(b.data() + i, &tag, 4) == 0)
            return true;
    return false;
}

// A place with every device failed by hand is torn down at its visit and
// built again somewhere the map allows; what lived there ended; the
// network, the solve, the link table and heat carry on; messages for its id
// reach the new place. The world is the same on one host thread and three,
// and from a snapshot taken after the respawn.
void test_at_a_visit() {
    const u32 kWho = 14;
    u64 hash[2] = {}, mid = 0;
    ByteWriter saved;
    u64 visit_tick = 0, save_tick = 0;
    double balance = 0;
    u32 torn = 0, arrivals = 0;
    u32 flying = 0, landed = 0; // messages for its id on their way elsewhere at the teardown, and those delivered
    u64 lost = 0;
    Array<u64> followed;
    double from_x = 0, from_y = 0, to_x = 0, to_y = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, "", &why));
        CHECK(w.respawning());
        for (u32 i = 0; i < w.place_count(); ++i) {
            const PlaceHw& hw = w.dealt(i);
            const Place& q = w.place(i);
            CHECK(hw.strict == q.mem.strict() && hw.page_size == q.mem.page_size() && hw.cores == q.core_count());
            CHECK(hw.devices == q.mem.device_count() && hw.cooler == q.has_cooler && w.respawned_at(i) == ~0ull);
        }
        crowd(w);
        w.start_threads(pass == 0 ? 1 : 3);
        Observer obs;
        ObserverSettings os;
        os.sample_every = 100;
        obs.init(os);
        if (pass == 0)
            w.watch(&obs);
        for (u32 t = 0; t < 60; ++t)
            w.tick();
        Place* p = &w.place(kWho);
        const u64 visits = p->visits, uid = p->os.next_uid(), births = p->births;
        visit_tick = p->next_visit;
        const double x0 = w.map().x(kWho), y0 = w.map().y(kWho);
        // Its neighbours' programs die in their own time; its own are
        // counted at the teardown. One device left working: no teardown
        // (the share is 1), so first fail all but the cooler or a core and
        // look at another place's visit.
        fail_all(*p);
        CHECK(p->failed_devices() == p->device_count());
        const NetCounts before = w.net().counts();
        const u64 respawns = w.respawns;
        while (w.tick_count() <= visit_tick) {
            const bool due = w.tick_count() == visit_tick;
            const u32 live = p->os.live_programs();
            if (due && pass == 0) {
                // The messages for its id on their way at other places, or
                // crossing to them, as it is torn down: they go on to the
                // new place.
                for (u32 m = 0; m < w.net().message_slots(); ++m) {
                    const Message& g = w.net().message_slot(m);
                    if (g.live && g.kind == MsgKind::data && g.dst == kWho && g.at != kWho)
                        followed.push(g.seq);
                }
                flying = static_cast<u32>(followed.size());
            }
            w.tick();
            if (due && pass == 0)
                landed += reached(w.net(), followed, kWho);
            if (!due) {
                CHECK(w.respawns == respawns && w.place(kWho).incarnation == 0);
                continue;
            }
            p = &w.place(kWho);
            CHECK(w.respawns == respawns + 1 && p->incarnation == 1 && w.map().incarnation(kWho) == 1);
            CHECK(w.respawned_at(kWho) == visit_tick);
            CHECK(p->visits == visits + 1 && p->last_visit == visit_tick && p->next_visit > visit_tick);
            CHECK(p->failed_devices() == 0 && p->os.next_uid() >= uid && p->births >= births);
            CHECK(p->env.tick == w.tick_count());
            if (pass == 0) {
                torn = live;
                from_x = x0;
                from_y = y0;
                to_x = w.map().x(kWho);
                to_y = w.map().y(kWho);
                CHECK(w.respawns_moved == 1 && (to_x != from_x || to_y != from_y));
                CHECK(same_from_nothing(w, cfg));
            }
        }
        const NetCounts after = w.net().counts();
        if (pass == 0)
            lost = after.unlinked - before.unlinked + after.cut - before.cut;
        // Carried on: the new place takes arrivals for its id, and is lived
        // in again.
        const u64 runs = p->region.arrival_joined + p->os.inbox_joined;
        const u64 born = p->births;
        for (u32 t = 0; t < 200; ++t) {
            w.tick();
            if (pass == 0) {
                landed += reached(w.net(), followed, kWho);
                const double b = world_ok(w, t % 50 == 0);
                balance = b > balance ? b : balance;
            }
        }
        p = &w.place(kWho);
        if (pass == 0) {
            arrivals = static_cast<u32>(p->region.arrival_joined + p->os.inbox_joined - runs);
            CHECK(arrivals > 0 && p->births > born && p->os.live_programs() > 0);
            mid = w.hash();
            save_tick = w.tick_count();
            snapshot(w, cfg, saved);
        } else {
            CHECK_EQ_U64(w.hash(), mid);
        }
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        hash[pass] = w.hash();
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(balance < 0.02 && flying > 0 && landed > 0);
    {
        SnapshotHeader h;
        TextBuf err, why;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.version == 6 && h.tick == save_tick);
        World w;
        Config cfg;
        CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
        CHECK_EQ_U64(w.hash(), mid);
        CHECK(w.place(kWho).incarnation == 1 && w.map().x(kWho) == to_x && w.map().y(kWho) == to_y);
        world_ok(w, true);
        w.start_threads(2);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
    }
    printf("  respawn: a place with every device failed by hand was torn down at its visit, tick %llu, and built "
           "again: from (%.3f, %.3f) to (%.3f, %.3f); %u programs alive there ended, %llu messages lost with its "
           "device and wires; of the %u for its id on their way elsewhere then, %u reached the new place; in the "
           "next 200 ticks %u messages for its id reached it and it was lived in again; power balanced within "
           "%.3f%%. The same on 1 host thread and 3, and from a snapshot taken after\n",
           static_cast<unsigned long long>(visit_tick), from_x, from_y, to_x, to_y, torn,
           static_cast<unsigned long long>(lost), flying, landed, arrivals, balance * 100);
}

// With the share at 1 a working device is enough: places with all but one
// device failed are repaired at their visits, never torn down. With the
// share lower, they are torn down. And above 1, or with no visits, nothing
// respawns and the save says nothing of it.
void test_share() {
    const char* const kTexts[4] = {"", "visit.respawn_share = 0.5\n", "visit.respawn_share = 1.5\n",
                                   "visit.mean = 0\n"};
    u64 counts[4] = {};
    for (u32 v = 0; v < 4; ++v) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, kTexts[v], &why));
        CHECK(w.respawning() == (v < 2));
        crowd(w);
        w.start_threads(3);
        u64 visits = 0;
        for (u32 t = 0; t < 400; ++t) {
            // Every 40 ticks, five places lose every device but one core.
            if (t % 40 == 0)
                for (u32 k = 0; k < 5; ++k) {
                    Place& p = w.place((t / 40 * 5 + k) % w.place_count());
                    fail_all(p);
                    p.replace_core(0);
                }
            Array<u32> inc;
            Array<u8> all, due;
            for (u32 i = 0; i < w.place_count(); ++i) {
                const Place& p = w.place(i);
                inc.push(p.incarnation);
                all.push(p.failed_devices() == p.device_count() ? 1 : 0);
                due.push(p.next_visit == w.tick_count() ? 1 : 0);
            }
            w.tick();
            for (u32 i = 0; i < w.place_count(); ++i) {
                const Place& p = w.place(i);
                visits += due[i];
                if (v == 0)
                    CHECK(p.incarnation == inc[i] + (due[i] && all[i] ? 1 : 0));
                else if (v >= 2)
                    CHECK(p.incarnation == 0);
                else
                    CHECK(p.incarnation == inc[i] || due[i]);
            }
        }
        counts[v] = w.respawns;
        ByteWriter out;
        w.save(out);
        CHECK(has_tag(out, 0x52535031) == (v < 2)); // "RSP1"
        CHECK(v == 3 || visits > 40);
        if (v < 2)
            world_ok(w, true);
    }
    CHECK(counts[0] == 0 && counts[1] > 10 && counts[2] == 0 && counts[3] == 0);
    printf("  respawn: 400 ticks with places losing every device but one core: at a share of 1, none torn down; at "
           "0.5, %llu; above 1, and with no visits, none, and the save says nothing of respawns\n",
           static_cast<unsigned long long>(counts[1]));
}

// Many respawns in a busy world, by hand between ticks, live places and
// all: every check holds after each, the engine's state by the map is what
// building it from nothing gives, messages on gone wires are counted, the
// observer's count of the living agrees, and the world is the same on one
// host thread and three and from a snapshot in the middle.
void test_many() {
    const u32 kTicks = 600;
    u64 hash[2] = {}, mid = 0;
    ByteWriter saved;
    double balance = 0;
    u64 torn = 0, moved = 0, n = 0;
    NetCounts nc;
    u32 rebuilt = 0;
    for (u32 pass = 0; pass < 3; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        if (pass < 2) {
            CHECK(build_world(w, cfg, "", &why));
            crowd(w);
        } else {
            SnapshotHeader h;
            TextBuf err;
            Config::ParseError pe;
            CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.tick == kTicks / 2);
            CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
            ByteReader in(saved.data() + h.world_offset, h.world_size);
            CHECK(w.load(in, h.version));
            CHECK_EQ_U64(w.hash(), mid);
        }
        w.start_threads(pass == 1 ? 1 : 3);
        Observer obs;
        ObserverSettings os;
        os.sample_every = 100;
        obs.init(os);
        if (pass == 0)
            w.watch(&obs);
        for (u32 t = pass == 2 ? kTicks / 2 : 0; t < kTicks; ++t) {
            // The picks are the same in every pass: drawn by the tick.
            Rng r(5, t, 998, 0);
            if (t % 7 == 3) {
                const u32 i = static_cast<u32>(r.below(w.place_count()));
                w.respawn(i);
                if (t % 4 == 0)
                    crowd_one(w, i); // lived in again at once
                if (pass == 0) {
                    world_ok(w, t % 70 == 3, false);
                    if (t % 35 == 3) {
                        CHECK(same_from_nothing(w, cfg));
                        ++rebuilt;
                    }
                }
            }
            if (t == kTicks / 2) {
                if (pass == 0) {
                    mid = w.hash();
                    snapshot(w, cfg, saved);
                } else {
                    CHECK_EQ_U64(w.hash(), mid);
                }
            }
            w.tick();
            if (pass == 0) {
                const double b = world_ok(w, false);
                balance = b > balance ? b : balance;
            }
        }
        if (pass == 0) {
            hash[0] = w.hash();
            torn = w.torn_programs;
            moved = w.respawns_moved;
            n = w.respawns;
            nc = w.net().counts();
            // The observer's census against the places': every program it
            // was told of that is alive in its books is alive in a place.
            u64 live = 0, counted = 0;
            for (u32 i = 0; i < w.place_count(); ++i)
                live += w.place(i).os.live_programs();
            for (u32 i = 0; i < obs.lineage_count(); ++i)
                counted += obs.lineage_at(i).origin ? obs.lineage_at(i).alive : 0;
            CHECK(live > 0 && counted == live);
            // Counters carried with the ids: a save of them loads into a
            // world from the snapshot of now.
            ByteWriter counters;
            w.save_counters(counters);
            ByteWriter now;
            snapshot(w, cfg, now);
            SnapshotHeader h;
            TextBuf err;
            Config::ParseError pe;
            CHECK(snapshot_parse(now.data(), now.size(), &h, &err));
            World v;
            Config c2;
            CHECK(snapshot_config(h, &c2, &pe) && v.init(c2, &why));
            ByteReader in(now.data() + h.world_offset, h.world_size);
            CHECK(v.load(in, h.version));
            Observer watching;
            watching.init(os);
            v.watch(&watching);
            ByteReader cin(counters.data(), counters.size());
            CHECK(v.load_counters(cin));
            CHECK(v.respawns == n && v.torn_programs == torn && v.respawns_moved == moved);
            u32 first = 0;
            for (u32 i = 0; i < w.place_count(); ++i) {
                CHECK(v.respawned_at(i) == w.respawned_at(i) && v.place(i).births == w.place(i).births);
                first += w.respawned_at(i) != ~0ull ? 1 : 0;
            }
            CHECK(first == w.map().respawned() && first > 20);
        } else {
            CHECK_EQ_U64(w.hash(), hash[0]);
        }
    }
    CHECK(n > 80 && torn > 100 && nc.unlinked > 0 && balance < 0.02);
    printf("  respawn: %llu respawns by hand in a crowded world over %u ticks (%llu to a new point), %llu programs "
           "ended by them; %llu messages lost on gone wires, %llu with a torn-down place's device; every check held "
           "after each, and %u times the world built again from nothing on the map had the same hash; power "
           "balanced within %.3f%%. The same on 1 host thread and 3, and from a snapshot at tick %u\n",
           static_cast<unsigned long long>(n), kTicks, static_cast<unsigned long long>(moved),
           static_cast<unsigned long long>(torn), static_cast<unsigned long long>(nc.unlinked),
           static_cast<unsigned long long>(nc.cut), rebuilt, balance * 100, kTicks / 2);
}

// Worlds without a landscape, sources or a network respawn too: the place
// is built again where it is.
void test_plain() {
    const char* const kTexts[3] = {"world.places = 6\nvisit.mean = 100\nwear.rate = 1\nworld.map = none\n",
                                   "world.places = 9\nvisit.mean = 100\nwear.rate = 1\nworld.map = grid\n",
                                   "world.places = 12\nvisit.mean = 100\nwear.rate = 1\nmap.fields = false\n"};
    for (u32 v = 0; v < 3; ++v) {
        u64 hash[2] = {};
        for (u32 pass = 0; pass < 2; ++pass) {
            World w;
            Config cfg;
            Config::ParseError pe;
            TextBuf why;
            CHECK(cfg.parse(Str(kTexts[v], strlen(kTexts[v])), &pe) && w.init(cfg, &why));
            CHECK(w.respawning());
            w.start_threads(pass ? 3 : 1);
            for (u32 i = 0; i < w.place_count(); ++i)
                if (!w.place(i).mem.strict())
                    memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
            for (u32 t = 0; t < 300; ++t) {
                if (t == 50)
                    fail_all(w.place(2));
                if (t % 60 == 59)
                    w.respawn(t / 60 % w.place_count());
                w.tick();
                TextBuf bad;
                bool ok = w.net().check(&bad);
                for (u32 i = 0; ok && i < w.place_count(); ++i)
                    ok = w.place(i).check(&bad);
                if (!ok)
                    printf("  respawn: plain world %u at tick %u: %s\n", v, t, bad.c_str());
                CHECK(ok);
            }
            CHECK(w.respawns >= 6 && w.place(2).incarnation >= 1 && w.map().respawns() == w.respawns);
            if (pass == 0)
                CHECK(same_from_nothing(w, cfg));
            hash[pass] = w.hash();
        }
        CHECK_EQ_U64(hash[0], hash[1]);
    }
    printf("  respawn: worlds with no map, a grid, and a landscape without fields: places torn down and built again "
           "where the map puts them, the same on 1 host thread and 3 and built again from a snapshot\n");
}

} // namespace

// A respawned place's limit on a program's threads (os.thread_limits; 9.6,
// 2026-11-25) is drawn again as its page size is (7.3): the limit the world
// dealt to the place whose field value is nearest the new place's at its
// point. A save taken after the respawns loads to the same limits.
void test_thread_limit_respawn() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build_world(w, cfg, "os.thread_limits = 1 4 32 0\n", &why));
    CHECK(w.respawning());
    crowd(w);
    w.start_threads(3);
    u32 checked = 0, changed = 0;
    for (u32 t = 0; t < 60; ++t) {
        w.tick();
        if (t % 5 != 2)
            continue;
        const u32 i = (t * 7) % w.place_count();
        w.respawn(i);
        const Map& m = w.map();
        const u32 who = m.nearest_dealt(Field::threads, m.field_now(Field::threads, i));
        const u32 page = m.nearest_dealt(Field::page, m.field_now(Field::page, i));
        CHECK(w.place(i).os.settings().thread_limit == w.dealt(who).thread_limit);
        CHECK(w.place(i).mem.page_size() == w.dealt(page).page_size);
        changed += w.place(i).os.settings().thread_limit != w.dealt(i).thread_limit ? 1 : 0;
        ++checked;
    }
    ByteWriter saved;
    snapshot(w, cfg, saved);
    SnapshotHeader h;
    TextBuf err;
    Config::ParseError pe;
    Config again;
    World b;
    CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && snapshot_config(h, &again, &pe) &&
          b.init(again, &why));
    ByteReader in(saved.data() + h.world_offset, h.world_size);
    CHECK(b.load(in, h.version));
    CHECK_EQ_U64(b.hash(), w.hash());
    bool same = true;
    for (u32 i = 0; i < w.place_count(); ++i)
        same = same && b.place(i).os.settings().thread_limit == w.place(i).os.settings().thread_limit;
    CHECK(same && checked == 12);
    printf("  thread limits at respawns: %u places respawned by hand, %u with another limit than they began with\n",
           checked, changed);
}

void suite_respawn() {
    test_thread_limit_respawn();
    test_unmoved();
    test_respawns();
    test_islands();
    test_test_maps();
    test_at_a_visit();
    test_share();
    test_many();
    test_plain();
}
