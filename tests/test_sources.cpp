// The sources on the map (nova2/sources.h; design.md 4.2, 4.3;
// build-plan-gate4.md step 3): what the seed fixes of each against its
// definition; the same sources in the same states however the ticks are
// asked for; the long-run count against the chance x the lifetime; landing
// in proportion to catchment area; the day's phase following a place's
// east-west position; the types' shares, sizes, stiffness and lives;
// drop-outs and swells; the sources placed by hand; and the settings
// refused.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/sources.h"

using namespace nova2;

namespace {

struct Built {
    Map map;
    Sources src;
    SourceSettings ss;
};

bool build(Built& b, u64 seed, u32 places, const char* extra, TextBuf* why) {
    Config cfg;
    Config::ParseError pe;
    char head[64];
    snprintf(head, sizeof head, "world.places = %u\nsupply.model = sources\n", places);
    TextBuf text;
    text.append(Str(head, strlen(head)));
    text.append(Str(extra, strlen(extra)));
    CHECK(cfg.parse(text.view(), &pe));
    MapSettings ms;
    read_map_settings(cfg, &ms);
    read_source_settings(cfg, &b.ss);
    CHECK(cfg.ok());
    if (!cfg.ok())
        printf("  settings: %s\n", cfg.value_error());
    return b.map.build(seed, places, ms, why) && b.src.init(b.ss, &b.map, seed, why);
}

bool refused(const char* extra) {
    Config cfg;
    Config::ParseError pe;
    TextBuf text;
    text.append("world.places = 48\nsupply.model = sources\n");
    text.append(Str(extra, strlen(extra)));
    if (!cfg.parse(text.view(), &pe))
        return true;
    SourceSettings ss;
    read_source_settings(cfg, &ss);
    if (!cfg.ok())
        return true;
    MapSettings ms;
    read_map_settings(cfg, &ms);
    Map m;
    Sources s;
    TextBuf why;
    return !m.build(1, 48, ms, &why) || !s.init(ss, &m, 1, &why);
}

bool same_source(const Source& a, const Source& b) {
    return a.index == b.index && a.place == b.place && a.cell == b.cell && a.x == b.x && a.y == b.y &&
           a.type == b.type && a.fixed == b.fixed && a.voltage == b.voltage && a.sag == b.sag &&
           a.rated_uw == b.rated_uw && a.gs == b.gs && a.birth == b.birth && a.life == b.life && a.phase == b.phase;
}

bool same_state(const SourceState& a, const SourceState& b) {
    return a.capacity == b.capacity && a.swell == b.swell && a.g == b.g && a.uv == b.uv;
}

bool same_now(const Sources& a, const Sources& b) {
    if (a.count() != b.count() || a.begun() != b.begun() || a.ended() != b.ended())
        return false;
    for (u32 k = 0; k < a.count(); ++k)
        if (!same_source(a.source(k), b.source(k)) || !same_state(a.state(k), b.state(k)))
            return false;
    return true;
}

// Lives and swings short enough for a test to see sources come and go.
const char* const kQuick = "sources.life = 4000\n"
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
                           "sources.swell.longest = 60\n";

// What the seed fixes of a source, and its state, against the definitions.
void test_definitions() {
    Built b;
    TextBuf why;
    CHECK(build(b, 3, 48, kQuick, &why));
    const Sources& s = b.src;
    b.src.seek(0);
    CHECK(s.start_count() == 12 && s.count() == 12 && s.begun() == 12 && s.ended() == 0);
    u32 growing = 0, fading = 0, whole = 0;
    for (u64 tick = 0; tick < 6000; tick += 7) {
        b.src.seek(tick);
        for (u32 k = 0; k < s.count(); ++k) {
            const Source& o = s.source(k);
            const SourceState& st = s.state(k);
            const SourceType& y = b.ss.type[o.type];
            // The seed's part.
            CHECK(o.type < kSourceTypes && o.place < 48 && !o.fixed);
            CHECK(o.voltage == y.voltage && o.sag >= y.sag_min && o.sag <= y.sag_max);
            CHECK(o.rated_uw >= 100000000ull && o.rated_uw <= 800000000ull);
            CHECK(o.life >= 1000 && o.life <= 16000);
            CHECK(o.gs == source_conductance(o.voltage, o.sag, o.rated_uw) && o.gs > 0);
            CHECK(o.phase == s.phase_of(o.place) && o.phase < 200);
            CHECK(b.map.cell_place(o.cell % b.map.raster_w(), o.cell / b.map.raster_w()) == o.place);
            CHECK(same_source(o, s.make(o.index, o.birth)));
            // It is alive now, and where it is in its life.
            const i64 age = static_cast<i64>(tick) - o.birth;
            CHECK(age >= 0 && age < static_cast<i64>(o.life));
            const u64 tenth = o.life / 10, left = o.life - static_cast<u64>(age);
            u32 env = kCapacityOne;
            if (static_cast<u64>(age) < tenth && static_cast<u64>(age) <= left) {
                env = static_cast<u32>((static_cast<u64>(age) << 20) / tenth);
                ++growing;
            } else if (left < tenth) {
                env = static_cast<u32>((left << 20) / tenth);
                ++fading;
            } else {
                ++whole;
            }
            CHECK(s.envelope(o, tick) == env);
            // Its capacity is the product of the four factors, its
            // conductance that share of its full one, and its voltage its
            // type's times its swell.
            u64 c = env;
            c = c * s.day(o, tick) >> 20;
            c = c * s.swing(o, tick) >> 20;
            c = c * s.dropout(o, tick) >> 20;
            CHECK(st.capacity == c && st.capacity <= kCapacityOne);
            CHECK(st.g == static_cast<u64>(static_cast<double>(o.gs) * static_cast<double>(c) / 1048576.0) ||
                  o.gs > (1ull << 52));
            CHECK(st.swell == s.swell(o, tick) && st.swell >= kCapacityOne);
            CHECK(st.uv == static_cast<i64>(static_cast<u64>(o.voltage) * 1000 * st.swell >> 20));
            // The factors keep their bounds.
            CHECK(s.day(o, tick) <= kCapacityOne && s.day(o, tick) >= static_cast<u32>(y.night * 1048576.0));
            CHECK(s.swing(o, tick) <= kCapacityOne &&
                  s.swing(o, tick) + 2 >= static_cast<u32>((1 - y.swing) * 1048576.0));
        }
    }
    CHECK(growing > 0 && fading > 0 && whole > growing + fading);
    printf("  sources: seed 3's first 6000 ticks at a short life: %llu begun, %llu ended; %u looks growing, %u "
           "fading, %u whole\n",
           static_cast<unsigned long long>(s.begun()), static_cast<unsigned long long>(s.ended()), growing, fading,
           whole);
}

// Principle 3: the sources are a function of the seed and the tick alone.
// One walk tick by tick, one in strides, one jumping about and back: the
// same sources in the same states at every tick asked for.
void test_same_any_way() {
    Built a, b, c;
    TextBuf why;
    CHECK(build(a, 7, 48, kQuick, &why) && build(b, 7, 48, kQuick, &why) && build(c, 7, 48, kQuick, &why));
    u32 differ = 0;
    u64 seen = 0;
    for (u64 tick = 0; tick < 20000; ++tick) {
        a.src.seek(tick);
        if (tick % 97 == 0) {
            b.src.seek(tick);
            differ += same_now(a.src, b.src) ? 0 : 1;
        }
        if (tick % 331 == 0) {
            c.src.seek(19999 - tick); // backwards
            c.src.seek(tick);
            differ += same_now(a.src, c.src) ? 0 : 1;
            c.src.seek(tick + 5000); // and ahead
        }
        seen += a.src.count();
    }
    CHECK(differ == 0);
    CHECK(a.src.ended() > 20 && a.src.begun() > a.src.ended());
    // Another seed is another world.
    Built d;
    CHECK(build(d, 8, 48, kQuick, &why));
    d.src.seek(19999);
    a.src.seek(19999);
    CHECK(!same_now(a.src, d.src));
    printf("  sources: seed 7 over 20,000 ticks: %.1f alive on average, %llu begun, %llu ended\n",
           static_cast<double>(seen) / 20000.0, static_cast<unsigned long long>(a.src.begun()),
           static_cast<unsigned long long>(a.src.ended()));
}

// The long-run count is the chance x the average lifetime (4.2); left
// unset, the chance keeps the count the run starts with.
void test_count() {
    // The default chance, from the start and the life.
    {
        Built b;
        TextBuf why;
        CHECK(build(b, 1, 48, "", &why));
        CHECK(fabs(b.ss.chance - 25.0 / (source_mean_life(20000000) / 1e6)) < 1e-12);
        CHECK(fabs(b.ss.chance - 0.924196) < 1e-6);
        CHECK(fabs(source_mean_life(20000000) - 20000000.0 * 3.75 / log(16.0)) < 1e-3);
    }
    // 200 places, a life of 2,000 in the middle: 50 at the start, kept.
    Built b;
    TextBuf why;
    CHECK(build(b, 11, 200, "sources.life = 2000\n", &why));
    const double mean_life = source_mean_life(2000);
    const double want = b.ss.chance * 2.0 / 1e6 * mean_life; // 200 places
    CHECK(fabs(want - 50.0) < 1e-9);
    double sum = 0, life_sum = 0;
    u64 n = 0, births = 0;
    for (u64 tick = 0; tick < 2000000; tick += 50) {
        b.src.seek(tick);
        sum += b.src.count();
        ++n;
    }
    births = b.src.begun() - 50;
    const double avg = sum / static_cast<double>(n);
    // Births come at the chance: 2,000,000 ticks x 0.0185 a tick.
    const double births_want = b.ss.chance * 2.0 / 1e6 * 2000000.0;
    CHECK(fabs(static_cast<double>(births) - births_want) < 5 * sqrt(births_want));
    CHECK(fabs(avg - want) < 0.03 * want);
    // And the lifetimes average what the law says.
    for (u32 i = 50; i < 50 + 20000; ++i)
        life_sum += static_cast<double>(b.src.make(i, 0).life);
    CHECK(fabs(life_sum / 20000.0 - mean_life) < 0.02 * mean_life);
    printf("  sources: 200 places, life 2,000: %.2f alive on average over 2,000,000 ticks (the chance x the "
           "lifetime: %.2f); %llu births (%.0f expected); a life averages %.0f ticks (%.0f)\n",
           avg, want, static_cast<unsigned long long>(births), births_want, life_sum / 20000.0, mean_life);
    // No chance: those at the start die out and nothing follows.
    Built none;
    CHECK(build(none, 11, 200, "sources.life = 2000\nsources.chance = 0\n", &why));
    none.src.seek(8001);
    CHECK(none.src.count() == 0 && none.src.begun() == 50 && none.src.ended() == 50);
    // No sources at all.
    Built dark;
    CHECK(build(dark, 11, 200, "sources.start = 0\nsources.chance = 0\n", &why));
    dark.src.seek(1000000);
    CHECK(dark.src.count() == 0 && dark.src.begun() == 0);
}

// Sources land by area (4.2): a place's share of them is its catchment's
// area to the power, and the cell is one of its catchment's, any as likely
// as another.
void landing(u64 seed, const char* extra, double power) {
    Built b;
    TextBuf why;
    CHECK(build(b, seed, 48, extra, &why));
    const u32 n = 48, draws = 60000;
    Array<u32> got;
    got.resize(n);
    got.fill(0);
    Array<u32> cell_hits;
    cell_hits.resize(static_cast<usize>(b.map.raster_w()) * b.map.raster_h());
    cell_hits.fill(0);
    for (u32 i = 0; i < draws; ++i) {
        const Source s = b.src.make(1000 + i, 0);
        ++got[s.place];
        ++cell_hits[s.cell];
        CHECK(b.map.land(s.cell % b.map.raster_w(), s.cell / b.map.raster_w()));
        CHECK(b.map.cell_place(s.cell % b.map.raster_w(), s.cell / b.map.raster_w()) == s.place);
    }
    double total = 0;
    for (u32 p = 0; p < n; ++p)
        total += pow(static_cast<double>(b.map.catchment(p)), power);
    double worst = 0, chi = 0;
    for (u32 p = 0; p < n; ++p) {
        const double share = pow(static_cast<double>(b.map.catchment(p)), power) / total;
        const double want = share * draws, sd = sqrt(want * (1 - share));
        const double off = fabs(got[p] - want) / (sd > 0 ? sd : 1);
        worst = off > worst ? off : worst;
        chi += want > 0 ? (got[p] - want) * (got[p] - want) / want : 0;
    }
    CHECK(worst < 5);  // every place within 5 standard deviations of its share
    CHECK(chi < 100);  // 47 degrees of freedom: 100 is past one chance in 100,000
    // Within the largest catchment, every cell hit about as often.
    u32 big = 0;
    for (u32 p = 1; p < n; ++p)
        big = b.map.catchment(p) > b.map.catchment(big) ? p : big;
    u32 lo = ~0u, hi = 0, cells = 0;
    for (u32 cy = 0; cy < b.map.raster_h(); ++cy)
        for (u32 cx = 0; cx < b.map.raster_w(); ++cx)
            if (b.map.cell_place(cx, cy) == big) {
                const u32 h = cell_hits[static_cast<usize>(cy) * b.map.raster_w() + cx];
                lo = h < lo ? h : lo;
                hi = h > hi ? h : hi;
                ++cells;
            }
    CHECK(cells == b.map.catchment(big));
    const double each = static_cast<double>(got[big]) / cells;
    CHECK(hi < each + 6 * sqrt(each) + 6);
    printf("  sources: landing at power %.0f, seed %llu: 60,000 land with every place within %.1f standard "
           "deviations of its share (chi-square %.1f on 47); the largest catchment's %u cells took %u to %u "
           "each (%.1f on average)\n",
           power, static_cast<unsigned long long>(seed), worst, chi, cells, lo, hi, each);
}

void test_landing() {
    landing(1, "", 1);
    landing(5, "", 1);
    landing(5, "sources.area_power = 0\n", 0);
    landing(5, "sources.area_power = 2\n", 2);
    // A map with no raster: every place as likely as another, the point the
    // place.
    Built g;
    TextBuf why;
    CHECK(build(g, 2, 30, "world.map = grid\n", &why));
    u32 got[30] = {};
    for (u32 i = 0; i < 30000; ++i) {
        const Source s = g.src.make(500 + i, 0);
        ++got[s.place];
        CHECK(s.cell == ~0u && s.x == g.map.x(s.place) && s.y == g.map.y(s.place));
    }
    for (u32 p = 0; p < 30; ++p)
        CHECK(got[p] > 1000 - 160 && got[p] < 1000 + 160);
    Built none;
    CHECK(build(none, 2, 5, "world.map = none\n", &why));
    none.src.seek(0);
    CHECK(none.src.count() == 1 && none.src.source(0).phase == 0);
}

// The day sweeps the map from east to west (4.3): a place's phase follows
// its east-west position, sources.day_span days from edge to edge.
void test_day() {
    Built b;
    TextBuf why;
    CHECK(build(b, 4, 48, "supply.day = 2000\nsource.daylight.swing = 0\nsource.daylight.dropouts = 0\n", &why));
    const double w = b.map.width();
    u32 east = 0, west = 0;
    for (u32 p = 0; p < 48; ++p) {
        const u64 ph = b.src.phase_of(p);
        CHECK(ph == static_cast<u64>(b.map.x(p) / w * 2000.0));
        east = b.map.x(p) > b.map.x(east) ? p : east;
        west = b.map.x(p) < b.map.x(west) ? p : west;
    }
    CHECK(b.src.phase_of(east) > b.src.phase_of(west) + 1000); // most of a day apart
    // A daylight source's day: its noon comes when its place's clock reads
    // half a day, sooner the further east.
    u32 seen = 0;
    for (u32 i = 0; i < 400 && seen < 40; ++i) {
        const Source s = b.src.make(100 + i, 0);
        if (s.type != 1)
            continue;
        ++seen;
        u64 best = 0;
        u32 most = 0;
        for (u64 tick = 0; tick < 2000; ++tick) {
            const u32 d = b.src.day(s, tick);
            if (d > most) {
                most = d;
                best = tick;
            }
        }
        CHECK(most == kCapacityOne);
        CHECK((best + s.phase) % 2000 == 1000);
        CHECK(b.src.day(s, (3000 - s.phase) % 2000 + 1000) == static_cast<u32>(0.2 * 1048576.0 + 0.5)); // midnight
        // A steady source and a gusty one ignore the day.
    }
    CHECK(seen == 40);
    for (u32 i = 0; i < 100; ++i) {
        const Source s = b.src.make(100 + i, 0);
        if (s.type != 1)
            for (u64 tick = 0; tick < 2000; tick += 50)
                CHECK(b.src.day(s, tick) == kCapacityOne);
    }
    // No span: one time of day for the whole map. Half a day: half the phases.
    Built flat, half;
    CHECK(build(flat, 4, 48, "sources.day_span = 0\n", &why) && build(half, 4, 48, "sources.day_span = 0.5\n", &why));
    for (u32 p = 0; p < 48; ++p) {
        CHECK(flat.src.phase_of(p) == 0);
        CHECK(half.src.phase_of(p) == static_cast<u64>(half.map.x(p) / w * 0.5 * 2000.0));
    }
    // Two days across the map wrap.
    Built two;
    CHECK(build(two, 4, 48, "sources.day_span = 2\n", &why));
    for (u32 p = 0; p < 48; ++p) {
        const double f = two.map.x(p) / w * 2.0;
        CHECK(two.src.phase_of(p) == static_cast<u64>((f - floor(f)) * 2000.0));
    }
}

int by_value(const void* a, const void* b) {
    const double x = *static_cast<const double*>(a), y = *static_cast<const double*>(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

// The catalogue (4.2): the types' shares, each type's voltage and its
// stiffness's range, the skewed sizes.
void test_catalogue() {
    Built b;
    TextBuf why;
    CHECK(build(b, 9, 48, "", &why));
    const u32 n = 40000;
    u32 by[kSourceTypes] = {};
    Array<double> size, sag[kSourceTypes];
    for (u32 i = 0; i < n; ++i) {
        const Source s = b.src.make(100 + i, 0);
        ++by[s.type];
        size.push(static_cast<double>(s.rated_uw) / 1e6);
        sag[s.type].push(s.sag);
        const i32 volts[3] = {1050, 1250, 1350};
        CHECK(s.voltage == volts[s.type]);
    }
    CHECK(fabs(by[0] - 0.25 * n) < 5 * sqrt(n * 0.25 * 0.75));
    CHECK(fabs(by[1] - 0.5 * n) < 5 * sqrt(n * 0.5 * 0.5));
    CHECK(fabs(by[2] - 0.25 * n) < 5 * sqrt(n * 0.25 * 0.75));
    // Sizes: 100 x 8^(u^3), from 100 to 800 W (agreed 2026-11-20; they were
    // 50 to 400). The middle one is 100 x 8^(1/8), and one in ten is over
    // 100 x 8^0.729.
    qsort(size.data(), size.size(), sizeof(double), by_value);
    CHECK(size[0] >= 100 && size[n - 1] <= 800);
    const double median = size[n / 2], tenth = size[n * 9 / 10];
    CHECK(fabs(median - 100 * pow(8.0, 0.125)) < 3);
    CHECK(fabs(tenth - 100 * pow(8.0, 0.729)) < 16);
    // Stiffness: evenly on a log scale, so the middle one is the geometric
    // mean of the range's ends.
    const double lo[3] = {20, 50, 50}, hi[3] = {50, 300, 300};
    for (u32 t = 0; t < kSourceTypes; ++t) {
        qsort(sag[t].data(), sag[t].size(), sizeof(double), by_value);
        CHECK(sag[t][0] >= lo[t] && sag[t][sag[t].size() - 1] <= hi[t]);
        CHECK(fabs(sag[t][sag[t].size() / 2] - sqrt(lo[t] * hi[t])) < 0.04 * sqrt(lo[t] * hi[t]) + 1);
    }
    printf("  sources: 40,000 drawn: %u steady, %u daylight, %u gusty; sizes %.0f to %.0f W, the middle one "
           "%.1f W, one in ten over %.0f W\n",
           by[0], by[1], by[2], size[0], size[n - 1], median, tenth);
    // Shares of nothing leave a type out.
    Built two;
    CHECK(build(two, 9, 48, "source.daylight.share = 0\nsource.gusty.share = 3\n", &why));
    u32 got[kSourceTypes] = {};
    for (u32 i = 0; i < 8000; ++i)
        ++got[two.src.make(100 + i, 0).type];
    CHECK(got[1] == 0 && fabs(got[0] - 8000.0 * 0.25 / 3.25) < 120 && got[0] + got[2] == 8000);
}

// Drop-outs and swells: at their rates, within their bounds, each source's
// its own.
void test_events() {
    Built b;
    TextBuf why;
    CHECK(build(b, 6, 48, kQuick, &why));
    const Sources& s = b.src;
    CHECK(s.swell_block() == 64);
    // A gusty source's swells over many stretches: 1,000 a million ticks.
    Source gusty, steady;
    bool have_g = false, have_s = false;
    for (u32 i = 0; i < 100 && !(have_g && have_s); ++i) {
        const Source o = s.make(200 + i, 0);
        if (o.type == 2 && !have_g) {
            gusty = o;
            have_g = true;
        }
        if (o.type == 0 && !have_s) {
            steady = o;
            have_s = true;
        }
    }
    CHECK(have_g && have_s);
    gusty.life = steady.life = 1ull << 40;
    gusty.fixed = steady.fixed = true; // no growing or fading in the way
    u32 swells = 0;
    const u64 blocks = 200000;
    for (u64 j = 0; j < blocks; ++j) {
        Swell w;
        if (!s.swell_in(gusty, j, &w))
            continue;
        ++swells;
        CHECK(w.start >= j * 64 && w.start < (j + 1) * 64 && w.length >= 10 && w.length <= 60);
        CHECK(w.factor >= static_cast<u32>(1.1 * 1048576.0) && w.factor <= static_cast<u32>(1.8 * 1048576.0 + 1));
        // While it lasts the voltage is the type's times the factor (unless
        // a higher one overlaps), and before it starts it isn't raised by it.
        const SourceState st = s.state_at(gusty, w.start);
        CHECK(st.swell >= w.factor && st.uv >= static_cast<i64>(1350ull * 1000 * w.factor >> 20));
        const SourceState end = s.state_at(gusty, w.start + w.length - 1);
        CHECK(end.swell >= w.factor);
        Swell x;
        CHECK(!s.swell_in(steady, j, &x)); // a steady source never swells
    }
    const double want = 1000.0 / 1e6 * 64 * blocks;
    CHECK(fabs(swells - want) < 5 * sqrt(want));
    // Tick by tick over a stretch: the swell at a tick is the highest of
    // those covering it, and 1 with none.
    u32 raised = 0;
    for (u64 tick = 0; tick < 64 * 3000; ++tick) {
        u32 f = kCapacityOne;
        const u64 j = tick / 64;
        for (u64 k = j ? j - 1 : 0; k <= j; ++k) {
            Swell w;
            if (s.swell_in(gusty, k, &w) && tick >= w.start && tick - w.start < w.length && w.factor > f)
                f = w.factor;
        }
        CHECK(s.swell(gusty, tick) == f);
        raised += f > kCapacityOne ? 1 : 0;
        CHECK(s.swell(steady, tick) == kCapacityOne);
    }
    CHECK(raised > 0);
    // Drop-outs: 1,000 a million ticks, each source's its own, and a
    // blackout leaves no conductance at all.
    u32 drops = 0, dark = 0, shared = 0;
    for (u64 j = 0; j < 100000; ++j) {
        Dropout d, e;
        const bool g = b.src.dropout(gusty, j * 64 + 7, &d) < kCapacityOne;
        const bool t = b.src.dropout(steady, j * 64 + 7, &e) < kCapacityOne;
        drops += g ? 1 : 0;
        shared += g && t ? 1 : 0;
        if (g && d.blackout()) {
            ++dark;
            const SourceState st = s.state_at(gusty, j * 64 + 7);
            CHECK(st.capacity == 0 && st.g == 0);
        }
    }
    CHECK(drops > 1000 && dark > drops / 4 && dark < drops * 3 / 4);
    CHECK(shared < drops / 5); // each on its own stream
    printf("  sources: a gusty source: %u swells in %llu ticks (%.0f expected), raised for %u of 192,000 ticks; "
           "%u of 100,000 ticks sampled in a drop-out, %u of them blackouts\n",
           swells, static_cast<unsigned long long>(blocks * 64), want, raised, drops, dark);
}

// Sources placed by hand (sources.fixed), for test worlds.
void test_fixed() {
    Built b;
    TextBuf why;
    CHECK(build(b, 2, 30,
                "world.map = grid\nsources.start = 0\nsources.chance = 0\n"
                "sources.fixed = 0 steady 100 30, 17 gusty 250.5 100 ,29 daylight 60 75\n",
                &why));
    b.src.seek(0);
    CHECK(b.src.count() == 3 && b.src.fixed_count() == 3);
    const u32 place[3] = {0, 17, 29}, type[3] = {0, 2, 1};
    const i32 sag[3] = {30, 100, 75}, volts[3] = {1050, 1350, 1250};
    const u64 uw[3] = {100000000ull, 250500000ull, 60000000ull};
    for (u32 k = 0; k < 3; ++k) {
        const Source& s = b.src.source(k);
        CHECK(s.index == k && s.fixed && s.place == place[k] && s.type == type[k] && s.sag == sag[k] &&
              s.voltage == volts[k] && s.rated_uw == uw[k] && s.birth == 0);
        CHECK(s.x == b.map.x(place[k]) && s.y == b.map.y(place[k]));
        CHECK(s.gs == source_conductance(volts[k], sag[k], uw[k]));
        CHECK(b.src.envelope(s, 0) == kCapacityOne && b.src.envelope(s, 1ull << 40) == kCapacityOne);
    }
    // 100 W at 30 mV of sag from 1,050 mV: 100 / (0.03 x 1.02) siemens.
    const double g = static_cast<double>(b.src.source(0).gs) / 1048576.0;
    CHECK(fabs(g - 100.0 / (0.030 * 1.020)) < 0.01);
    // They outlast any run, and drawn ones come after them in the sequence.
    b.src.seek(1000000000000ull);
    CHECK(b.src.count() == 3 && b.src.ended() == 0);
    Built c;
    CHECK(build(c, 2, 30, "world.map = grid\nsources.fixed = 3 steady 80 25\n", &why));
    c.src.seek(0);
    CHECK(c.src.count() == 1 + 8 && c.src.source(0).fixed && !c.src.source(1).fixed && c.src.source(1).index == 1);
    TextBuf t;
    c.src.describe(&t);
    printf("  %s\n", t.c_str());
    // Refused: a place the world lacks, an unknown type, a sag past the
    // voltage, no load, a word missing, a word too many.
    CHECK(refused("sources.fixed = 48 steady 100 30\n"));
    CHECK(refused("sources.fixed = 0 solar 100 30\n"));
    CHECK(refused("sources.fixed = 0 steady 100 1050\n"));
    CHECK(refused("sources.fixed = 0 steady 0 30\n"));
    CHECK(refused("sources.fixed = 0 steady 100\n"));
    CHECK(refused("sources.fixed = 0 steady 100 30 7\n"));
    CHECK(!refused("sources.fixed = 0 steady 100 30\n"));
    CHECK(!refused("sources.fixed = 0 steady 100 30,\n"));
}

void test_refused() {
    CHECK(!refused(""));
    CHECK(refused("sources.size.max = 10\n"));
    CHECK(refused("sources.swell.max = 1.05\n"));
    CHECK(refused("sources.swell.longest = 5\n"));
    CHECK(refused("source.gusty.sag.max = 10\n"));
    CHECK(refused("source.steady.sag.max = 1050\n"));
    CHECK(refused("source.steady.share = 0\nsource.daylight.share = 0\nsource.gusty.share = 0\n"));
    CHECK(refused("source.gusty.swells = 1000\n"));   // 1,024-tick stretches: at most 976 a million ticks
    CHECK(refused("source.steady.dropouts = 1000\n"));
}

} // namespace

void suite_sources() {
    test_definitions();
    test_same_any_way();
    test_count();
    test_landing();
    test_day();
    test_catalogue();
    test_events();
    test_fixed();
    test_refused();
}
