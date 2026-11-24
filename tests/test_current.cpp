// The replication current (design.md 12.6; build-plan-gate5.md step 9, gap
// 15).
//
// - The schedule: the heading and strength by season, the day, the gusts,
//   and every block's draws, against the definition, tick by tick; the same
//   whatever the programs do.
// - The blocks each link carries follow its alignment with the heading, and
//   none go upstream.
// - A block that finds its way shut is dropped and counted, and never
//   waits; a parted strait carries none.
// - A landed block is written into the region as it is, cut short at the
//   region's end, and wakes a halted core.
// - A hand-placed replicator the current copies comes alive downstream
//   through a static run, is its source's lineage's, is marked as carried,
//   and isn't counted as spread.
// - On 1 host thread and 3, from a snapshot, and the offs.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/hash.h"
#include "nova2/observe.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

const char* const kLand = "world.places = 48\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "current.strength = 60\n"
                          "current.season = 700\n"
                          "supply.day = 400\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
const u8 kFault = encode(kPush, kA); // an idle run has no stack: this faults

constexpr double kTurn = 6.283185307179586;

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
            printf("  current: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        fflush(stdout);
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

bool near(double a, double b, double tol = 1e-12) { return fabs(a - b) <= tol * (1 + fabs(a) + fabs(b)); }

double unit_of(u64 v) { return static_cast<double>(v >> 11) * 0x1.0p-53; }

// --- the current by its definition (current.h) ------------------------------------

double knot_turn(u64 seed, const CurrentSettings& s, u64 k) {
    const double u = unit_of(Rng(seed, Rng::kWorldPlace, kPurposeSeason, k).at(0));
    return k == 0 ? u : s.turn * (2 * u - 1);
}

double knot_heading(u64 seed, const CurrentSettings& s, u64 k) {
    double h = 0;
    for (u64 j = 0; j <= k; ++j)
        h += knot_turn(seed, s, j);
    return h;
}

double knot_strength(u64 seed, const CurrentSettings& s, u64 k) {
    const double u = unit_of(Rng(seed, Rng::kWorldPlace, kPurposeSeason, k).at(1));
    return s.strength_min * pow(s.strength_max / s.strength_min, u);
}

void season_at(u64 seed, const CurrentSettings& s, u64 tick, double* heading, double* strength) {
    const u64 k = tick / s.season;
    const double f = static_cast<double>(tick % s.season) / static_cast<double>(s.season);
    const double ease = f * f * (3 - 2 * f);
    const double h0 = knot_heading(seed, s, k), h1 = knot_heading(seed, s, k + 1);
    const double f0 = knot_strength(seed, s, k), f1 = knot_strength(seed, s, k + 1);
    *heading = h0 + (h1 - h0) * ease;
    *strength = f0 + (f1 - f0) * ease;
}

double day_share(const CurrentSettings& s, u64 tick, u64 phase) {
    if (s.night >= 1)
        return 1;
    const double d = static_cast<double>(s.day), x = static_cast<double>((tick + phase) % s.day);
    const double w = x * (d - x), b = 16 * w / (5 * d * d - 4 * w);
    return s.night + (1 - s.night) * b * b;
}

u64 phase_of(World& w, u32 i) { return w.powered() ? w.sources().phase_of(i) : w.place(i).supply.phase(); }

// A link's weight from place i toward place q, at a heading in turns.
double weight_of(const Map& map, u32 i, u32 q, double heading) {
    const double vx = map.x(q) - map.x(i), vy = map.y(q) - map.y(i);
    const double len = sqrt(vx * vx + vy * vy);
    const double c = len > 0 ? (vx * cos(kTurn * heading) + vy * sin(kTurn * heading)) / len : 0;
    return c > 0 ? c : 0;
}

bool memory_there(const Place& p, u32 addr) {
    const u32 d = p.mem.device_of(addr);
    return !p.memory_worn[d].failed && p.memory_state[d].on;
}

// A world's current, followed tick by tick against the definition.
struct Follow {
    u64 drawn = 0, becalmed = 0, unlit = 0, unread = 0, down = 0, dark = 0; // by hand
    u64 passed = 0;     // drawn blocks with nothing in their way but the queue's room
    double expected = 0, noon_expected = 0;
    u64 noon_drawn = 0; // drawn with the day's share at 0.6 or more
    u64 gusted = 0;     // drawn under a gust
    u64 mark = 0;       // of every drawn block: its place, tick, address, link and spot
    u64 sent = 0, landed = 0, cut_short = 0, flipped = 0, upstream = 0, held = 0;
    u64 compared = 0;   // sent blocks whose bytes were looked at against the memory they came from
    u64 waiting = 0;    // the most messages waiting after a tick
    Array<u32> drawn_at, sent_from, by_pair; // by place; by place x place
    double heading = 0, strength = 0;        // as of the last tick
    bool empty = false;                      // a world with no programs: nothing may ever wait

    struct Pick {
        bool drawn = false, open = false;
        u32 addr = 0, link = kNoLink, to = 0, spot = 0;
    };
    Array<Pick> pick;

    void after_tick(World& w) {
        const u32 n = w.place_count();
        if (pick.size() != n) {
            pick.resize(n);
            drawn_at.resize(n);
            sent_from.resize(n);
            by_pair.resize(static_cast<usize>(n) * n);
        }
        const u64 seed = w.seed(), t = w.tick_count() - 1;
        const CurrentSettings& cs = w.current().settings();
        const Map& map = w.map();
        season_at(seed, cs, t, &heading, &strength);
        CHECK(near(w.current().heading(), heading) && near(w.current().strength(), strength));
        CHECK(near(w.current().dx(), cos(kTurn * heading)) && near(w.current().dy(), sin(kTurn * heading)));
        const bool gusts = w.events().any(PatchType::gust);
        const u8* gone = w.links().on() ? w.links().data_down() : nullptr;
        for (u32 i = 0; i < n; ++i) {
            Place& p = w.place(i);
            Pick& k = pick[i];
            k = Pick{};
            const double gust = gusts ? w.events().gust_at(map.x(i), map.y(i)) : 1;
            const double day = day_share(cs, t, phase_of(w, i));
            const double chance = cs.strength * strength * day * gust / static_cast<double>(cs.day);
            expected += chance;
            noon_expected += day >= 0.6 ? chance : 0;
            const Rng r(seed, i, kPurposeCurrent, t);
            if (!(unit_of(r.at(0)) < chance))
                continue;
            ++drawn;
            ++drawn_at[i];
            noon_drawn += day >= 0.6 ? 1 : 0;
            gusted += gust > 1 ? 1 : 0;
            k.drawn = true;
            u64 hi = 0;
            _umul128(r.at(1), p.mem.size() - cs.block + 1, &hi);
            k.addr = static_cast<u32>(hi);
            k.spot = static_cast<u32>(r.at(3) >> 32);
            double total = 0;
            for (u32 j = 0; j < map.degree(i); ++j)
                total += weight_of(map, i, map.other(map.link_at(i, j), i), heading);
            mark = mix64(mark ^ mix64((static_cast<u64>(i) << 40) ^ (t << 8) ^ k.addr) ^ k.spot);
            if (!(total > 0)) {
                ++becalmed;
                continue;
            }
            double at = unit_of(r.at(2)) * total;
            for (u32 j = 0; j < map.degree(i); ++j) {
                const double wj = weight_of(map, i, map.other(map.link_at(i, j), i), heading);
                if (!(wj > 0))
                    continue;
                k.link = map.link_at(i, j);
                if (at < wj)
                    break;
                at -= wj;
            }
            k.to = map.other(k.link, i);
            mark = mix64(mark ^ k.link);
            if (!p.network_state.on)
                ++unlit;
            else if (!memory_there(p, k.addr) || !memory_there(p, k.addr + cs.block - 1))
                ++unread;
            else if (gone && gone[k.link])
                ++down;
            else if (!w.place(k.to).network_state.on)
                ++dark;
            else
                k.open = true;
            passed += k.open ? 1 : 0;
        }
        const CurrentCounts& c = w.drift;
        CHECK(c.drawn == drawn && c.becalmed == becalmed && c.unlit == unlit && c.unread == unread);
        CHECK(c.down == down && c.dark == dark && c.full + c.sent == passed);
        // The blocks queued at this barrier: each where its draws put it,
        // with the bytes that lay there. (Where something was written into
        // the place's memory later in the barrier, they can't be looked at
        // now: a block landing in its region, or a message read by a RECV.)
        Network& net = w.net();
        Array<u8> written;
        written.resize(n);
        for (u32 d = 0; d < net.delivered_count(); ++d) {
            const Message& g = net.delivered(d);
            if (g.kind == MsgKind::current || (g.kind == MsgKind::data && g.port != 0))
                written[g.dst] = 1;
        }
        for (u32 s = 0; s < net.message_slots(); ++s) {
            const Message& g = net.message_slot(s);
            if (!g.live || g.kind != MsgKind::current || g.sent != t)
                continue;
            const Pick& k = pick[g.src];
            Place& p = w.place(g.src);
            CHECK(k.drawn && k.open && g.dst == k.to && g.size == cs.block && g.tag_seq == k.spot && g.port == 0);
            CHECK(g.flips == 0);
            if (!written[g.src]) {
                CHECK(memcmp(net.data(g), p.mem.bytes() + k.addr, cs.block) == 0);
                ++compared;
            }
            CHECK(g.sender == p.os.holder_of(k.addr, cs.block));
            upstream += weight_of(map, g.src, g.dst, heading) > 0 ? 0 : 1;
            held += g.sender ? 1 : 0;
            ++sent;
            ++sent_from[g.src];
            ++by_pair[static_cast<usize>(g.src) * n + g.dst];
        }
        CHECK(sent == c.sent && c.sent_bytes == c.sent * cs.block && c.held == held);
        // Those delivered at it: written into the region as they arrived,
        // at their spot, cut short at its end. (Of two at a place in one
        // barrier the later may lie over the earlier: the last is looked at.)
        Array<u8> seen;
        seen.resize(n);
        u64 arrived = 0;
        for (u32 d = net.delivered_count(); d-- > 0;) {
            const Message& g = net.delivered(d);
            if (g.kind != MsgKind::current)
                continue;
            ++arrived;
            Place& p = w.place(g.dst);
            if (!memory_there(p, p.mem.region_base()))
                continue;
            ++landed;
            const u32 size = p.mem.region_size();
            const u32 spot = static_cast<u32>((static_cast<u64>(g.tag_seq) * size) >> 32);
            const u32 len = g.size < size - spot ? g.size : size - spot;
            cut_short += len < g.size ? 1 : 0;
            flipped += g.flips ? 1 : 0;
            if (seen[g.dst])
                continue;
            seen[g.dst] = 1;
            CHECK(memcmp(p.mem.bytes() + p.mem.region_base() + spot, net.data(g), len) == 0);
            CHECK(p.region.landing_count() > 0);
            const Landing& l = p.region.landing(p.region.landing_count() - 1);
            CHECK(l.at == spot && l.len == len && l.sender == (g.sender | kCarried));
        }
        CHECK(landed == c.landed && net.counts().drifted == c.landed + c.unlanded);
        CHECK(arrived <= net.counts().drifted);
        // Nothing of the current's ever waits; in a world with no programs,
        // nothing at all does.
        TextBuf why;
        if (!net.check(&why)) {
            printf("  current: at tick %llu: %s\n", static_cast<ull>(t), why.c_str());
            CHECK(false);
        }
        waiting = net.waiting() > waiting ? net.waiting() : waiting;
        CHECK(!empty || net.waiting() == 0);
    }

    // Every block the schedule drew is accounted for.
    void at_end(World& w) {
        const CurrentCounts& c = w.drift;
        const NetCounts& nc = w.net().counts();
        CHECK(c.drawn == c.becalmed + c.unlit + c.unread + c.down + c.dark + c.full + c.sent);
        u64 flying = 0;
        Network& net = w.net();
        for (u32 s = 0; s < net.message_slots(); ++s)
            flying += net.message_slot(s).live && net.message_slot(s).kind == MsgKind::current ? 1 : 0;
        for (u32 d = 0; d < net.delivered_count(); ++d)
            flying -= net.delivered(d).kind == MsgKind::current ? 1 : 0;
        CHECK(c.sent == nc.drifted + nc.adrift + flying);
        CHECK(nc.drifted == c.landed + c.unlanded && upstream == 0);
        u64 blocks = 0, bytes = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            blocks += w.place(i).region.drift_blocks;
            bytes += w.place(i).region.drift_bytes;
        }
        CHECK(blocks == c.landed && bytes == c.landed_bytes);
    }
};

// The schedule, in a world with nothing living and in one crowded with
// replicators and traffic: the same blocks drawn, tick by tick.
void test_schedule() {
    const u32 ticks = 3000;
    Follow f[2];
    u64 drawn_at_500[2] = {};
    for (u32 k = 0; k < 2; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "", &why));
        CHECK(w.drifting());
        if (k)
            crowd(w);
        w.start_threads(2);
        f[k].empty = k == 0;
        for (u32 t = 0; t < ticks; ++t) {
            w.tick();
            f[k].after_tick(w);
            if (t == 499)
                drawn_at_500[k] = f[k].drawn;
        }
        f[k].at_end(w);
        if (k == 0) {
            // The places' days differ, and the day is the supply's shape.
            u32 phases = 0;
            for (u32 i = 1; i < w.place_count(); ++i)
                phases += phase_of(w, i) != phase_of(w, 0) ? 1 : 0;
            CHECK(phases > 30);
            const CurrentSettings& cs = w.current().settings();
            CHECK(near(day_share(cs, 0, 0), 0.2) && near(day_share(cs, 200, 0), 1) && near(day_share(cs, 150, 50), 1));
            for (u32 x = 0; x < 400; x += 7) {
                const double s = sin(3.141592653589793 * x / 400.0);
                CHECK(fabs(day_share(cs, x, 0) - (0.2 + 0.8 * s * s)) < 0.004);
                CHECK(near(w.current().day_at(x, 13), day_share(cs, x, 13)));
            }
            // The seasons: each knot's heading within the turn of the one
            // before, each strength in its range, and the easing between.
            double lo = 1e9, hi = 0, most = 0;
            for (u64 j = 0; j < 400; ++j) {
                const double turn = knot_turn(w.seed(), cs, j + 1), s = knot_strength(w.seed(), cs, j);
                CHECK(fabs(turn) <= cs.turn && s >= cs.strength_min && s <= cs.strength_max);
                CHECK(near(w.current().knot_heading(j), knot_heading(w.seed(), cs, j)));
                CHECK(near(w.current().knot_strength(j), s));
                lo = s < lo ? s : lo;
                hi = s > hi ? s : hi;
                most = fabs(turn) > most ? fabs(turn) : most;
            }
            CHECK(lo < 0.6 && hi > 1.7 && most > 0.11);
            double h = 0, s = 0, hm = 0, sm = 0;
            season_at(w.seed(), cs, 3 * cs.season, &h, &s);
            season_at(w.seed(), cs, 3 * cs.season + cs.season / 2, &hm, &sm);
            CHECK(near(h, knot_heading(w.seed(), cs, 3)) && near(s, knot_strength(w.seed(), cs, 3)));
            CHECK(near(hm, (knot_heading(w.seed(), cs, 3) + knot_heading(w.seed(), cs, 4)) / 2));
            CHECK(near(sm, (knot_strength(w.seed(), cs, 3) + knot_strength(w.seed(), cs, 4)) / 2));
            // Sought out of order, the same.
            Current again;
            again.init(cs, w.seed());
            const u64 at[] = {2999, 12, 701, 700, 699, 5000000, 0, 1399};
            for (u32 j = 0; j < 8; ++j) {
                again.seek(at[j]);
                season_at(w.seed(), cs, at[j], &h, &s);
                CHECK(near(again.heading(), h) && near(again.strength(), s));
            }
        }
    }
    // Whatever the programs do: the heading, the strength, and every block
    // drawn, with its address, link and spot.
    CHECK(f[0].drawn == f[1].drawn && f[0].mark == f[1].mark && f[0].becalmed == f[1].becalmed);
    CHECK(drawn_at_500[0] == drawn_at_500[1] && drawn_at_500[0] > 0);
    CHECK(f[0].heading == f[1].heading && f[0].strength == f[1].strength);
    for (u32 k = 0; k < 2; ++k) {
        // As many as the chances add up to, by day and by night.
        CHECK(fabs(f[k].drawn - f[k].expected) < 5 * sqrt(f[k].expected));
        CHECK(fabs(f[k].noon_drawn - f[k].noon_expected) < 5 * sqrt(f[k].noon_expected));
        CHECK(f[k].noon_drawn * 2 > f[k].drawn && f[k].sent > 500 && f[k].landed > 500 && f[k].cut_short > 20);
    }
    CHECK(f[1].held > 0 && f[0].held == 0);
    CHECK(f[0].compared * 10 > f[0].sent * 7 && f[1].compared * 10 > f[1].sent * 7);
    printf("  current: 48 places, %u ticks, seasons of 700 and days of 400: the heading and strength to the tick "
           "by the definition; %llu blocks drawn (%.1f expected), %llu of them in the brighter part of their "
           "place's day (%.1f expected); the same blocks, addresses, links and spots in a world crowded with "
           "replicators and traffic\n",
           ticks, static_cast<ull>(f[0].drawn), f[0].expected, static_cast<ull>(f[0].noon_drawn),
           f[0].noon_expected);
    printf("  current: with nothing living: %llu becalmed, %llu sent, %llu landed (%llu cut short at the region's "
           "end, %llu with a flipped bit); crowded: %llu sent (%llu found no room), %llu landed, %llu of the sent "
           "held by a program\n",
           static_cast<ull>(f[0].becalmed), static_cast<ull>(f[0].sent), static_cast<ull>(f[0].landed),
           static_cast<ull>(f[0].cut_short), static_cast<ull>(f[0].flipped), static_cast<ull>(f[1].sent),
           static_cast<ull>(f[1].passed - f[1].sent), static_cast<ull>(f[1].landed), static_cast<ull>(f[1].held));
}

// Each link's share: with the heading held, a place's blocks go down its
// links in proportion to their weights, and none across or upstream.
void test_links() {
    const u32 ticks = 6000;
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "current.strength = 200\ncurrent.night = 1\ncurrent.turn = 0\ncurrent.strength.min = 1\n"
                "current.strength.max = 1\nsupply.day = 2000\nnet.queue = 200\nmap.bandwidth.min = 64\n",
                &why));
    w.start_threads(2);
    Follow f;
    f.empty = true;
    for (u32 t = 0; t < ticks; ++t) {
        w.tick();
        f.after_tick(w);
        CHECK(f.strength == 1);
    }
    f.at_end(w);
    const CurrentCounts& c = w.drift;
    CHECK(c.full == 0 && c.unlit == 0 && c.dark == 0 && c.unread == 0 && c.down == 0);
    const Map& map = w.map();
    const u32 n = w.place_count();
    const double heading = knot_heading(w.seed(), w.current().settings(), 0);
    CHECK(near(f.heading, heading));
    u32 none = 0, pairs = 0, shut = 0;
    u64 becalmed = 0;
    double worst = 0;
    for (u32 i = 0; i < n; ++i) {
        double total = 0;
        for (u32 j = 0; j < map.degree(i); ++j)
            total += weight_of(map, i, map.other(map.link_at(i, j), i), heading);
        if (!(total > 0)) {
            ++none;
            becalmed += f.drawn_at[i];
            CHECK(f.sent_from[i] == 0);
            continue;
        }
        CHECK(f.sent_from[i] == f.drawn_at[i]);
        // A tenth of a block a tick.
        CHECK(fabs(f.drawn_at[i] - 0.1 * ticks) < 5 * sqrt(0.1 * ticks));
        for (u32 q = 0; q < n; ++q) {
            double mine = 0;
            for (u32 j = 0; j < map.degree(i); ++j)
                mine += map.other(map.link_at(i, j), i) == q ? weight_of(map, i, q, heading) : 0;
            const u32 got = f.by_pair[static_cast<usize>(i) * n + q];
            if (!(mine > 0)) {
                CHECK(got == 0);
                bool linked = false;
                for (u32 j = 0; j < map.degree(i); ++j)
                    linked = linked || map.other(map.link_at(i, j), i) == q;
                shut += linked ? 1 : 0;
                continue;
            }
            ++pairs;
            const double share = mine / total, want = share * f.sent_from[i];
            const double sd = sqrt(f.sent_from[i] * share * (1 - share));
            CHECK(fabs(got - want) <= 4.5 * sd + 2);
            if (sd > 0 && fabs(got - want) / sd > worst)
                worst = fabs(got - want) / sd;
        }
    }
    CHECK(none > 0 && becalmed == c.becalmed && pairs > 40 && shut > 40);
    printf("  current: the heading held at %.3f of a turn for %u ticks: %llu blocks sent down %u links, each "
           "link's count within %.1f standard deviations of its share by its cosine with the heading; none down "
           "the %u links across or upstream; %u places with none downstream sent nothing (%llu blocks becalmed)\n",
           heading - floor(heading), ticks, static_cast<ull>(f.sent), pairs, worst, shut, none,
           static_cast<ull>(becalmed));
}

// What stands in a block's way drops it: a queue with no room; a place gone
// dark; a failed link or a parted strait. Nothing of the current's waits,
// with a lifetime or without.
void test_drops() {
    // Queues of one message, 1,024 bytes: the smallest there are.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "current.strength = 200\ncurrent.night = 1\nnet.queue = 1\nnet.lifetime = 1000\n", &why));
        Follow f;
        f.empty = true;
        for (u32 t = 0; t < 1500; ++t) {
            w.tick();
            f.after_tick(w);
        }
        f.at_end(w);
        u32 small = 0;
        for (u32 k = 0; k < w.map().link_count(); ++k)
            small += w.net().capacity(k) == 1024 ? 1 : 0;
        CHECK(w.drift.full > 500 && w.drift.sent > 100 && small > 20 && w.net().counts().full == 0);
        printf("  current: with queues of one tick of bandwidth (%u of %u links' at the least, one message): %llu "
               "blocks found no room and were dropped, %llu were sent, none waited\n",
               small, w.map().link_count(), static_cast<ull>(w.drift.full), static_cast<ull>(w.drift.sent));
    }
    // Places blacking out and straits parting, with a lifetime and without;
    // empty, and crowded with traffic that does wait.
    for (u32 k = 0; k < 3; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        char extra[500];
        snprintf(extra, sizeof extra,
                 "current.strength = 100\nsupply.sag = 50\nsupply.dropouts = 1000\nsupply.dropout.min = 2\n"
                 "supply.dropout.max = 300\nsupply.blackout_share = 0.5\nstrait.share = 1\nstrait.spell.min = 50\n"
                 "strait.spell.max = 400\nnet.queue = 40\nnet.lifetime = %u\n",
                 k == 1 ? 0 : 1000);
        CHECK(build(w, cfg, extra, &why));
        if (k == 2)
            crowd(w);
        w.start_threads(2);
        Follow f;
        f.empty = k < 2;
        u64 on_shut = 0;
        for (u32 t = 0; t < 2500; ++t) {
            w.tick();
            f.after_tick(w);
            if (k == 1)
                continue;
            // With a lifetime: no block is left on a link that is down, or
            // toward a place that is dark (it would be waiting).
            const Network& net = w.net();
            for (u32 s = 0; s < net.message_slots(); ++s) {
                const Message& g = net.message_slot(s);
                if (!g.live || g.kind != MsgKind::current || g.at != g.src)
                    continue;
                bool open = false; // a link to there that is up
                for (u32 j = 0; j < w.map().degree(g.src); ++j) {
                    const u32 l = w.map().link_at(g.src, j);
                    open = open || (w.map().other(l, g.src) == g.dst && !w.links().data_down()[l]);
                }
                on_shut += open && w.place(g.dst).network_state.on ? 0 : 1;
            }
        }
        f.at_end(w);
        const CurrentCounts& c = w.drift;
        const NetCounts& nc = w.net().counts();
        CHECK(c.unlit > 20 && c.dark > 20 && c.down > 20 && c.sent > 500 && nc.adrift > 0 && on_shut == 0);
        CHECK(w.links().partings > 5);
        if (k == 2)
            CHECK(nc.waited > 0 && f.waiting > 0);
        else
            CHECK(nc.waited == 0 && nc.stranded == 0 && nc.lost == 0 && nc.cut == 0 && nc.parted == 0);
        printf("  current: places blacking out and straits parting, %s: of %llu blocks drawn, %llu at a place "
               "with its network device off, %llu toward a dark place, %llu at a failed or parted link, %llu with "
               "no room, %llu sent; %llu let go on the way; none ever waited%s\n",
               k == 0 ? "a lifetime of 1,000" : k == 1 ? "no lifetime" : "crowded with traffic",
               static_cast<ull>(c.drawn), static_cast<ull>(c.unlit), static_cast<ull>(c.dark),
               static_cast<ull>(c.down), static_cast<ull>(c.full), static_cast<ull>(c.sent),
               static_cast<ull>(nc.adrift), k == 2 ? ", while programs' messages did" : "");
    }
}

// A landed block wakes a halted core. Every byte of every memory faults, so
// each core starts once, faults and halts, and with a long retry starts
// again only when something writes to its region: a block landing.
void test_wake() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "current.strength = 20\nregion.retry = 1000000\nnoise.cosmic = 0\n", &why));
    const u32 n = w.place_count();
    for (u32 i = 0; i < n; ++i)
        memset(w.place(i).mem.bytes(), kFault, w.place(i).mem.size());
    w.start_threads(2);
    Follow f;
    f.empty = true;
    Array<u64> started, blocks;
    started.resize(n);
    blocks.resize(n);
    Array<u8> landed; // at the barrier before
    landed.resize(n);
    u64 wakes = 0, woken = 0, quiet = 0;
    for (u32 t = 0; t < 3000; ++t) {
        w.tick();
        f.after_tick(w);
        for (u32 i = 0; i < n; ++i) {
            Place& p = w.place(i);
            const u64 more = p.region.started - started[i];
            started[i] = p.region.started;
            u32 working = 0;
            for (u32 c = 0; c < p.core_count(); ++c) {
                working += p.cores[c].working ? 1 : 0;
                CHECK(!p.cores[c].working || p.region.state(c).phase == RunPhase::halted);
            }
            if (t == 0) {
                CHECK(more == working);
            } else if (landed[i]) {
                CHECK(more == working && working > 0);
                ++wakes;
                woken += more;
            } else {
                CHECK(more == 0);
                ++quiet;
            }
            landed[i] = p.region.drift_blocks != blocks[i];
            blocks[i] = p.region.drift_blocks;
        }
    }
    f.at_end(w);
    u64 faulted = 0, all = 0;
    for (u32 i = 0; i < n; ++i) {
        faulted += w.place(i).region.faulted;
        all += w.place(i).region.started;
    }
    CHECK(wakes > 300 && faulted == all && all > woken && w.drift.landed >= wakes);
    printf("  current: every byte a fault and a retry of a million ticks: %llu blocks landed, each waking its "
           "place's halted cores at the next tick (%llu runs started so); in the %llu place-ticks after no "
           "landing, no core started\n",
           static_cast<ull>(w.drift.landed), static_cast<ull>(woken), static_cast<ull>(quiet));
}

// A hand-placed program holding a stretch full of the replicator, upstream;
// every other byte of the world a fault. The current copies blocks of it
// downstream, static runs find the replicator in them, and what they make
// is the holder's lineage's, marked as carried, and no colonist.
// (The replicator here is 62 bytes: 48 of MOV A, which does nothing, and
// then A.5's, copying 62 bytes from where its run started. A static run
// starts where its core's next spot happens to be, and so most often
// part-way into a landed block: a run starting anywhere in the 48 bytes
// comes to the replicator and copies a whole one, itself turned round.
// A.5's own 14 bytes come alive only from a start at their first byte.)
void test_carried() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "current.strength = 200\ncurrent.night = 1\ncurrent.turn = 0\ncurrent.strength.min = 1\n"
                "current.strength.max = 1\nsupply.day = 2000\nnet.queue = 200\nmap.bandwidth.min = 64\n"
                "noise.cosmic = 0\nnoise.p_floor = 0\nnoise.e_floor = 0\nmemory.max_alloc = 16384\n"
                "",
                &why));
    const u32 n = w.place_count();
    const Map& map = w.map();
    for (u32 i = 0; i < n; ++i)
        memset(w.place(i).mem.bytes(), kFault, w.place(i).mem.size());
    // The place to start at: the one with the most lax places downstream.
    const double heading = knot_heading(w.seed(), w.current().settings(), 0);
    u32 a = kNoPlace, most = 0;
    for (u32 i = 0; i < n; ++i) {
        u32 lax = 0;
        for (u32 j = 0; j < map.degree(i); ++j) {
            const u32 q = map.other(map.link_at(i, j), i);
            lax += weight_of(map, i, q, heading) > 0 && !w.place(q).mem.strict() ? 1 : 0;
        }
        if (lax > most) {
            most = lax;
            a = i;
        }
    }
    CHECK(a != kNoPlace);
    Observer obs;
    ObserverSettings os;
    os.sample_every = 100;
    obs.init(os);
    w.watch(&obs);
    // The holder: the replicator end to end through its allocation, and a
    // thread that loops in its last two bytes.
    u8 sled[62];
    memset(sled, encode(kMov, kA), sizeof sled);
    memcpy(sled + 48, kLax, sizeof kLax);
    sled[48 + 1] = sled[48 + 8] = 62; // MOV # 62 (2 mod 3: "both"), and CPY # 62
    Place& pa = w.place(a);
    u32 base = 0, size = 0;
    for (u32 want = 16384; want >= 4096 && !base; want /= 2) {
        base = pa.mem.alloc(1, want, pa.mem.strict() ? 1 : 2, 0);
        size = want;
    }
    CHECK(base != 0);
    for (u32 at = 0; at + 2 < size; ++at)
        pa.mem.bytes()[base + at] = sled[at % 62];
    pa.mem.bytes()[base + size - 2] = encode(kJmp, kImm);
    pa.mem.bytes()[base + size - 1] = 0;
    const u32 id = pa.os.place_by_hand(1, base, base + size - 2, 0);
    CHECK(id != 0);
    u64 holder = 0;
    for (u32 t = 0; t < pa.os.thread_capacity(); ++t)
        if (pa.os.thread(t).state != ThreadState::free && pa.os.thread(t).id == id)
            holder = pa.os.program(pa.os.thread(t).program).uid;
    CHECK(holder != 0);
    w.start_threads(3);
    Follow f;
    StageCriteria crit;
    crit.span = 300;
    Array<LineageClock> real, pretend; // each colony's settling clock: as it is, and as if a colonist had founded it
    u32 holds = 0, settled = 0, would = 0;
    u64 carried = 0, orphans = 0, kin = 0;
    usize seen = 0;
    for (u32 t = 0; t < 6000; ++t) {
        w.tick();
        f.after_tick(w);
        // The tick's births by the current, while they are alive to be
        // looked at: a place's first fills its memory with its copies, and
        // dies of the first allocation refused.
        for (; seen < obs.births.size(); ++seen) {
            const BirthFact& b = obs.births[seen];
            if (!carried_by_current(b.parent_uid))
                continue;
            ++carried;
            const ProgramRec* r = obs.program(b.uid);
            CHECK(b.cause == BirthCause::static_bytes && r && r->carried && r->arrived);
            if (!r)
                continue;
            CHECK(r->parent == parent_of(b.parent_uid) && r->place == b.place);
            if (!parent_of(b.parent_uid)) {
                // The block was nobody's where it was copied (bytes in
                // another region, say): carried, and a lineage of its own.
                ++orphans;
                CHECK(r->origin == r->uid);
                continue;
            }
            // It joined the lineage of the program that held the bytes.
            const ProgramRec* pr = obs.program(r->parent);
            CHECK(r->origin != r->uid && pr && pr->origin == r->origin && pr->place != r->place);
            kin += pr ? 1 : 0;
        }
        if (w.tick_count() % 100)
            continue;
        while (real.size() < obs.colony_count()) {
            real.push(LineageClock{});
            pretend.push(LineageClock{});
        }
        holds = settled = would = 0;
        for (u32 i = 0; i < obs.colony_count(); ++i) {
            const ColonyRec& C = obs.colony_at(i);
            const u32 programs = w.place(C.place).os.live_programs();
            holds += lineage_holds(C.alive, C.alive_copies, programs, crit) ? 1 : 0;
            settled += colony_settles(C, w.tick_count(), programs, crit, &real[i]) ? 1 : 0;
            ColonyRec D = C;
            D.colonist = 1;
            would += colony_settles(D, w.tick_count(), programs, crit, &pretend[i]) ? 1 : 0;
        }
    }
    f.at_end(w);
    // What the regions saw, and what the observers made of it.
    u64 runs = 0, made = 0, arrival_runs = 0;
    u32 peopled = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = w.place(i);
        runs += p.region.carried_runs;
        made += p.region.carried_made;
        arrival_runs += p.region.landed_runs + p.region.arrival_runs;
        peopled += i != a && p.os.live_programs() ? 1 : 0;
    }
    CHECK(f.held > 100 && runs > 0 && made > 0 && made <= runs && arrival_runs == 0 && peopled > 0);
    u64 copies = 0, strays = 0;
    for (usize i = 0; i < obs.births.size(); ++i) {
        const BirthFact& b = obs.births[i];
        if (b.uid == holder) {
            CHECK(b.cause == BirthCause::hand);
            continue;
        }
        CHECK(b.place != a); // nothing comes back upstream
        if (b.cause == BirthCause::thread) {
            ++copies; // a copy one of them made of itself
            continue;
        }
        if (!carried_by_current(b.parent_uid)) {
            // A static run in bytes no longer as they landed (a later block
            // lay over part of them): nobody's, and a lineage of its own.
            CHECK(b.cause == BirthCause::static_bytes && b.parent_uid == 0);
            ++strays;
            continue;
        }
    }
    CHECK(carried == made && kin > 0 && kin + orphans == carried && copies > 0);
    CHECK(obs.births_by_cause[static_cast<u8>(BirthCause::arrival)] == 0);
    // The holder's lineage, beyond its place.
    const LineageRec* L = obs.lineage(holder);
    CHECK(L && L->place == a && L->alive > 1 && L->colonies >= 1 && obs.lineage_count() <= 1 + strays + orphans);
    // Every colony is the current's: no colonist, no founding, and so not
    // spread, though some hold their places; with a colonist they would be.
    u32 founded = 0, own = 0;
    for (u32 i = 0; i < obs.colony_count(); ++i) {
        const ColonyRec& C = obs.colony_at(i);
        CHECK(C.by_current() && C.colonist == 0 && C.foundings == 0 && C.drifted >= 1);
        founded += C.drifted;
        own += C.origin == holder ? 1 : 0;
    }
    obs.sample(w);
    const Sample& s = obs.last_sample();
    CHECK(own >= 1 && obs.colony_count() >= peopled && obs.current_colonies() == obs.colony_count());
    CHECK(founded >= obs.colony_count() && founded <= carried);
    CHECK(s.colonies == 0 && s.colonies_alive == 0 && s.settled == 0 && s.colonists == 0);
    CHECK(holds > 0 && settled == 0 && would > 0);
    printf("  current: a hand-placed program holding %u bytes of the replicator at place %u, every other byte a "
           "fault: %llu blocks a program held were sent downstream; %llu static runs started in landed blocks "
           "and %llu made a program, each its holder's lineage's and marked as carried; they made %llu more by "
           "copying; %llu of the carried came of blocks nobody held, and %llu more programs of bytes no longer "
           "as they landed: lineages of their own; %u places peopled so, %u colonies held by their lineage, "
           "none counted as a colony or as settled (%u would have settled had a colonist founded them)\n",
           size, a, static_cast<ull>(f.held), static_cast<ull>(runs), static_cast<ull>(made),
           static_cast<ull>(copies), static_cast<ull>(orphans), static_cast<ull>(strays), peopled, holds, would);
}

struct Ran {
    u64 hash = 0, mid = 0;
    CurrentCounts c;
    NetCounts net;
    u64 runs = 0, gusted = 0, drawn = 0, mark = 0;
    u32 flying = 0; // the current's blocks on their way when the snapshot was taken
    u32 gusts = 0;  // and the gusts there then
};

const char* const kBusy = "current.strength = 100\npatch.life.min = 100\n"
                          "patch.life.max = 500\nramp.lead.min = 10\nramp.lead.max = 100\nstrait.share = 1\n"
                          "strait.spell.min = 50\nstrait.spell.max = 400\nnet.lifetime = 300\nnet.queue = 40\n";

Ran run(const char* extra, u32 ticks, u32 threads, ByteWriter* saved = nullptr, ByteWriter* counters = nullptr,
        u32 save_at = 0, bool from = false) {
    Ran r;
    World w;
    Config cfg;
    TextBuf why, err;
    if (from) {
        SnapshotHeader sh;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved->data(), saved->size(), &sh, &err) && sh.tick == save_at);
        CHECK(snapshot_config(sh, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved->data() + sh.world_offset, sh.world_size);
        CHECK(w.load(in, sh.version));
        ByteReader cin(counters->data(), counters->size());
        CHECK(w.load_counters(cin));
    } else {
        CHECK(build(w, cfg, extra, &why));
        crowd(w);
    }
    w.start_threads(threads);
    Follow f;
    if (from) { // it goes on from the counts the save carried
        const CurrentCounts& c = w.drift;
        f.drawn = c.drawn;
        f.becalmed = c.becalmed;
        f.unlit = c.unlit;
        f.unread = c.unread;
        f.down = c.down;
        f.dark = c.dark;
        f.passed = c.full + c.sent;
        f.sent = c.sent;
        f.held = c.held;
        f.landed = c.landed;
    }
    while (w.tick_count() < ticks) {
        if (!from && saved && w.tick_count() == save_at) {
            r.mid = w.hash();
            snapshot(w, cfg, *saved);
            w.save_counters(*counters);
            for (u32 s = 0; s < w.net().message_slots(); ++s)
                r.flying += w.net().message_slot(s).live && w.net().message_slot(s).kind == MsgKind::current ? 1 : 0;
            for (u32 k = 0; k < w.events().count(); ++k)
                r.gusts += w.events().now(k).type == static_cast<u8>(PatchType::gust) ? 1 : 0;
        }
        if (from && w.tick_count() == save_at)
            r.mid = w.hash();
        w.tick();
        if (w.drifting())
            f.after_tick(w);
    }
    if (w.drifting())
        f.at_end(w);
    r.hash = w.hash();
    r.c = w.drift;
    r.net = w.net().counts();
    r.gusted = f.gusted;
    r.drawn = f.drawn;
    r.mark = f.mark;
    for (u32 i = 0; i < w.place_count(); ++i)
        r.runs += w.place(i).region.carried_runs;
    return r;
}

bool same(const CurrentCounts& a, const CurrentCounts& b) { return memcmp(&a, &b, sizeof a) == 0; }

// Gusts, straits and traffic together: the same on 1 host thread and 3, and
// from a snapshot taken with blocks on their way and a gust overhead.
void test_same() {
    const u32 ticks = 2400, at = 1100;
    ByteWriter saved, counters;
    char gusty[600];
    snprintf(gusty, sizeof gusty, "%spatch.gust.rate = 30000\n", kBusy);
    const Ran a = run(gusty, ticks, 1, &saved, &counters, at);
    const Ran b = run(gusty, ticks, 3);
    const Ran c = run(gusty, ticks, 2, &saved, &counters, at, true);
    CHECK_EQ_U64(a.hash, b.hash);
    CHECK_EQ_U64(a.hash, c.hash);
    CHECK_EQ_U64(a.mid, c.mid);
    CHECK(same(a.c, b.c) && same(a.c, c.c) && a.runs == b.runs && a.runs == c.runs);
    CHECK(a.net.drifted == c.net.drifted && a.net.adrift == c.net.adrift && a.net.drifted == b.net.drifted);
    CHECK(a.flying > 0 && a.gusts > 0 && a.gusted > 0 && a.c.down > 0 && a.net.adrift > 0 && a.net.waited > 0);
    // Without the gusts: the same blocks, less those the gusts added.
    const Ran d = run(kBusy, ticks, 3);
    CHECK(d.gusted == 0 && d.drawn + a.gusted >= a.drawn && d.drawn < a.drawn && d.hash != a.hash);
    printf("  current: crowded, with traffic, gusts and straits parting, %u ticks: %llu blocks drawn (%llu under a "
           "gust; %llu with no gusts), %llu sent, %llu landed, %llu static runs started in them; the same on 1 "
           "host thread and 3, and from a snapshot of tick %u taken with %u blocks on their way and %u gusts "
           "there\n",
           ticks, static_cast<ull>(a.c.drawn), static_cast<ull>(a.gusted), static_cast<ull>(d.drawn),
           static_cast<ull>(a.c.sent), static_cast<ull>(a.c.landed), static_cast<ull>(a.runs), at, a.flying,
           a.gusts);
}

// The offs: a strength of 0 is the world without, whatever the current's
// other settings, and saves no counts of it.
void test_settings() {
    u64 hash[3] = {};
    usize counted[3] = {};
    for (u32 k = 0; k < 3; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg,
                    k == 0   ? "current.strength = 0\n"
                    : k == 1 ? "current.strength = 0\ncurrent.block = 64\ncurrent.season = 50\ncurrent.night = 1\n"
                             : "",
                    &why));
        crowd(w);
        CHECK(w.drifting() == (k == 2));
        w.start_threads(3);
        for (u32 t = 0; t < 800; ++t)
            w.tick();
        hash[k] = w.hash();
        ByteWriter out;
        w.save_counters(out);
        const u32 cuc = 0x43554331; // "CUC1": the current's counts
        for (usize i = 0; i + 4 <= out.size(); ++i)
            counted[k] += memcmp(out.data() + i, &cuc, 4) == 0 ? 1 : 0;
        const CurrentCounts none;
        CHECK((k == 2) != same(w.drift, none));
        CHECK(k == 2 || (w.net().counts().drifted == 0 && w.net().counts().adrift == 0));
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(hash[2] != hash[0] && counted[0] == 0 && counted[1] == 0 && counted[2] == 1);
    World bad;
    Config c1, c2, c3;
    TextBuf why;
    CHECK(!build(bad, c1, "current.block = 2000\n", &why, false));
    CHECK(!build(bad, c2, "current.strength.min = 3\ncurrent.strength.max = 2\n", &why, false));
    CHECK(build(bad, c3, "current.strength = 0\ncurrent.block = 2000\n", &why));
    // On a map with no positions there is no current.
    World flat;
    Config c4;
    CHECK(build(flat, c4, "world.map = none\n", &why) && !flat.drifting());
    for (u32 t = 0; t < 50; ++t)
        flat.tick();
    CHECK(flat.drift.drawn == 0);
    printf("  current: a strength of 0 is the world without, whatever its other settings, and saves no counts; a "
           "block bigger than a message, and a most strength under the least, refused; no current on a map with "
           "no positions\n");
}

} // namespace

void suite_current() {
    test_schedule();
    test_links();
    test_drops();
    test_wake();
    test_carried();
    test_same();
    test_settings();
}
