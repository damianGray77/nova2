// Gate 5's named checks as the observers read them (build-plan-gate5.md
// step 11 and 2, gap 1; design.md 12.2, 12.5 and 14.1).
//
// - The cascade rule (watch.h) on inputs made by hand, each cascade worked
//   by hand: a chain set off by load, one begun too late, one whose place
//   went off too long after its voltage moved, a fall before a cut-out, a
//   strike and a swell; places that go off in the same tick pass nothing,
//   and a place that has gone off since its neighbour did isn't passed it
//   again.
// - Test worlds (R7): a crowd cut off by hand at a soft source pushes its
//   neighbours past their maximum, and the observers report a cascade of
//   the size worked from the world's record; a strike on a town reports
//   one, named so; a drop-out that takes a source's places in one tick
//   reports none.
// - Worn ground: places worn by hand, and one with a device failed by
//   hand, are labelled worn and the rest fresh, with their holds' ages as
//   placed by hand.
// - The root's ground (root_grounds): windows recorded by hand, out of
//   order, and cascades begun either side of their edges, each read as the
//   window over its first tick has it, and none where none was recorded.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/watch.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

// --- the rule, by hand ---------------------------------------------------------

// A line of places 0 - 1 - 2 - 3, as its neighbours' lists.
const u32 kLineBegin[5] = {0, 1, 3, 5, 6};
const u32 kLineAdj[6] = {1, 0, 2, 1, 3, 2};

// Every place's inputs over ticks 0 to kTicks - 1: 1000 mV under a least
// maximum of 1300, and nothing happening, unless set.
struct Script {
    static constexpr u32 kTicks = 400, kPlaces = 4;
    CascadeInput in[kTicks][kPlaces];
    void reset() {
        for (u32 t = 0; t < kTicks; ++t)
            for (u32 i = 0; i < kPlaces; ++i)
                in[t][i] = CascadeInput{1000, 1300, false, false, false, false};
    }
    void mv(u32 place, u32 from, u32 to, i32 v) {
        for (u32 t = from; t <= to; ++t)
            in[t][place].mv = v;
    }
};

bool has_cascade(const CascadeWatch& w, u32 root, u32 size, u64 start, u64 last, CascadeCause cause) {
    for (usize k = 0; k < w.cascades.size(); ++k) {
        const CascadeRec& c = w.cascades[k];
        if (c.root == root && c.size == size && c.start == start && c.last == last && c.cause == cause)
            return true;
    }
    return false;
}

void test_rule() {
    static Script s;
    s.reset();
    // Ticks 50 to 52: places 0 to 2 cut out in one tick as their voltage
    // falls, and place 1 again two ticks on: nothing passes.
    for (u32 i = 0; i < 3; ++i) {
        s.mv(i, 50, 60, 900);
        s.in[50][i].cut = true;
    }
    s.mv(1, 52, 60, 800);
    s.in[52][1].cut = true;
    // Ticks 100 to 110: load. Place 0 cuts out; place 1's voltage rises past
    // its maximum 2 ticks on and it trips 3 after that; place 2's rises a
    // tick after place 1 trips, and it trips 4 after that: one cascade of 3.
    s.in[100][0].cut = true;
    s.mv(1, 102, 120, 1310);
    s.in[105][1].trip = true;
    s.mv(2, 106, 120, 1310);
    s.in[110][2].trip = true;
    // Ticks 150 to 166: place 0 cuts out, and place 1's rise comes 12 ticks
    // on, too late; it trips, and passes it to place 2, whose rise comes a
    // tick after and which trips 2 after that: a cascade of 2 begun at 1.
    s.in[150][0].cut = true;
    s.mv(1, 162, 175, 1310);
    s.in[163][1].trip = true;
    s.mv(2, 164, 170, 1310);
    s.in[166][2].trip = true;
    // Ticks 200 to 213: place 0 cuts out and place 1's voltage rises at
    // once, but it trips 12 ticks after the rise: nothing.
    s.in[200][0].cut = true;
    s.mv(1, 201, 215, 1310);
    s.in[213][1].trip = true;
    // Ticks 250 to 258: place 0 trips; place 1's voltage falls from 255 and
    // it cuts out at 258: a cascade of 2.
    s.in[250][0].trip = true;
    s.mv(1, 255, 255, 990);
    s.mv(1, 256, 256, 980);
    s.mv(1, 257, 270, 970);
    s.in[258][1].cut = true;
    // Ticks 300 to 303: a strike at place 3 spikes place 2 the same tick;
    // place 3 trips the tick after, place 2 two after that: a cascade of 2,
    // begun by the strike.
    s.in[300][3].struck = true;
    s.in[301][3].trip = true;
    s.mv(2, 300, 300, 1500);
    s.in[303][2].trip = true;
    // Ticks 350 to 360: a swell at place 0 for three ticks; it trips at 355,
    // and place 1, its voltage past its maximum from 356, trips at 360.
    for (u32 t = 350; t <= 352; ++t)
        s.in[t][0].swelled = true;
    s.in[355][0].trip = true;
    s.mv(1, 356, 365, 1310);
    s.in[360][1].trip = true;

    CascadeWatch w;
    w.init(Script::kPlaces);
    u32 returned_right = 0;
    for (u32 t = 0; t < Script::kTicks; ++t) {
        const usize before = w.cascades.size();
        returned_right += w.step(t, s.in[t], Script::kPlaces, kLineBegin, kLineAdj) == before ? 1 : 0;
    }
    CHECK(returned_right == Script::kTicks);
    CHECK(w.cascades.size() == 5);
    CHECK(has_cascade(w, 0, 3, 100, 110, CascadeCause::load));
    CHECK(has_cascade(w, 1, 2, 163, 166, CascadeCause::load));
    CHECK(has_cascade(w, 0, 2, 250, 258, CascadeCause::load));
    CHECK(has_cascade(w, 3, 2, 301, 303, CascadeCause::strike));
    CHECK(has_cascade(w, 0, 2, 355, 360, CascadeCause::swell));
    // A respawned place starts again with nothing known of it.
    CHECK(w.places[1].off == 360 && w.places[1].seen == kCascadeRing);
    w.respawned(1);
    CHECK(w.places[1].off == ~0ull && w.places[1].seen == 0 && w.places[1].cascade == ~0u);
    // The bins.
    CHECK(size_bin(255) == 0 && size_bin(256) == 1 && size_bin(1023) == 1 && size_bin(1024) == 2);
    CHECK(size_bin(4095) == 2 && size_bin(4096) == 3 && size_bin(16383) == 3 && size_bin(16384) == 4);
    CHECK(age_bin(999999) == 0 && age_bin(1000000) == 1 && age_bin(3999999) == 1 && age_bin(4000000) == 2);
    CHECK(age_bin(15999999) == 2 && age_bin(16000000) == 3);
    printf("  watch: the cascade rule on inputs made by hand over 400 ticks of a line of four places: the five "
           "cascades worked by\n  hand (one of 3 set off by load, a fall before a cut-out, a strike's, a "
           "swell's, and one begun where a rise came\n  too late for the first), and none from places going off "
           "in one tick, a going off that came too long after its\n  rise, or a place's going off again\n");
}

// --- test worlds -----------------------------------------------------------------

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

// Three places in a chain, 1 - 0 - 2, and a source placed by hand.
const char* const kChain = "world.places = 3\n"
                           "world.map = grid\n"
                           "map.resistance.min = 1\n"
                           "map.resistance.max = 1\n"
                           "map.border.strength = 0\n"
                           "grid.drain = 0\n"
                           "grid.link_resistance = 2\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 100000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "cooler.share = 0\n"
                           "supply.model = sources\n"
                           "sources.start = 0\n"
                           "sources.chance = 0\n"
                           "source.steady.swing = 0\n"
                           "source.steady.dropouts = 0\n"
                           "source.steady.swells = 0\n";

bool build(World& w, Config& cfg, const char* a, const char* b) {
    Config::ParseError pe;
    if (!cfg.parse(Str(a, strlen(a)), &pe))
        return false;
    Config over; // b's settings replace a's
    if (!over.parse(Str(b, strlen(b)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  watch: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why.c_str());
        return false;
    }
    return true;
}

void place_program(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 128, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    CHECK(p.os.place_by_hand(1, at, at, 0) != 0);
}

i32 least_max(const Place& p) {
    const PowerSettings& s = p.power();
    i32 m = s.memory_maximum < s.network_maximum ? s.memory_maximum : s.network_maximum;
    m = p.os.disk_size() && s.disk_maximum < m ? s.disk_maximum : m;
    for (u32 c = 0; c < p.core_count(); ++c) {
        const i32 k = p.cores[c].kind == DeviceKind::e_core ? s.e.maximum : s.p.maximum;
        m = k < m ? k : m;
    }
    return m;
}

u64 cuts_of(const Place& p) {
    return p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts + p.network_cutouts;
}

u64 trips_of(const Place& p) {
    u64 n = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        n += p.trips[k];
    return n;
}

// A world's record over a stretch of ticks, place by place: the voltage,
// and whether devices cut out or tripped; and the voltage before it.
struct Record {
    static constexpr u32 kMost = 64, kPlaces = 3;
    u64 first = 0;
    u32 ticks = 0;
    i32 mv0[kPlaces];
    i32 mv[kMost][kPlaces];
    bool cut[kMost][kPlaces], trip[kMost][kPlaces];
    u64 cuts0[kPlaces], trips0[kPlaces];
    void begin(World& w) {
        first = w.tick_count();
        ticks = 0;
        for (u32 i = 0; i < kPlaces; ++i) {
            mv0[i] = w.place(i).env.voltage_mv;
            cuts0[i] = cuts_of(w.place(i));
            trips0[i] = trips_of(w.place(i));
        }
    }
    void tick(World& w) {
        w.tick();
        for (u32 i = 0; i < kPlaces && ticks < kMost; ++i) {
            const Place& p = w.place(i);
            mv[ticks][i] = p.env.voltage_mv;
            cut[ticks][i] = cuts_of(p) > cuts0[i];
            trip[ticks][i] = trips_of(p) > trips0[i];
            cuts0[i] = cuts_of(p);
            trips0[i] = trips_of(p);
        }
        ++ticks;
    }
    bool off(u32 t, u32 i) const { return cut[t][i] || trip[t][i]; }
    i32 before(u32 t, u32 i) const { return t ? mv[t - 1][i] : mv0[i]; }
};

// Worked from the record by hand, for a place `i` next to one that went
// off at the record's tick `at`: the first tick its voltage rose past its
// maximum `most` within 10 ticks of that, and the first tick after `at`,
// and within 10 of the rise, that it tripped (~0: none).
void rise_and_trip(const Record& r, u32 i, u32 at, i32 most, u32* rose, u32* tripped) {
    *rose = *tripped = ~0u;
    const u32 from = at > kCascadeTicks ? at - kCascadeTicks : 0;
    for (u32 s = from; s <= at + kCascadeTicks && s < r.ticks && *rose == ~0u; ++s)
        if (r.mv[s][i] > most && r.before(s, i) <= most)
            *rose = s;
    for (u32 t = at + 1; *rose != ~0u && t <= *rose + kCascadeTicks && t < r.ticks && *tripped == ~0u; ++t)
        if (r.trip[t][i] && t >= *rose)
            *tripped = t;
}

// Two worlds the same to the tick of the cut, the twin left alone. The
// heat limits of the cores next to the crowd are set from the twin: just
// above the most their cores reach in it over the next 15 ticks, so a core
// trips only on the heat the cut brings.
void test_rejection() {
    const char* const kSoft = "heat.on = true\nsource.steady.voltage = 1270\nsources.fixed = 1 steady 150 150\n";
    World w, twin;
    Config cfg, cfg2;
    CHECK(build(w, cfg, kChain, kSoft) && build(twin, cfg2, kChain, kSoft));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    place_program(w.place(0), kLax, sizeof kLax);
    place_program(twin.place(0), kLax, sizeof kLax);
    for (u32 t = 0; t < 400; ++t) {
        w.tick();
        twin.tick();
    }
    CHECK(w.hash() == twin.hash() && obs.cascade_watch.cascades.size() == 0);
    i32 most[3];
    for (u32 i = 0; i < 3; ++i)
        most[i] = least_max(w.place(i));
    const i32 mv_before[3] = {w.place(0).env.voltage_mv, w.place(1).env.voltage_mv, w.place(2).env.voltage_mv};
    const double crowd_w = static_cast<double>(w.place(0).draw.total()) / 1e6;
    const u32 threads = w.place(0).os.live_threads();
    i64 hottest[3][2];
    for (u32 i = 0; i < 3; ++i)
        hottest[i][0] = hottest[i][1] = -1000 * kDegree;
    for (u32 t = 0; t < 15; ++t) {
        twin.tick();
        for (u32 i = 1; i < 3; ++i) {
            const Place& q = twin.place(i);
            for (u32 c = 0; c < q.core_count(); ++c) {
                const u32 e = q.cores[c].kind == DeviceKind::e_core ? 1 : 0;
                hottest[i][e] = q.core_temp[c] > hottest[i][e] ? q.core_temp[c] : hottest[i][e];
            }
        }
    }
    // Cut off by hand: every thread at place 0 ends, its cores cut out (a
    // cut-out counted once), and its neighbours' cores at their heat limit.
    Place& p = w.place(0);
    for (u32 t = 0; t < p.os.thread_top(); ++t)
        if (p.os.thread(t).state != ThreadState::free)
            p.os.end_thread(t, DeathCause::cut_out);
    ++p.cutouts[0];
    for (u32 i = 1; i < 3; ++i) {
        w.place(i).heat.limit[kHeatP] = hottest[i][0] + kDegree / 10000;
        w.place(i).heat.limit[kHeatE] = hottest[i][1] + kDegree / 10000;
    }
    Record r;
    r.begin(w);
    for (u32 t = 0; t < 30; ++t)
        r.tick(w);
    // By hand: place 0 went off at the record's first tick; each of its
    // neighbours whose voltage rose past its maximum in time and which then
    // tripped in time joined it.
    u32 rose[3] = {}, tripped[3] = {}, size = 1;
    for (u32 i = 1; i < 3; ++i) {
        CHECK(mv_before[i] <= most[i]); // under their maximum with the crowd there
        rise_and_trip(r, i, 0, most[i], &rose[i], &tripped[i]);
        size += tripped[i] != ~0u ? 1 : 0;
    }
    CHECK(size == 3); // both neighbours pushed past their maximum, and both tripping for it
    const Array<CascadeRec>& cs = obs.cascade_watch.cascades;
    CHECK(cs.size() == 1);
    if (cs.size() == 1) {
        const u64 last = r.first + (tripped[1] > tripped[2] ? tripped[1] : tripped[2]);
        CHECK(cs[0].root == 0 && cs[0].size == size && cs[0].start == r.first && cs[0].last == last);
        CHECK(cs[0].cause == CascadeCause::load && cs[0].sag == 150);
    }
    printf("  watch: load rejection: a crowd of %u threads drawing %.1f W at the middle of three places in a chain, "
           "fed by a\n  source at one end sagging 150 mV at 150 W; with it there the places stood at %d, %d and %d "
           "mV, under their\n  maximum of %d; cut off by hand, the two either side rose past it %u and %u ticks "
           "later and tripped %u and %u\n  ticks after the cut, at heat limits set from a twin left alone: the "
           "observers report one cascade, of %u places\n  as worked from the record, named load, its source "
           "sagging %d mV\n",
           threads, crowd_w, mv_before[0], mv_before[1], mv_before[2], most[1], rose[1], rose[2], tripped[1],
           tripped[2], cs.size() ? cs[0].size : 0, cs.size() ? cs[0].sag : 0);
}


// Storms over the chain, a few patches at a time: no ramps, no other patch
// types; a crowd at the middle place, the town.
const char* const kStorm = "heat.on = true\n"
                           "source.steady.voltage = 1000\n"
                           "sources.fixed = 1 steady 150 20\n"
                           "ramp.lead.min = 1\n"
                           "ramp.lead.max = 0\n"
                           "patch.life.min = 2000\n"
                           "patch.life.max = 2000\n"
                           "patch.still = 1\n"
                           "patch.storm.rate = 160000\n"
                           "patch.storm.radius.min = 5\n"
                           "patch.storm.radius.max = 5\n"
                           "patch.storm.strength.min = 0.00003\n"
                           "patch.storm.strength.max = 0.00003\n"
                           "patch.swell.rate = 0\n"
                           "patch.brownout.rate = 0\n"
                           "patch.heatwave.rate = 0\n"
                           "patch.outage.rate = 0\n"
                           "patch.gust.rate = 0\n";

// Worked by hand from the record: whether place i, next to one that went
// off at the record's tick `at`, was passed it, and when (~0: it wasn't).
// Its first going off after `at`, within twice the window, and none at
// `at` itself; and before it its voltage's move toward that edge (a rise
// past its maximum `most` before a trip, the first drop of the fall before
// a cut-out), begun within the window of both.
u32 passed_at(const Record& r, u32 i, u32 at, i32 most) {
    if (r.off(at, i))
        return ~0u;
    u32 u = at + 1;
    while (u < r.ticks && u <= at + 2 * kCascadeTicks && !r.off(u, i))
        ++u;
    if (u >= r.ticks || u > at + 2 * kCascadeTicks)
        return ~0u;
    const u32 K = kCascadeTicks;
    const u32 from = at > K && u > K ? (at - K > u - K ? at - K : u - K) : (u > K ? u - K : 0);
    const u32 to = at + K < u ? at + K : u;
    if (r.trip[u][i])
        for (u32 s = from; s <= to; ++s)
            if (r.mv[s][i] > most && r.before(s, i) <= most)
                return u;
    if (r.cut[u][i]) {
        u32 s = u;
        while (s > 0 && r.mv[s][i] <= r.mv[s - 1][i])
            --s;
        ++s;
        while (s <= u && r.mv[s][i] >= r.before(s, i))
            ++s;
        if (s <= u && s >= from && s <= to)
            return u;
    }
    return ~0u;
}

// The hottest core of each kind at a place (P, then E).
void hottest_cores(const Place& q, i64* most) {
    most[0] = most[1] = -1000 * kDegree;
    for (u32 c = 0; c < q.core_count(); ++c) {
        const u32 e = q.cores[c].kind == DeviceKind::e_core ? 1 : 0;
        most[e] = q.core_temp[c] > most[e] ? q.core_temp[c] : most[e];
    }
}

// One strike on the town, watched: its record from the tick before it
// (its tick 1 is the strike's, 2 the one after), and the cascades begun in
// it, worked by hand and as the observers report them. `limit`: place 1's
// heat limits set by hand as the strike lands (nullptr: as built); `warm`:
// place 1's hottest cores, at each tick of the record.
struct StrikeRun {
    Record r;
    u32 size = 1, passed[3] = {~0u, ~0u, ~0u}, last = 2;
    u32 reported = 0;
    CascadeRec seen{};
    i64 warm[Record::kMost][2];
};

void strike_run(u64 ts, const i64* limit, StrikeRun* x) {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kChain, kStorm));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    place_program(w.place(0), kLax, sizeof kLax);
    for (u64 t = 0; t + 1 < ts; ++t)
        w.tick();
    if (limit) {
        w.place(1).heat.limit[kHeatP] = limit[0];
        w.place(1).heat.limit[kHeatE] = limit[1];
    }
    i32 most[3];
    for (u32 i = 0; i < 3; ++i)
        most[i] = least_max(w.place(i));
    x->r.begin(w);
    for (u32 t = 0; t < 30; ++t) {
        x->r.tick(w);
        hottest_cores(w.place(1), x->warm[t]);
    }
    CHECK(x->r.off(2, 0)); // the town went off the tick after the strike
    for (u32 i = 1; i < 3; ++i) {
        x->passed[i] = passed_at(x->r, i, 2, most[i]);
        x->size += x->passed[i] != ~0u ? 1 : 0;
        x->last = x->passed[i] != ~0u && x->passed[i] > x->last ? x->passed[i] : x->last;
    }
    const Array<CascadeRec>& cs = obs.cascade_watch.cascades;
    for (usize k = 0; k < cs.size(); ++k)
        if (cs[k].start >= x->r.first && cs[k].start < x->r.first + x->r.ticks) {
            ++x->reported;
            x->seen = cs[k];
        }
}

// Three worlds the same to a strike on the town: the first after tick 200
// that takes the town off the tick after it lands. In the second, watched,
// the town and the neighbour away from the source go off in that one tick
// (the strike's own draw takes the voltage down around it) and nothing is
// passed on. In the third, the neighbour at the source, whose cores are
// warming, has its heat limit set by hand at the most they reach in the
// second 4 ticks after the strike, so it trips some ticks after the town,
// and the strike's cascade reaches it.
void test_strike() {
    u64 ts = 0;
    u32 strikes = 0;
    {
        World a;
        Config ca;
        CHECK(build(a, ca, kChain, kStorm));
        place_program(a.place(0), kLax, sizeof kLax);
        u64 struck = ~0ull, off0 = cuts_of(a.place(0)) + trips_of(a.place(0));
        for (u64 t = 0; t < 200000 && !ts; ++t) {
            a.tick();
            const u64 off = cuts_of(a.place(0)) + trips_of(a.place(0));
            if (struck != ~0ull && t == struck + 1 && off > off0)
                ts = struck;
            off0 = off;
            if (a.struck(0)) {
                ++strikes;
                struck = t >= 200 ? t : ~0ull;
            }
        }
    }
    CHECK(ts > 1);
    static StrikeRun as_built, warmed;
    strike_run(ts, nullptr, &as_built);
    CHECK(as_built.r.off(2, 2) && as_built.size == 1 && as_built.reported == 0);
    // Each kind's limit where its hottest core is warming from the strike's
    // tick on; out of reach where it isn't.
    i64 limit[2];
    u32 warming = 0;
    for (u32 e = 0; e < 2; ++e) {
        const bool up = as_built.warm[5][e] > as_built.warm[1][e] && as_built.warm[1][e] > -1000 * kDegree;
        limit[e] = up ? as_built.warm[5][e] : 1000 * kDegree;
        warming += up ? 1 : 0;
    }
    CHECK(warming > 0);
    strike_run(ts, limit, &warmed);
    CHECK(warmed.passed[1] != ~0u && warmed.passed[1] > 2 && warmed.size == 2 && warmed.reported == 1);
    if (warmed.reported == 1) {
        const CascadeRec& c = warmed.seen;
        CHECK(c.root == 0 && c.size == warmed.size && c.start == warmed.r.first + 2);
        // Its last going off: the neighbour's, or a later one of the town's
        // own, passed back to it within the window.
        CHECK(c.last >= warmed.r.first + warmed.last && c.cause == CascadeCause::strike);
    }
    const Record& r = as_built.r;
    printf("  watch: a strike on a town: storms over the same chain, a crowd at its middle place; the first "
           "strike there after\n  tick 200 that took it off the tick after (the %u%s to land there in the run, at "
           "tick %llu) took it to %d mV,\n  the neighbour at the source to %d and the other to %d; the tick after, "
           "the town and the other went off\n  together, as the strike's own draw took them down to %d and %d mV, "
           "and nothing was passed on: no cascade.\n  With the cores at the source, warming, set by hand to trip "
           "at the heat they reach 4 ticks on, that place\n  went off %u ticks after the town, and the observers "
           "report one cascade, of %u places as worked from the\n  record, named strike\n",
           strikes, strikes % 10 == 1 && strikes % 100 != 11   ? "st"
                    : strikes % 10 == 2 && strikes % 100 != 12 ? "nd"
                    : strikes % 10 == 3 && strikes % 100 != 13 ? "rd"
                                                               : "th",
           static_cast<ull>(ts), r.mv[1][0], r.mv[1][1], r.mv[1][2], r.mv[2][0], r.mv[2][2],
           warmed.passed[1] != ~0u ? warmed.passed[1] - 2 : 0, warmed.size);
}

// Drop-outs of the one source: every place goes off in the tick it drops,
// and nothing is passed on.
void test_dropout() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kChain,
                "heat.on = false\nsource.steady.voltage = 1000\nsources.fixed = 1 steady 150 20\n"
                "source.steady.dropouts = 900\n"));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    place_program(w.place(0), kLax, sizeof kLax);
    u64 cuts[3] = {};
    u32 together = 0, apart = 0;
    for (u64 t = 0; t < 6000; ++t) {
        w.tick();
        u32 off = 0;
        for (u32 i = 0; i < 3; ++i) {
            off += cuts_of(w.place(i)) > cuts[i] ? 1 : 0;
            cuts[i] = cuts_of(w.place(i));
        }
        together += off == 3 ? 1 : 0;
        apart += off && off < 3 ? 1 : 0;
    }
    CHECK(together >= 2 && apart == 0 && obs.cascade_watch.cascades.size() == 0);
    printf("  watch: drop-outs: the same chain on a source that drops out about once in 1,100 ticks; over 6,000 "
           "ticks %u\n  drop-outs took every place off in one tick, and the observers report no cascade\n",
           together);
}


// --- worn ground ---------------------------------------------------------------

const char* const kFour = "world.places = 4\n"
                          "world.map = none\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "hw.cores.max = 4\n"
                          "os.strict_share = 0\n"
                          "region.retry = 3\n"
                          "noise.p_floor = 0\n"
                          "noise.e_floor = 0\n"
                          "noise.cosmic = 0\n"
                          "heat.on = false\n";

// Four places, each with the replicator's lineage holding it. At tick 300
// place 0's cores are worn by hand, a third of the way from their
// threshold to the place's voltage, and one of place 1's cores fails; the
// holds standing then are placed by hand at ages in each of the four bins.
// The windows from 300 on are worn at places 0 and 1 and fresh at 2 and 3,
// and the one from 400 holds the holds at their ages as placed.
void test_worn() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kFour, ""));
    for (u32 i = 0; i < w.place_count(); ++i) // a test world (R7)
        memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    os.turnover.window = 100;
    obs.init(os);
    StageCriteria sc;
    sc.members = 5;
    obs.settle_by(sc);
    w.watch(&obs);
    for (u32 t = 0; t < 300; ++t)
        w.tick();
    const u64 now = w.tick_count();
    static const u64 kAge[4] = {2000000, 5000000, 20000000, 0}; // 0: left as it is, under a million
    static const u32 kBin[4] = {1, 2, 3, 0};
    u32 placed[4] = {};
    for (u32 i = 0; i < 4; ++i)
        for (u32 k = 0; k < obs.lineage_count(); ++k) {
            const LineageRec& L = obs.lineage_at(k);
            if (L.place == i && L.origin && kAge[i] && obs.place_hold(false, k, now - kAge[i]))
                ++placed[i];
            else if (L.place == i && L.origin && !kAge[i] && obs.place_hold(false, k, now - 100))
                ++placed[i]; // its own age, about: the window's
        }
    for (u32 i = 0; i < 4; ++i)
        CHECK(placed[i] >= 1);
    Place& p0 = w.place(0);
    const i32 mv = p0.env.voltage_mv;
    for (u32 c = 0; c < p0.core_count(); ++c) {
        const i32 thr = p0.cores[c].kind == DeviceKind::e_core ? p0.power().e.threshold : p0.power().p.threshold;
        p0.cores[c].worn_mv = (mv - thr) / 3;
    }
    w.place(1).fail_core(0);
    for (u32 t = 0; t < 200; ++t)
        w.tick();
    u32 rows = 0, right = 0;
    u32 slow[4] = {}, failed[4] = {}, held[4][kAgeBins] = {}, programs[4] = {};
    for (usize k = 0; k < obs.ground_windows.size(); ++k) {
        const GroundWindow g = obs.ground_windows[k];
        if (g.start < 300 || g.place >= 4)
            continue;
        ++rows;
        right += g.worn == (g.place < 2) ? 1 : 0;
        if (g.start != 400)
            continue;
        slow[g.place] = g.slow_ppm;
        failed[g.place] = g.failed;
        for (u32 a = 0; a < kAgeBins; ++a)
            held[g.place][a] = g.held[a];
        programs[g.place] = g.holder_programs;
        CHECK(g.holder_programs > 0 && g.holder_programs <= g.starters);
    }
    CHECK(rows == 8 && right == 8);
    CHECK(slow[0] >= 100000 && failed[0] == 0 && slow[1] < 100000 && failed[1] == 100);
    CHECK(slow[2] < 100000 && failed[2] == 0 && slow[3] < 100000 && failed[3] == 0);
    for (u32 i = 0; i < 4; ++i)
        for (u32 a = 0; a < kAgeBins; ++a)
            CHECK(held[i][a] == (a == kBin[i] ? placed[i] : 0));
    printf("  watch: worn ground: four places a lineage held; at tick 300 place 0's cores worn by hand (its cores "
           "%.1f%% slower\n  at its voltage, on average over the window from 400) and a core of place 1's failed: "
           "the windows from 300\n  worn at those two and fresh at the others; the holds placed by hand at ages "
           "of 2, 5 and 20 million ticks and\n  left at under one, each in its bin as the window from 400 began "
           "(holders' programs then: %u, %u, %u, %u)\n",
           slow[0] / 1e4, programs[0], programs[1], programs[2], programs[3]);
}

// --- the root's ground ------------------------------------------------------------

// Windows of 100 ticks recorded by hand, out of order: place 0's from 0
// (fair), 100 (dim) and 200 (hot); place 1's from 100 (hot and dim) and 300
// (fair), none from 200. Cascades begun either side of their edges: a
// window from s holds the ticks done after s, to s + 100.
void test_roots() {
    struct Win {
        u32 place;
        u64 start;
        Ground ground;
        bool both;
    };
    static const Win kWin[] = {{1, 300, Ground::fair, false},
                               {0, 200, Ground::hot, false},
                               {0, 0, Ground::fair, false},
                               {1, 100, Ground::hot, true},
                               {0, 100, Ground::dim, false}};
    Records<GroundWindow> wins;
    for (u32 k = 0; k < sizeof kWin / sizeof kWin[0]; ++k) {
        GroundWindow g{};
        g.place = kWin[k].place;
        g.start = kWin[k].start;
        g.ground = kWin[k].ground;
        g.both = kWin[k].both;
        wins.push(g);
    }
    struct Root {
        u32 root;
        u64 start;
        u8 want;
    };
    const u8 fair = static_cast<u8>(Ground::fair), hot = static_cast<u8>(Ground::hot);
    const u8 dim = static_cast<u8>(Ground::dim);
    const Root kRoot[] = {{0, 1, fair},       {0, 100, fair},      {0, 101, dim},      {0, 300, hot},
                          {0, 301, kRootNone}, {1, 50, kRootNone}, {1, 150, kRootBoth}, {1, 250, kRootNone},
                          {1, 400, fair},      {2, 10, kRootNone}};
    const u32 n = sizeof kRoot / sizeof kRoot[0];
    Array<CascadeRec> cs;
    for (u32 k = 0; k < n; ++k) {
        CascadeRec c;
        c.root = kRoot[k].root;
        c.start = c.last = kRoot[k].start;
        c.size = 2;
        cs.push(c);
    }
    Array<u8> got;
    root_grounds(wins, cs, 100, &got);
    u32 right = 0;
    for (u32 k = 0; k < n && k < got.size(); ++k)
        right += got[k] == kRoot[k].want ? 1 : 0;
    CHECK(got.size() == n && right == n);
    // With no window's length, or no windows, none is known.
    Records<GroundWindow> none;
    u32 unknown = 0;
    root_grounds(wins, cs, 0, &got);
    for (u32 k = 0; k < got.size(); ++k)
        unknown += got[k] == kRootNone ? 1 : 0;
    root_grounds(none, cs, 100, &got);
    for (u32 k = 0; k < got.size(); ++k)
        unknown += got[k] == kRootNone ? 1 : 0;
    CHECK(unknown == 2 * n);
    printf("  watch: the root's ground: windows of 100 ticks recorded by hand, out of order, at two places; of %u "
           "cascades\n  begun either side of their edges, %u read as the window over their first tick has it "
           "(none where none was\n  recorded)\n",
           n, right);
}

} // namespace

void suite_watch() {
    test_rule();
    test_rejection();
    test_strike();
    test_dropout();
    test_worn();
    test_roots();
}
