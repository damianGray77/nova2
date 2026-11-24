// Straits that part and rejoin (design.md 11.6; build-plan-gate5.md step 9,
// gap 13).
//
// - The schedule: which straits part, and their spells, from the seed and
//   the strait alone; the same whatever the programs do.
// - A parted strait's bridges carry nothing: failed links for routing, no
//   visit mends them, and the messages crossing as it parts are lost.
// - Messages for the other side take another way if one is joined, or wait,
//   and cross when the strait joins in time or are lost at their lifetime.
// - With places respawning, and from a snapshot; and the offs.
// - Gate 4's classing worlds: built afresh, the run's respawns replayed and
//   moved to a tick, they have the run's map, places and straits there.
// - The observers' joinings and partings with places respawning: the
//   engine's, each strait known by its two landmasses.
// - The report's straits section reads each place-window and colony by the
//   landmass its place stood on then, a respawn onto another among them.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/io.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

const char* const kLand = "world.places = 48\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "strait.share = 1\n"
                          "strait.spell.min = 50\n"
                          "strait.spell.max = 400\n"
                          "net.lifetime = 1000\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build(World& w, Config& cfg, const char* extra, TextBuf* why, bool say = true) {
    Config::ParseError pe;
    if (!cfg.parse(kLand, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        if (say)
            printf("  straits: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

void crowd(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        place_traffic(p, w.place_count(), i);
    }
}

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

// Whether a strait is parted at a tick, from its spells alone.
bool parted_at(const Links& links, const Strait& s, u64 tick) {
    if (!s.parts)
        return false;
    u64 end = 0;
    for (u64 j = 0;; ++j) {
        end += links.spell_length(s, j);
        if (tick < end)
            return (j & 1) != 0;
    }
}

// After a tick: the invariants; every strait where its spells put it at the
// tick just run; every bridge down with its strait, and shown as parted.
// The straits parted.
u32 straits_ok(World& w) {
    TextBuf why;
    if (!w.links().check(w.tick_count(), &why) || !w.net().check(&why)) {
        printf("  straits: at tick %llu: %s\n", static_cast<ull>(w.tick_count()), why.c_str());
        CHECK(false);
    }
    const Links& links = w.links();
    const u64 t = w.tick_count() - 1;
    u32 parted = 0;
    for (u32 i = 0; i < links.strait_count(); ++i) {
        const Strait& s = links.strait(i);
        CHECK(s.parted() == parted_at(links, s, t));
        CHECK(s.ma < s.mb && s.bridges > 0);
        parted += s.parted() ? 1 : 0;
    }
    const Map& map = w.map();
    for (u32 k = 0; k < map.link_count(); ++k) {
        const Line& l = links.line(k);
        const MapLink& m = map.link(k);
        const bool crossing = m.bridge && map.landmass(m.a) != map.landmass(m.b);
        CHECK((l.strait != kNoLink) == crossing);
        if (!crossing)
            continue;
        const Strait& s = links.strait(l.strait);
        const u32 x = map.landmass(m.a), y = map.landmass(m.b);
        CHECK(s.ma == (x < y ? x : y) && s.mb == (x < y ? y : x));
        CHECK(l.parted == s.parted() && (links.data_down()[k] == 2) == s.parted());
    }
    return parted;
}

// Whether b can be reached from a over the data links that are up, between
// places whose network devices are on.
bool reachable(World& w, u32 a, u32 b) {
    const Map& map = w.map();
    const u32 n = w.place_count();
    Array<u8> seen;
    seen.resize(n);
    Array<u32> todo;
    todo.push(a);
    seen[a] = 1;
    for (usize at = 0; at < todo.size(); ++at) {
        const u32 p = todo[at];
        if (p == b)
            return true;
        for (u32 j = 0; j < map.degree(p); ++j) {
            const u32 k = map.link_at(p, j), q = map.other(k, p);
            if (seen[q] || w.links().data_down()[k] || !w.place(q).network_state.on)
                continue;
            seen[q] = 1;
            todo.push(q);
        }
    }
    return false;
}

// The seed whose map has the most straits, of the first eight.
u32 best_seed() {
    static u32 best = 0;
    if (best)
        return best;
    u32 most = 0;
    for (u32 seed = 1; seed <= 8; ++seed) {
        char extra[64];
        snprintf(extra, sizeof extra, "world.seed = %u\n", seed);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        if (w.links().strait_count() > most) {
            most = w.links().strait_count();
            best = seed;
        }
    }
    CHECK(most >= 3);
    return best;
}

// A seed, of the first sixteen, whose map has a strait with a way round it:
// two landmasses still joined through others when their own bridges go.
u32 round_seed() {
    static u32 found = 0;
    if (found)
        return found;
    for (u32 seed = 1; seed <= 16 && !found; ++seed) {
        char extra[64];
        snprintf(extra, sizeof extra, "world.seed = %u\n", seed);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        const Map& map = w.map();
        for (u32 s = 0; s < w.links().strait_count() && !found; ++s) {
            // From one end of the strait's first bridge to the other, without the strait's bridges.
            u32 a = kNoPlace, b = kNoPlace;
            for (u32 k = 0; k < map.link_count() && a == kNoPlace; ++k)
                if (w.links().line(k).strait == s) {
                    a = map.link(k).a;
                    b = map.link(k).b;
                }
            Array<u8> seen;
            seen.resize(w.place_count());
            Array<u32> todo;
            todo.push(a);
            seen[a] = 1;
            for (usize at = 0; at < todo.size(); ++at) {
                const u32 p = todo[at];
                for (u32 j = 0; j < map.degree(p); ++j) {
                    const u32 k = map.link_at(p, j), q = map.other(k, p);
                    if (seen[q] || w.links().line(k).strait == s)
                        continue;
                    seen[q] = 1;
                    todo.push(q);
                }
            }
            if (seen[b])
                found = seed;
        }
    }
    CHECK(found != 0);
    return found ? found : 1;
}

// The schedule: every strait parts and joins at the ticks its spells give,
// whatever the programs do.
void test_schedule() {
    const u32 ticks = 2500;
    char extra[64];
    snprintf(extra, sizeof extra, "world.seed = %u\n", best_seed());
    Array<u64> flips[2]; // (strait << 32 | tick) at each change
    u32 straits = 0, bridges = 0, most_parted = 0;
    u64 births = 0, partings = 0, joinings = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        if (pass == 1)
            crowd(w);
        const Links& links = w.links();
        CHECK(links.straits() && links.on() && links.parted_straits() == 0 && links.down_count() == 0);
        straits = links.strait_count();
        Array<u8> was;
        was.resize(straits);
        w.start_threads(3);
        for (u32 t = 0; t < ticks; ++t) {
            w.tick();
            const u32 parted = straits_ok(w);
            most_parted = parted > most_parted ? parted : most_parted;
            for (u32 i = 0; i < straits; ++i) {
                const u8 now = links.strait(i).parted() ? 1 : 0;
                if (now != was[i])
                    flips[pass].push(static_cast<u64>(i) << 32 | t);
                was[i] = now;
            }
        }
        if (pass == 0) {
            // The changes are the spells' sums, and each spell is within the settings.
            usize at = 0;
            const Array<u64>& seen = flips[0];
            for (u32 i = 0; i < straits; ++i) {
                const Strait& s = links.strait(i);
                CHECK(s.parts);
                bridges += s.bridges;
                u64 end = 0;
                for (u64 j = 0;; ++j) {
                    const u64 len = links.spell_length(s, j);
                    CHECK(len >= 50 && len <= 400);
                    end += len;
                    if (end >= ticks)
                        break;
                    bool found = false;
                    for (usize k = 0; k < seen.size() && !found; ++k)
                        found = seen[k] == (static_cast<u64>(i) << 32 | end);
                    CHECK(found);
                    ++at;
                }
            }
            CHECK(at == flips[0].size());
            partings = links.partings;
            joinings = links.joinings;
            CHECK(partings + joinings == flips[0].size() && partings >= joinings && partings - joinings <= straits);
        } else {
            for (u32 i = 0; i < w.place_count(); ++i)
                births += w.place(i).births;
            CHECK(births > 100 && w.net().counts().sent > 1000);
        }
    }
    CHECK(flips[0].size() == flips[1].size());
    for (usize i = 0; i < flips[0].size() && i < flips[1].size(); ++i)
        CHECK(flips[0][i] == flips[1][i]);
    printf("  straits: seed %u, %u straits of %u bridges, spells of 50 to 400 ticks, 2,500 ticks: %llu partings and "
           "%llu joinings at the ticks their spells give, up to %u parted at once; the same with replicators and "
           "traffic at work (%llu births)\n",
           best_seed(), straits, bridges, static_cast<ull>(partings), static_cast<ull>(joinings), most_parted,
           static_cast<ull>(births));
}

// The share that part, over many seeds; and the spells' spread.
void test_share() {
    u32 straits = 0, part = 0, bridges = 0, spells = 0, low = 0;
    for (u32 seed = 1; seed <= 30; ++seed) {
        char extra[160];
        snprintf(extra, sizeof extra,
                 "world.seed = %u\nstrait.share = 0.5\nstrait.spell.min = 2000000\nstrait.spell.max = 20000000\n",
                 seed);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        const Links& links = w.links();
        for (u32 i = 0; i < links.strait_count(); ++i) {
            const Strait& s = links.strait(i);
            ++straits;
            bridges += s.bridges;
            CHECK(!s.parted() && s.from == 0);
            if (!s.parts) {
                CHECK(s.until == ~0ull);
                continue;
            }
            ++part;
            CHECK(s.until == links.spell_length(s, 0));
            for (u64 j = 0; j < 8; ++j) {
                const u64 len = links.spell_length(s, j);
                CHECK(len >= 2000000 && len <= 20000000);
                ++spells;
                low += len < 6324555 ? 1 : 0; // the middle of the log scale
            }
        }
    }
    CHECK(straits > 100 && fabs(part - straits * 0.5) < 4 * sqrt(straits * 0.25));
    CHECK(fabs(low - spells * 0.5) < 4 * sqrt(spells * 0.25));
    printf("  straits: 30 seeds of 48 places at a share of a half: %u of %u straits part (%u bridges); of %u spells "
           "of 2 to 20 million ticks, %u under the middle, 6.3 million\n",
           part, straits, bridges, spells, low);
}

struct Carried {
    NetCounts net;
    u64 hash = 0, mid = 0;
    u32 cut_off = 0, round = 0; // pairs looked at with no way between them, and with one round a parted strait
    u64 visits = 0, mended = 0;
};

// A world with traffic for `ticks` ticks. Each tick the routes are checked
// against the links that are up, for a pair of places either side of each
// parted strait.
Carried carry(const char* extra, u32 ticks, u32 threads, ByteWriter* saved = nullptr, u32 save_at = 0,
              const ByteWriter* from = nullptr) {
    Carried c;
    World w;
    Config cfg;
    TextBuf why, err;
    if (from) {
        SnapshotHeader sh;
        Config::ParseError pe;
        CHECK(snapshot_parse(from->data(), from->size(), &sh, &err) && sh.tick == save_at);
        CHECK(snapshot_config(sh, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(from->data() + sh.world_offset, sh.world_size);
        CHECK(w.load(in, sh.version));
        straits_ok(w);
    } else {
        CHECK(build(w, cfg, extra, &why));
        crowd(w);
    }
    w.start_threads(threads);
    const Map& map = w.map();
    while (w.tick_count() < ticks) {
        if (saved && w.tick_count() == save_at) {
            c.mid = w.hash();
            snapshot(w, cfg, *saved);
        }
        if (from && w.tick_count() == save_at)
            c.mid = w.hash();
        w.tick();
        straits_ok(w);
        for (u32 k = 0; k < map.link_count(); ++k) {
            const Line& l = w.links().line(k);
            if (!l.parted || !w.place(l.a).network_state.on || !w.place(l.b).network_state.on)
                continue;
            // The network's routes are worked out for the next tick from
            // the links as they are now.
            const bool way = reachable(w, l.a, l.b);
            CHECK((w.net().exit_end(l.a, l.b) != Network::kNone) == way);
            ++(way ? c.round : c.cut_off);
        }
    }
    c.net = w.net().counts();
    c.hash = w.hash();
    c.mended = w.links().mended;
    for (u32 i = 0; i < w.place_count(); ++i)
        c.visits += w.place(i).visits;
    return c;
}

// Messages and parted straits.
void test_messages() {
    char extra[200];
    const u32 ticks = 2500, at = 900;
    // A long lifetime: messages for the far side wait, and cross when the
    // strait joins.
    snprintf(extra, sizeof extra, "world.seed = %u\nnet.lifetime = 1000\n", round_seed());
    ByteWriter saved;
    const Carried a = carry(extra, ticks, 1, &saved, at);
    CHECK(a.net.parted > 0 && a.net.waited > 0 && a.net.rerouted > 0 && a.net.delivered > 1000);
    CHECK(a.cut_off > 0 && a.round > 0);
    // The same on three host threads, and from a snapshot taken with
    // straits parted and messages waiting.
    const Carried b = carry(extra, ticks, 3);
    CHECK_EQ_U64(a.hash, b.hash);
    const Carried c = carry(extra, ticks, 3, nullptr, at, &saved);
    CHECK_EQ_U64(c.mid, a.mid);
    CHECK_EQ_U64(c.hash, a.hash);
    // A short lifetime: they are lost before it joins.
    snprintf(extra, sizeof extra, "world.seed = %u\nnet.lifetime = 20\n", round_seed());
    const Carried d = carry(extra, ticks, 3);
    CHECK(d.net.expired > a.net.expired && d.net.expired > 20 && d.net.parted > 0);
    // Visits don't mend a parted bridge: with a visit every 100 ticks or so
    // the straits still part and join by their spells (straits_ok), and no
    // link is mended, for none has failed.
    snprintf(extra, sizeof extra, "world.seed = %u\nvisit.mean = 100\n", best_seed());
    const Carried e = carry(extra, 1200, 3);
    CHECK(e.visits > 200 && e.mended == 0 && e.net.parted > 0);
    printf("  straits: seed %u with traffic, 2,500 ticks: %llu messages delivered, %llu lost crossing a strait as it "
           "parted, %llu began a wait, %llu went on by a route (%llu ran out of their 1,000 ticks); %u pairs across a "
           "parted strait had no way and %u a way round, as the routes had it; the same on 1 host thread and 3 "
           "and from a snapshot of tick %u\n",
           round_seed(), static_cast<ull>(a.net.delivered), static_cast<ull>(a.net.parted),
           static_cast<ull>(a.net.waited),
           static_cast<ull>(a.net.rerouted), static_cast<ull>(a.net.expired), a.cut_off, a.round, at);
    printf("  straits: with a lifetime of 20 ticks, %llu ran out waiting; with a visit every 100 ticks (%llu "
           "visits) the straits kept their spells and no link was mended\n",
           static_cast<ull>(d.net.expired), static_cast<ull>(e.visits));
}

// With the sources on the map, links wearing and cut, and places respawning:
// a parted bridge carries no power, and every strait, a new crossing's
// among them, is where its spells from tick 0 put it.
void test_respawning() {
    char extra[900];
    snprintf(extra, sizeof extra,
             "world.seed = %u\nsupply.model = sources\nheat.on = true\nwear.rate = 1\nwear.p.endurance = 500\n"
             "wear.e.endurance = 500\nwear.memory.endurance = 500\nwear.disk.endurance = 400\n"
             "wear.network.endurance = 400\nwear.cooler.endurance = 400\nwear.link.endurance = 2000\n"
             "link.cuts = 0.0003\nvisit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\n"
             "visit.respawn_share = 0.3\nnet.lifetime = 40\n",
             best_seed());
    u64 hash[2] = {};
    u64 respawns = 0, moved = 0, cut = 0, missed = 0;
    u32 first = 0, last = 0, parted_ticks = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        crowd(w);
        w.start_threads(pass == 0 ? 1 : 3);
        first = w.links().strait_count();
        for (u32 t = 0; t < 2000; ++t) {
            w.tick();
            const u32 parted = straits_ok(w);
            if (pass)
                continue;
            parted_ticks += parted ? 1 : 0;
            // No power over a parted bridge.
            for (u32 k = 0; k < w.map().link_count(); ++k) {
                const Line& l = w.links().line(k);
                if (l.parted && l.power != kNoLink)
                    CHECK(w.links().power_down()[l.power] == 1 && w.grid().link_g(l.power) == 0);
            }
        }
        hash[pass] = w.hash();
        respawns = w.respawns;
        moved = w.respawns_moved;
        cut = w.links().cut;
        missed = w.links().missed;
        last = w.links().strait_count();
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(moved > 3 && cut > 10 && parted_ticks > 500);
    printf("  straits: with sources, links wearing and cut and places respawning, 2,000 ticks: %llu respawns (%llu "
           "to a new point), %u straits at the start and %u at the end, each where its spells put it at every "
           "tick; %llu links cut and %llu cuts that found theirs failed or parted; no power over a parted bridge; "
           "the same on 1 host thread and 3\n",
           static_cast<ull>(respawns), static_cast<ull>(moved), first, last, static_cast<ull>(cut),
           static_cast<ull>(missed));
}

// Gate 4's classing worlds (build-plan-gate5.md 11, "Yes, as built"): a
// world built afresh with every other gate 5 strength off, the run's
// respawns up to a tick replayed in order (World::respawn_at) and moved to
// that tick (World::begin_at) has the run's map, places and straits there:
// each place's point and incarnation, its values as its incarnation was
// dealt, the same links and bridges, and every strait in the spell the run
// had it in, its parted bridges carrying no power.
void test_classing_worlds() {
    char extra[900];
    snprintf(extra, sizeof extra,
             "world.seed = %u\nsupply.model = sources\nheat.on = true\nwear.rate = 1\nwear.p.endurance = 500\n"
             "wear.e.endurance = 500\nwear.memory.endurance = 500\nwear.disk.endurance = 400\n"
             "wear.network.endurance = 400\nwear.cooler.endurance = 400\nwear.link.endurance = 2000\n"
             "link.cuts = 0.0003\nvisit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\n"
             "visit.respawn_share = 0.3\nnet.lifetime = 40\n",
             best_seed());
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, extra, &why));
    crowd(w);
    w.start_threads(3);
    TextBuf text;
    cfg.write(text);
    Config::ParseError pe;
    const u32 n = w.place_count();
    Array<u32> inc;
    inc.resize(n);
    inc.fill(0);
    Array<u64> at_tick;
    Array<u32> at_place;
    u32 checked = 0, parted_seen = 0, moved_seen = 0;
    const u64 kAt[3] = {600, 1300, 2000};
    for (u32 c = 0; c < 3; ++c) {
        // A classing at tick T starts where tick T's first solve does: tick
        // T's respawns made and its straits moved, both first in its inputs
        // and nothing else in the tick moving them. So the run is compared
        // once tick T is done, its respawns at tick T among those replayed.
        while (w.tick_count() <= kAt[c]) {
            const u64 t = w.tick_count();
            w.tick();
            for (u32 i = 0; i < n; ++i)
                if (w.map().incarnation(i) != inc[i]) {
                    inc[i] = w.map().incarnation(i);
                    at_tick.push(t);
                    at_place.push(i);
                }
        }
        // The classing's settings: the run's, every gate 5 strength off but
        // the straits.
        World f;
        Config fc;
        CHECK(fc.parse(text.view(), &pe));
        gate5_off(&fc, true);
        fc.set("strait.share", "1");
        CHECK(f.init(fc, &why));
        for (usize k = 0; k < at_tick.size(); ++k)
            f.respawn_at(at_place[k], at_tick[k]);
        f.begin_at(kAt[c]);
        moved_seen = static_cast<u32>(f.respawns_moved);
        CHECK(f.respawns == at_tick.size() && f.respawns_moved == w.respawns_moved);
        CHECK(f.map().link_count() == w.map().link_count() && f.map().power_link_count() == w.map().power_link_count());
        for (u32 i = 0; i < n; ++i) {
            CHECK(f.map().x(i) == w.map().x(i) && f.map().y(i) == w.map().y(i));
            CHECK(f.map().incarnation(i) == w.map().incarnation(i));
            const Place& a = w.place(i);
            const Place& b = f.place(i);
            CHECK(a.core_count() == b.core_count() && a.mem.page_size() == b.mem.page_size() &&
                  a.mem.strict() == b.mem.strict());
        }
        CHECK(f.links().count() == w.links().count() && f.links().strait_count() == w.links().strait_count());
        for (u32 k = 0; k < f.links().count() && k < w.links().count(); ++k) {
            const Line& a = w.links().line(k);
            const Line& b = f.links().line(k);
            CHECK(a.a == b.a && a.b == b.b && a.strait == b.strait && a.parted == b.parted);
            if (a.a != b.a || a.b != b.b || a.strait != b.strait || a.parted != b.parted)
                printf("  straits: at tick %llu, line %u: %u-%u strait %u parted %u in the run, %u-%u strait %u "
                       "parted %u in the classing world\n",
                       static_cast<ull>(kAt[c]), k, a.a, a.b, a.strait, a.parted ? 1u : 0u, b.a, b.b, b.strait,
                       b.parted ? 1u : 0u);
            if (b.parted && b.power != kNoLink)
                CHECK(f.links().power_down()[b.power] == 1 && f.grid().link_g(b.power) == 0);
            parted_seen += b.parted ? 1 : 0;
        }
        for (u32 s = 0; s < f.links().strait_count() && s < w.links().strait_count(); ++s) {
            const Strait& a = w.links().strait(s);
            const Strait& b = f.links().strait(s);
            CHECK(a.ma == b.ma && a.mb == b.mb && a.parts == b.parts && a.spell == b.spell);
            if (a.ma != b.ma || a.mb != b.mb || a.parts != b.parts || a.spell != b.spell)
                printf("  straits: at tick %llu, strait %u: %u-%u spell %llu [%llu, %llu) in the run, %u-%u spell "
                       "%llu [%llu, %llu) in the classing world\n",
                       static_cast<ull>(kAt[c]), s, a.ma, a.mb, static_cast<ull>(a.spell), static_cast<ull>(a.from),
                       static_cast<ull>(a.until), b.ma, b.mb, static_cast<ull>(b.spell), static_cast<ull>(b.from),
                       static_cast<ull>(b.until));
        }
        ++checked;
    }
    CHECK(checked == 3 && at_tick.size() > 3 && moved_seen > 3 && parted_seen > 0);
    printf("  straits: gate 4's classing worlds, built afresh with gate 5 off but the straits: with the run's %llu "
           "respawns (%u to a new point) replayed and moved to ticks 600, 1,300 and 2,000, each had the run's map, "
           "places, links and straits there, %u bridges parted over the three, none carrying power\n",
           static_cast<ull>(at_tick.size()), moved_seen, parted_seen);
}

// The observers' count of the straits joining again (gap 13; build-plan-
// gate5.md 11, Damian's "fix", 2026-11-23). A respawn builds the list of
// straits again from the map (Links::relink), in another order when the
// bridges move, so a strait is known by its two landmasses, not its place in
// the list. With places respawning, every joining the observers record is
// one the engine counts, at its tick and between its two landmasses, and
// every parting is in the events log, the same after the observers are
// saved and read back mid-run. The engine's counts are checked tick by tick
// against the straits' states here, each strait known by its landmasses.
void test_observed_joinings() {
    char extra[900];
    snprintf(extra, sizeof extra,
             "world.seed = %u\nsupply.model = sources\nheat.on = true\nwear.rate = 1\nwear.p.endurance = 500\n"
             "wear.e.endurance = 500\nwear.memory.endurance = 500\nwear.disk.endurance = 400\n"
             "wear.network.endurance = 400\nwear.cooler.endurance = 400\nwear.link.endurance = 2000\n"
             "link.cuts = 0.0003\nvisit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\n"
             "visit.respawn_share = 0.3\nnet.lifetime = 40\n",
             best_seed());
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, extra, &why));
    crowd(w);
    ObserverSettings os;
    Observer obs, back;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(3);
    struct Seen {
        u32 ma, mb;
        bool parted;
    };
    Array<Seen> was, now;
    Array<JoinRec> joined; // the joinings, as worked here
    Array<u64> parted;     // the partings' ticks
    u64 engine_j = 0, engine_p = 0;
    u32 rebuilt = 0, off = 0;
    Observer* on = &obs;
    for (u32 t = 0; t < 2000; ++t) {
        if (t == 1000) {
            FileOut out;
            CHECK(out.open("test_straits_joinings.obs"));
            obs.save(out);
            CHECK(out.close());
            back.init(os);
            FileIn in;
            CHECK(in.open("test_straits_joinings.obs") && back.load(in));
            w.watch(&back);
            on = &back;
        }
        w.tick();
        const Links& L = w.links();
        const u64 tick = w.tick_count() - 1; // the tick just done, as the observers have it
        bool same = was.size() == L.strait_count();
        u64 j = 0, p = 0;
        now.clear();
        for (u32 k = 0; k < L.strait_count(); ++k) {
            const Strait& s = L.strait(k);
            now.push(Seen{s.ma, s.mb, s.parted()});
            same = same && was[k].ma == s.ma && was[k].mb == s.mb;
            for (usize i = 0; i < was.size(); ++i) {
                if (was[i].ma != s.ma || was[i].mb != s.mb)
                    continue;
                if (was[i].parted && !s.parted()) {
                    joined.push(JoinRec{tick, k, s.ma, s.mb});
                    ++j;
                }
                if (!was[i].parted && s.parted()) {
                    parted.push(tick);
                    ++p;
                }
                break;
            }
        }
        rebuilt += !same && !was.empty() ? 1 : 0;
        off += L.joinings - engine_j != j || L.partings - engine_p != p ? 1 : 0;
        engine_j = L.joinings;
        engine_p = L.partings;
        was.clear();
        for (usize k = 0; k < now.size(); ++k)
            was.push(now[k]);
    }
    const Links& L = w.links();
    // The straits' list built again in another order, and the engine's
    // counts the states' changes here, each strait by its landmasses.
    CHECK(rebuilt > 0 && off == 0 && joined.size() == L.joinings && parted.size() == L.partings);
    // The observers' joinings: the engine's, each at its tick between its two
    // landmasses.
    CHECK(on->joinings.size() == joined.size());
    u32 differ = 0;
    for (usize k = 0; k < on->joinings.size() && k < joined.size(); ++k) {
        const JoinRec& a = on->joinings[k];
        const JoinRec& b = joined[k];
        differ += a.tick != b.tick || a.ma != b.ma || a.mb != b.mb || a.strait != b.strait ? 1 : 0;
    }
    CHECK(differ == 0);
    // The partings in the events log, from its first tick on (the oldest let
    // go past kEventsKept, perhaps partway through a tick).
    const u64 from = on->event_log.empty() ? 0 : on->event_log[0].tick;
    u64 logged = 0, want = 0;
    for (usize k = 0; k < on->event_log.size(); ++k)
        logged += on->event_log[k].kind == static_cast<u8>(EventKind::parted) && on->event_log[k].tick > from ? 1 : 0;
    for (usize k = 0; k < parted.size(); ++k)
        want += parted[k] > from ? 1 : 0;
    CHECK(logged == want && want > 0);
    if (on->joinings.size() != joined.size() || differ || logged != want)
        printf("  straits: the observers recorded %llu joinings (%u differing) and %llu partings from tick %llu; "
               "the engine %llu and %llu\n",
               static_cast<ull>(on->joinings.size()), differ, static_cast<ull>(logged), static_cast<ull>(from),
               static_cast<ull>(joined.size()), static_cast<ull>(want));
    printf("  straits: with %llu respawns, the list of straits built again in another order at %u ticks, the "
           "observers recorded the engine's %llu joinings, each at its tick between its two landmasses, and its "
           "partings, read back from a save at tick 1,000 and carried on\n",
           static_cast<ull>(w.respawns), rebuilt, static_cast<ull>(joined.size()));
}

// Where each place stood, for the report's straits section (gap 13;
// build-plan-gate5.md 11, Damian's "fix", 2026-11-23). A respawn may put a
// place on another landmass, so a place-window is read by the landmass its
// place stood on as it began, its incarnation's, and a colony by its
// places' as it was founded: not where they stand at the run's end. With
// places respawning onto other landmasses, each respawn's record has the
// landmass the place stood on before it, and every place-window and colony
// is read by the landmass the run had its place on at its tick, recorded
// here tick by tick; windows either side of such a respawn among them.
void test_landmass_then() {
    char extra[900];
    snprintf(extra, sizeof extra,
             "world.seed = %u\nsupply.model = sources\nheat.on = true\nwear.rate = 1\nwear.p.endurance = 500\n"
             "wear.e.endurance = 500\nwear.memory.endurance = 500\nwear.disk.endurance = 400\n"
             "wear.network.endurance = 400\nwear.cooler.endurance = 400\nwear.link.endurance = 2000\n"
             "link.cuts = 0.0003\nvisit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\n"
             "visit.respawn_share = 0.3\nnet.lifetime = 40\n",
             best_seed());
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, extra, &why));
    crowd(w);
    ObserverSettings os;
    os.turnover.window = 100;
    Observer obs;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(3);
    const u32 n = w.place_count();
    const u64 kTicks = 2000;
    Array<u32> land; // by tick, then place: the landmass each stood on once the tick was done
    land.resize(static_cast<usize>(kTicks) * n);
    for (u64 t = 0; t < kTicks; ++t) {
        w.tick();
        for (u32 p = 0; p < n; ++p)
            land[static_cast<usize>(t) * n + p] = w.map().landmass(p);
    }
    const Map& m = w.map();
    PlaceLands lands;
    place_lands(obs, n, &lands);
    // Each respawn's record: the landmass its place stood on the tick
    // before. Those onto another landmass, by place and tick.
    u32 records_off = 0;
    Array<u32> cross_place;
    Array<u64> cross_tick;
    for (usize k = 0; k < obs.respawn_log.size(); ++k) {
        const RespawnRec& r = obs.respawn_log[k];
        if (r.tick == 0 || r.tick >= kTicks || r.place >= n)
            continue;
        records_off += r.left_mass != land[static_cast<usize>(r.tick - 1) * n + r.place] ? 1 : 0;
        if (r.left_mass != land[static_cast<usize>(r.tick) * n + r.place]) {
            cross_place.push(r.place);
            cross_tick.push(r.tick);
        }
    }
    // Every place-window: read as the run had its place as it began. The
    // place's landmass at the run's end, the old reading, misses some.
    u32 windows = 0, off = 0, old_off = 0, either_side = 0;
    for (usize k = 0; k < obs.ground_windows.size(); ++k) {
        const GroundWindow r = obs.ground_windows[k];
        if (r.place >= n || r.start >= kTicks)
            continue;
        const u32 then = land[static_cast<usize>(r.start) * n + r.place];
        ++windows;
        off += land_then(lands, m, r.place, r.start) != then ? 1 : 0;
        old_off += m.landmass(r.place) != then ? 1 : 0;
    }
    // A respawn onto another landmass with windows of its place either side.
    for (usize c = 0; c < cross_place.size(); ++c) {
        bool before = false, after = false;
        for (usize k = 0; k < obs.ground_windows.size(); ++k) {
            const GroundWindow r = obs.ground_windows[k];
            if (r.place != cross_place[c] || r.start >= kTicks)
                continue;
            before = before || r.start < cross_tick[c];
            after = after || r.start >= cross_tick[c];
        }
        either_side += before && after ? 1 : 0;
    }
    // Each colony's two places, as it was founded.
    u32 colonies = 0, colonies_off = 0;
    for (u32 i = 0; i < obs.colony_count(); ++i) {
        const ColonyRec& C = obs.colony_at(i);
        if (C.first >= kTicks || C.place >= n || C.from >= n)
            continue;
        ++colonies;
        colonies_off += land_then(lands, m, C.place, C.first) != land[static_cast<usize>(C.first) * n + C.place] ||
                                land_then(lands, m, C.from, C.first) != land[static_cast<usize>(C.first) * n + C.from]
                            ? 1
                            : 0;
    }
    CHECK(records_off == 0 && cross_place.size() > 0 && either_side > 0);
    CHECK(windows > 0 && off == 0 && old_off > 0 && colonies_off == 0);
    if (records_off || off || colonies_off)
        printf("  straits: %u respawn records with another landmass left; %u of %u place-windows and %u of %u "
               "colonies read on another landmass than the run had\n",
               records_off, off, windows, colonies_off, colonies);
    printf("  straits: with %llu respawns, %llu onto another landmass (%u with windows of their place either side), "
           "every place-window (%u) and colony (%u) read by the landmass its place stood on then; the place's at "
           "the run's end misses %u of the windows\n",
           static_cast<ull>(obs.respawn_log.size()), static_cast<ull>(cross_place.size()), either_side, windows,
           colonies, old_off);
}

// The offs and the settings.
void test_settings() {
    char extra[120];
    u64 hash[3] = {};
    for (u32 k = 0; k < 3; ++k) {
        snprintf(extra, sizeof extra, "world.seed = %u\n%s", best_seed(),
                 k == 0 ? "strait.share = 0\n" : k == 1 ? "strait.share = 0\nstrait.spell.min = 10\n" : "");
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        crowd(w);
        CHECK(w.links().straits() == (k == 2) && w.links().on() == (k == 2));
        CHECK(k == 2 || w.links().strait_count() == 0);
        w.start_threads(3);
        for (u32 t = 0; t < 600; ++t)
            w.tick();
        hash[k] = w.hash();
        if (k < 2)
            CHECK(w.net().counts().parted == 0 && w.links().partings == 0);
        else
            CHECK(w.links().partings > 0);
        // A share of 0 writes no links' section.
        ByteWriter out;
        snapshot(w, cfg, out);
        bool tag = false;
        const u32 lnk = 0x4C4E4B31;
        for (usize i = 0; i + 4 <= out.size(); ++i)
            tag = tag || memcmp(out.data() + i, &lnk, 4) == 0;
        CHECK(tag == (k == 2));
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(hash[2] != hash[0]);
    World bad;
    Config cfg;
    TextBuf why;
    CHECK(!build(bad, cfg, "strait.spell.min = 500\nstrait.spell.max = 400\n", &why, false));
    printf("  straits: a share of 0 is the world without, whatever the spells' settings, and saves nothing; a "
           "longest spell under the shortest refused\n");
}

} // namespace

void suite_straits() {
    test_schedule();
    test_share();
    test_messages();
    test_respawning();
    test_classing_worlds();
    test_observed_joinings();
    test_landmass_then();
    test_settings();
}
