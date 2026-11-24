// Ramps, regional events and strikes (nova2/events.h; design.md 12.2 to
// 12.4; build-plan-gate5.md step 7): a ramp's shape, its leads and its false
// alarms; the sources' drop-outs and swells rising over their leads, holding
// and easing, and with leads of 0 starting at once as gate 4's; patches
// forming at their rates, on land by the density, moving, turning and ending
// as functions of the seed and the tick; what each type scales, softer
// toward its edge, overlapping patches multiplying; a strike in the solve
// against a solve in doubles; a heat wave raising what its places cool
// toward; and worlds with all of it, on one host thread and three, through
// a snapshot taken mid-storm, and the same with nothing living.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/events.h"
#include "nova2/heat.h"
#include "nova2/links.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/snapshot.h"
#include "nova2/solve.h"
#include "nova2/sources.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

bool parse_over(Config& cfg, const char* base, const char* extra) {
    Config::ParseError pe;
    if (!cfg.parse(Str(base, strlen(base)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    return true;
}

u32 share(double v) { return static_cast<u32>(v * kCapacityOne + 0.5); }

// --- a ramp ----------------------------------------------------------------------------

void test_ramp() {
    RampSettings rs;
    RampLaw law;
    law.init(rs);
    CHECK(law.on() && law.longest() == 2000);
    const u32 n = 200000;
    u32 alarms = 0, shorter = 0;
    u64 least = ~0ull, most = 0;
    for (u32 k = 0; k < n; ++k) {
        const Ramp r = law.draw(5, k % 97, kPurposeRampSwell, k);
        least = r.lead < least ? r.lead : least;
        most = r.lead > most ? r.lead : most;
        shorter += r.lead < 200 ? 1 : 0; // the middle of 20 to 2,000 on a log scale
        if (r.false_alarm) {
            ++alarms;
            CHECK(r.peak >= share(0.1) && r.peak <= share(0.5));
        } else {
            CHECK(r.peak == kCapacityOne);
        }
    }
    CHECK(least == 20 && most == 2000);
    CHECK(fabs(alarms - n * 0.25) < 5 * sqrt(n * 0.25 * 0.75));
    CHECK(fabs(shorter - n * 0.5) < 5 * sqrt(n * 0.25) + n * 0.002);
    // The shape: a straight rise over the lead, the hold, and an ease as
    // long as the rise.
    Ramp r;
    r.lead = 100;
    const u64 hold = 250;
    CHECK(RampLaw::span(r, hold) == 450);
    CHECK(RampLaw::level(r, 0, hold) == 0 && RampLaw::level(r, 50, hold) == kCapacityOne / 2);
    CHECK(RampLaw::level(r, 25, hold) == kCapacityOne / 4 && RampLaw::level(r, 425, hold) == kCapacityOne / 4);
    CHECK(RampLaw::level(r, 400, hold) == kCapacityOne / 2 && RampLaw::level(r, 450, hold) == 0);
    CHECK(RampLaw::level(r, 449, hold) > 0 && RampLaw::level(r, 100000, hold) == 0);
    for (u64 age = 0; age < 450; ++age) {
        const u32 a = RampLaw::level(r, age, hold), b = RampLaw::level(r, age + 1, hold);
        CHECK(age < 100 ? b > a : age < 350 ? a == kCapacityOne && b == a : b < a);
    }
    // A false alarm: up to its share over the lead, and down at once. The
    // event's own length never comes.
    r.false_alarm = true;
    r.peak = kCapacityOne / 4;
    CHECK(RampLaw::span(r, hold) == 200);
    CHECK(RampLaw::level(r, 50, hold) == kCapacityOne / 8 && RampLaw::level(r, 100, hold) == kCapacityOne / 4);
    CHECK(RampLaw::level(r, 150, hold) == kCapacityOne / 8 && RampLaw::level(r, 200, hold) == 0);
    for (u64 age = 0; age < 200; ++age)
        CHECK(RampLaw::level(r, age, hold) <= kCapacityOne / 4);
    // With the longest lead at 0: no lead and no false alarms, so an event
    // is at its full strength from its first tick to its last, as gate 4's.
    rs.lead_max = 0;
    law.init(rs);
    CHECK(!law.on() && law.longest() == 0);
    for (u32 k = 0; k < 1000; ++k) {
        const Ramp o = law.draw(5, k, kPurposeRampDropout, k);
        CHECK(o.lead == 0 && !o.false_alarm && o.peak == kCapacityOne && RampLaw::span(o, 7) == 7);
        CHECK(RampLaw::level(o, 0, 7) == kCapacityOne && RampLaw::level(o, 6, 7) == kCapacityOne);
        CHECK(RampLaw::level(o, 7, 7) == 0);
    }
    printf("  events: of %u ramps drawn, %u false alarms (%.1f%%), leads from %llu to %llu ticks, %.1f%% under "
           "200; the rise, the hold and the ease to the tick\n",
           n, alarms, 100.0 * alarms / n, static_cast<ull>(least), static_cast<ull>(most), 100.0 * shorter / n);
}

// --- the sources' drop-outs and swells -------------------------------------------------

struct Built {
    Map map;
    Sources src;
    SourceSettings ss;
    RampLaw law;
};

const char* const kSources = "supply.model = sources\n"
                             "sources.life = 4000\n"
                             "sources.start = 25\n"
                             "supply.day = 200\n"
                             "source.steady.swing_period = 300\n"
                             "source.daylight.swing_period = 300\n"
                             "source.gusty.swing_period = 100\n"
                             "source.steady.dropouts = 1000\n"
                             "source.daylight.dropouts = 1000\n"
                             "source.gusty.dropouts = 1000\n"
                             "source.gusty.swells = 1000\n"
                             "supply.dropout.max = 50\n"
                             "sources.swell.longest = 60\n"
                             "world.places = 48\n";

bool build(Built& b, u64 seed, const char* extra) {
    Config cfg;
    if (!parse_over(cfg, kSources, extra))
        return false;
    MapSettings ms;
    read_map_settings(cfg, &ms);
    read_source_settings(cfg, &b.ss);
    if (!cfg.ok()) {
        printf("  events: settings: %s\n", cfg.value_error());
        return false;
    }
    b.law.init(b.ss.ramp);
    TextBuf why;
    return b.map.build(seed, 48, ms, &why) && b.src.init(b.ss, &b.map, seed, &why);
}

// A gusty source that lasts the run, with nothing growing or fading.
Source gusty_of(const Sources& s) {
    for (u32 i = 0;; ++i) {
        Source o = s.make(200 + i, 0);
        if (o.type != 2)
            continue;
        o.life = 1ull << 40;
        o.fixed = true;
        return o;
    }
}

// A source's swell factor and its drop-outs' capacity at a tick, by the
// definition: of every swell that began in the last 8,192 ticks, the
// highest of 1 + its ramp's level x (its factor - 1); and of every
// drop-out, the lowest of 1 - its ramp's level x (1 - what it keeps).
u32 swell_by_hand(const Built& b, u64 seed, const Source& o, u64 tick) {
    u32 f = kCapacityOne;
    const u64 size = b.src.swell_block(), j = tick / size, back = 8192 / size;
    for (u64 k = j > back ? j - back : 0; k <= j; ++k) {
        Swell w;
        if (!b.src.swell_in(o, k, &w) || tick < w.start)
            continue;
        const u64 level = RampLaw::level(b.law.draw(seed, o.index, kPurposeRampSwell, k), tick - w.start, w.length);
        const u32 up = kCapacityOne + static_cast<u32>((level * (w.factor - kCapacityOne)) >> kCapacityBits);
        f = up > f ? up : f;
    }
    return f;
}

u32 dropout_by_hand(const Built& b, u64 seed, const Source& o, u64 tick) {
    u32 c = kCapacityOne;
    const u64 size = b.src.dropout_stretch(o), j = tick / size, back = 8192 / size;
    for (u64 k = j > back ? j - back : 0; k <= j; ++k) {
        Dropout d;
        if (!b.src.dropout_in(o, k, &d) || tick < d.start)
            continue;
        const u64 level = RampLaw::level(b.law.draw(seed, o.index, kPurposeRampDropout, k), tick - d.start, d.length);
        const u32 keep = kCapacityOne - static_cast<u32>((level * (kCapacityOne - d.keep)) >> kCapacityBits);
        c = keep < c ? keep : c;
    }
    return c;
}

void test_sources() {
    // At the default leads: tick by tick against the definition.
    {
        const u64 seed = 6;
        Built b;
        CHECK(build(b, seed, "ramp.lead.max = 2000\n"));
        CHECK(b.law.on());
        const Source g = gusty_of(b.src);
        u32 raised = 0, part_up = 0, lowered = 0, part_down = 0;
        for (u64 tick = 0; tick < 60000; ++tick) {
            const u32 f = swell_by_hand(b, seed, g, tick), c = dropout_by_hand(b, seed, g, tick);
            CHECK(b.src.swell(g, tick) == f && b.src.dropout(g, tick) == c);
            raised += f > kCapacityOne ? 1 : 0;
            lowered += c < kCapacityOne ? 1 : 0;
            // Rising or easing: the tick before or after is different.
            part_up += f > kCapacityOne && b.src.swell(g, tick + 1) != f ? 1 : 0;
            part_down += c < kCapacityOne && b.src.dropout(g, tick + 1) != c ? 1 : 0;
            if (tick % 16 == 0) {
                const SourceState st = b.src.state_at(g, tick);
                CHECK(st.swell == f);
            }
        }
        CHECK(raised > 20000 && part_up > raised / 2 && lowered > 20000 && part_down > lowered / 2);
        printf("  events: a gusty source at the default leads, 60,000 ticks against the definition: its voltage "
               "raised for %u of them (%u rising or easing), its capacity lowered for %u (%u falling or coming "
               "back)\n",
               raised, part_up, lowered, part_down);
    }
    // With shorter leads, so that many stand alone: each swell and each
    // drop-out with no other near it, through its rise, its hold and its
    // ease.
    const u64 seed = 9;
    Built b, off;
    CHECK(build(b, seed, "ramp.lead.max = 200\n") && build(off, seed, "ramp.lead.max = 0\n"));
    CHECK(b.law.on() && !off.law.on());
    const Source g = gusty_of(b.src);
    const Source g0 = gusty_of(off.src);
    CHECK(g.index == g0.index);
    u32 swells = 0, swell_alarms = 0, swells_alone = 0, drops = 0, drop_alarms = 0, drops_alone = 0;
    const u64 size = b.src.swell_block();
    const i64 near = static_cast<i64>(640 / size) + 1;
    for (u64 j = 20; j < 200000; ++j) {
        Swell w, w0;
        if (!b.src.swell_in(g, j, &w))
            continue;
        ++swells;
        // The swells are gate 4's own: the ramps are drawn from streams of
        // their own, and move none.
        CHECK(off.src.swell_in(g0, j, &w0) && w0.start == w.start && w0.length == w.length && w0.factor == w.factor);
        CHECK(off.src.swell(g0, w.start) >= w.factor && off.src.swell(g0, w.start + w.length - 1) >= w.factor);
        const Ramp rp = b.law.draw(seed, g.index, kPurposeRampSwell, j);
        CHECK(rp.lead >= 20 && rp.lead <= 200);
        swell_alarms += rp.false_alarm ? 1 : 0;
        bool alone = true;
        for (i64 k = -near; k <= near && alone; ++k) {
            Swell o;
            alone = k == 0 || !b.src.swell_in(g, j + k, &o);
        }
        if (!alone)
            continue;
        ++swells_alone;
        const u64 a = w.start, l = rp.lead, by = w.factor - kCapacityOne;
        CHECK(b.src.swell(g, a) == kCapacityOne);
        if (rp.false_alarm) {
            const u32 top = kCapacityOne + static_cast<u32>((static_cast<u64>(rp.peak) * by) >> kCapacityBits);
            CHECK(b.src.swell(g, a + l) == top && top < w.factor && b.src.swell(g, a + 2 * l) == kCapacityOne);
            for (u64 t = a; t < a + 2 * l; ++t)
                CHECK(b.src.swell(g, t) <= top);
            continue;
        }
        const double half = kCapacityOne + by * static_cast<double>(l / 2) / l;
        CHECK(fabs(b.src.swell(g, a + l / 2) - half) < 2);
        CHECK(fabs(b.src.swell(g, a + l + w.length + l - l / 2) - half) < 2);
        for (u64 t = a + l; t <= a + l + w.length; ++t)
            CHECK(b.src.swell(g, t) == w.factor);
        for (u64 t = a; t < a + l; ++t)
            CHECK(b.src.swell(g, t) <= b.src.swell(g, t + 1) && b.src.swell(g, t) < w.factor);
        for (u64 t = a + l + w.length; t < a + 2 * l + w.length; ++t)
            CHECK(b.src.swell(g, t) >= b.src.swell(g, t + 1));
        CHECK(b.src.swell(g, a + 2 * l + w.length) == kCapacityOne);
    }
    const u64 dsize = b.src.dropout_stretch(g);
    const i64 dnear = static_cast<i64>(640 / dsize) + 1;
    for (u64 j = 20; j < 200000; ++j) {
        Dropout d, d0;
        if (!b.src.dropout_in(g, j, &d))
            continue;
        ++drops;
        CHECK(off.src.dropout_in(g0, j, &d0) && d0.start == d.start && d0.length == d.length && d0.keep == d.keep);
        CHECK(off.src.dropout(g0, d.start) <= d.keep && off.src.dropout(g0, d.start + d.length - 1) <= d.keep);
        const Ramp rp = b.law.draw(seed, g.index, kPurposeRampDropout, j);
        drop_alarms += rp.false_alarm ? 1 : 0;
        bool alone = true;
        for (i64 k = -dnear; k <= dnear && alone; ++k) {
            Dropout o;
            alone = k == 0 || !b.src.dropout_in(g, j + k, &o);
        }
        if (!alone)
            continue;
        ++drops_alone;
        const u64 a = d.start, l = rp.lead, by = kCapacityOne - d.keep;
        CHECK(b.src.dropout(g, a) == kCapacityOne);
        // Gate 4's, with no lead: at what it keeps from its first tick to
        // its last, and whole the tick after.
        CHECK(off.src.dropout(g0, a) == d.keep && off.src.dropout(g0, a + d.length) == kCapacityOne);
        if (rp.false_alarm) {
            const u32 low = kCapacityOne - static_cast<u32>((static_cast<u64>(rp.peak) * by) >> kCapacityBits);
            CHECK(b.src.dropout(g, a + l) == low && b.src.dropout(g, a + 2 * l) == kCapacityOne);
            CHECK(by == 0 || low > d.keep);
            for (u64 t = a; t < a + 2 * l; ++t)
                CHECK(b.src.dropout(g, t) >= low);
            continue;
        }
        const double half = kCapacityOne - by * static_cast<double>(l / 2) / l;
        CHECK(fabs(b.src.dropout(g, a + l / 2) - half) < 2);
        for (u64 t = a + l; t <= a + l + d.length; ++t)
            CHECK(b.src.dropout(g, t) == d.keep);
        for (u64 t = a; t < a + l; ++t)
            CHECK(b.src.dropout(g, t) >= b.src.dropout(g, t + 1));
        for (u64 t = a + l + d.length; t < a + 2 * l + d.length; ++t)
            CHECK(b.src.dropout(g, t) <= b.src.dropout(g, t + 1));
        CHECK(b.src.dropout(g, a + 2 * l + d.length) == kCapacityOne);
        // A blackout at its peak leaves the source no conductance at all.
        if (d.blackout()) {
            const SourceState st = b.src.state_at(g, a + l);
            CHECK(st.capacity == 0 && st.g == 0);
        }
    }
    CHECK(swells > 5000 && swells_alone > 500 && drops > 5000 && drops_alone > 500);
    CHECK(fabs(swell_alarms - swells * 0.25) < 5 * sqrt(swells * 0.1875));
    CHECK(fabs(drop_alarms - drops * 0.25) < 5 * sqrt(drops * 0.1875));
    printf("  events: with leads of 20 to 200 ticks: %u swells, %u of them false alarms (%.1f%%), %u standing "
           "alone followed tick by tick; %u drop-outs, %u false alarms (%.1f%%), %u alone followed; with leads "
           "of 0 the same swells and drop-outs, each at its full strength from its first tick\n",
           swells, swell_alarms, 100.0 * swell_alarms / swells, swells_alone, drops, drop_alarms,
           100.0 * drop_alarms / drops, drops_alone);
}

// --- patches ---------------------------------------------------------------------------

struct Land {
    Map map;
    Events ev;
    EventSettings es;
};

const char* const kPatches = "ramp.lead.max = 2000\n"
                             "patch.storm.rate = 20\n"
                             "patch.swell.rate = 20\n"
                             "patch.brownout.rate = 20\n"
                             "patch.heatwave.rate = 20\n"
                             "patch.outage.rate = 20\n"
                             "patch.gust.rate = 20\n";

bool build_land(Land& l, u64 seed, u32 places, const char* extra) {
    Config cfg;
    char head[64];
    snprintf(head, sizeof head, "world.places = %u\n", places);
    if (!parse_over(cfg, head, extra))
        return false;
    MapSettings ms;
    read_map_settings(cfg, &ms);
    read_event_settings(cfg, &l.es);
    if (!cfg.ok()) {
        printf("  events: settings: %s\n", cfg.value_error());
        return false;
    }
    TextBuf why;
    if (!l.map.build(seed, places, ms, &why) || !l.ev.init(l.es, &l.map, seed, &why)) {
        printf("  events: %s\n", why.c_str());
        return false;
    }
    return true;
}

u64 bits(double v) {
    u64 b;
    memcpy(&b, &v, 8);
    return b;
}

// The patches there now, as one number.
u64 mark(const Events& e) {
    u64 h = 1469598103934665603ull;
    for (u32 k = 0; k < e.count(); ++k) {
        const PatchNow& q = e.now(k);
        const Patch& p = e.patch(k);
        const u64 v[7] = {q.type, p.block, p.slot, bits(q.x), bits(q.y), bits(q.level), bits(q.radius)};
        for (u32 i = 0; i < 7; ++i)
            h = (h ^ v[i]) * 1099511628211ull;
    }
    return h;
}

// Whether a patch is among those there now, and which.
bool there(const Events& e, const Patch& p, u32* which) {
    for (u32 k = 0; k < e.count(); ++k) {
        const Patch& o = e.patch(k);
        if (o.type == p.type && o.block == p.block && o.slot == p.slot) {
            *which = k;
            return true;
        }
    }
    return false;
}

// A type's factor at a point by the definition, from the patches there
// now: each one's strength there is 1 - its distance over its radius,
// times its ramp's level. `sum`: those strengths x each patch's own,
// added; otherwise 1 + (its own - 1) x that, multiplied.
double by_hand(const Events& e, PatchType type, double x, double y, bool sum, u32* over) {
    double f = sum ? 0 : 1;
    *over = 0;
    for (u32 k = 0; k < e.count(); ++k) {
        const PatchNow& q = e.now(k);
        if (q.type != static_cast<u8>(type))
            continue;
        const double d = sqrt((x - q.x) * (x - q.x) + (y - q.y) * (y - q.y));
        if (d >= q.radius)
            continue;
        const double s = (1 - d / q.radius) * q.level;
        if (!(s > 0))
            continue;
        ++*over;
        if (sum)
            f += q.strength * s;
        else
            f *= 1 + (q.strength - 1) * s;
    }
    return f;
}

double at(const Events& e, PatchType type, double x, double y) {
    switch (type) {
    case PatchType::storm: return e.storm_at(x, y);
    case PatchType::swell: return e.swell_at(x, y);
    case PatchType::brownout: return e.brownout_at(x, y);
    case PatchType::heatwave: return e.heat_at(x, y);
    case PatchType::gust: return e.gust_at(x, y);
    default: return e.outage_at(x, y);
    }
}

void test_patches() {
    const u32 places = 200;
    const u64 seed = 12;
    Land l;
    CHECK(build_land(l, seed, places, kPatches));
    const Events& ev = l.ev;
    const Map& map = l.map;
    CHECK(ev.on() && map.kind() == MapKind::landscape);
    const double link = map.mean_length();
    // The land's cells and their densities, to check where patches form.
    Array<u32> cells;
    Array<double> cum;
    map.land_table(&cells, &cum);
    Array<u32> cell_at;
    cell_at.resize(static_cast<usize>(map.raster_w()) * map.raster_h());
    cell_at.fill(~0u);
    double w1 = 0, w2 = 0, w3 = 0;
    for (u32 k = 0; k < cells.size(); ++k) {
        cell_at[cells[k]] = k;
        const double w = cum[k] - (k ? cum[k - 1] : 0);
        w1 += w;
        w2 += w * w;
        w3 += w * w * w;
    }
    const double even = w1 / cells.size(), dense = w2 / w1, dense_sd = sqrt(w3 / w1 - dense * dense);
    CHECK(dense > even * 1.05); // the density isn't flat, so the check below can tell
    // Every patch of 400 stretches of each type.
    const u64 blocks = 400;
    u32 total = 0, still = 0, alarms = 0, short_lived = 0;
    double weight = 0;
    Array<Patch> moving, real;
    for (u32 t = 0; t < kPatchTypes; ++t) {
        const PatchType type = static_cast<PatchType>(t);
        const PatchKind& kind = l.es.kind[t];
        CHECK(ev.block(type) == 65536 && ev.slots(type) >= 1);
        u32 count = 0, count_real = 0;
        for (u64 b = 0; b < blocks; ++b)
            for (u32 slot = 0; slot < ev.slots(type); ++slot) {
                Patch p;
                if (!ev.make(type, b, slot, &p))
                    continue;
                ++count;
                CHECK(p.type == t && p.block == b && p.slot == slot);
                CHECK(p.start >= b * 65536 && p.start < (b + 1) * 65536);
                CHECK(p.life >= l.es.life_min && p.life <= l.es.life_max);
                CHECK(p.radius >= kind.radius_min * link && p.radius <= kind.radius_max * link);
                CHECK(p.strength >= kind.strength_min && p.strength <= kind.strength_max);
                CHECK(p.ramp.lead >= 20 && p.ramp.lead <= 2000);
                CHECK(p.speed >= 0 && p.speed <= l.es.speed * link && fabs(p.turn) <= l.es.turn * 6.2831853072);
                CHECK(p.end() == p.start + (p.ramp.false_alarm ? 0 : p.life) + 2 * p.ramp.lead);
                // On land.
                const u32 cx = static_cast<u32>(p.x * map.raster_h()), cy = static_cast<u32>(p.y * map.raster_h());
                CHECK(cx < map.raster_w() && cy < map.raster_h() && map.land(cx, cy));
                const u32 k = cell_at[static_cast<usize>(cy) * map.raster_w() + cx];
                if (k != ~0u)
                    weight += cum[k] - (k ? cum[k - 1] : 0);
                still += p.speed == 0 ? 1 : 0;
                alarms += p.ramp.false_alarm ? 1 : 0;
                short_lived += p.life < 10000 ? 1 : 0; // the middle of 2,000 to 50,000 on a log scale
                if (p.speed > 0 && moving.size() < 200)
                    moving.push(p);
                if (!p.ramp.false_alarm && count_real < 400) {
                    real.push(p);
                    ++count_real;
                }
            }
        // Its rate: 20 a million ticks for each 100 places.
        const double want = 20.0 / 1e6 * places / 100 * 65536 * blocks;
        CHECK(fabs(count - want) < 5 * sqrt(want));
        total += count;
    }
    CHECK(fabs(still - total / 3.0) < 5 * sqrt(total * 2.0 / 9));
    CHECK(fabs(alarms - total * 0.25) < 5 * sqrt(total * 0.1875));
    CHECK(fabs(short_lived - total * 0.5) < 5 * sqrt(total * 0.25));
    // Where they form follows the density: the average density of the
    // cells they landed on is what a draw by density gives, and not what an
    // even draw over the land would.
    const double landed = weight / total;
    CHECK(fabs(landed - dense) < 5 * dense_sd / sqrt(static_cast<double>(total)));
    CHECK(fabs(landed - even) > 5 * dense_sd / sqrt(static_cast<double>(total)));
    // Movement: a constant speed, the heading turning by the patch's own
    // amount at each day, and nothing else; a still patch never moves.
    const u64 day = l.es.day;
    double worst_step = 0, worst_turn = 0;
    for (u32 i = 0; i < moving.size(); ++i) {
        const Patch& p = moving[i];
        const double step = p.speed / static_cast<double>(day);
        double x0, y0, x1, y1;
        ev.where(p, p.start, &x0, &y0);
        CHECK(x0 == p.x && y0 == p.y);
        const u64 span = p.end() - p.start, days = span / day;
        for (u64 age = 0; age < span && age < 3 * day; ++age) {
            ev.where(p, p.start + age, &x0, &y0);
            ev.where(p, p.start + age + 1, &x1, &y1);
            const double d = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            worst_step = fabs(d - step) > worst_step ? fabs(d - step) : worst_step;
        }
        for (u64 n = 0; n + 1 < days && n < 40; ++n) {
            // The heading in day n, from two points in it; and in day n + 1.
            double ax, ay, bx, by, cx, cy, dx, dy;
            ev.where(p, p.start + n * day, &ax, &ay);
            ev.where(p, p.start + n * day + day, &bx, &by);
            ev.where(p, p.start + (n + 1) * day, &cx, &cy);
            ev.where(p, p.start + (n + 2) * day, &dx, &dy);
            CHECK(fabs(sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) - p.speed) < 1e-9); // a day's travel
            const double h0 = atan2(by - ay, bx - ax), h1 = atan2(dy - cy, dx - cx);
            const double turned = remainder(h1 - h0 - p.turn, 6.283185307179586);
            worst_turn = fabs(turned) > worst_turn ? fabs(turned) : worst_turn;
            if (n == 0)
                CHECK(fabs(remainder(h0 - p.heading, 6.283185307179586)) < 1e-6);
        }
    }
    CHECK(moving.size() == 200 && worst_step < 1e-12 && worst_turn < 1e-6);
    // Each real patch at its edges in time: not there before its start nor
    // at it (its level is 0), at its full level from its lead to its lead
    // and its life, and gone at its end.
    Events probe;
    TextBuf why;
    CHECK(probe.init(l.es, &map, seed, &why));
    u32 followed = 0;
    for (u32 i = 0; i < real.size(); i += 7) {
        const Patch& p = real[i];
        u32 k = 0;
        probe.seek(p.start);
        CHECK(!there(probe, p, &k));
        probe.seek(p.start + p.ramp.lead / 2);
        const double part_way = static_cast<double>(p.ramp.lead / 2) / static_cast<double>(p.ramp.lead);
        CHECK(there(probe, p, &k) && fabs(probe.now(k).level - part_way) < 1e-5);
        probe.seek(p.start + p.ramp.lead);
        CHECK(there(probe, p, &k) && probe.now(k).level == 1 && probe.now(k).radius == p.radius);
        probe.seek(p.start + p.ramp.lead + p.life);
        CHECK(there(probe, p, &k) && probe.now(k).level == 1);
        probe.seek(p.end() - 1);
        CHECK(there(probe, p, &k) && probe.now(k).level > 0 && probe.now(k).level < 0.06);
        probe.seek(p.end());
        CHECK(!there(probe, p, &k));
        ++followed;
    }
    // A function of the seed and the tick: a walk tick by tick, another
    // that jumps about, and one made new for each tick asked, all see the
    // same patches in the same places; another seed sees others.
    const u64 span = 300000, stride = 37;
    Array<u64> marks;
    Land walk, other;
    CHECK(build_land(walk, seed, places, kPatches) && build_land(other, seed + 1, places, kPatches));
    u32 differ = 0, most = 0;
    u64 patch_ticks = 0;
    for (u64 tick = 0; tick < span; ++tick) {
        walk.ev.seek(tick);
        if (tick % stride == 0) {
            marks.push(mark(walk.ev));
            other.ev.seek(tick);
            differ += mark(other.ev) != marks[marks.size() - 1] ? 1 : 0;
        }
        most = walk.ev.count() > most ? walk.ev.count() : most;
        patch_ticks += walk.ev.count();
    }
    CHECK(differ > marks.size() / 2);
    Rng r(seed, 0, 97, 0);
    for (u32 i = 0; i < 20000; ++i) {
        const u64 k = r.below(marks.size());
        probe.seek(k * stride);
        CHECK_EQ_U64(mark(probe), marks[k]);
        CHECK(probe.tick() == k * stride);
    }
    for (u32 i = 0; i < 40; ++i) {
        const u64 k = r.below(marks.size());
        Land fresh;
        CHECK(build_land(fresh, seed, places, kPatches));
        fresh.ev.seek(k * stride);
        CHECK_EQ_U64(mark(fresh.ev), marks[k]);
    }
    // What each type does at a point, against the definition: alone, its
    // own strength at its centre, half way to it at half its radius, and
    // nothing at its edge; where several overlap, their product (their sum
    // for a heat wave's degrees and a storm's chance).
    u32 alone[kPatchTypes] = {}, shared[kPatchTypes] = {}, points = 0;
    for (u32 i = 0; i < real.size(); ++i) {
        const Patch& p = real[i];
        const PatchType type = static_cast<PatchType>(p.type);
        const bool sum = type == PatchType::heatwave || type == PatchType::storm;
        const double none = sum ? 0 : 1;
        probe.seek(p.start + p.ramp.lead + r.below(p.life + 1));
        u32 k = 0, over = 0;
        CHECK(there(probe, p, &k));
        const PatchNow q = probe.now(k);
        CHECK(q.level == 1 && q.strength == p.strength);
        const double a = r.unit() * 6.283185307179586, ca = cos(a), sa = sin(a);
        const double part[5] = {0, 0.25, 0.5, 0.999, 1.25};
        for (u32 j = 0; j < 5; ++j) {
            const double x = q.x + part[j] * q.radius * ca, y = q.y + part[j] * q.radius * sa;
            const double want = by_hand(probe, type, x, y, sum, &over), got = at(probe, type, x, y);
            CHECK(fabs(got - (type == PatchType::storm && want > 1 ? 1 : want)) <= 1e-12 * (1 + fabs(want)));
            ++points;
            if (over >= 2)
                ++shared[p.type];
            if (over != (j < 4 ? 1u : 0u))
                continue;
            ++alone[p.type];
            // Alone: straight from its own strength at the centre to
            // nothing at its radius.
            const double mine = sum ? p.strength * (1 - part[j]) : 1 + (p.strength - 1) * (1 - part[j]);
            CHECK(fabs(got - (j < 4 ? mine : none)) < 1e-9 * (1 + fabs(mine)));
            if (j == 0)
                CHECK(fabs(got - p.strength) <= 1e-12 * (1 + p.strength));
            if (j == 2 && p.strength != none)
                CHECK(type == PatchType::brownout ? got > p.strength && got < 1
                                                  : got < p.strength && got > none);
        }
    }
    for (u32 t = 0; t < kPatchTypes; ++t)
        CHECK(alone[t] > 100 && shared[t] > 5);
    // A swell raises, a brownout lowers, an outage raises; a storm's chance
    // never passes 1.
    CHECK(l.es.kind[1].strength_min >= 1 && l.es.kind[2].strength_max <= 1 && l.es.kind[4].strength_min >= 1);
    printf("  events: 200 places, 400 stretches of 65,536 ticks: %u patches of 6 types at 20 a million ticks per "
           "100 places each; %.1f%% stay put, %.1f%% false alarms, %.1f%% live under 10,000 ticks; they landed on "
           "cells %.2f times as dense as the land's average (%.2f for a draw by density)\n",
           total, 100.0 * still / total, 100.0 * alarms / total, 100.0 * short_lived / total, landed / even,
           dense / even);
    printf("  events: %u moving patches kept their speed to the tick and turned by their own amount each day; %u "
           "followed from start to end; the same patches from a walk of %llu ticks (up to %u at once, %.1f on "
           "average), from 20,000 jumps and from 40 made new; %u points against the definition, %u under two or "
           "more patches of a type\n",
           static_cast<u32>(moving.size()), followed, static_cast<ull>(span), most,
           static_cast<double>(patch_ticks) / span, points,
           shared[0] + shared[1] + shared[2] + shared[3] + shared[4]);
}

// --- a strike in the solve -------------------------------------------------------------

u64 siemens(double s) { return static_cast<u64>(s * 1048576.0 + 0.5); }

void test_strike() {
    double worst = 0, far = 0;
    double rise[6] = {};
    u32 seen[6] = {};
    double at_strike = 0, heat_rise = 0, heat_own = 0, balance = 0;
    for (u32 round = 0; round < 6; ++round) {
        const u32 places = round >= 4 ? 300 : 48;
        Land l;
        CHECK(build_land(l, 40 + round, places, "grid.tolerance = 0.000001\n"));
        const Map& map = l.map;
        Grid grid;
        SolveSettings gs;
        {
            Config cfg;
            CHECK(parse_over(cfg, "grid.tolerance = 0.000001\n", ""));
            read_solve_settings(cfg, &gs);
            CHECK(cfg.ok());
        }
        TextBuf why;
        CHECK(grid.init(gs, &map, &why));
        CHECK(l.ev.strike_uv() == 20000000 && l.ev.strike_g() == siemens(1000.0 / 3));
        Rng r(round, 0, 98, 0);
        Array<u64> load;
        load.resize(places);
        Array<double> gsum, isum;
        gsum.resize(places);
        isum.resize(places);
        Array<u32> src_at;
        Array<i64> src_uv;
        Array<u64> src_g;
        for (u32 i = 0; i < places; ++i)
            load[i] = siemens(1 + static_cast<double>(r.below(2000)) / 100.0);
        for (u32 k = 0; k < places / 8 + 2; ++k) {
            src_at.push(static_cast<u32>(r.below(places)));
            src_uv.push(static_cast<i64>(1000 + r.below(400)) * 1000);
            src_g.push(siemens(100 + static_cast<double>(r.below(900))));
        }
        const u32 struck = static_cast<u32>(r.below(places));
        Array<i64> before;
        before.resize(places);
        u64 heat_before = 0;
        for (u32 pass = 0; pass < 2; ++pass) {
            grid.begin();
            gsum.fill(0);
            isum.fill(0);
            for (u32 i = 0; i < places; ++i)
                grid.load(i, load[i]);
            for (u32 k = 0; k < src_at.size(); ++k) {
                grid.source(src_at[k], src_uv[k], src_g[k]);
                gsum[src_at[k]] += static_cast<double>(src_g[k]);
                isum[src_at[k]] += static_cast<double>(src_g[k]) * static_cast<double>(src_uv[k]);
            }
            if (pass == 1) {
                // The strike: one more source, for this solve.
                grid.source(struck, l.ev.strike_uv(), l.ev.strike_g());
                gsum[struck] += static_cast<double>(l.ev.strike_g());
                isum[struck] += static_cast<double>(l.ev.strike_g()) * static_cast<double>(l.ev.strike_uv());
            }
            grid.solve(2000000);
            // In doubles.
            Array<double> v;
            v.resize(places);
            for (u32 sweep = 0; sweep < 2000000; ++sweep) {
                double move = 0;
                for (u32 i = 0; i < places; ++i) {
                    if (!grid.lit(i))
                        continue;
                    double num = isum[i];
                    double den = static_cast<double>(load[i]) + static_cast<double>(grid.drain_g(i)) + gsum[i];
                    for (u32 j = 0; j < map.power_degree(i); ++j) {
                        const u32 pk = map.power_link_at(i, j);
                        const double g = static_cast<double>(grid.link_g(pk));
                        num += g * v[map.power_other(pk, i)];
                        den += g;
                    }
                    const double nv = den > 0 ? num / den : 0;
                    move = fabs(nv - v[i]) > move ? fabs(nv - v[i]) : move;
                    v[i] = nv;
                }
                if (move < 1e-9)
                    break;
            }
            for (u32 i = 0; i < places; ++i) {
                const double d = fabs(static_cast<double>(grid.uv(i)) - v[i]);
                worst = d > worst ? d : worst;
            }
            const double given = static_cast<double>(grid.given_uw);
            const double taken = static_cast<double>(grid.loads_uw + grid.links_uw + grid.drains_uw);
            const double out = fabs(given - taken) / given;
            balance = out > balance ? out : balance;
            CHECK(out < 1e-3);
            if (pass == 0) {
                for (u32 i = 0; i < places; ++i)
                    before[i] = grid.uv(i);
                heat_before = grid.place_heat(struck);
                continue;
            }
            // How far the spike carries: each place's rise, by its hops
            // from the struck place along the proximity links.
            Array<u32> hops, queue;
            hops.resize(places);
            hops.fill(~0u);
            hops[struck] = 0;
            queue.push(struck);
            for (u32 q = 0; q < queue.size(); ++q) {
                const u32 a = queue[q];
                for (u32 j = 0; j < map.power_degree(a); ++j) {
                    const u32 pk = map.power_link_at(a, j), b = map.power_other(pk, a);
                    if (!grid.link_g(pk) || hops[b] != ~0u)
                        continue;
                    hops[b] = hops[a] + 1;
                    queue.push(b);
                }
            }
            double sum[6] = {};
            u32 count[6] = {};
            for (u32 i = 0; i < places; ++i) {
                const double up = static_cast<double>(grid.uv(i) - before[i]) / 1e6;
                if (hops[i] == ~0u) {
                    far = fabs(up) > far ? fabs(up) : far; // not joined to it: nothing reaches
                    continue;
                }
                CHECK(up > -1e-4);
                // No neighbour further off rises more than the nearest
                // place on its way: the spike falls off along the links.
                const u32 h = hops[i] < 5 ? hops[i] : 5;
                sum[h] += up;
                ++count[h];
            }
            for (u32 h = 0; h < 6; ++h) {
                if (!count[h])
                    continue;
                rise[h] += sum[h] / count[h];
                ++seen[h];
            }
            CHECK(count[1] > 0 && sum[0] > sum[1] / count[1]);
            if (count[2])
                CHECK(sum[1] / count[1] > sum[2] / count[2]);
            // Its own loss heats the place it struck: the square of the
            // volts across its resistance, over the resistance.
            const u64 own = power_uw(l.ev.strike_uv() - grid.uv(struck), l.ev.strike_g());
            CHECK(grid.place_heat(struck) >= heat_before + own - own / 1000);
            at_strike += static_cast<double>(grid.uv(struck)) / 1e6;
            heat_rise += static_cast<double>(grid.place_heat(struck) - heat_before) / 1e6;
            heat_own += static_cast<double>(own) / 1e6;
        }
    }
    CHECK(worst < 200 && far < 1e-3);
    for (u32 h = 0; h + 1 < 6; ++h)
        CHECK(!seen[h + 1] || rise[h] / seen[h] > rise[h + 1] / seen[h + 1]);
    printf("  events: a strike of 20 V behind 3 milliohms on 6 landscapes: every place within %.0f microvolts of a "
           "solve in doubles, with it and without; the struck place went to %.2f V on average; the rise fell off "
           "by hops along the links: %.3f V at the place, %.3f at 1 hop, %.3f at 2, %.3f at 3, %.3f at 4, %.3f "
           "further; its place took %.1f W more heat that tick, %.1f W of it the strike's own loss; the power "
           "balanced within %.4f%%\n",
           worst, at_strike / 6, rise[0] / 6, seen[1] ? rise[1] / seen[1] : 0, seen[2] ? rise[2] / seen[2] : 0,
           seen[3] ? rise[3] / seen[3] : 0, seen[4] ? rise[4] / seen[4] : 0, seen[5] ? rise[5] / seen[5] : 0,
           heat_rise / 6, heat_own / 6, balance * 100);
}

// --- worlds ----------------------------------------------------------------------------

const char* const kWorld = "world.places = 40\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "supply.model = sources\n"
                           "heat.on = true\n"
                           "wear.rate = 1\n"
                           "link.cuts = 0.0006\n"
                           "net.lifetime = 30\n"
                           "visit.mean = 150\n"
                           "visit.respawn_share = 2\n"
                           "ramp.lead.min = 10\n"
                           "ramp.lead.max = 100\n";

const char* const kAll = "patch.life.min = 100\npatch.life.max = 600\n"
                         "patch.storm.rate = 4000\n"
                         "patch.storm.strength.min = 0.01\npatch.storm.strength.max = 0.03\n"
                         "patch.swell.rate = 4000\n"
                         "patch.brownout.rate = 4000\n"
                         "patch.heatwave.rate = 4000\n"
                         "patch.outage.rate = 4000\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build_world(World& w, Config& cfg, const char* extra) {
    TextBuf text, why;
    text.append(kWorld);
    text.append(Str(extra, strlen(extra)));
    Config::ParseError pe;
    if (!cfg.parse(text.view(), &pe))
        return false;
    if (!w.init(cfg, &why)) {
        printf("  events: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why.c_str());
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

// What a run with every type of patch went through.
struct Went {
    u64 hash_mid = 0, hash_end = 0;
    Array<u64> marks;
    u64 strikes = 0, patch_cuts = 0;
    double strikes_due = 0, cuts_due = 0;
    u64 raised = 0, lowered = 0, sources = 0; // source-ticks under a swell patch, a brownout, and in all
    double least_g = 1, most_uv = 1;
    double balance = 0;
    double struck_v = 0, struck_heat = 0;
    u64 struck_failed = 0, births = 0;
    double struck_temp = 0; // the struck place's rise that tick, in degrees
    // By hops from a struck place, 0 to 3: the rise in volts that tick; and
    // of the places with every device working then, three ticks on: how
    // many, their devices, those failed, and the places with none left.
    double rise[4] = {};
    u64 reached[4] = {}, watched[4] = {}, devices[4] = {}, failed_later[4] = {}, killed[4] = {};
    u32 mid_patches = 0, mid_ramping = 0;
    u64 patch_ticks = 0;
};

// Runs it for `first` ticks and `rest` more. `check`: each tick against the
// definitions. `saved`: a snapshot at `first`.
void run_world(World& w, Config& cfg, u32 first, u32 rest, bool check, ByteWriter* saved, Went* g) {
    const Events& ev = w.events();
    const Map& map = w.map();
    Array<u32> failed, since, hop_of, hops, queue;
    Array<i64> temp, volts;
    failed.resize(w.place_count());
    since.resize(w.place_count());
    hop_of.resize(w.place_count());
    hops.resize(w.place_count());
    temp.resize(w.place_count());
    volts.resize(w.place_count());
    for (u32 t = 0; t < first + rest; ++t) {
        if (t == first) {
            g->hash_mid = w.hash();
            g->mid_patches = ev.count();
            for (u32 k = 0; k < ev.count(); ++k)
                g->mid_ramping += ev.now(k).level < 1 ? 1 : 0;
            if (saved)
                snapshot(w, cfg, *saved);
        }
        if (check)
            for (u32 i = 0; i < w.place_count(); ++i)
                failed[i] = w.place(i).failed_devices();
        if (check)
            for (u32 i = 0; i < w.place_count(); ++i) {
                temp[i] = w.place(i).temp;
                volts[i] = w.grid().uv(i);
            }
        const u64 cuts0 = w.links().patch_cut + w.links().patch_missed;
        w.tick();
        CHECK(!ev.on() || ev.tick() == w.tick_count() - 1);
        g->marks.push(mark(ev));
        g->patch_ticks += ev.count();
        if (!check)
            continue;
        TextBuf why;
        if (!w.links().check(w.tick_count(), &why) || !w.net().check(&why)) {
            printf("  events: at tick %llu: %s\n", static_cast<ull>(w.tick_count()), why.c_str());
            CHECK(false);
        }
        Grid& grid = w.grid();
        const double given = static_cast<double>(grid.given_uw);
        const double taken = static_cast<double>(grid.loads_uw + grid.links_uw + grid.drains_uw);
        const double out = given > 1e6 ? fabs(given - taken) / given : 0;
        g->balance = out > g->balance ? out : g->balance;
        // The sources the solve was given: each of the map's, its voltage
        // times the swell patches over its landing point and its
        // conductance times the brownouts.
        u32 gi = 0, over = 0;
        Sources& src = w.sources();
        for (u32 k = 0; k < src.count(); ++k) {
            const Source& s = src.source(k);
            const SourceState& st = src.state(k);
            const double up = by_hand(ev, PatchType::swell, s.x, s.y, false, &over);
            g->raised += over ? 1 : 0;
            const double down = by_hand(ev, PatchType::brownout, s.x, s.y, false, &over);
            g->lowered += over ? 1 : 0;
            ++g->sources;
            CHECK(up >= 1 && down <= 1 && down >= 0);
            const i64 uv = static_cast<i64>(static_cast<double>(st.uv) * up + 0.5);
            const u64 cond = static_cast<u64>(static_cast<double>(st.g) * down);
            if (!cond || uv <= 0)
                continue;
            CHECK(gi < grid.source_count() && grid.source_place(gi) == s.place);
            CHECK(llabs(grid.source_uv(gi) - uv) <= 1);
            CHECK(grid.source_g(gi) + 1 >= cond && grid.source_g(gi) <= cond + 1);
            g->most_uv = up > g->most_uv ? up : g->most_uv;
            g->least_g = down < g->least_g ? down : g->least_g;
            ++gi;
        }
        // The rest are this tick's strikes, in place order, each on a place
        // inside a storm.
        u32 last = 0;
        for (; gi < grid.source_count(); ++gi) {
            const u32 at = grid.source_place(gi);
            CHECK(grid.source_uv(gi) == ev.strike_uv() && grid.source_g(gi) == ev.strike_g());
            CHECK(ev.storm_at(map.x(at), map.y(at)) > 0 && (g->strikes == w.strikes || at >= last));
            last = at;
            ++g->strikes;
            g->struck_v += grid.uv(at) / 1e6;
            g->struck_heat += static_cast<double>(grid.place_heat(at)) / 1e6;
            g->struck_failed += w.place(at).failed_devices() - failed[at];
            g->struck_temp += static_cast<double>(w.place(at).temp - temp[at]) / kDegree;
            // Out to three hops along the proximity links: how far the
            // spike carried, and the places to look at again.
            hops.fill(~0u);
            hops[at] = 0;
            queue.resize(0);
            queue.push(at);
            for (u32 q = 0; q < queue.size(); ++q) {
                const u32 x = queue[q];
                g->rise[hops[x]] += static_cast<double>(grid.uv(x) - volts[x]) / 1e6;
                ++g->reached[hops[x]];
                if (!since[x] && failed[x] == 0) {
                    since[x] = 1; // every device working when the strike came
                    hop_of[x] = hops[x];
                }
                if (hops[x] == 3)
                    continue;
                for (u32 j = 0; j < map.power_degree(x); ++j) {
                    const u32 pk = map.power_link_at(x, j), y = map.power_other(pk, x);
                    if (!grid.link_g(pk) || hops[y] != ~0u)
                        continue;
                    hops[y] = hops[x] + 1;
                    queue.push(y);
                }
            }
        }
        CHECK(g->strikes == w.strikes);
        // What a strike leaves of its place, three ticks on.
        for (u32 i = 0; i < w.place_count(); ++i) {
            if (!since[i] || ++since[i] < 5)
                continue;
            since[i] = 0;
            const u32 h = hop_of[i];
            ++g->watched[h];
            g->failed_later[h] += w.place(i).failed_devices();
            g->devices[h] += w.place(i).device_count();
            g->killed[h] += w.place(i).failed_devices() == w.place(i).device_count() ? 1 : 0;
        }
        for (u32 i = 0; i < w.place_count(); ++i)
            g->strikes_due += by_hand(ev, PatchType::storm, map.x(i), map.y(i), true, &over);
        // The cuts outages made: each link's chance, times the outages over
        // its middle less 1.
        g->patch_cuts += w.links().patch_cut + w.links().patch_missed - cuts0;
        for (u32 i = 0; i < w.links().count(); ++i) {
            const Line& line = w.links().line(i);
            const double f = by_hand(ev, PatchType::outage, (map.x(line.a) + map.x(line.b)) / 2,
                                     (map.y(line.a) + map.y(line.b)) / 2, false, &over);
            const double p = static_cast<double>(line.cut_p64) * 0x1.0p-64 * (f - 1);
            g->cuts_due += p > 1 ? 1 : p;
        }
    }
    g->hash_end = w.hash();
    for (u32 i = 0; i < w.place_count(); ++i)
        g->births += w.place(i).births;
}

void test_worlds() {
    const u32 first = 500, rest = 900;
    Went a, b, c, e;
    ByteWriter saved;
    {
        World w;
        Config cfg;
        CHECK(build_world(w, cfg, kAll));
        CHECK(w.events().on());
        crowd(w);
        w.start_threads(1);
        run_world(w, cfg, first, rest, true, &saved, &a);
    }
    {
        World w;
        Config cfg;
        CHECK(build_world(w, cfg, kAll));
        crowd(w);
        w.start_threads(3);
        run_world(w, cfg, first, rest, false, nullptr, &b);
    }
    CHECK_EQ_U64(a.hash_mid, b.hash_mid);
    CHECK_EQ_U64(a.hash_end, b.hash_end);
    // What the patches did.
    CHECK(a.mid_patches > 0 && a.mid_ramping > 0); // the snapshot was taken with patches there, some mid-ramp
    CHECK(a.strikes > 20 && fabs(a.strikes - a.strikes_due) < 5 * sqrt(a.strikes_due));
    CHECK(a.patch_cuts > 20 && fabs(a.patch_cuts - a.cuts_due) < 5 * sqrt(a.cuts_due));
    CHECK(a.raised > 100 && a.lowered > 100 && a.most_uv > 1.05 && a.least_g < 0.9);
    CHECK(a.births > 1000 && a.balance < 0.02);
    // From the snapshot.
    {
        SnapshotHeader h;
        TextBuf err, why;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.version == 6 && h.tick == first);
        World w;
        Config cfg;
        CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
        CHECK_EQ_U64(w.hash(), a.hash_mid);
        for (u32 t = 0; t < rest; ++t) {
            w.tick();
            CHECK_EQ_U64(mark(w.events()), a.marks[first + t]);
        }
        CHECK_EQ_U64(w.hash(), a.hash_end);
    }
    // The same whatever the programs do: with nothing living, the same
    // patches in the same places each tick, the same strikes and the same
    // cuts by outages.
    {
        World w;
        Config cfg;
        CHECK(build_world(w, cfg, kAll));
        w.start_threads(3);
        run_world(w, cfg, first, rest, true, nullptr, &c);
        CHECK(c.hash_end != a.hash_end && c.marks.size() == a.marks.size() && c.births < a.births / 10);
        for (u32 t = 0; t < c.marks.size(); ++t)
            CHECK_EQ_U64(c.marks[t], a.marks[t]);
        CHECK_EQ_U64(c.strikes, a.strikes);
        CHECK_EQ_U64(c.patch_cuts, a.patch_cuts);
    }
    // And they matter: the same world with no patches is another world.
    {
        World w;
        Config cfg;
        CHECK(build_world(w, cfg, ""));
        CHECK(!w.events().on());
        crowd(w);
        w.start_threads(3);
        run_world(w, cfg, first, rest, true, nullptr, &e);
        CHECK(e.hash_end != a.hash_end && e.strikes == 0 && e.patch_cuts == 0 && e.patch_ticks == 0);
        CHECK(e.raised == 0 && e.lowered == 0);
    }
    printf("  events: 40 places with every type of patch, crowded, over %u ticks (%.1f patches there at a time): "
           "%llu strikes (%.1f expected), the struck place at %.2f V and taking %.1f W of heat on average, %llu "
           "devices failing the tick they were struck and the place %.1f degrees warmer; %llu links cut by outages "
           "or cut again (%.1f expected); "
           "sources under a swell patch %.1f%% of the time (up to x%.2f) and under a brownout %.1f%% (down to "
           "x%.2f); the power balanced within %.3f%% every tick\n",
           first + rest, static_cast<double>(a.patch_ticks) / (first + rest), static_cast<ull>(a.strikes),
           a.strikes_due, a.strikes ? a.struck_v / a.strikes : 0, a.strikes ? a.struck_heat / a.strikes : 0,
           static_cast<ull>(a.struck_failed), a.strikes ? a.struck_temp / a.strikes : 0,
           static_cast<ull>(a.patch_cuts), a.cuts_due, 100.0 * a.raised / a.sources, a.most_uv,
           100.0 * a.lowered / a.sources, a.least_g, a.balance * 100);
    for (u32 h = 0; h < 4; ++h)
        printf("  events: %u hop%s from a strike: the voltage rose %.2f V on average (%llu places); of %llu with "
               "every device working, three ticks on %llu of their %llu devices had failed and %llu places had "
               "none left\n",
               h, h == 1 ? "" : "s", a.reached[h] ? a.rise[h] / a.reached[h] : 0, static_cast<ull>(a.reached[h]),
               static_cast<ull>(a.watched[h]), static_cast<ull>(a.failed_later[h]), static_cast<ull>(a.devices[h]),
               static_cast<ull>(a.killed[h]));
    CHECK(a.reached[0] == a.strikes && a.rise[0] / a.reached[0] > a.rise[1] / a.reached[1]);
    CHECK(a.rise[1] / a.reached[1] > a.rise[2] / a.reached[2] && a.killed[0] > 0);
    printf("  events: the same world on 1 host thread and 3, and from a snapshot of tick %u taken with %u patches "
           "there, %u of them rising or easing; with nothing living, the same patches, strikes and cuts; with no "
           "patches, another world (the power balanced within %.3f%%)\n",
           first, a.mid_patches, a.mid_ramping, e.balance * 100);
}

// --- a heat wave -----------------------------------------------------------------------

void test_heatwave() {
    // Two empty worlds from one seed, one with heat waves: the same until
    // the first wave's first tick, and then each place under it cools
    // toward a higher temperature by the wave's degrees there.
    const char* const kWave = "patch.heatwave.rate = 6000\npatch.life.min = 300\npatch.life.max = 600\n";
    World a, b;
    Config ca, cb;
    CHECK(build_world(a, ca, kWave) && build_world(b, cb, ""));
    CHECK(a.events().on() && !b.events().on());
    a.start_threads(1);
    b.start_threads(1);
    const u32 n = a.place_count();
    u32 same = 0, first_under = 0, exact = 0;
    bool begun = false;
    double warmest = 0, strongest = 0;
    for (u32 t = 0; t < 2500; ++t) {
        a.tick();
        b.tick();
        const Events& ev = a.events();
        if (!begun && ev.count() == 0) {
            ++same;
            for (u32 i = 0; i < n; ++i)
                CHECK(a.place(i).temp == b.place(i).temp);
            continue;
        }
        if (!begun) {
            // The first tick of the first wave.
            begun = true;
            for (u32 i = 0; i < n; ++i) {
                const Place& p = b.place(i);
                const i64 add = static_cast<i64>(ev.heat_at(a.map().x(i), a.map().y(i)) * kDegree);
                const i64 diff = a.place(i).temp - p.temp;
                CHECK(a.place(i).heat.outside == p.heat.outside); // leakage's and wear's reference is the world's
                if (add == 0) {
                    CHECK(diff == 0);
                    continue;
                }
                ++first_under;
                // By the step's own arithmetic: what leaves toward the
                // outside falls by its rate x the degrees added.
                const u64 rate = a.cooling_rate(i) + ((p.heat.cooler_rate * p.cooler_work) >> 20);
                const i64 less = heat_flow(rate, 0, -add);
                CHECK(diff >= 0 && less >= 0);
                const double want = static_cast<double>(less) * kDegree / static_cast<double>(p.heat.place_capacity);
                if (fabs(diff - want) <= 2 + want * 1e-6)
                    ++exact;
                else
                    CHECK(diff > 0 && diff < want + 2); // a place whose flows outrun its capacity steps less
            }
        }
        for (u32 i = 0; i < n; ++i) {
            const double d = static_cast<double>(a.place(i).temp - b.place(i).temp) / kDegree;
            warmest = d > warmest ? d : warmest;
            const double s = ev.heat_at(a.map().x(i), a.map().y(i));
            strongest = s > strongest ? s : strongest;
        }
    }
    CHECK(begun && same > 0 && first_under > 0 && exact > 0 && warmest > 1 && warmest <= strongest + 0.01);
    printf("  events: two empty worlds of 40 places from one seed, one with heat waves: the same for %u ticks; at "
           "the first wave's first tick %u places were under it and %u of them stepped by exactly their cooling "
           "rate x the degrees added (the rest less, as the step allows); over 2,500 ticks a place ran up to "
           "%.2f degrees warmer than its twin, under waves of up to %.2f degrees there\n",
           same, first_under, exact, warmest, strongest);
}

// --- settings refused, and off -----------------------------------------------------------

bool refused(const char* extra) {
    Config cfg;
    if (!parse_over(cfg, "world.places = 40\n", extra))
        return true;
    EventSettings es;
    read_event_settings(cfg, &es);
    return !cfg.ok();
}

void test_settings() {
    CHECK(!refused(""));
    CHECK(refused("ramp.lead.min = 300\nramp.lead.max = 200\n"));
    CHECK(!refused("ramp.lead.min = 300\nramp.lead.max = 0\n"));
    CHECK(refused("ramp.false.min = 0.6\n"));
    CHECK(refused("patch.swell.strength.min = 0.9\n"));
    CHECK(refused("patch.outage.strength.min = 0.5\n"));
    CHECK(refused("patch.brownout.strength.max = 1.5\n"));
    CHECK(refused("patch.storm.strength.max = 2\n"));
    CHECK(refused("patch.heatwave.radius.min = 4\n"));
    CHECK(refused("patch.life.min = 60000\n"));
    CHECK(refused("strike.voltage = 40000\n"));
    // With every rate at 0 nothing is on, whatever the map; and patches
    // need a map with positions.
    Land none, flat;
    CHECK(build_land(none, 3, 40, ""));
    CHECK(!none.ev.on() && none.ev.count() == 0);
    none.ev.seek(5000);
    CHECK(none.ev.count() == 0 && none.ev.swell_at(0.5, 0.5) == 1 && none.ev.heat_at(0.5, 0.5) == 0);
    CHECK(build_land(flat, 3, 40, "world.map = none\npatch.storm.rate = 100\n"));
    CHECK(!flat.ev.on());
    // On a grid, a test map: a patch forms at a place.
    Land grid;
    CHECK(build_land(grid, 3, 36, "world.map = grid\npatch.swell.rate = 2000\nramp.lead.max = 100\n"));
    CHECK(grid.ev.on());
    u32 made = 0;
    for (u64 b = 0; b < 50; ++b)
        for (u32 slot = 0; slot < grid.ev.slots(PatchType::swell); ++slot) {
            Patch p;
            if (!grid.ev.make(PatchType::swell, b, slot, &p))
                continue;
            ++made;
            bool on_place = false;
            for (u32 i = 0; i < 36 && !on_place; ++i)
                on_place = grid.map.x(i) == p.x && grid.map.y(i) == p.y;
            CHECK(on_place);
        }
    CHECK(made > 20);
    printf("  events: settings out of order refused; no patches with every rate at 0 or on a map with no "
           "positions; on a grid %u patches each formed at a place\n",
           made);
}

} // namespace

void suite_events() {
    test_ramp();
    test_sources();
    test_patches();
    test_strike();
    test_heatwave();
    test_worlds();
    test_settings();
}
