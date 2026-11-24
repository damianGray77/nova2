// nova2 worlds, gate 4's test worlds at full size (build-plan-gate4.md step
// 10; R7: evidence about the mechanism, not about life). worlds.test picks
// one; the config is the world, with world.places at full size (1,000), and
// each runs worlds.ticks ticks, reporting every worlds.report_every.
//
//   band:      a stiff source and a soft one of the same voltage, each alone
//              at the middle of its own map, the voltage above the cores'
//              maximum, where a busy core draws most and runs hottest. Pushy
//              migrants (traffic_world.h) are placed by hand wherever a
//              core runs at the start and SEND themselves to their
//              neighbours. Reported: by links from the source, where
//              threads last as the load grows, and the nearest and farthest
//              such places over time (the inner edge's creep, 4.2);
//   heat:      one place, a counting thread on every core, under a steady
//              source and under a daylight one, at three cooling rates (a
//              quarter of the setting, the setting, and four times it),
//              the threads placed again as they end, with no noise.
//              Reported: over the last day, whether its temperature
//              settles, follows the day or cycles through trips (5);
//   water:     one stiff source at the middle of the map and nothing placed.
//              Reported: the voltage on the source's landmass and on each
//              landmass across water from it, by the water crossed (6.1);
//   sleeper:   daylight sources only, with deep nights. In every place, by
//              hand, four threads with a low-voltage handler that sleeps
//              through the night and four without. Reported: who is left
//              after each night, and the interrupt's use (9.5, 12.4). The
//              threads don't copy themselves (a replicator with a handler
//              isn't written yet), and there is no noise, so only the
//              nights end them;
//   rejection: a crowd of counting threads within two links of a soft
//              source and of a stiff one, ended at once by hand. Reported:
//              how far the voltage's rise over a twin world, where the
//              crowd is left alone, spreads, by links from the crowd, and
//              what trips or passes its maximum (12.2). There is no
//              noise, so the crowd lasts until it is cut off;
//   numbers:   not a world of its own: what gate 4's numbers do
//              (build-plan-gate4.md 7 and step 11). A busy core's draw and
//              the temperature it rests at, by the engine's own laws; the
//              config's places at a steady 1,000 mV, idle and with every
//              core busy; and where the power reaches in the world as it
//              stands, with its ground hot, dim and fair: with nothing
//              living in it, with every core kept busy, and with each
//              place in turn busy alone.
//   classes:   not a world of its own either: numbers' last part, for
//              gate 4's pass mark (build-plan-gate4.md 11, the pass mark as
//              changed). Each place's ground named by what a full place
//              gets there with nothing else living, under the sources and
//              straits of tick worlds.from and of every worlds.class_every
//              ticks after, to worlds.class_to. With worlds.respawns, each
//              classing's worlds have the run's respawns to its tick
//              applied, and a place that respawned after a classing is
//              classed alone at its respawn's tick (build-plan-gate5.md 11).
//              A row a place for each classing goes to
//              <worlds.classes>/seed-N-classes.csv.
//
// Each world is built from the config as given, with the settings the test
// needs set over it (named in its first lines).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef far // windows.h's, from 16-bit days: names here
#undef near

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h" // gate5_off
#include "nova2/world.h"
#include "traffic_world.h"

namespace nova2 {

namespace {

// A respawn of the run being classed, a row of its seed-N-respawns.csv
// (watch_report.cpp writes them, in the order they happened).
struct RespawnRow {
    u64 tick = 0;
    u32 place = 0, incarnation = 0;
    double x = 0, y = 0;
    bool moved = false;
};

struct Run {
    u64 ticks = 0, every = 0;
    u32 threads = 0;
    // The classing's (worlds.from, .class_every, .class_to, .classes), and
    // the run's respawns (worlds.respawns), each applied in a classing's
    // worlds from its tick on.
    u64 from = 0, class_every = 0, class_to = 0;
    char classes[300] = {};
    char respawns_file[400] = {};
    Array<RespawnRow> respawns;
};

typedef unsigned long long ull;

const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};

// A copy of the config with `sets` (key, value, ..., null) set over it.
void derive(Config& base, Config* out, const char* const* sets) {
    TextBuf text;
    base.write(text);
    Config::ParseError pe;
    out->parse(text.view(), &pe);
    for (u32 i = 0; sets && sets[i]; i += 2)
        out->set(sets[i], sets[i + 1]);
}

bool build(World& w, Config& cfg) {
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return false;
    }
    return true;
}

// The place nearest the map's middle.
u32 middle_place(const Map& m) {
    u32 best = 0;
    double least = 1e300;
    const double cx = m.width() / 2, cy = 0.5;
    for (u32 i = 0; i < m.place_count(); ++i) {
        const double dx = m.x(i) - cx, dy = m.y(i) - cy;
        if (dx * dx + dy * dy < least) {
            least = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

// Each place's links from `from` (or from the nearest of several, those
// with from[i] set) along the proximity links the solve keeps; ~0: no way.
void power_hops(World& w, const u8* from, Array<u32>* hops) {
    const Map& m = w.map();
    const u32 n = w.place_count();
    hops->resize(0);
    hops->resize(n);
    hops->fill(~0u);
    Array<u32> queue;
    for (u32 i = 0; i < n; ++i)
        if (from[i]) {
            (*hops)[i] = 0;
            queue.push(i);
        }
    for (usize q = 0; q < queue.size(); ++q) {
        const u32 v = queue[q];
        for (u32 j = 0; j < m.power_degree(v); ++j) {
            const u32 k = m.power_link_at(v, j);
            if (!w.grid().link_g(k))
                continue;
            const u32 u = m.power_link(k).a == v ? m.power_link(k).b : m.power_link(k).a;
            if ((*hops)[u] == ~0u) {
                (*hops)[u] = (*hops)[v] + 1;
                queue.push(u);
            }
        }
    }
}

bool place_counter(Place& p) {
    const u32 at = p.mem.alloc(1, 64, p.mem.strict() ? 1 : 2, 0);
    if (!at)
        return false;
    memcpy(p.mem.bytes() + at, kCounter, sizeof kCounter);
    return p.os.place_by_hand(1, at, at, 0) != 0;
}

u64 trips_of(const Place& p) {
    u64 n = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        n += p.trips[k];
    return n;
}

// A world with one source placed by hand at its map's middle place, and no
// others: `watts` rated, sagging `sag` mV at that load. The map is worked
// out first to find the place. False if it can't be built.
bool one_source(Config& base, World& w, Config* cfg, const char* const* sets, double watts, u32 sag, u32* at) {
    static const char* const kNone[] = {"supply.model", "sources", "sources.start", "0", "sources.chance", "0",
                                        "source.steady.swing", "0", "source.steady.dropouts", "0",
                                        "source.steady.swells", "0", nullptr};
    Config probe;
    derive(base, &probe, kNone);
    for (u32 i = 0; sets && sets[i]; i += 2)
        probe.set(sets[i], sets[i + 1]);
    {
        World first;
        if (!build(first, probe))
            return false;
        if (!first.map().placed()) {
            fprintf(stderr, "nova2: gate 4's test worlds need a map (world.map)\n");
            return false;
        }
        *at = middle_place(first.map());
    }
    TextBuf text;
    probe.write(text);
    Config::ParseError pe;
    cfg->parse(text.view(), &pe);
    char fixed[96];
    snprintf(fixed, sizeof fixed, "%u steady %.0f %u", *at, watts, sag);
    cfg->set("sources.fixed", fixed);
    return build(w, *cfg);
}

// --- the band ---------------------------------------------------------------

struct BandEnd {
    u32 inner_first = ~0u, inner_last = ~0u, outer_first = ~0u, outer_last = ~0u;
    u32 threads = 0, places = 0;
    double watts = 0, source_mv = 0;
};

bool band_one(Config& base, const Run& run, const char* name, u32 sag, BandEnd* end) {
    // The source's voltage is above the cores' maximum, unless the config
    // says otherwise, and it is rated for half a watt a place:
    // about what the band around it comes to draw, so the soft one sags.
    static const char* const kHigh[] = {"source.steady.voltage", "1500", nullptr};
    World w;
    Config cfg;
    u32 src = 0;
    const u32 places = key_u32(base, "world.places");
    if (!one_source(base, w, &cfg, base.has("source.steady.voltage") ? nullptr : kHigh, 0.5 * places, sag, &src))
        return false;
    const u32 n = w.place_count();
    Array<u8> from;
    from.resize(n);
    from[src] = 1;
    Array<u32> hops;
    power_hops(w, from.data(), &hops);
    u32 far = 0;
    for (u32 i = 0; i < n; ++i)
        far = hops[i] != ~0u && hops[i] > far ? hops[i] : far;
    // Migrants wherever a core runs at the start.
    const HandCode code = migrant(MigrantKind::pushy);
    u32 placed = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = w.place(i);
        if (p.env.voltage_mv >= p.power().e.threshold)
            placed += place_migrant(w.place(i), code) ? 1 : 0;
    }
    printf("%s source (sags %u mV at its rated %.0f W) at place %u, %d mV with no load; the farthest place is %u "
           "links from it; pushy migrants placed at the %u places whose cores ran at the start\n",
           name, sag, 0.5 * places, src, w.sources().count() ? w.sources().source(0).voltage : 0, far, placed);
    w.start_threads(run.threads);
    Array<u32> by_places, by_live, by_threads;
    Array<double> by_mv, by_temp;
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (w.tick_count() % run.every != 0 && w.tick_count() != run.ticks)
            continue;
        u32 inner = ~0u, outer = ~0u, threads = 0, live = 0;
        u64 trips = 0, cuts = 0;
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            const u32 t = p.os.live_threads();
            threads += t;
            trips += trips_of(p);
            cuts += p.cutouts[0] + p.cutouts[1];
            if (!t || hops[i] == ~0u)
                continue;
            ++live;
            inner = inner == ~0u || hops[i] < inner ? hops[i] : inner;
            outer = outer == ~0u || hops[i] > outer ? hops[i] : outer;
        }
        if (end->inner_first == ~0u && inner != ~0u) {
            end->inner_first = inner;
            end->outer_first = outer;
        }
        end->inner_last = inner;
        end->outer_last = outer;
        end->threads = threads;
        end->places = live;
        end->watts = static_cast<double>(w.grid().given_uw) / 1e6;
        end->source_mv = w.place(src).env.voltage_mv;
        printf("  tick %-7llu the source gives %7.1f W at %4d mV; %6u threads in %4u places, from %2d to %2d links "
               "out; %llu heat trips, %llu cut-outs\n",
               static_cast<ull>(w.tick_count()), end->watts, w.place(src).env.voltage_mv, threads, live,
               inner == ~0u ? -1 : static_cast<int>(inner), outer == ~0u ? -1 : static_cast<int>(outer),
               static_cast<ull>(trips), static_cast<ull>(cuts));
        fflush(stdout);
    }
    // At the end, by links from the source.
    by_places.resize(far + 1);
    by_live.resize(far + 1);
    by_threads.resize(far + 1);
    by_mv.resize(far + 1);
    by_temp.resize(far + 1);
    for (u32 i = 0; i < n; ++i) {
        if (hops[i] == ~0u)
            continue;
        const Place& p = w.place(i);
        ++by_places[hops[i]];
        by_live[hops[i]] += p.os.live_threads() ? 1 : 0;
        by_threads[hops[i]] += p.os.live_threads();
        by_mv[hops[i]] += p.env.voltage_mv;
        by_temp[hops[i]] += static_cast<double>(p.temp) / kDegree;
    }
    printf("  at the end, by links from the source (places, mean mV, mean degrees, places with threads, threads):\n");
    for (u32 h = 0; h <= far; ++h)
        if (by_places[h])
            printf("    %2u: %4u places, %6.0f mV, %5.1f degrees, %4u with threads, %6u threads\n", h, by_places[h],
                   by_mv[h] / by_places[h], by_temp[h] / by_places[h], by_live[h], by_threads[h]);
    printf("\n");
    return true;
}

int band(Config& base, const Run& run) {
    BandEnd stiff, soft;
    if (!band_one(base, run, "stiff", 20, &stiff) || !band_one(base, run, "soft", 400, &soft))
        return 2;
    printf("the band: under the stiff source threads lasted from %d to %d links out at the first report and from "
           "%d to %d at the end (%u threads in %u places, the source at %.0f mV giving %.1f W); under the soft "
           "one from %d to %d at first and from %d to %d at the end (%u threads in %u places, %.0f mV, %.1f W)\n",
           static_cast<int>(stiff.inner_first), static_cast<int>(stiff.outer_first),
           static_cast<int>(stiff.inner_last), static_cast<int>(stiff.outer_last), stiff.threads, stiff.places,
           stiff.source_mv, stiff.watts, static_cast<int>(soft.inner_first), static_cast<int>(soft.outer_first),
           static_cast<int>(soft.inner_last), static_cast<int>(soft.outer_last), soft.threads, soft.places,
           soft.source_mv, soft.watts);
    return 0;
}

// --- heat --------------------------------------------------------------------

int heat(Config& base, const Run& run) {
    const double rate = key_f64(base, "heat.place.cooling");
    const u64 day = key_u64(base, "supply.day");
    static const double kTimes[3] = {0.25, 1, 4};
    static const char* const kTypes[2] = {"steady", "daylight"};
    u32 cores = 0;
    for (u32 type = 0; type < 2; ++type)
        for (u32 r = 0; r < 3; ++r) {
            // The source is rated for 15 W a core (a P core flat out at
            // 1,050 mV draws about that), and sags 30 mV there.
            char cooling[32], fixed[64];
            snprintf(cooling, sizeof cooling, "%g", rate * kTimes[r]);
            snprintf(fixed, sizeof fixed, "0 %s %u 30", kTypes[type], cores ? 15 * cores : 60);
            const char* const sets[] = {"world.places",          "1",       "world.map",                "none",
                                        "supply.model",          "sources", "sources.start",            "0",
                                        "sources.chance",        "0",       "sources.fixed",            fixed,
                                        "heat.on",               "true",    "heat.place.cooling",       cooling,
                                        "region.retry",          "1000000", "source.steady.swing",      "0",
                                        "source.steady.dropouts", "0",      "source.daylight.swing",    "0",
                                        "source.daylight.dropouts", "0",    "noise.p_floor",            "0",
                                        "noise.e_floor",         "0",       "noise.cosmic",             "0",
                                        nullptr};
            Config cfg;
            derive(base, &cfg, sets);
            World w;
            if (!build(w, cfg))
                return 2;
            Place& p = w.place(0);
            if (!cores) { // the first build only finds the cores
                cores = p.core_count();
                printf("one place of %u cores, kept full by hand (a counting thread for every core, placed again "
                       "when one ends), its source rated %u W and sagging 30 mV there; no noise; the day is %llu "
                       "ticks, and the last one is reported\n",
                       cores, 15 * cores, static_cast<ull>(day));
                --r;
                continue;
            }
            double low = 1e9, high = -1e9, hot = 0;
            u64 trips0 = 0, cuts0 = 0, placed = 0, heat0 = 0, p0 = 0, e0 = 0;
            i64 mv_low = 1 << 30, mv_high = 0;
            for (u64 t = 0; t < run.ticks; ++t) {
                if (t + day == run.ticks) {
                    trips0 = trips_of(p);
                    p0 = p.trips[kHeatP];
                    e0 = p.trips[kHeatE];
                    cuts0 = p.cutouts[0] + p.cutouts[1];
                    heat0 = p.heat_threads;
                    placed = 0;
                }
                while (p.os.live_threads() < cores && place_counter(p))
                    ++placed;
                w.tick();
                if (t + day < run.ticks)
                    continue;
                const double deg = static_cast<double>(p.temp) / kDegree;
                low = deg < low ? deg : low;
                high = deg > high ? deg : high;
                mv_low = p.env.voltage_mv < mv_low ? p.env.voltage_mv : mv_low;
                mv_high = p.env.voltage_mv > mv_high ? p.env.voltage_mv : mv_high;
                for (u32 c = 0; c < p.core_count(); ++c) {
                    const double d = static_cast<double>(p.core_temp[c]) / kDegree;
                    hot = d > hot ? d : hot;
                }
            }
            const u64 trips = trips_of(p) - trips0, cuts = p.cutouts[0] + p.cutouts[1] - cuts0;
            const char* verdict = trips            ? "cycles through trips"
                                  : high - low < 1 ? "settles"
                                                   : "follows the day";
            printf("  %-8s source, cooling %6.3f W a degree: the place from %5.1f to %5.1f degrees, the hottest "
                   "core %5.1f; %4d to %4d mV; in the day %llu heat trips (%llu of P cores, %llu of E cores), "
                   "%llu core cut-outs, %llu threads ended by heat and %llu placed again: it %s\n",
                   kTypes[type], rate * kTimes[r], low, high, hot, static_cast<int>(mv_low),
                   static_cast<int>(mv_high), static_cast<ull>(trips), static_cast<ull>(p.trips[kHeatP] - p0),
                   static_cast<ull>(p.trips[kHeatE] - e0), static_cast<ull>(cuts),
                   static_cast<ull>(p.heat_threads - heat0), static_cast<ull>(placed), verdict);
            fflush(stdout);
        }
    return 0;
}

// --- water -------------------------------------------------------------------

int water(Config& base, const Run& run) {
    World w;
    Config cfg;
    u32 src = 0;
    const u32 places = key_u32(base, "world.places");
    if (!one_source(base, w, &cfg, nullptr, 5.0 * places, 20, &src))
        return 2;
    w.start_threads(run.threads);
    const u64 ticks = run.ticks < 200 ? run.ticks : 200; // nothing is placed: it settles at once
    for (u64 t = 0; t < ticks; ++t)
        w.tick();
    const Map& m = w.map();
    const u32 n = w.place_count(), home = m.landmass(src);
    const u32 masses = m.landmasses();
    // Each landmass: its places, its voltages, and the least water on a
    // proximity link the solve keeps between it and the source's landmass.
    Array<u32> count;
    Array<double> sum, low, high, least;
    count.resize(masses);
    sum.resize(masses);
    low.resize(masses);
    high.resize(masses);
    least.resize(masses);
    low.fill(1e9);
    least.fill(-1);
    for (u32 i = 0; i < n; ++i) {
        const u32 k = m.landmass(i);
        const double mv = w.place(i).env.voltage_mv;
        ++count[k];
        sum[k] += mv;
        low[k] = mv < low[k] ? mv : low[k];
        high[k] = mv > high[k] ? mv : high[k];
    }
    u32 kept = 0, dropped = 0;
    for (u32 k = 0; k < m.power_link_count(); ++k) {
        const PowerLink& l = m.power_link(k);
        if (l.water <= 0)
            continue;
        if (!w.grid().link_g(k)) {
            ++dropped;
            continue;
        }
        ++kept;
        const u32 a = m.landmass(l.a), b = m.landmass(l.b);
        if ((a == home) == (b == home))
            continue;
        const u32 other = a == home ? b : a;
        const double width = l.water / m.mean_length(); // in average links
        if (least[other] < 0 || width < least[other])
            least[other] = width;
    }
    printf("one stiff source at place %u (landmass %u, %u places): after %llu ticks its landmass is at %.0f to "
           "%.0f mV, %.0f on average; %u proximity links cross water and carry power, %u more are too weak to "
           "keep (water.reach %g average links)\n",
           src, home, count[home], static_cast<ull>(ticks), low[home], high[home],
           count[home] ? sum[home] / count[home] : 0.0, kept, dropped, key_f64(cfg, "map.water.reach"));
    printf("the landmasses across water from it, by the least water on a link straight to it (in average links):\n");
    // Nearest first: a strait, then wider water, then those with no link straight across (islands beyond).
    u32 shown = 0, beyond = 0, beyond_places = 0, dark = 0;
    double beyond_high = 0;
    for (u32 pass = 0; pass < masses; ++pass) {
        u32 best = ~0u;
        for (u32 k = 0; k < masses; ++k)
            if (k != home && count[k] && least[k] >= 0 && (best == ~0u || least[k] < least[best]))
                best = k;
        if (best == ~0u)
            break;
        printf("  landmass %3u, %3u places, %6.3f links of water: %4.0f to %4.0f mV, %4.0f on average\n", best,
               count[best], least[best], low[best], high[best], sum[best] / count[best]);
        least[best] = -1;
        count[best] = 0;
        ++shown;
    }
    for (u32 k = 0; k < masses; ++k)
        if (k != home && count[k]) {
            ++beyond;
            beyond_places += count[k];
            beyond_high = high[k] > beyond_high ? high[k] : beyond_high;
            dark += high[k] <= 0 ? 1 : 0;
        }
    printf("%u landmasses with a link straight across; %u more (%u places) reached only through others or not at "
           "all: %u of them at 0 V, the highest voltage among them %.0f mV\n",
           shown, beyond, beyond_places, dark, beyond_high);
    return 0;
}

// --- the sleeper -------------------------------------------------------------

int sleeper(Config& base, const Run& run) {
    // Daylight sources only, with nights deep enough that the cores cut
    // out, and a margin wide enough to catch the dusk.
    static const char* const kNights[] = {"supply.model",          "sources", "source.steady.share",    "0",
                                          "source.gusty.share",    "0",       "source.daylight.share",  "1",
                                          "source.daylight.night", "0.02",    "source.daylight.swing",  "0",
                                          "source.daylight.dropouts", "0",    "region.retry",           "1000000",
                                          "noise.p_floor",         "0",       "noise.e_floor",          "0",
                                          "noise.cosmic",          "0",       nullptr};
    Config cfg;
    derive(base, &cfg, kNights);
    World w;
    if (!build(w, cfg))
        return 2;
    const u64 day = key_u64(cfg, "supply.day");
    // The guarded thread: sets a low-voltage handler (16 bytes in), then
    // counts. Its handler sleeps 100 x `hundreds` ticks, about half a day,
    // and returns.
    const u32 hundreds = static_cast<u32>(day / 200 ? (day / 200 > 120 ? 120 : day / 200) : 1);
    const u8 guarded[22] = {encode(kMov, kB),   encode(kAdd, kImm), 12,     encode(kSto, kB),
                            encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                            encode(kAdd, kImm), 1,                  encode(kJmp, kImm), static_cast<u8>(-2),
                            encode(kMov, kImm), 100,                encode(kMul, kImm), static_cast<u8>(hundreds),
                            encode(kSys, kImm), kSleep,             encode(kSys, kImm), kHret,
                            0,                  0};
    const u32 n = w.place_count();
    // The threads placed, by place: their record indices and ids.
    struct Placed {
        u32 place, thread, id;
        u64 program;
        bool guarded;
    };
    Array<Placed> placed;
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        for (u32 k = 0; k < 8; ++k) {
            const bool g = k < 4;
            const u32 at = p.mem.alloc(1, 64, p.mem.strict() ? 1 : 2, 0);
            if (!at)
                break;
            memcpy(p.mem.bytes() + at, g ? guarded : kCounter, g ? 20 : sizeof kCounter);
            const u32 id = p.os.place_by_hand(1, at, at, 0);
            for (u32 t = 0; id && t < p.os.thread_capacity(); ++t)
                if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id) {
                    placed.push(Placed{i, t, id, p.os.program(p.os.thread(t).program).uid, g});
                    break;
                }
        }
    }
    u32 placed_g = 0, placed_p = 0;
    for (usize k = 0; k < placed.size(); ++k)
        (placed[k].guarded ? placed_g : placed_p) += 1;
    printf("%u places under daylight sources (night at %.0f%% of the day's capacity, the day %llu ticks, the "
           "low-voltage margin %u mV): %u threads with a handler that sleeps %u ticks on the interrupt and %u "
           "without, placed by hand, 4 and 4 a place; none copies itself\n",
           n, 100 * key_f64(cfg, "source.daylight.night"), static_cast<ull>(day),
           key_u32(cfg, "power.low_voltage_margin"), placed_g, 100 * hundreds, placed_p);
    w.start_threads(run.threads);
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (w.tick_count() % run.every != 0 && w.tick_count() != run.ticks)
            continue;
        u32 left_g = 0, left_p = 0, asleep = 0, lit = 0;
        u64 raised = 0, taken = 0, slept = 0, outlived = 0, cuts = 0;
        for (usize k = 0; k < placed.size(); ++k) {
            const Place& p = w.place(placed[k].place);
            if (placed[k].thread >= p.os.thread_top())
                continue;
            const Thread& th = p.os.thread(placed[k].thread);
            if (th.state == ThreadState::free || th.id != placed[k].id ||
                p.os.program(th.program).uid != placed[k].program)
                continue;
            (placed[k].guarded ? left_g : left_p) += 1;
            asleep += th.state == ThreadState::sleeping ? 1 : 0;
        }
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            raised += p.sched.lv_raised;
            taken += p.sched.lv_taken;
            slept += p.lv_slept;
            outlived += p.lv_outlived;
            cuts += p.cutouts[0] + p.cutouts[1];
            lit += p.env.voltage_mv >= p.power().p.threshold ? 1 : 0;
        }
        printf("  tick %-7llu %4u places above the P cores' threshold; left: %5u of %u with the handler (%u "
               "asleep), %5u of %u without; the interrupt raised %llu times, taken %llu, %llu slept on it, %llu "
               "of those woke after a cut; %llu core cut-outs\n",
               static_cast<ull>(w.tick_count()), lit, left_g, placed_g, asleep, left_p, placed_p,
               static_cast<ull>(raised), static_cast<ull>(taken), static_cast<ull>(slept),
               static_cast<ull>(outlived), static_cast<ull>(cuts));
        fflush(stdout);
    }
    return 0;
}

// --- load rejection -----------------------------------------------------------

// A counting thread for every core of the crowd's places that has none
// (cut-outs end threads, so a crowd left alone thins out).
void fill_crowd(World& w, const Array<u8>& crowd) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        if (!crowd[i])
            continue;
        Place& p = w.place(i);
        while (p.os.live_threads() < p.core_count() && place_counter(p)) {
        }
    }
}

// The crowd of one world: the places within two links of the source,
// marked; how many they are.
u32 place_crowd(World& w, u32 src, Array<u8>* crowd) {
    const u32 n = w.place_count();
    Array<u8> from;
    from.resize(n);
    from[src] = 1;
    Array<u32> hops;
    power_hops(w, from.data(), &hops);
    crowd->resize(n);
    u32 crowded = 0;
    for (u32 i = 0; i < n; ++i)
        if (hops[i] <= 2) {
            (*crowd)[i] = 1;
            ++crowded;
        }
    return crowded;
}

void trips_and_cuts(World& w, u64* trips, u64* cuts) {
    *trips = *cuts = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        *trips += trips_of(w.place(i));
        *cuts += w.place(i).cutouts[0] + w.place(i).cutouts[1];
    }
}

// Two worlds the same to the tick of the cut, the crowd kept full in both
// (a thread placed again for each core that has lost its own): the crowd
// is cut off in one and kept in its twin. A world this size is never at
// rest (cores at the edge of the lit places cut in and out, and places
// warm for thousands of ticks), so the cut's doing is the difference
// between the two from there on, tick for tick, not the difference between
// before and after.
bool rejection_one(Config& base, const Run& run, const char* name, u32 sag) {
    static const char* const kQuiet[] = {"region.retry", "1000000", "noise.p_floor", "0", "noise.e_floor", "0",
                                         "noise.cosmic", "0",       nullptr};
    World w, twin;
    Config cfg, cfg2;
    u32 src = 0, src2 = 0;
    // Rated for the crowd alone: 15 W for each of about 20 places.
    if (!one_source(base, w, &cfg, kQuiet, 300, sag, &src) || !one_source(base, twin, &cfg2, kQuiet, 300, sag, &src2))
        return false;
    const u32 n = w.place_count();
    Array<u8> crowd, crowd2;
    const u32 crowded = place_crowd(w, src, &crowd);
    place_crowd(twin, src2, &crowd2);
    Array<u32> hops;
    power_hops(w, crowd.data(), &hops); // links from the crowd
    u32 far = 0;
    for (u32 i = 0; i < n; ++i)
        far = hops[i] != ~0u && hops[i] > far ? hops[i] : far;
    w.start_threads(run.threads);
    twin.start_threads(run.threads);
    const u64 settle = run.ticks > 400 ? run.ticks - 100 : 300;
    for (u64 t = 0; t < settle; ++t) {
        fill_crowd(w, crowd);
        fill_crowd(twin, crowd);
        w.tick();
        twin.tick();
    }
    if (w.hash() != twin.hash()) {
        fprintf(stderr, "nova2: the two worlds of the load rejection test differ before the cut\n");
        return false;
    }
    double crowd_w = 0;
    for (u32 i = 0; i < n; ++i)
        crowd_w += crowd[i] ? static_cast<double>(w.place(i).draw.total()) / 1e6 : 0;
    u64 trips0 = 0, cuts0 = 0;
    trips_and_cuts(w, &trips0, &cuts0);
    const double given0 = static_cast<double>(w.grid().given_uw) / 1e6;
    const i32 src_before = w.place(src).env.voltage_mv;
    // Cut off at once, by hand: every thread in the crowd ends.
    u32 ended = 0;
    for (u32 i = 0; i < n; ++i) {
        if (!crowd[i])
            continue;
        Place& p = w.place(i);
        for (u32 t = 0; t < p.os.thread_top(); ++t)
            if (p.os.thread(t).state != ThreadState::free) {
                p.os.end_thread(t, DeathCause::exit);
                ++ended;
            }
    }
    // The 70 ticks after, in both: each place's rise over its twin, on
    // average over ticks 20 to 70 and at its most in any tick.
    Array<i64> rise, peak, peak2, most_tick;
    rise.resize(n);
    peak.resize(n);
    peak2.resize(n);
    most_tick.resize(n);
    double crowd_after = 0, crowd_twin = 0; // the crowd's draw at the end, in each
    for (u32 t = 0; t < 70; ++t) {
        fill_crowd(twin, crowd);
        w.tick();
        twin.tick();
        for (u32 i = 0; i < n; ++i) {
            const i64 mv = w.place(i).env.voltage_mv, mv2 = twin.place(i).env.voltage_mv;
            peak[i] = mv > peak[i] ? mv : peak[i];
            peak2[i] = mv2 > peak2[i] ? mv2 : peak2[i];
            most_tick[i] = mv - mv2 > most_tick[i] ? mv - mv2 : most_tick[i];
            if (t >= 20)
                rise[i] += mv - mv2;
        }
    }
    u64 trips1 = 0, cuts1 = 0, trips2 = 0, cuts2 = 0;
    trips_and_cuts(w, &trips1, &cuts1);
    trips_and_cuts(twin, &trips2, &cuts2);
    u32 over = 0;
    i64 highest = 0;
    for (u32 i = 0; i < n; ++i) {
        over += peak[i] > w.place(i).power().p.maximum && peak2[i] <= w.place(i).power().p.maximum ? 1 : 0;
        highest = most_tick[i] > highest ? most_tick[i] : highest;
        crowd_after += crowd[i] ? static_cast<double>(w.place(i).draw.total()) / 1e6 : 0;
        crowd_twin += crowd[i] ? static_cast<double>(twin.place(i).draw.total()) / 1e6 : 0;
    }
    printf("%s source (sags %u mV at 300 W) at place %u: a crowd kept full in the %u places within two links of it "
           "drew %.1f W of the %.1f W it gave, at %d mV; its %u threads cut off at once, the crowd's places drew "
           "%.1f W 70 ticks later against %.1f W in the twin (the same world with the crowd kept), and the "
           "source's place stood %.2f mV above its twin's over ticks 20 to 70 after\n",
           name, sag, src, crowded, crowd_w, given0, src_before, ended, crowd_after, crowd_twin,
           static_cast<double>(rise[src]) / 50);
    printf("  the rise over the twin, by links from the crowd (places; the mean, the least and the most in mV):\n");
    u32 reach_mean = 0, reach_any = 0;
    for (u32 h = 0; h <= far; ++h) {
        u32 count = 0;
        double sum = 0, most = -1e9, least = 1e9;
        for (u32 i = 0; i < n; ++i) {
            if (hops[i] != h)
                continue;
            const double r = static_cast<double>(rise[i]) / 50;
            ++count;
            sum += r;
            most = r > most ? r : most;
            least = r < least ? r : least;
        }
        if (!count)
            continue;
        if (sum / count >= 1)
            reach_mean = h;
        if (most >= 1)
            reach_any = h;
        if (h <= 12 || most >= 1 || least <= -1)
            printf("    %2u: %4u places, %7.2f mV, %7.2f mV, %7.2f mV\n", h, count, sum / count, least, most);
    }
    printf("  a mean rise of a millivolt or more reached %u links from the crowd, and some place's %u links; the "
           "most any place stood above its twin in one tick was %lld mV; %u places passed their cores' maximum "
           "that stayed under it in the twin; %llu heat trips and %llu core cut-outs in the 70 ticks after, "
           "against %llu and %llu in the twin\n\n",
           reach_mean, reach_any, static_cast<long long>(highest), over, static_cast<ull>(trips1 - trips0),
           static_cast<ull>(cuts1 - cuts0), static_cast<ull>(trips2 - trips0), static_cast<ull>(cuts2 - cuts0));
    fflush(stdout);
    return true;
}

int rejection(Config& base, const Run& run) {
    return rejection_one(base, run, "soft", 300) && rejection_one(base, run, "stiff", 20) ? 0 : 2;
}

// --- section 7's numbers, measured ---------------------------------------------

// A busy core's temperature at rest in a place at `place` degrees, drawing
// `watts` before leakage: T = place + watts x (1 + leak x (T - outside)) /
// sink, solved for T. 1e9 if the leakage runs away.
double core_rest(double watts, double place, double sink, double leak, double outside) {
    const double g = leak * watts / sink;
    return g >= 1 ? 1e9 : (place + watts / sink * (1 - leak * outside)) / (1 - g);
}

// The lowest voltage at which a core working all tick rests at or past its
// heat limit in a place at `place` degrees; 0 if it never does up to 2 V.
i32 trip_voltage(const CorePower& k, const HeatPart& part, const HeatSettings& hs, double place) {
    for (i32 mv = k.threshold; mv <= 2000; ++mv)
        if (core_rest(core_work_uw(k, mv) / 1e6, place, part.sink, hs.leakage, hs.outside / 10.0) >= part.limit)
            return mv;
    return 0;
}

void numbers_cores(const PowerSettings& ps, const HeatSettings& hs) {
    const double outside = hs.outside / 10.0;
    const CorePower* kinds[2] = {&ps.p, &ps.e};
    const char* names[2] = {"P", "E"};
    for (u32 c = 0; c < 2; ++c) {
        const CorePower& k = *kinds[c];
        const HeatPart& part = hs.part[c == 0 ? kHeatP : kHeatE];
        printf("a %s core (threshold %d mV, maximum %d mV, heatsink %.3f W a degree, limit %.0f degrees), working "
               "all tick:\n  mV, cycles a tick, watts, and its temperature at rest in a place at 25, 40 and 60 "
               "degrees (leakage %.1f%% a degree above %.0f)\n",
               names[c], k.threshold, k.maximum, part.sink, part.limit, 100 * hs.leakage, outside);
        for (i32 mv = k.threshold; mv <= k.maximum + 100; mv += 50) {
            const double w = core_work_uw(k, mv) / 1e6;
            printf("  %5d %7u %7.2f W ", mv, core_speed(k, mv), w);
            const double at[3] = {25, 40, 60};
            for (u32 a = 0; a < 3; ++a) {
                const double t = core_rest(w, at[a], part.sink, hs.leakage, outside);
                if (t > 999)
                    printf("  runs away");
                else
                    printf(" %8.1f%s", t, t >= part.limit ? "*" : " ");
            }
            printf("\n");
        }
        printf("  it trips from (mV), by its place's temperature: ");
        const double places[5] = {25, 30, 40, 50, 60};
        for (u32 a = 0; a < 5; ++a)
            printf("%.0f degrees: %d%s", places[a], trip_voltage(k, part, hs, places[a]), a < 4 ? "; " : "\n");
        // 7's rule: it should trip about 100 mV under its maximum in a
        // place at 40 degrees. The heatsink that does that, with the
        // leakage counted and without.
        const double w = core_work_uw(k, k.maximum - 100) / 1e6;
        printf("  7's rule (trips 100 mV under its maximum, at %d mV, in a place at 40 degrees) needs a heatsink of "
               "%.3f W a degree with the leakage counted, %.3f without; idle it draws %.2f W\n\n",
               k.maximum - 100, w * (1 + hs.leakage * (part.limit - outside)) / (part.limit - 40),
               w / (part.limit - 40), k.idle_mw / 1e3);
    }
}

double middle_of(Array<u64>& v) {
    if (v.empty())
        return 0;
    sort_u64(v.data(), v.size());
    return static_cast<double>(v[v.size() / 2]);
}

// The config's places under a steady 1,000 mV each (a supply of its own,
// no sag, no day): what each draws idle and with every core busy, and the
// temperature it comes to rest at.
bool numbers_places(Config& base, const Run& run, const HeatSettings& hs) {
    static const char* const kFlat[] = {"supply.model", "place",  "supply.voltage",  "1000", "supply.sag",    "0",
                                        "supply.night", "1",      "supply.swing",    "0",    "supply.dropouts", "0",
                                        "noise.p_floor", "0",     "noise.e_floor",   "0",    "noise.cosmic",  "0",
                                        "region.retry", "1000000", nullptr};
    Config cfg;
    derive(base, &cfg, kFlat);
    World w;
    if (!build(w, cfg))
        return false;
    w.start_threads(run.threads);
    const u32 n = w.place_count();
    const u64 rest = 4000; // eight of a place's time constants
    for (u64 t = 0; t < rest; ++t)
        w.tick();
    Array<u64> idle, idle_t, cool; // by place: microwatts, a temperature, microwatts a degree
    double idle_sum = 0, mem = 0, disk = 0, net = 0, cores = 0;
    u32 coolers = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = w.place(i);
        idle.push(p.draw.total());
        idle_t.push(static_cast<u64>(p.temp));
        cool.push(w.cooling_rate(i));
        idle_sum += p.draw.total() / 1e6;
        mem += p.draw.memory / 1e6;
        disk += p.draw.disk / 1e6;
        net += p.draw.network / 1e6;
        cores += p.draw.cores / 1e6;
        coolers += p.has_cooler ? 1 : 0;
    }
    const double idle_mid = middle_of(idle) / 1e6, temp_mid = middle_of(idle_t) / kDegree;
    const double cool_mid = middle_of(cool) / 1e6; // and cool is sorted now
    printf("%u places at a steady 1,000 mV, nothing running, after %llu ticks: they draw %.1f W in all, %.2f a place "
           "(cores %.2f, memory %.2f, disk %.2f, network %.2f); the middle place %.2f W, at %.1f degrees; cooling "
           "rates from %.2f to %.2f W a degree, the middle %.2f; %u have a cooling device\n",
           n, static_cast<ull>(rest), idle_sum, idle_sum / n, cores / n, mem / n, disk / n, net / n, idle_mid,
           temp_mid, cool.empty() ? 0.0 : cool[0] / 1e6, cool.empty() ? 0.0 : cool[cool.size() - 1] / 1e6, cool_mid,
           coolers);
    // Every core busy, kept so by hand.
    u64 trips0 = 0;
    for (u32 i = 0; i < n; ++i)
        trips0 += trips_of(w.place(i));
    Array<u64> temp_sum, draw_sum;
    temp_sum.resize(n);
    draw_sum.resize(n);
    const u64 last = 1000;
    for (u64 t = 0; t < rest + last; ++t) {
        for (u32 i = 0; i < n; ++i) {
            Place& p = w.place(i);
            while (p.os.live_threads() < p.core_count() && place_counter(p)) {
            }
        }
        w.tick();
        if (t >= rest)
            for (u32 i = 0; i < n; ++i) {
                temp_sum[i] += static_cast<u64>(w.place(i).temp);
                draw_sum[i] += w.place(i).draw.total();
            }
    }
    printf("every core kept busy for %llu ticks more, the last %llu averaged; by cores (places; watts drawn: the "
           "least, the mean, the most; degrees at rest: the least, the mean, the most; heat trips a place in "
           "those %llu ticks):\n",
           static_cast<ull>(rest + last), static_cast<ull>(last), static_cast<ull>(rest + last));
    double full_sum = 0;
    u32 over_disk = 0;
    for (u32 c = 1; c <= kMaxCores; ++c) {
        u32 places = 0;
        double wl = 1e9, wm = 0, wh = 0, tl = 1e9, tm = 0, th = 0;
        u64 trips = 0;
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            if (p.core_count() != c)
                continue;
            const double watts = draw_sum[i] / 1e6 / last, deg = static_cast<double>(temp_sum[i]) / kDegree / last;
            ++places;
            wl = watts < wl ? watts : wl;
            wh = watts > wh ? watts : wh;
            wm += watts;
            tl = deg < tl ? deg : tl;
            th = deg > th ? deg : th;
            tm += deg;
            trips += trips_of(p);
            full_sum += watts;
            over_disk += deg >= hs.part[kHeatDisk].limit ? 1 : 0;
        }
        if (places)
            printf("  %u cores: %3u places; %6.2f %6.2f %6.2f W; %6.1f %6.1f %6.1f degrees; %.0f trips\n", c, places,
                   wl, wm / places, wh, tl, tm / places, th, static_cast<double>(trips) / places);
    }
    u64 trips1 = 0;
    for (u32 i = 0; i < n; ++i)
        trips1 += trips_of(w.place(i));
    printf("  in all %.1f W, %.2f a place; %u places rest at or past the disks' limit; %llu heat trips\n\n", full_sum,
           full_sum / n, over_disk, static_cast<ull>(trips1 - trips0));
    fflush(stdout);
    return true;
}

// The config's world as it stands, without noise: where the power reaches,
// with nothing living in it, or (busy) with every core kept busy by hand,
// the two ends between which a living world lies. For each place, its links
// from the nearest source alive at the end, its mean voltage, the shares of
// its cores' time cut out and off by a heat trip, its mean temperature, and
// its ground by the ground measure's rule over the whole run; summed by
// links.
bool numbers_lay(Config& base, const Run& run, bool busy) {
    static const char* const kEmpty[] = {"noise.p_floor", "0", "noise.e_floor", "0", "noise.cosmic", "0",
                                         "region.retry",  "1000000", nullptr};
    Config cfg;
    derive(base, &cfg, kEmpty);
    World w;
    if (!build(w, cfg))
        return false;
    if (!w.powered() || !w.map().placed()) {
        printf("(the lay of the power needs the sources and a map: skipped)\n");
        return true;
    }
    w.start_threads(run.threads);
    const u32 n = w.place_count();
    Array<u64> mv, temp;
    mv.resize(n);
    temp.resize(n);
    double given = 0, drawn = 0;
    for (u64 t = 0; t < run.ticks; ++t) {
        if (busy)
            for (u32 i = 0; i < n; ++i) {
                Place& p = w.place(i);
                while (p.os.live_threads() < p.core_count() && place_counter(p)) {
                }
            }
        w.tick();
        given += w.grid().given_uw / 1e6;
        for (u32 i = 0; i < n; ++i) {
            mv[i] += static_cast<u64>(w.place(i).env.voltage_mv);
            temp[i] += static_cast<u64>(w.place(i).temp);
            drawn += w.place(i).draw.total() / 1e6;
        }
    }
    Array<u8> from;
    from.resize(n);
    const Sources& s = w.sources();
    double rated = 0;
    u32 alive = 0;
    for (u32 k = 0; k < s.count(); ++k) {
        from[s.source(k).place] = 1;
        rated += s.source(k).rated_uw / 1e6;
        ++alive;
    }
    Array<u32> hops;
    power_hops(w, from.data(), &hops);
    printf("seed %llu as it stands, %s, %llu ticks: %u sources rated %.0f W in all gave %.0f W on average, the "
           "places drew %.0f W. By links from the nearest source (places; mean mV; the shares of their cores' time "
           "cut out and off by a heat trip; places hot, dim and fair by the ground measure's rule over the whole "
           "run; mean degrees):\n",
           static_cast<ull>(key_u64(cfg, "world.seed")), busy ? "every core kept busy" : "nothing living",
           static_cast<ull>(run.ticks), alive, rated, given / run.ticks, drawn / run.ticks);
    u32 far = 0, kinds_all[kGrounds] = {}, both_all = 0;
    double cut_all = 0, heat_all = 0, time_all = 0;
    for (u32 i = 0; i < n; ++i)
        far = hops[i] != ~0u && hops[i] > far ? hops[i] : far;
    for (u32 h = 0; h <= far + 1; ++h) {
        const bool none = h == far + 1; // the places no source reaches
        u32 places = 0, kinds[kGrounds] = {};
        double v = 0, cut = 0, hot = 0, core_time = 0, deg = 0;
        for (u32 i = 0; i < n; ++i) {
            if (none ? hops[i] != ~0u : hops[i] != h)
                continue;
            const Place& p = w.place(i);
            const u64 off = p.off_ticks[0] + p.off_ticks[1], trip = p.heat_ticks[0] + p.heat_ticks[1];
            bool both = false;
            const Ground g = ground_of(p.core_count(), run.ticks, off, trip, p.region_off_ticks, &both);
            ++places;
            ++kinds[static_cast<u32>(g)];
            both_all += both ? 1 : 0;
            v += static_cast<double>(mv[i]) / run.ticks;
            deg += static_cast<double>(temp[i]) / kDegree / run.ticks;
            cut += static_cast<double>(off);
            hot += static_cast<double>(trip);
            core_time += static_cast<double>(p.core_count()) * run.ticks;
        }
        if (!places)
            continue;
        for (u32 k = 0; k < kGrounds; ++k)
            kinds_all[k] += kinds[k];
        cut_all += cut;
        heat_all += hot;
        time_all += core_time;
        printf("  %s%2u: %3u places, %5.0f mV, %5.1f%% cut out, %5.1f%% tripped, %2u hot, %2u dim, %2u fair, %5.1f "
               "degrees\n",
               none ? "no way " : "", none ? 0 : h, places, v / places, core_time ? 100 * cut / core_time : 0.0,
               core_time ? 100 * hot / core_time : 0.0, kinds[static_cast<u32>(Ground::hot)],
               kinds[static_cast<u32>(Ground::dim)], kinds[static_cast<u32>(Ground::fair)], deg / places);
    }
    printf("  of %u places: %u hot, %u dim, %u fair (%u of them hot and dim at once); %.1f%% of all core time cut "
           "out, %.1f%% tripped\n\n",
           n, kinds_all[static_cast<u32>(Ground::hot)], kinds_all[static_cast<u32>(Ground::dim)],
           kinds_all[static_cast<u32>(Ground::fair)], both_all, time_all ? 100 * cut_all / time_all : 0.0,
           time_all ? 100 * heat_all / time_all : 0.0);
    fflush(stdout);
    return true;
}

// A classing (build-plan-gate4.md 11, the pass mark as changed): each place
// in turn with its cores kept busy and nothing else living, in the config's
// world without noise, under the sources of one tick: the ground a first
// settler finds there. A world is built for each place, moved to the tick
// and run 5 days (or worlds.ticks, if less); the place's ground is the
// ground measure's over that run.
//
// The places' worlds run side by side, one host thread each, on run.threads
// threads: a world's run is the same on any number of threads, and no world
// here knows of another, so the rows are the same however many there are.
struct ClassRow {
    u64 tick = 0; // the classing's: its world's sources, straits and respawns are this tick's
    u32 place = 0, page = 0, cores = 0;
    u32 incarnation = 0; // the place's at the classing's tick: its respawns to then
    u32 links = ~0u; // from the nearest source alive at the classing's tick; ~0: no way
    bool strict = false, both = false, failed = false;
    Ground ground = Ground::fair;
    u64 ticks = 0;                   // the place was kept full for
    u64 cut = 0, heat = 0, dark = 0; // core-ticks cut out and off by heat; ticks the reset region's memory was off
    u64 mv = 0;                      // its voltage, summed over the ticks
};

struct Classing {
    Str text; // the settings every one of its worlds is built from
    u64 from = 0, ticks = 0;
    ClassRow* rows = nullptr;
    // The run's respawns at or before the tick, in the order they came.
    const RespawnRow* respawns = nullptr;
    u32 respawn_count = 0;
};

// A world of the classing's settings at the classing's tick, not yet run:
// the run's respawns to the tick applied, each checked against the run's
// record of it (its incarnation, its point to the record's six places, and
// whether it moved), so that the map is the run's. Built from the settings'
// text: a Config is one thread's. False, with why if given, if a world
// can't be built or a respawn isn't the run's.
bool class_world(const Classing& c, World* w, TextBuf* why = nullptr) {
    Config cfg;
    Config::ParseError pe;
    cfg.parse(c.text, &pe);
    if (!build(*w, cfg))
        return false;
    for (u32 k = 0; k < c.respawn_count; ++k) {
        const RespawnRow& r = c.respawns[k];
        if (r.place >= w->place_count()) {
            if (why)
                why->append("a respawn of a place this world doesn't have");
            return false;
        }
        const u64 moved = w->respawns_moved;
        w->respawn_at(r.place, r.tick);
        const Map& m = w->map();
        const double dx = m.placed() ? m.x(r.place) - r.x : 0, dy = m.placed() ? m.y(r.place) - r.y : 0;
        const bool same = m.incarnation(r.place) == r.incarnation && (w->respawns_moved > moved) == r.moved &&
                          dx < 6e-7 && dx > -6e-7 && dy < 6e-7 && dy > -6e-7;
        if (!same) {
            if (why) {
                char say[300];
                snprintf(say, sizeof say,
                         "the respawn of place %u at tick %llu (its record's row %u) isn't the run's: incarnation %u "
                         "for %u, at (%.6f, %.6f) for (%.6f, %.6f), %s",
                         r.place, static_cast<ull>(r.tick), k + 1, m.incarnation(r.place), r.incarnation,
                         m.placed() ? m.x(r.place) : 0.0, m.placed() ? m.y(r.place) : 0.0, r.x, r.y,
                         (w->respawns_moved > moved) == r.moved ? "moved as it did" : "moved where it didn't, or not");
                why->append(say);
            }
            return false;
        }
    }
    w->begin_at(c.from);
    return true;
}

// Place `at` of a world at its classing's tick kept full alone for `ticks`
// ticks: what it lost, and its class.
void keep_full(World& w, u32 at, u64 ticks, ClassRow& r) {
    for (u64 t = 0; t < ticks; ++t) {
        Place& p = w.place(at);
        while (p.os.live_threads() < p.core_count() && place_counter(p)) {
        }
        w.tick();
        r.mv += static_cast<u64>(w.place(at).env.voltage_mv);
    }
    const Place& p = w.place(at);
    r.ticks = ticks;
    r.cut = p.off_ticks[0] + p.off_ticks[1];
    r.heat = p.heat_ticks[0] + p.heat_ticks[1];
    r.dark = p.region_off_ticks;
    r.ground = ground_of(p.core_count(), ticks, r.cut, r.heat, r.dark, &r.both);
}

// Each place's links from the nearest source alive at the world's tick.
void source_hops(World& w, Array<u32>* hops) {
    Array<u8> at;
    at.resize(w.place_count());
    at.fill(0);
    for (u32 k = 0; k < w.sources().count(); ++k)
        at[w.sources().source(k).place] = 1;
    power_hops(w, at.data(), hops);
}

// What a row says of place i before it is kept full: the place as it stands
// at the tick (its incarnation's, as dealt), and its links.
void row_as_built(World& w, u32 i, const Array<u32>& hops, ClassRow* r) {
    *r = ClassRow{};
    r->tick = w.tick_count();
    r->place = i;
    r->incarnation = w.map().incarnation(i);
    r->strict = w.place(i).mem.strict();
    r->page = w.place(i).mem.page_size();
    r->cores = w.place(i).core_count();
    r->links = hops[i];
}

void class_one(void* ctx, u32 at) {
    const Classing& c = *static_cast<Classing*>(ctx);
    ClassRow& r = c.rows[at];
    World w;
    if (!class_world(c, &w)) {
        r.failed = true;
        return;
    }
    keep_full(w, at, c.ticks, r);
}

// The places classed alone at their respawns' ticks (build-plan-gate5.md
// 11; Damian, 2026-11-22, "Yes, both leans"): job k is respawn jobs[k] of
// the run, its place classed under the sources and straits of its tick with
// the run's respawns at or before that tick applied.
struct RespawnClassing {
    Str text;
    u64 ticks = 0;
    const RespawnRow* respawns = nullptr;
    u32 respawn_count = 0; // the run's
    const u32* jobs = nullptr;
    ClassRow* rows = nullptr;
};

// The classing a respawn's job is: its world's tick and respawns.
Classing respawn_classing(const RespawnClassing& c, u32 job) {
    const u32 k = c.jobs[job];
    Classing one;
    one.text = c.text;
    one.from = c.respawns[k].tick;
    one.ticks = c.ticks;
    one.respawns = c.respawns;
    one.respawn_count = k + 1;
    while (one.respawn_count < c.respawn_count && c.respawns[one.respawn_count].tick <= one.from)
        ++one.respawn_count;
    return one;
}

void class_respawned(void* ctx, u32 job) {
    const RespawnClassing& c = *static_cast<RespawnClassing*>(ctx);
    const Classing one = respawn_classing(c, job);
    const u32 place = c.respawns[c.jobs[job]].place;
    ClassRow& r = c.rows[job];
    World w;
    if (!class_world(one, &w)) {
        r = ClassRow{};
        r.failed = true;
        return;
    }
    Array<u32> hops;
    source_hops(w, &hops);
    row_as_built(w, place, hops, &r);
    keep_full(w, place, c.ticks, r);
}

// The settings a classing's worlds are built from: the run's, without
// noise, and with every gate 5 strength off but the straits
// (build-plan-gate4.md 11; build-plan-gate5.md 11, "Yes, as built"): the
// class is of the place as built, under the sources of the tick, with the
// straits parted or joined as their spells stand then, parting and joining
// through the classing's ticks as the run's do. Both are the seed's and the
// tick's alone.
void classing_settings(Config& base, TextBuf* text) {
    static const char* const kEmpty[] = {"noise.p_floor", "0", "noise.e_floor", "0", "noise.cosmic", "0",
                                         "region.retry",  "1000000", nullptr};
    Config cfg;
    derive(base, &cfg, kEmpty);
    const double straits = key_f64(cfg, "strait.share");
    gate5_off(&cfg, true);
    char share[40];
    snprintf(share, sizeof share, "%.17g", straits);
    cfg.set("strait.share", share);
    cfg.write(*text);
}

// The classing from tick `from`: a row a place. No rows, and true, in a
// world with no sources on a map (the lay of the power has said so).
bool class_places(Config& base, const Run& run, u64 from, u64 ticks, Array<ClassRow>* rows) {
    rows->resize(0);
    TextBuf text;
    classing_settings(base, &text);
    Classing c;
    c.text = text.view();
    c.from = from;
    c.ticks = ticks;
    // The run's respawns at or before the tick (a tick's respawns come
    // first among its inputs, so they are the classing's from its first).
    c.respawns = run.respawns.data();
    while (c.respawn_count < run.respawns.size() && run.respawns[c.respawn_count].tick <= from)
        ++c.respawn_count;
    u32 n = 0;
    {
        // What is the same in every one of its worlds: the places as built,
        // and their links from the nearest source alive at the tick.
        World w;
        TextBuf why;
        if (!class_world(c, &w, &why)) {
            if (why.size())
                fprintf(stderr, "nova2: classing tick %llu: %s (%s)\n", static_cast<ull>(from), why.c_str(),
                        run.respawns_file);
            return false;
        }
        if (!w.powered() || !w.map().placed())
            return true;
        n = w.place_count();
        rows->resize(n);
        Array<u32> hops;
        source_hops(w, &hops);
        for (u32 i = 0; i < n; ++i)
            row_as_built(w, i, hops, &(*rows)[i]);
    }
    c.rows = rows->data();
    ThreadPool pool;
    pool.start(run.threads);
    pool.run(n, class_one, &c);
    pool.stop();
    for (u32 i = 0; i < n; ++i)
        if ((*rows)[i].failed)
            return false;
    return true;
}

// The places that respawned after the classing at tick `from` and before
// the next (worlds.class_every on), each classed alone at its respawn's
// tick: a row each, in the run's order (Damian, 2026-11-22, "Yes, both
// leans"). A respawn at a classing's own tick needs none: that classing has
// it. So every incarnation a run had is classed at or before its first
// tick, and calls that split a seed's classings give the same rows.
bool class_respawns(Config& base, const Run& run, u64 from, u64 ticks, Array<ClassRow>* rows) {
    rows->resize(0);
    Array<u32> jobs;
    for (u32 k = 0; k < run.respawns.size(); ++k) {
        const u64 t = run.respawns[k].tick;
        if (t > from && t - from < run.class_every)
            jobs.push(k);
    }
    if (jobs.empty())
        return true;
    TextBuf text;
    classing_settings(base, &text);
    RespawnClassing c;
    c.text = text.view();
    c.ticks = ticks;
    c.respawns = run.respawns.data();
    c.respawn_count = static_cast<u32>(run.respawns.size());
    c.jobs = jobs.data();
    rows->resize(jobs.size());
    c.rows = rows->data();
    ThreadPool pool;
    pool.start(run.threads);
    pool.run(static_cast<u32>(jobs.size()), class_respawned, &c);
    pool.stop();
    for (u32 j = 0; j < jobs.size(); ++j) {
        if (!(*rows)[j].failed)
            continue;
        // Say why, from a world built again here.
        const Classing one = respawn_classing(c, j);
        World w;
        TextBuf why;
        class_world(one, &w, &why);
        fprintf(stderr, "nova2: the classing of place %u at its respawn's tick %llu: %s (%s)\n",
                run.respawns[jobs[j]].place, static_cast<ull>(one.from), why.size() ? why.c_str() : "no world",
                run.respawns_file);
        return false;
    }
    return true;
}

// A classing summed by links from the nearest source.
void print_classes(const Array<ClassRow>& rows, u64 from, u64 ticks) {
    const u32 n = static_cast<u32>(rows.size());
    char when[80] = "";
    if (from)
        snprintf(when, sizeof when, ", under the sources of tick %llu", static_cast<ull>(from));
    printf("each place in turn kept busy alone, nothing else living, %llu ticks each%s. By links from the nearest "
           "source (places; the busy place's mean mV; the shares of its cores' time cut out and off by a heat trip; "
           "places hot, dim and fair):\n",
           static_cast<ull>(ticks), when);
    u32 far = 0, kinds_all[kGrounds] = {}, both = 0;
    for (u32 i = 0; i < n; ++i) {
        far = rows[i].links != ~0u && rows[i].links > far ? rows[i].links : far;
        both += rows[i].both ? 1 : 0;
    }
    for (u32 h = 0; h <= far + 1; ++h) {
        const bool none = h == far + 1; // the places no source reaches
        u32 places = 0, kinds[kGrounds] = {};
        double v = 0, off = 0, hot = 0, core_time = 0;
        for (u32 i = 0; i < n; ++i) {
            const ClassRow& r = rows[i];
            if (none ? r.links != ~0u : r.links != h)
                continue;
            ++places;
            ++kinds[static_cast<u32>(r.ground)];
            v += static_cast<double>(r.mv) / ticks;
            off += static_cast<double>(r.cut);
            hot += static_cast<double>(r.heat);
            core_time += static_cast<double>(r.cores) * ticks;
        }
        if (!places)
            continue;
        for (u32 k = 0; k < kGrounds; ++k)
            kinds_all[k] += kinds[k];
        printf("  %s%2u: %3u places, %5.0f mV, %5.1f%% cut out, %5.1f%% tripped, %2u hot, %2u dim, %2u fair\n",
               none ? "no way " : "", none ? 0 : h, places, v / places, core_time ? 100 * off / core_time : 0.0,
               core_time ? 100 * hot / core_time : 0.0, kinds[static_cast<u32>(Ground::hot)],
               kinds[static_cast<u32>(Ground::dim)], kinds[static_cast<u32>(Ground::fair)]);
    }
    printf("  of %u places, busy alone: %u hot, %u dim, %u fair", n, kinds_all[static_cast<u32>(Ground::hot)],
           kinds_all[static_cast<u32>(Ground::dim)], kinds_all[static_cast<u32>(Ground::fair)]);
    if (both)
        printf(" (%u of them hot and dim at once)", both);
    printf("\n\n");
    fflush(stdout);
}

// Numbers' last part: the classing from worlds.from.
bool numbers_alone(Config& base, const Run& run) {
    const u64 ticks = run.ticks < 10000 ? run.ticks : 10000;
    Array<ClassRow> rows;
    if (!class_places(base, run, run.from, ticks, &rows))
        return false;
    if (!rows.empty())
        print_classes(rows, run.from, ticks);
    return true;
}

// <worlds.classes>/seed-N-classes.csv: a row a place for each classing,
// and one for each place classed alone at its respawn's tick, in tick order.
//   tick        the classing's: its worlds have the sources, straits and
//               respawns of this tick
//   place, strict, page, cores   the place as built
//   links       from the nearest source alive at that tick (-1: no way)
//   class       hot, dim or fair, by the ground measure's rule over the
//               ticks the place was kept full; both: hot and dim at once
//               (it takes the name of the larger loss)
//   ticks       the place was kept full for; its core time is cores x ticks
//   cut, heat   core-ticks its cores were cut out, and off by a heat trip
//   dark        ticks the memory holding its reset region was off
//   cut_share, heat_share   cut and heat over the core time: the two
//               values behind the class (hot: a heat share over 0.25; dim:
//               a cut share over 0.25, or dark over a quarter of the ticks)
//   mv          the place's mean voltage over the ticks
//   incarnation the place's respawns at or before the tick, applied from
//               worlds.respawns (0 with none given: the first map's place)
const char* const kClassHead = "tick,place,strict,page,cores,links,class,both,ticks,cut,heat,dark,cut_share,"
                               "heat_share,mv,incarnation\n";

bool class_folder(const char* path) {
    char buf[300];
    const usize n = strlen(path);
    if (n == 0 || n >= sizeof buf)
        return false;
    memcpy(buf, path, n + 1);
    for (usize i = 1; i <= n; ++i) {
        if (buf[i] != '/' && buf[i] != '\\' && buf[i] != 0)
            continue;
        if (buf[i - 1] == ':') // a drive, as in E:/
            continue;
        const char c = buf[i];
        buf[i] = 0;
        if (!CreateDirectoryA(buf, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        buf[i] = c;
    }
    return true;
}

// Begins the file for classings from tick `from` on: the rows an earlier
// call left from before that tick are kept, the rest go.
bool classes_begin(const char* path, u64 from) {
    Array<char> kept;
    if (FILE* f = fopen(path, "rb")) {
        char line[400];
        bool head = true;
        while (fgets(line, sizeof line, f)) {
            if (head) {
                head = false;
                if (strcmp(line, kClassHead) != 0) {
                    fclose(f);
                    fprintf(stderr, "nova2: %s is not a classes file of this build's columns\n", path);
                    return false;
                }
                continue;
            }
            const usize n = strlen(line);
            if (n == 0 || line[n - 1] != '\n')
                break; // a row cut short: it and what follows go
            if (strtoull(line, nullptr, 10) < from)
                kept.append(line, n);
        }
        fclose(f);
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return false;
    }
    fputs(kClassHead, f);
    if (!kept.empty())
        fwrite(kept.data(), 1, kept.size(), f);
    return fclose(f) == 0;
}

bool classes_append(const char* path, const Array<ClassRow>& rows) {
    FILE* f = fopen(path, "ab");
    if (!f) {
        fprintf(stderr, "nova2: can't write %s\n", path);
        return false;
    }
    for (usize i = 0; i < rows.size(); ++i) {
        const ClassRow& r = rows[i];
        static const char* const kNames[kGrounds] = {"fair", "hot", "dim"};
        const double core_time = static_cast<double>(r.cores) * static_cast<double>(r.ticks);
        fprintf(f, "%llu,%u,%u,%u,%u,%d,%s,%u,%llu,%llu,%llu,%llu,%.4f,%.4f,%.1f,%u\n", static_cast<ull>(r.tick),
                r.place, r.strict ? 1u : 0u, r.page, r.cores, r.links == ~0u ? -1 : static_cast<int>(r.links),
                kNames[static_cast<u32>(r.ground) < kGrounds ? static_cast<u32>(r.ground) : 0], r.both ? 1u : 0u,
                static_cast<ull>(r.ticks), static_cast<ull>(r.cut), static_cast<ull>(r.heat),
                static_cast<ull>(r.dark), core_time ? r.cut / core_time : 0.0, core_time ? r.heat / core_time : 0.0,
                r.ticks ? static_cast<double>(r.mv) / r.ticks : 0.0, r.incarnation);
    }
    return fclose(f) == 0;
}

// The run's respawns for the seed: <folder>/seed-N-respawns.csv, as the
// origin test writes it, in the order they came. False, said, if it can't
// be read or isn't one.
bool read_respawns(const char* folder, u64 seed, Run* run) {
    snprintf(run->respawns_file, sizeof run->respawns_file, "%s/seed-%llu-respawns.csv", folder,
             static_cast<ull>(seed));
    FILE* f = fopen(run->respawns_file, "rb");
    if (!f) {
        fprintf(stderr, "nova2: can't read %s\n", run->respawns_file);
        return false;
    }
    char line[400];
    bool head = true, bad = false;
    u64 last = 0;
    while (!bad && fgets(line, sizeof line, f)) {
        if (head) {
            head = false;
            bad = strncmp(line, "tick,place,incarnation,x,y,moved", 32) != 0;
            continue;
        }
        RespawnRow r;
        unsigned long long tick = 0;
        unsigned place = 0, inc = 0, moved = 0;
        bad = sscanf(line, "%llu,%u,%u,%lf,%lf,%u", &tick, &place, &inc, &r.x, &r.y, &moved) != 6 || tick < last ||
              inc == 0 || moved > 1;
        r.tick = last = tick;
        r.place = place;
        r.incarnation = inc;
        r.moved = moved != 0;
        run->respawns.push(r);
    }
    fclose(f);
    if (bad || head) {
        fprintf(stderr, "nova2: %s isn't a run's record of respawns\n", run->respawns_file);
        return false;
    }
    return true;
}

// worlds.test = classes: the classings from worlds.from, every
// worlds.class_every ticks, none after worlds.class_to.
int classes(Config& base, const Run& run) {
    const u64 ticks = run.ticks < 10000 ? run.ticks : 10000;
    char path[400] = "";
    if (run.classes[0]) {
        if (!class_folder(run.classes)) {
            fprintf(stderr, "nova2: can't make the folder %s\n", run.classes);
            return 2;
        }
        snprintf(path, sizeof path, "%s/seed-%llu-classes.csv", run.classes,
                 static_cast<ull>(key_u64(base, "world.seed")));
        if (!classes_begin(path, run.from))
            return 2;
    }
    if (run.respawns_file[0]) {
        printf("the run's respawns: %u in %s, each applied in the classings at or after its tick, at its point; "
               "a place that respawns after a classing is classed alone at its respawn's tick",
               static_cast<u32>(run.respawns.size()), run.respawns_file);
        if (!run.respawns.empty())
            printf(" (the last at tick %llu)", static_cast<ull>(run.respawns[run.respawns.size() - 1].tick));
        printf("\n\n");
    }
    u32 done = 0, alone = 0;
    const double t0 = clock_seconds();
    for (u64 at = run.from;; at += run.class_every) {
        Array<ClassRow> rows;
        if (!class_places(base, run, at, ticks, &rows))
            return 2;
        if (rows.empty()) {
            printf("no sources on a map in this world: nothing to class\n");
            return 0;
        }
        print_classes(rows, at, ticks);
        if (path[0] && !classes_append(path, rows))
            return 2;
        // Then the places that respawn before the next classing, each alone
        // at its respawn's tick: their rows come next, in tick order.
        if (!class_respawns(base, run, at, ticks, &rows))
            return 2;
        if (!rows.empty()) {
            u32 kinds[kGrounds] = {};
            for (usize k = 0; k < rows.size(); ++k)
                ++kinds[static_cast<u32>(rows[k].ground) < kGrounds ? static_cast<u32>(rows[k].ground) : 0];
            printf("and %u place%s that respawned before the next classing, each classed alone at its respawn's "
                   "tick (%llu to %llu): %u hot, %u dim, %u fair\n\n",
                   static_cast<u32>(rows.size()), rows.size() == 1 ? "" : "s", static_cast<ull>(rows[0].tick),
                   static_cast<ull>(rows[rows.size() - 1].tick), kinds[static_cast<u32>(Ground::hot)],
                   kinds[static_cast<u32>(Ground::dim)], kinds[static_cast<u32>(Ground::fair)]);
            alone += static_cast<u32>(rows.size());
            if (path[0] && !classes_append(path, rows))
                return 2;
        }
        fflush(stdout);
        ++done;
        if (at >= run.class_to || run.class_to - at < run.class_every)
            break;
    }
    printf("%u classing%s", done, done == 1 ? "" : "s");
    if (alone)
        printf(" and %u place%s classed alone at %s respawn's tick", alone, alone == 1 ? "" : "s",
               alone == 1 ? "its" : "their");
    printf(" in %.0f seconds", clock_seconds() - t0);
    if (path[0])
        printf("; rows in %s", path);
    printf("\n");
    return 0;
}

// Section 7's numbers against what the plan says each should do
// (build-plan-gate4.md 7 and step 11): the cores' draw and heat by the
// engine's own laws, the config's places at a steady 1,000 mV idle and
// full, and the lay of the power in the world as it stands: empty, wholly
// busy, and with each place busy alone.
int numbers(Config& base, const Run& run) {
    HeatSettings hs;
    read_heat_settings(base, &hs);
    {
        World probe;
        Config cfg;
        derive(base, &cfg, nullptr);
        if (!build(probe, cfg))
            return 2;
        numbers_cores(probe.place(0).power(), hs);
    }
    if (!numbers_places(base, run, hs))
        return 2;
    return numbers_lay(base, run, false) && numbers_lay(base, run, true) && numbers_alone(base, run) ? 0 : 2;
}

} // namespace

int cmd_power_worlds(Config& cfg, Str test) {
    Run run;
    run.ticks = key_u64(cfg, "worlds.ticks");
    run.every = key_u64(cfg, "worlds.report_every");
    run.threads = key_u32(cfg, "run.threads");
    run.from = key_u64(cfg, "worlds.from");
    run.class_every = key_u64(cfg, "worlds.class_every");
    run.class_to = key_u64(cfg, "worlds.class_to");
    const Str classes_to = key_str(cfg, "worlds.classes");
    snprintf(run.classes, sizeof run.classes, "%.*s", static_cast<int>(classes_to.n), classes_to.p);
    const Str respawns_from = key_str(cfg, "worlds.respawns");
    if (respawns_from.n) {
        char folder[300];
        snprintf(folder, sizeof folder, "%.*s", static_cast<int>(respawns_from.n), respawns_from.p);
        if (!read_respawns(folder, key_u64(cfg, "world.seed"), &run))
            return 2;
    }
    key_accept_known(cfg);
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    printf("nova2 worlds: %.*s (gate 4), seed %llu, %u places, %llu ticks, %u host threads\n\n",
           static_cast<int>(test.n), test.p, static_cast<ull>(key_u64(cfg, "world.seed")),
           key_u32(cfg, "world.places"), static_cast<ull>(run.ticks),
           run.threads ? run.threads : ThreadPool::hardware_threads());
    fflush(stdout);
    if (test.eq("band"))
        return band(cfg, run);
    if (test.eq("heat"))
        return heat(cfg, run);
    if (test.eq("water"))
        return water(cfg, run);
    if (test.eq("sleeper"))
        return sleeper(cfg, run);
    if (test.eq("numbers"))
        return numbers(cfg, run);
    if (test.eq("classes"))
        return classes(cfg, run);
    return rejection(cfg, run);
}

} // namespace nova2
