// nova2 bench: the first benchmark (design.md 2; build-plan-gate1.md step 11).
//
// Three workloads, each warmed up before it's timed:
//   loops:       on every core of every place, a thread counting in a loop
//                (ADD # 1; JMP # -2): the interpreter at its plainest;
//   replicators: a replicator in each lax place's reset region (a test
//                world, R7), so the places fill with threads copying
//                themselves. Most of their time goes on SPAWN's cost, so few
//                instructions run: ticks and births per second say more;
//   static:      memory full of static, as an origin run starts, with idle
//                runs retrying every tick, so the cores keep trying paths.
// For each: one place on one host thread; the world's places on 1 to 24
// host threads; and the per-access counters (9.8, 4.4) off and on. Reported:
// instructions, ticks (simulated time) and births per second (design.md 2).
//
// Then gate 2's part (build-plan-gate2.md step 7): the static world's ticks
// timed part by part on one host thread, on gate 1's steady supply, on
// section 7's and on a hard one, so the supply, the devices' states, fading
// and the draw can be costed against the rest of the tick.
//
// Then gate 3's part (build-plan-gate3.md step 5): a traffic world
// (traffic_world.h) on its map, under the hard supply, whose nights and
// blackouts take network devices down, at 1,000 places on one host thread
// and on many, and at 10,000 on many: the barrier's network step and the
// routes worked out, against the tick. Then the routes alone on each map
// (11.3): a whole table at 1,000 places; at 10,000, a sample of the columns
// found on demand, and what a whole table would take; each with every place
// lit and again after a nightfall.
//
// Then gate 4's part (build-plan-gate4.md step 8): the traffic world on its
// map with the voltage solved from sources and heat on, at 48, 1,000 and
// 10,000 places, as the settings are and again with sources that come and go
// fast: the solve and the heat between places against the tick, the sweeps
// a solve and the solves that stopped at the cap; and the routes there
// (gate 3 left their cost at 10,000 places open), whose topology now
// changes with the sources.
//
// Then gate 5's part (build-plan-gate5.md step 12): the same traffic world
// under the sources with heat, at 48, 1,000 and 10,000 places: gate 5's
// strengths as the settings are against all off, for what wear, the weak
// cells and rowhammer add to a place-tick and the tick's inputs and the
// links' wear to a tick; a respawn's relinking, a place at a time, and the
// routes worked out again after it; and rerouting, with links cut and
// mended often enough that about one in twenty is down at a time; and
// strikes, and how often a strike's tick stops the solve at its cap.
//
// The parts before gate 4's run with a supply at each place and no heat, as
// they were measured, and every part before gate 5's with its strengths
// off: bench.part picks one part.
#include <cstdio>
#include <cstring>
#include <intrin.h> // __rdtsc: the parts' timings are in its cycles

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

namespace nova2 {

namespace {

enum class Load { loops, replicators, statics, traffic };

// A.5's 14-byte replicator on a lax place (tests/test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
const u8 kLoop[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};

struct Totals {
    u64 instructions = 0, births = 0;
};

Totals totals(World& w) {
    Totals t;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        t.instructions += p.sched.instructions + p.region.instructions;
        t.births += p.births;
    }
    return t;
}

// Gate 2's hard supply: a short day with deep nights, fast swings, and
// drop-outs as often as the settings allow, half of them blackouts.
const char* const kHard[] = {"supply.sag",          "50",   "supply.day",         "400",
                             "supply.night",        "0.1",  "supply.swing",       "0.3",
                             "supply.swing_period", "150",  "supply.dropouts",    "1000",
                             "supply.dropout.min",  "2",    "supply.dropout.max", "300",
                             "supply.blackout_share", "0.5", nullptr};

// sets, more: more settings, key then value, each list ending with a null
// key.
// gate5: gate 5's strengths as the settings are, not off.
bool build(Config& base, World& w, u32 places, Load load, bool counters, const char* const* sets = nullptr,
           const char* const* more = nullptr, bool gate5 = false) {
    Config cfg;
    TextBuf text;
    base.write(text);
    Config::ParseError pe;
    cfg.parse(text.view(), &pe);
    char v[32];
    snprintf(v, sizeof v, "%u", places);
    cfg.set("world.places", v);
    // Gates 1 to 3's parts are measured on their own world; gate 4's part
    // asks for the sources and heat in its sets.
    if (!cfg.has("supply.model"))
        cfg.set("supply.model", "place");
    if (!cfg.has("heat.on"))
        cfg.set("heat.on", "false");
    // Gate 5's strengths are off in the parts measured before it, unless
    // asked for by name; its own part has them as the settings are.
    if (!gate5)
        gate5_off(&cfg, false);
    if (load == Load::statics)
        cfg.set("region.retry", "1");
    for (u32 i = 0; sets && sets[i]; i += 2)
        cfg.set(sets[i], sets[i + 1]);
    for (u32 i = 0; more && more[i]; i += 2)
        cfg.set(more[i], more[i + 1]);
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return false;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        p.mem.enable_counters(counters);
        if (load == Load::replicators && !p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        if (load == Load::traffic)
            place_traffic(p, w.place_count(), i);
        if (load == Load::loops)
            for (u32 c = 0; c < p.core_count(); ++c) { // a test world (R7): placed by hand
                const u32 at = p.mem.alloc(1, 64, p.mem.strict() ? 1 : 2, 0);
                if (!at)
                    break;
                memcpy(p.mem.bytes() + at, kLoop, sizeof kLoop);
                p.os.place_by_hand(1, at, at, 0);
            }
    }
    return true;
}

struct Rate {
    double instr = 0, ticks = 0, births = 0;
};

Rate measure(Config& base, u32 places, u32 threads, Load load, bool counters, u32 warm, u32 ticks) {
    Rate r;
    World w;
    if (!build(base, w, places, load, counters))
        return r;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    const Totals before = totals(w);
    const double t0 = clock_seconds();
    for (u32 t = 0; t < ticks; ++t)
        w.tick();
    const double secs = clock_seconds() - t0;
    const Totals after = totals(w);
    if (secs > 0) {
        r.instr = static_cast<double>(after.instructions - before.instructions) / secs;
        r.ticks = ticks / secs;
        r.births = static_cast<double>(after.births - before.births) / secs;
    }
    return r;
}

void line(const char* what, const Rate& r, double base_instr) {
    printf("  %-30s %9.1f M instr/s %10.0f ticks/s %10.0f births/s", what, r.instr / 1e6, r.ticks, r.births);
    if (base_instr > 0)
        printf("  x%.1f", r.instr / base_instr);
    printf("\n");
}

void workload(Config& base, const char* name, Load load, u32 places, u32 warm, u32 ticks) {
    printf("%s:\n", name);
    line("one place, one host thread", measure(base, 1, 1, load, false, warm, ticks * 4), 0);
    const u32 hw = ThreadPool::hardware_threads();
    const u32 counts[] = {1, 2, 4, 8, 16, 24};
    double first = 0;
    char what[64];
    for (u32 i = 0; i < sizeof counts / sizeof counts[0]; ++i) {
        if (counts[i] > hw)
            break;
        const Rate r = measure(base, places, counts[i], load, false, warm, ticks);
        if (i == 0)
            first = r.instr;
        snprintf(what, sizeof what, "%u places, %u host threads", places, counts[i]);
        line(what, r, first);
    }
    const Rate off = measure(base, places, 1, load, false, warm, ticks);
    const Rate on = measure(base, places, 1, load, true, warm, ticks);
    printf("  per-access counters, one host thread: off %.1f, on %.1f M instr/s: %+.1f%%\n\n", off.instr / 1e6,
           on.instr / 1e6, off.instr > 0 ? 100.0 * (on.instr - off.instr) / off.instr : 0);
}

// Gate 2's parts of a tick, on one supply: the static world on one host
// thread, warmed up, then `ticks` ticks timed part by part. Each part's mean
// per place-tick, in nanoseconds (the time-stamp counter's rate is measured
// over the same ticks).
void parts(Config& base, const char* name, const char* const* sets, u32 places, u32 warm, u32 ticks) {
    World w;
    if (!build(base, w, places, Load::statics, false, sets))
        return;
    w.start_threads(1);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    for (u32 i = 0; i < w.place_count(); ++i) {
        w.place(i).timing = true;
        for (u32 k = 0; k < Place::kPhases; ++k)
            w.place(i).phase_cycles[k] = 0;
    }
    const Totals before = totals(w);
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < ticks; ++t)
        w.tick();
    const u64 c1 = __rdtsc();
    const double secs = clock_seconds() - t0;
    const Totals after = totals(w);
    if (secs <= 0 || c1 <= c0)
        return;
    double cyc[Place::kPhases] = {};
    u64 cuts = 0, faded = 0, colds = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 k = 0; k < Place::kPhases; ++k)
            cyc[k] += static_cast<double>(p.phase_cycles[k]);
        cuts += p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts;
        faded += p.noise.cells_faded;
        colds += p.cold_starts;
    }
    const double ns = 1e9 * secs / static_cast<double>(c1 - c0) / (static_cast<double>(places) * ticks);
    double total = 0, own = 0;
    for (u32 k = 0; k < Place::kPhases; ++k) {
        cyc[k] *= ns;
        total += cyc[k];
    }
    own = cyc[Place::kPhaseSupply] + cyc[Place::kPhaseDevices] + cyc[Place::kPhaseFade] + cyc[Place::kPhaseDraw];
    printf("  %-22s %8.0f ticks/s %7.1f M instr/s; a place-tick %.2f us: supply %.0f ns, devices %.0f, "
           "execution %.0f, barrier %.0f, fade %.0f, cosmic %.0f, draw %.0f; gate 2's parts %.0f ns (%.2f%%); "
           "%llu cut-outs, %llu cells faded, %llu cold starts\n",
           name, ticks / secs, static_cast<double>(after.instructions - before.instructions) / secs / 1e6,
           total / 1000, cyc[Place::kPhaseSupply], cyc[Place::kPhaseDevices], cyc[Place::kPhaseRun],
           cyc[Place::kPhaseBarrier], cyc[Place::kPhaseFade], cyc[Place::kPhaseCosmic], cyc[Place::kPhaseDraw], own,
           total > 0 ? 100 * own / total : 0, static_cast<unsigned long long>(cuts),
           static_cast<unsigned long long>(faded), static_cast<unsigned long long>(colds));
}

void gate2_parts(Config& base, u32 places) {
    printf("gate 2's parts of a tick (static, idle runs retrying every tick; %u places, one host thread):\n", places);
    static const char* const kSteady[] = {nullptr};
    static const char* const kSection7[] = {"supply.sag",   "50",   "supply.night",    "0.2", "supply.swing", "0.25",
                                            "supply.dropouts", "1", nullptr};
    parts(base, "gate 1's steady supply", kSteady, places, 10, 400);
    parts(base, "section 7's supply", kSection7, places, 10, 400);
    // Long enough to meet drop-outs, which come about one a place every
    // 1,000 ticks, and short ones at that.
    parts(base, "a hard supply", kHard, places, 10, 2000);
    printf("\n");
}

// The routes alone on a map (11.3): `want` columns worked out, spread over
// the destinations (all of them is a whole table), with every place lit;
// then again after a nightfall takes down the network devices of the places
// in the map's western tenth.
void routes_alone(const Map& m, u32 want) {
    const u32 n = m.place_count();
    Routes r;
    r.init(&m);
    Array<u8> dark;
    dark.resize(n);
    double secs[2] = {};
    u64 cols[2] = {};
    u32 night = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            for (u32 i = 0; i < n; ++i) {
                dark[i] = m.x(i) < m.width() * 0.1 ? 1 : 0;
                night += dark[i];
            }
            r.set_dark(dark.data());
        }
        const u64 before = r.columns;
        const double t0 = clock_seconds();
        for (u32 k = 0; k < want; ++k) {
            const u32 d = static_cast<u32>(static_cast<u64>(k) * n / want);
            u32 s = (d + 1) % n;
            while (dark[s] && s != d)
                s = (s + 1) % n;
            r.next(s, d); // works out d's column, unless d is dark
        }
        secs[pass] = clock_seconds() - t0;
        cols[pass] = r.columns - before;
    }
    const double each = cols[0] ? 1e6 * secs[0] / static_cast<double>(cols[0]) : 0;
    const double after = cols[1] ? 1e6 * secs[1] / static_cast<double>(cols[1]) : 0;
    const double column_kb = n * sizeof(u16) / 1024.0;
    if (want >= n)
        printf("  routes alone, %u places (%u links): a whole table, %llu columns (%.1f MB), in %.1f ms, %.0f us a "
               "column; with the %u places in the map's western tenth dark (a nightfall's edge), %llu columns in "
               "%.1f ms, %.0f us a column\n",
               n, m.link_count(), static_cast<unsigned long long>(cols[0]), column_kb * cols[0] / 1024,
               1e3 * secs[0], each, night, static_cast<unsigned long long>(cols[1]), 1e3 * secs[1], after);
    else
        printf("  routes alone, %u places (%u links): %llu columns found on demand in %.1f ms, %.0f us a column "
               "(%.1f KB each); a whole table would take %.2f s and %.0f MB; with the %u places in the map's western "
               "tenth dark, %.0f us a column\n",
               n, m.link_count(), static_cast<unsigned long long>(cols[0]), 1e3 * secs[0], each, column_kb,
               each * n / 1e6, column_kb * n / 1024, night, after);
    fflush(stdout);
}

// Gate 3's part at one size: the traffic world on `threads` host threads,
// warmed up, then `ticks` ticks timed; with `want`, then the routes alone on
// its map.
void traffic(Config& base, u32 places, u32 threads, const char* const* sets, u32 warm, u32 ticks, u32 want) {
    World w;
    if (!build(base, w, places, Load::traffic, false, kHard, sets))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    Routes& r = w.net().routes();
    const NetCounts n0 = w.net().counts();
    const u64 net0 = w.net_cycles, route0 = r.cycles, cols0 = r.columns, changes0 = r.changes;
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < ticks; ++t)
        w.tick();
    const u64 c1 = __rdtsc();
    const double secs = clock_seconds() - t0;
    if (secs <= 0 || c1 <= c0)
        return;
    const NetCounts& n1 = w.net().counts();
    const double ms = 1e3 * secs / static_cast<double>(c1 - c0); // a cycle, in ms
    const double tick = 1e3 * secs / ticks;
    const double net = ms * static_cast<double>(w.net_cycles - net0) / ticks;
    const double route = ms * static_cast<double>(r.cycles - route0) / ticks;
    const u64 cols = r.columns - cols0, changes = r.changes - changes0;
    const double per = 1.0 / ticks;
    // The routes are worked out across the host threads, so their time is
    // the threads' summed, not a share of the tick.
    printf("  %u places, %u host thread%s: %.0f ticks/s, a tick %.2f ms; the network step %.3f ms (%.1f%%); routes "
           "%.3f ms of the threads' time: %llu topology changes, %llu columns worked out (%.0f us each); a tick's "
           "messages: %.0f sent, %.0f delivered, %.0f PINGs answered; dropped %.0f full, %.0f stranded, %.0f lost, "
           "%.0f cut; %u in flight at the end; %.1f MB of columns kept\n",
           places, threads, threads == 1 ? "" : "s", ticks / secs, tick, net, 100 * net / tick, route,
           static_cast<unsigned long long>(changes), static_cast<unsigned long long>(cols),
           cols ? 1e3 * route * ticks / static_cast<double>(cols) : 0, per * static_cast<double>(n1.sent - n0.sent),
           per * static_cast<double>(n1.delivered - n0.delivered), per * static_cast<double>(n1.pings - n0.pings),
           per * static_cast<double>(n1.full - n0.full), per * static_cast<double>(n1.stranded - n0.stranded),
           per * static_cast<double>(n1.lost - n0.lost), per * static_cast<double>(n1.cut - n0.cut),
           w.net().in_flight(), r.kept_bytes() / (1024.0 * 1024.0));
    fflush(stdout);
    if (want)
        routes_alone(w.map(), want);
}

void gate3_parts(Config& base) {
    printf("gate 3's parts (a traffic world on its map, under the hard supply):\n");
    const u32 hw = ThreadPool::hardware_threads();
    const u32 many = hw < 24 ? hw : 24;
    static const char* const k1000[] = {"memory.size", "32768", "disk.size", "65536", nullptr};
    static const char* const k10000[] = {"memory.size", "16384", "disk.size", "16384", nullptr};
    traffic(base, 1000, 1, k1000, 20, 400, 0);
    traffic(base, 1000, many, k1000, 20, 400, 1000);
    traffic(base, 10000, many, k10000, 10, 400, 500);
    printf("\n");
}

// Gate 4's sources as section 7 has them, and coming and going fast: lives
// of a few thousand ticks, with drop-outs and swells as often as the
// settings allow.
const char* const kPowered[] = {"supply.model", "sources", "heat.on", "true", nullptr};
const char* const kRestless[] = {"supply.model",           "sources", "heat.on",                  "true",
                                 "sources.life",           "4000",    "source.steady.dropouts",   "1000",
                                 "source.daylight.dropouts", "1000",  "source.gusty.dropouts",    "1000",
                                 "source.steady.swells",   "1000",    "source.daylight.swells",   "1000",
                                 "source.gusty.swells",    "1000",    "supply.dropout.max",       "300",
                                 "sources.swell.longest",  "300",     "supply.day",               "400",
                                 nullptr};

// Gate 4's part at one size: the traffic world on `threads` host threads
// under `power`, warmed up, then `ticks` ticks timed.
void powered(Config& base, const char* name, u32 places, u32 threads, const char* const* power,
             const char* const* sets, u32 warm, u32 ticks) {
    World w;
    if (!build(base, w, places, Load::traffic, false, power, sets))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    Routes& r = w.net().routes();
    const Grid& g = w.grid();
    const u64 solve0 = w.solve_cycles, heat0 = w.heat_cycles, net0 = w.net_cycles;
    const u64 solves0 = g.solves, sweeps0 = g.sweeps, capped0 = g.capped;
    const u64 route0 = r.cycles, cols0 = r.columns, changes0 = r.changes;
    const Totals before = totals(w);
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < ticks; ++t)
        w.tick();
    const u64 c1 = __rdtsc();
    const double secs = clock_seconds() - t0;
    if (secs <= 0 || c1 <= c0)
        return;
    const Totals after = totals(w);
    const double ms = 1e3 * secs / static_cast<double>(c1 - c0); // a cycle, in ms
    const double tick = 1e3 * secs / ticks;
    const double solve = ms * static_cast<double>(w.solve_cycles - solve0) / ticks;
    const double heat = ms * static_cast<double>(w.heat_cycles - heat0) / ticks;
    const double net = ms * static_cast<double>(w.net_cycles - net0) / ticks;
    const double route = ms * static_cast<double>(r.cycles - route0) / ticks;
    const u64 solves = g.solves - solves0, cols = r.columns - cols0;
    u32 dark = 0, lit = 0, tripped = 0;
    double temp = 0, hottest = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        dark += p.env.voltage_mv == 0 ? 1 : 0;
        lit += p.network_state.on ? 1 : 0;
        const double t = static_cast<double>(p.temp) / kDegree;
        temp += t;
        hottest = t > hottest ? t : hottest;
        for (u32 c = 0; c < p.core_count(); ++c)
            tripped += p.core_tripped[c] ? 1 : 0;
    }
    printf("  %s, %u places, %u host thread%s: %.0f ticks/s, %.1f M instr/s, a tick %.3f ms; the solve %.3f ms "
           "(%.1f%%), %.1f sweeps a solve, %.2f%% of solves stopped at the cap; heat between places %.3f ms "
           "(%.1f%%); the network step %.3f ms (%.1f%%); routes %.3f ms of the threads' time, %llu topology "
           "changes and %llu columns worked out in %u ticks, %.1f MB of columns kept; at the end %u sources, %u "
           "places at 0 V, %u network devices on, %u cores tripped, a place at %.1f degrees on average and the "
           "hottest at %.1f\n",
           name, places, threads, threads == 1 ? "" : "s", ticks / secs,
           static_cast<double>(after.instructions - before.instructions) / secs / 1e6, tick, solve,
           100 * solve / tick, solves ? static_cast<double>(g.sweeps - sweeps0) / solves : 0.0,
           solves ? 100.0 * static_cast<double>(g.capped - capped0) / solves : 0.0, heat, 100 * heat / tick, net,
           100 * net / tick, route, static_cast<unsigned long long>(r.changes - changes0),
           static_cast<unsigned long long>(cols), ticks, r.kept_bytes() / (1024.0 * 1024.0), w.sources().count(),
           dark, lit, tripped, w.place_count() ? temp / w.place_count() : 0.0, hottest);
    fflush(stdout);
}

void gate4_parts(Config& base, u32 most) {
    printf("gate 4's parts (a traffic world on its map, the voltage solved from sources, heat on):\n");
    const u32 hw = ThreadPool::hardware_threads();
    u32 many = hw < 24 ? hw : 24;
    if (most && most < many)
        many = most;
    static const char* const k48[] = {"memory.size", "65536", "disk.size", "65536", nullptr};
    static const char* const k1000[] = {"memory.size", "32768", "disk.size", "65536", nullptr};
    static const char* const k10000[] = {"memory.size", "16384", "disk.size", "16384", nullptr};
    powered(base, "as the settings are", 48, 1, kPowered, k48, 50, 400);
    powered(base, "as the settings are", 48, many, kPowered, k48, 50, 400);
    powered(base, "restless sources", 48, many, kRestless, k48, 50, 400);
    powered(base, "as the settings are", 1000, many, kPowered, k1000, 50, 400);
    powered(base, "restless sources", 1000, many, kRestless, k1000, 50, 400);
    powered(base, "as the settings are", 10000, many, kPowered, k10000, 20, 200);
    powered(base, "restless sources", 10000, many, kRestless, k10000, 20, 200);
    printf("\n");
}

// Gate 5's counters summed over the places.
struct Worn {
    u64 failures = 0, hammered = 0, flips = 0, weak = 0;
};

Worn worn(World& w) {
    Worn s;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k)
            s.failures += p.failures[k];
        s.hammered += p.noise.hammered;
        s.flips += p.noise.hammer;
        s.weak += p.noise.weak;
    }
    return s;
}

// Gate 5's part at one size, first (build-plan-gate5.md step 12): the
// traffic world under gate 4's sources and heat, with gate 5's strengths as
// the settings are (`on`) or all off, warmed up, then `ticks` ticks timed:
// what wear, the weak cells and rowhammer add to a place-tick, part by part
// as gate 2's, and what the tick's inputs and the links' wear add to a tick;
// and over all the ticks timed, the flips and failures.
// The per-access counters are on as a run has them: rowhammer counts its
// row activations there.
void wearing(Config& base, u32 places, u32 threads, bool on, const char* const* sets, u32 warm, u32 ticks) {
    World w;
    if (!build(base, w, places, Load::traffic, true, kPowered, sets, on))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    for (u32 i = 0; i < w.place_count(); ++i)
        w.place(i).timing = true;
    // The ticks timed in five slices, and the fastest kept: the host's other
    // work lands in some slices and not others.
    const u32 slices = 5, each = ticks / slices > 0 ? ticks / slices : 1;
    const Worn worn0 = worn(w);
    double best = 0, secs = 0, cyc[Place::kPhases] = {};
    u64 cycles = 0, instr = 0, input_c = 0, lwear_c = 0, solve_c = 0, net_c = 0;
    for (u32 s = 0; s < slices; ++s) {
        for (u32 i = 0; i < w.place_count(); ++i)
            for (u32 k = 0; k < Place::kPhases; ++k)
                w.place(i).phase_cycles[k] = 0;
        const u64 input0 = w.input_cycles, lwear0 = w.link_wear_cycles, solve0 = w.solve_cycles;
        const u64 net0 = w.net_cycles;
        const Totals before = totals(w);
        const double t0 = clock_seconds();
        const u64 c0 = __rdtsc();
        for (u32 t = 0; t < each; ++t)
            w.tick();
        const u64 c1 = __rdtsc();
        const double took = clock_seconds() - t0;
        if (took <= 0 || c1 <= c0 || (best > 0 && took >= best))
            continue;
        best = secs = took;
        cycles = c1 - c0;
        instr = totals(w).instructions - before.instructions;
        input_c = w.input_cycles - input0;
        lwear_c = w.link_wear_cycles - lwear0;
        solve_c = w.solve_cycles - solve0;
        net_c = w.net_cycles - net0;
        for (u32 k = 0; k < Place::kPhases; ++k) {
            cyc[k] = 0;
            for (u32 i = 0; i < w.place_count(); ++i)
                cyc[k] += static_cast<double>(w.place(i).phase_cycles[k]);
        }
    }
    if (secs <= 0)
        return;
    const Worn worn1 = worn(w);
    const double ns = 1e9 * secs / static_cast<double>(cycles) / (static_cast<double>(places) * each);
    double total = 0;
    for (u32 k = 0; k < Place::kPhases; ++k) {
        cyc[k] *= ns;
        total += cyc[k];
    }
    const double own = cyc[Place::kPhaseWear] + cyc[Place::kPhaseWeak] + cyc[Place::kPhaseHammer];
    const double ms = 1e3 * secs / static_cast<double>(cycles); // a cycle, in ms
    const double tick = 1e3 * secs / each;
    const double input = ms * static_cast<double>(input_c) / each;
    const double lwear = ms * static_cast<double>(lwear_c) / each;
    const double solve = ms * static_cast<double>(solve_c) / each;
    const double net = ms * static_cast<double>(net_c) / each;
    printf("  %s, %u places, %u host thread%s: %.0f ticks/s, %.1f M instr/s, a tick %.3f ms; a place-tick's thread "
           "time %.2f us: execution %.0f ns, wear %.0f, weak cells %.0f, rowhammer %.0f (gate 5's parts %.0f ns, "
           "%.2f%%); of a tick, the inputs %.3f ms (%.1f%%), the links' wear %.3f ms (%.1f%%), the solve %.3f ms, "
           "the network step %.3f ms; in the ticks timed %llu activations past a threshold, %llu bits they "
           "flipped, %llu bits weak cells lost, %llu devices failed\n",
           on ? "as the settings are" : "gate 5 off", places, threads, threads == 1 ? "" : "s", each / secs,
           static_cast<double>(instr) / secs / 1e6, tick, total / 1000,
           cyc[Place::kPhaseRun], cyc[Place::kPhaseWear], cyc[Place::kPhaseWeak], cyc[Place::kPhaseHammer], own,
           total > 0 ? 100 * own / total : 0, input, 100 * input / tick, lwear, 100 * lwear / tick, solve, net,
           static_cast<unsigned long long>(worn1.hammered - worn0.hammered),
           static_cast<unsigned long long>(worn1.flips - worn0.flips),
           static_cast<unsigned long long>(worn1.weak - worn0.weak),
           static_cast<unsigned long long>(worn1.failures - worn0.failures));
    fflush(stdout);
}

// A respawn's relinking (7.8): the same world, warmed up, then `count`
// places spread over the map torn down and built again by hand between
// ticks, each timed, with 5 ticks between; and the tick after each, and the
// 5 after that, against 20 ticks before the first: the routes worked out
// again round the place's new links.
void respawning(Config& base, u32 places, u32 threads, const char* const* sets, u32 warm, u32 count) {
    World w;
    if (!build(base, w, places, Load::traffic, true, kPowered, sets, true))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    Routes& r = w.net().routes();
    const u32 quiet = 20, settle = 5;
    const u64 route0 = r.cycles, cols0 = r.columns;
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < quiet; ++t)
        w.tick();
    const u64 c1 = __rdtsc();
    const double secs = clock_seconds() - t0;
    if (secs <= 0 || c1 <= c0 || count == 0)
        return;
    const double ms = 1e3 * secs / static_cast<double>(c1 - c0); // a cycle, in ms
    const double tick = 1e3 * secs / quiet;
    const double route = ms * static_cast<double>(r.cycles - route0) / quiet;
    const double cols = static_cast<double>(r.columns - cols0) / quiet;
    double own = 0, worst = 0, next = 0, next_route = 0, next_cols = 0, later = 0, later_route = 0, later_cols = 0;
    const u64 moved0 = w.respawns_moved;
    for (u32 j = 0; j < count; ++j) {
        const u32 i = static_cast<u32>((2ull * j + 1) * places / (2ull * count));
        const u64 a = __rdtsc();
        w.respawn(i);
        const u64 b = __rdtsc();
        const u64 rc = r.cycles, rcols = r.columns;
        w.tick();
        const u64 c = __rdtsc();
        const u64 rc2 = r.cycles, rcols2 = r.columns;
        for (u32 t = 0; t < settle; ++t)
            w.tick();
        const u64 d = __rdtsc();
        const double one = ms * static_cast<double>(b - a);
        own += one;
        worst = one > worst ? one : worst;
        next += ms * static_cast<double>(c - b);
        next_route += ms * static_cast<double>(rc2 - rc);
        next_cols += static_cast<double>(rcols2 - rcols);
        later += ms * static_cast<double>(d - c) / settle;
        later_route += ms * static_cast<double>(r.cycles - rc2) / settle;
        later_cols += static_cast<double>(r.columns - rcols2) / settle;
    }
    const double n = count;
    printf("  respawns, %u places, %u host thread%s: %u places torn down and built again, %llu at a new point; a "
           "respawn %.3f ms (the longest %.3f); the tick after %.3f ms, routes %.3f ms of the threads' time and %.0f "
           "columns worked out; the %u ticks after that %.3f ms a tick, routes %.3f ms and %.1f columns; before the "
           "first, a tick %.3f ms, routes %.3f ms and %.1f columns\n",
           places, threads, threads == 1 ? "" : "s", count,
           static_cast<unsigned long long>(w.respawns_moved - moved0), own / n, worst, next / n, next_route / n,
           next_cols / n, settle, later / n, later_route / n, later_cols / n, tick, route, cols);
    fflush(stdout);
}

// Rerouting (11.6): the same world with links cut at a thousandth a tick
// each, of the average length, and mended at visits about every 100 ticks
// a place, so that about one link in twenty is down at a time and messages
// whose way on is gone wait for a route (net.lifetime as the settings are).
// The routes and the network step against the tick, with the topology's
// changes, the cuts and mends and the waits.
// Two lists of settings as one, into out (room for `cap`, the null key
// too).
void join(const char* const* a, const char* const* b, const char** out, u32 cap) {
    u32 k = 0;
    for (u32 i = 0; a && a[i] && k + 3 <= cap; i += 2) {
        out[k++] = a[i];
        out[k++] = a[i + 1];
    }
    for (u32 i = 0; b && b[i] && k + 3 <= cap; i += 2) {
        out[k++] = b[i];
        out[k++] = b[i + 1];
    }
    out[k] = nullptr;
}

void rerouting(Config& base, u32 places, u32 threads, const char* const* sets, u32 warm, u32 ticks) {
    static const char* const kCut[] = {"link.cuts", "0.001", "visit.mean", "100", nullptr};
    const char* more[24];
    join(sets, kCut, more, 24);
    World w;
    if (!build(base, w, places, Load::traffic, true, kPowered, more, true))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    Routes& r = w.net().routes();
    Links& l = w.links();
    const NetCounts n0 = w.net().counts();
    const u64 net0 = w.net_cycles, input0 = w.input_cycles, route0 = r.cycles, cols0 = r.columns;
    const u64 changes0 = r.changes, cut0 = l.cut, mended0 = l.mended;
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < ticks; ++t)
        w.tick();
    const u64 c1 = __rdtsc();
    const double secs = clock_seconds() - t0;
    if (secs <= 0 || c1 <= c0)
        return;
    const NetCounts& n1 = w.net().counts();
    const double ms = 1e3 * secs / static_cast<double>(c1 - c0); // a cycle, in ms
    const double tick = 1e3 * secs / ticks;
    const double net = ms * static_cast<double>(w.net_cycles - net0) / ticks;
    const double input = ms * static_cast<double>(w.input_cycles - input0) / ticks;
    const double route = ms * static_cast<double>(r.cycles - route0) / ticks;
    const u64 changes = r.changes - changes0, cols = r.columns - cols0;
    const double per = 1.0 / ticks;
    printf("  rerouting, %u places (%u links), %u host thread%s: %.0f ticks/s, a tick %.3f ms; the inputs %.3f ms, "
           "the network step %.3f ms (%.1f%%), routes %.3f ms of the threads' time: %.2f topology changes and %.0f "
           "columns worked out a tick (%.0f us each); a tick's links: %.2f cut, %.2f mended, %u down at the end; "
           "a tick's messages: %.1f sent, %.1f delivered, %.2f began a wait, %.2f rerouted, %.2f ran out of time, "
           "%.2f stranded\n",
           places, l.count(), threads, threads == 1 ? "" : "s", ticks / secs, tick, input, net, 100 * net / tick,
           route, per * static_cast<double>(changes), per * static_cast<double>(cols),
           cols ? 1e3 * route * ticks / static_cast<double>(cols) : 0, per * static_cast<double>(l.cut - cut0),
           per * static_cast<double>(l.mended - mended0), l.down_count(),
           per * static_cast<double>(n1.sent - n0.sent), per * static_cast<double>(n1.delivered - n0.delivered),
           per * static_cast<double>(n1.waited - n0.waited), per * static_cast<double>(n1.rerouted - n0.rerouted),
           per * static_cast<double>(n1.expired - n0.expired),
           per * static_cast<double>(n1.stranded - n0.stranded));
    fflush(stdout);
}

// Strikes and the solve's cap (12.2): the same world under storms formed
// often, about one in 400 ticks at any size, and striking often, a tick at
// a time: the ticks a strike landed in, their solve's sweeps and whether it
// stopped at the cap, against the ticks with none. A strike is a source for
// one tick, at 20 V.
void striking(Config& base, u32 places, u32 threads, const char* const* sets, u32 warm, u32 ticks) {
    char rate[32];
    snprintf(rate, sizeof rate, "%.6g", 5000.0 * 48 / places); // in a million ticks for each 100 places
    const char* const kStorms[] = {"patch.storm.rate",         rate,   "patch.storm.strength.min", "0.01",
                                   "patch.storm.strength.max", "0.01", nullptr};
    const char* more[24];
    join(sets, kStorms, more, 24);
    World w;
    if (!build(base, w, places, Load::traffic, true, kPowered, more, true))
        return;
    w.start_threads(threads);
    for (u32 t = 0; t < warm; ++t)
        w.tick();
    const Grid& g = w.grid();
    u64 hit[2] = {}, solves[2] = {}, sweeps[2] = {}, capped[2] = {}, cycles[2] = {};
    const u64 strikes0 = w.strikes;
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    for (u32 t = 0; t < ticks; ++t) {
        const u64 s0 = w.strikes, n0 = g.solves, sw0 = g.sweeps, cap0 = g.capped, sc0 = w.solve_cycles;
        w.tick();
        const u32 k = w.strikes != s0 ? 1 : 0;
        ++hit[k];
        solves[k] += g.solves - n0;
        sweeps[k] += g.sweeps - sw0;
        capped[k] += g.capped - cap0;
        cycles[k] += w.solve_cycles - sc0;
    }
    const double secs = clock_seconds() - t0;
    const u64 c1 = __rdtsc();
    if (secs <= 0 || c1 <= c0)
        return;
    const double ms = 1e3 * secs / static_cast<double>(c1 - c0); // a cycle, in ms
    printf("  strikes, %u places, %u host thread%s: %llu strikes in %llu of %u ticks; in those, %.1f sweeps a solve, "
           "%.1f%% of solves stopped at the cap, the solve %.3f ms; in the rest, %.1f sweeps, %.1f%% at the cap, "
           "%.3f ms\n",
           places, threads, threads == 1 ? "" : "s", static_cast<unsigned long long>(w.strikes - strikes0),
           static_cast<unsigned long long>(hit[1]), ticks,
           solves[1] ? static_cast<double>(sweeps[1]) / solves[1] : 0.0,
           solves[1] ? 100.0 * capped[1] / solves[1] : 0.0, hit[1] ? ms * cycles[1] / hit[1] : 0.0,
           solves[0] ? static_cast<double>(sweeps[0]) / solves[0] : 0.0,
           solves[0] ? 100.0 * capped[0] / solves[0] : 0.0, hit[0] ? ms * cycles[0] / hit[0] : 0.0);
    fflush(stdout);
}

void gate5_parts(Config& base, u32 most) {
    printf("gate 5's parts (a traffic world on its map, under the sources with heat on, the per-access counters "
           "on; gate 5 against gate 5 off timed over the fastest fifth of the ticks, the counts over all of "
           "them):\n");
    const u32 hw = ThreadPool::hardware_threads();
    u32 many = hw < 24 ? hw : 24;
    if (most && most < many)
        many = most;
    static const char* const k48[] = {"memory.size", "65536", "disk.size", "65536", nullptr};
    static const char* const k1000[] = {"memory.size", "32768", "disk.size", "65536", nullptr};
    static const char* const k10000[] = {"memory.size", "16384", "disk.size", "16384", nullptr};
    wearing(base, 48, 1, false, k48, 50, 400);
    wearing(base, 48, 1, true, k48, 50, 400);
    wearing(base, 48, many, false, k48, 50, 400);
    wearing(base, 48, many, true, k48, 50, 400);
    wearing(base, 1000, many, false, k1000, 50, 400);
    wearing(base, 1000, many, true, k1000, 50, 400);
    wearing(base, 10000, many, false, k10000, 20, 200);
    wearing(base, 10000, many, true, k10000, 20, 200);
    respawning(base, 48, many, k48, 50, 8);
    respawning(base, 1000, many, k1000, 50, 8);
    respawning(base, 10000, many, k10000, 20, 8);
    rerouting(base, 48, many, k48, 100, 400);
    rerouting(base, 1000, many, k1000, 100, 400);
    rerouting(base, 10000, many, k10000, 100, 200);
    striking(base, 48, many, k48, 400, 2000);
    striking(base, 1000, many, k1000, 400, 1000);
    printf("\n");
}

} // namespace

int cmd_bench(Config& cfg) {
    const u32 places = key_u32(cfg, "world.places");
    const Str part = key_choice(cfg, "bench.part");
    const u32 most = key_u32(cfg, "run.threads");
    const bool all = part.n == 3;                                       // "all"
    const bool only5 = part.n == 5 && part.p[4] == '5';                 // "gate5"
    const bool only4 = part.n == 5 && part.p[4] == '4';                 // "gate4"
    const bool only3 = part.n == 5 && part.p[4] == '3';
    const bool only2 = part.n == 5 && part.p[4] == '2';
    const bool loads = part.n == 9;                                     // "workloads"
    key_accept_known(cfg);
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    printf("nova2 bench: %u places (a release build gives the real figures)\n\n", places);
    if (all || loads) {
        workload(cfg, "loops (a counting thread on every core)", Load::loops, places, 5, 40);
        workload(cfg, "replicators (one in every lax place's region)", Load::replicators, places, 40, 100);
        workload(cfg, "static (idle runs retrying every tick)", Load::statics, places, 10, 200);
    }
    if (all || only2)
        gate2_parts(cfg, places);
    if (all || only3)
        gate3_parts(cfg);
    if (all || only4)
        gate4_parts(cfg, most);
    if (all || only5)
        gate5_parts(cfg, most);
    return 0;
}

} // namespace nova2
