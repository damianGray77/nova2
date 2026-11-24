// nova2 taker: 9.4's test world (build-plan-gate2.md step 9).
//
// One program placed by hand in every place (R7: a test world, evidence
// about the mechanism, not about life) takes all of its place's free memory
// and fills it with sleeping threads, the frozen place of gate 1 made as
// hard as it can be (taker_world.h: its code, and the fill built by hand at
// tick 0). The world runs under the config's supply for the origin test's
// budget (origin.ticks ticks, or origin.budget instructions), and the report
// says how long each place's taker held and what ended it.
//
// What can end a sleeper at gate 2: a cold start, which ends every program,
// or fading, when its memory is cut out and a bit of its saved registers
// fades (9.4). It never runs again, so it never faults or meets a core that
// cuts out. So the threads a taker loses before it ends are lost to fading,
// or with os.saved_state_fails to any bit of their saved registers failing:
// a fade, a cosmic-ray flip or an edge error (build-plan-gate2.md step 10).
//
// The config is an origin test's: the world and its supply as the origin
// runs have them. The settings only the origin test reads are accepted and
// left alone.
#include <cstdio>
#include <cstring>

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/world.h"
#include "taker_world.h"

namespace nova2 {

namespace {

constexpr u64 kNone = ~0ull;

// One place's taker.
struct Taker {
    bool placed = false;
    TakerPlaced at0; // as placed, filled at tick 0
    // The last sample while it lived (the run's end, if it lasted).
    u32 last_threads = 0, last_held = 0, last_free = 0;
    // Its end.
    u64 end_tick = kNone;
    DeathCause cause = DeathCause::exit;
    u64 births_then = 0; // the place's births when it ended
    bool in_drop = false; // the drop-out it ended in or after (last_dropout)
    Dropout drop;
};

bool alive(const Place& pl, const Taker& t) {
    if (!t.placed)
        return false;
    const Program& pr = pl.os.program(t.at0.holder);
    return pr.alive && pr.uid == t.at0.uid;
}

// The last drop-out to start at or before tick t, in t's stretch of the
// schedule or the two before it: the one a death came in or after. A cold
// start comes when the reset region's memory restarts, its off-time and
// restart gap past, which can be after a short drop-out has ended. False if
// there's none.
bool last_dropout(const Supply& su, u64 t, Dropout* out) {
    const u64 j = t / su.block();
    for (u64 back = 0; back <= 2 && back <= j; ++back) {
        Dropout d;
        if (su.dropout_in(j - back, &d) && d.start <= t) {
            *out = d;
            return true;
        }
    }
    return false;
}

// The deaths in `deaths` that end a taker.
void note_ends(World& w, const Array<DeathFact>& deaths, Taker* takers) {
    for (usize k = 0; k < deaths.size(); ++k) {
        const DeathFact& d = deaths[k];
        if (d.place >= w.place_count())
            continue;
        Taker& t = takers[d.place];
        if (!t.placed || t.end_tick != kNone || d.uid != t.at0.uid)
            continue;
        t.end_tick = d.tick;
        t.cause = d.cause;
        t.births_then = w.place(d.place).births;
        t.in_drop = last_dropout(w.place(d.place).supply, d.tick, &t.drop);
    }
}

const char* cause_name(DeathCause c) {
    switch (c) {
    case DeathCause::exit: return "exit";
    case DeathCause::fault: return "fault";
    case DeathCause::cut_out: return "cut-out";
    case DeathCause::device_failed: return "device";
    case DeathCause::memory_lost: return "memory";
    case DeathCause::cold_start: return "cold start";
    case DeathCause::heat: return "heat";
    case DeathCause::oom: return "out of memory";
    case DeathCause::watchdog: return "watchdog";
    }
    return "?";
}

} // namespace

int cmd_taker(Config& cfg) {
    // Gate 2's test world is measured on gate 2's supply: one at each place,
    // no heat and no low-voltage interrupt, unless the config says.
    if (!cfg.has("supply.model"))
        cfg.set("supply.model", "place");
    if (!cfg.has("heat.on"))
        cfg.set("heat.on", "false");
    if (!cfg.has("power.low_voltage_margin"))
        cfg.set("power.low_voltage_margin", "0");
    const u64 ticks = key_u64(cfg, "origin.ticks");
    const u64 budget = key_u64(cfg, "origin.budget");
    const u32 threads = key_u32(cfg, "run.threads");
    ObserverSettings obs;
    obs.sample_every = key_u32(cfg, "observe.sample_every");
    obs.write_every = key_u32(cfg, "observe.write_every");
    obs.genome_key_every = key_u32(cfg, "observe.genome_key_every");
    obs.group_threshold = key_f64(cfg, "observe.group_threshold");
    obs.min_genome = key_u32(cfg, "observe.min_genome");
    obs.keep_generations = key_u32(cfg, "observe.keep_generations");
    obs.lineage_generations = obs.keep_generations; // a lineage with a row is one whose genomes could be kept
    obs.day = key_u32(cfg, "supply.day");
    obs.turnover.window = key_u64(cfg, "observe.turnover_window");
    obs.turnover.members = key_u32(cfg, "observe.turnover_members");
    obs.keep_facts = true; // the deaths each tick, to see the takers' ends; cleared as they're read
    key_accept_known(cfg); // the world's settings, and the origin test's it leaves alone
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    if (!cfg.ok()) {
        fprintf(stderr, "nova2: %s\n", cfg.value_error());
        return 2;
    }
    World w;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return 2;
    }
    const u32 n = w.place_count();
    Array<Taker> takers;
    takers.resize(n);
    for (u32 i = 0; i < n; ++i) {
        takers[i] = Taker{};
        Taker& t = takers[i];
        if (!place_taker(w.place(i), &t.at0)) {
            fprintf(stderr, "nova2: place %u: couldn't place the taker\n", i);
            return 2;
        }
        t.placed = true;
        t.last_threads = t.at0.threads;
        t.last_held = t.at0.held;
        t.last_free = t.at0.free;
        TextBuf broken;
        if (!w.place(i).check(&broken)) { // the state built by hand, by the invariant checker (step 7)
            fprintf(stderr, "nova2: place %u: the taker's state breaks an invariant: %s\n", i, broken.c_str());
            return 2;
        }
    }
    Observer o;
    o.init(obs);
    w.watch(&o);
    w.start_threads(threads ? threads : ThreadPool::hardware_threads());

    const WorldSettings& ws = w.settings();
    const SupplySettings& su = ws.supply;
    printf("the 9.4 test world (build-plan-gate2.md step 9): seed %llu, %u places, a taker placed by hand in each\n"
           "each takes its place's free memory and fills it with threads asleep until an interrupt that never "
           "comes;\n%llu ticks or %llu instructions, the origin test's budget\n",
           static_cast<unsigned long long>(w.settings().seed), n, static_cast<unsigned long long>(ticks),
           static_cast<unsigned long long>(budget));
    printf("supply: a source of %d mV sagging %d mV at a place's full draw, night at %.2f of noon over a %llu-tick "
           "day, swings to %.2f below full, %.2f drop-outs a place per million ticks (%.0f%% blackouts)\n",
           su.voltage, su.sag, su.night, static_cast<unsigned long long>(su.day), su.swing, su.dropouts,
           100 * su.blackout_share);
    fflush(stdout);

    const double t0 = clock_seconds();
    u64 samples = 0;
    u64 instr = 0, tenths_shown = 0;
    while (w.tick_count() < ticks && (budget == 0 || instr < budget)) {
        w.tick();
        note_ends(w, o.deaths, takers.data());
        o.deaths.clear();
        o.births.clear();
        if (o.samples_taken() == samples)
            continue;
        samples = o.samples_taken();
        instr = o.last_sample().instructions;
        for (u32 i = 0; i < n; ++i) {
            Place& pl = w.place(i);
            Taker& t = takers[i];
            if (!alive(pl, t))
                continue;
            t.last_threads = pl.os.program(t.at0.holder).threads;
            t.last_held = held_by(pl.mem, t.at0.holder);
            t.last_free = pl.mem.free_bytes();
        }
        u64 tenths = w.tick_count() * 10 / ticks;
        if (budget && instr * 10 / budget > tenths)
            tenths = instr * 10 / budget;
        if (tenths > tenths_shown) {
            tenths_shown = tenths;
            u32 left = 0;
            for (u32 i = 0; i < n; ++i)
                left += alive(w.place(i), takers[i]) ? 1 : 0;
            fprintf(stderr, "  tick %llu (%llu0%%), %u takers left, %.0f s\n",
                    static_cast<unsigned long long>(w.tick_count()), static_cast<unsigned long long>(tenths), left,
                    clock_seconds() - t0);
        }
    }
    // Deaths the observer hasn't seen yet (fading's, after the last barrier).
    for (u32 i = 0; i < n; ++i)
        note_ends(w, w.place(i).os.deaths, takers.data());
    const double secs = clock_seconds() - t0;
    const u64 end = w.tick_count();

    printf("\n%5s %-13s %7s %7s %9s %8s %15s %6s %10s %-10s %-9s %8s %13s %8s %7s\n", "place", "type", "devices",
           "its mV", "region mV", "threads", "held of memory", "free", "ended", "by", "drop-out", "lost", "held then",
           "births", "at end");
    // What the lost threads went to: fading alone, or with os.saved_state_fails
    // any failed bit of their saved registers.
    const bool fails = n && w.place(0).os.settings().saved_state_fails;
    const char* lost_to = fails ? "memory failing" : "fading";
    u32 lasted = 0, by_cold = 0, by_memory = 0, by_other = 0;
    u32 in_blackout = 0, in_partial = 0, in_none = 0; // the drop-out each that ended came in or after
    u64 lost_all = 0, ended_sum = 0;
    u32 lost_places = 0;
    Array<u64> holds; // ticks held, of those that ended
    for (u32 i = 0; i < n; ++i) {
        Place& pl = w.place(i);
        const Taker& t = takers[i];
        char type[32];
        snprintf(type, sizeof type, "%s %u", pl.mem.strict() ? "strict" : "lax", pl.mem.page_size());
        char held[32];
        snprintf(held, sizeof held, "%u of %u", t.at0.held, pl.mem.size());
        printf("%5u %-13s %7u %7d %9d %8u %15s %6u", i, type, pl.mem.device_count(),
               pl.mem.device(t.at0.device).threshold, pl.mem.device(pl.mem.region_device()).threshold, t.at0.threads,
               held, t.at0.free);
        // The threads it lost before its end (or the run's), all to memory.
        const u32 lost = t.at0.threads > t.last_threads ? t.at0.threads - t.last_threads : 0;
        lost_all += lost;
        lost_places += lost ? 1 : 0;
        if (t.end_tick == kNone) {
            printf(" %10s %-10s %-9s %8u %13u %8s", "held", "", "", lost, t.last_held, "-");
            ++lasted;
        } else {
            char in[16];
            if (!t.in_drop) {
                snprintf(in, sizeof in, "-");
                ++in_none;
            } else if (t.drop.blackout()) {
                snprintf(in, sizeof in, "blackout");
                ++in_blackout;
            } else {
                snprintf(in, sizeof in, "to %.3f", static_cast<double>(t.drop.keep) / kCapacityOne);
                ++in_partial;
            }
            printf(" %10llu %-10s %-9s %8u %13u %8llu", static_cast<unsigned long long>(t.end_tick),
                   cause_name(t.cause), in, lost, t.last_held,
                   static_cast<unsigned long long>(pl.births - t.births_then));
            holds.push(t.end_tick);
            ended_sum += t.end_tick;
            if (t.cause == DeathCause::cold_start)
                ++by_cold;
            else if (t.cause == DeathCause::memory_lost)
                ++by_memory;
            else
                ++by_other;
        }
        printf(" %7u\n", pl.os.live_programs());
    }
    printf("\ncolumns: the place's memory devices; the thresholds of the device the taker's code and threads are on "
           "and of the reset region's;\nits threads, the bytes it held and the bytes left free at tick 0; the tick "
           "it ended, what ended it, and the drop-out it ended in or after\n(a blackout, or the share of capacity it "
           "left); the threads it lost before then (or before the run's end), all to %s; the bytes it\nheld at "
           "the last sample before then; births in the place after it ended; programs alive in the place at the "
           "end\n",
           lost_to);

    sort_u64(holds.data(), holds.size());
    printf("\n%u of %u takers held to the end (tick %llu); %u ended, %u at a cold start, %u by %s, %u otherwise\n",
           lasted, n, static_cast<unsigned long long>(end), static_cast<u32>(holds.size()), by_cold, by_memory,
           lost_to, by_other);
    printf("those that ended: %u in or after a blackout, %u in or after a drop-out that left some capacity, %u with "
           "no drop-out in\nthe schedule's stretch or the two before it (%llu ticks each)\n",
           in_blackout, in_partial, in_none, static_cast<unsigned long long>(w.place(0).supply.block()));
    if (holds.size())
        printf("those that ended held %.0f ticks on average (the median %llu, the shortest %llu, the longest %llu)\n",
               static_cast<double>(ended_sum) / holds.size(), static_cast<unsigned long long>(holds[holds.size() / 2]),
               static_cast<unsigned long long>(holds[0]), static_cast<unsigned long long>(holds[holds.size() - 1]));
    printf("threads lost to %s before a taker's end: %llu, in %u places\n", lost_to,
           static_cast<unsigned long long>(lost_all), lost_places);

    // The world's supply and what it did, summed over places.
    u64 mv = 0, below_p = 0, below_e = 0, below_region = 0, cold = 0, mem_cut = 0, faded = 0, faded_threads = 0;
    u64 failed_threads = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& pl = w.place(i);
        mv += pl.mv_total;
        below_p += pl.below_p;
        below_e += pl.below_e;
        below_region += pl.below_region;
        cold += pl.cold_starts;
        mem_cut += pl.memory_cutouts;
        faded += pl.noise.cells_faded;
        faded_threads += pl.faded_threads;
        failed_threads += pl.failed_threads;
    }
    const double pt = static_cast<double>(end) * n;
    if (pt > 0) {
        printf("the places: %.1f mV on average; below the P cores' threshold %.2f%% of place-ticks, the E cores' "
               "%.3f%%, the reset region's memory's %.4f%%;\n%llu cold starts, %llu memory cut-outs, %llu cells "
               "faded, ending %llu threads",
               mv / pt, 100 * below_p / pt, 100 * below_e / pt, 100 * below_region / pt,
               static_cast<unsigned long long>(cold), static_cast<unsigned long long>(mem_cut),
               static_cast<unsigned long long>(faded), static_cast<unsigned long long>(faded_threads));
        if (fails)
            printf("; flips and edge errors ended %llu threads (os.saved_state_fails)",
                   static_cast<unsigned long long>(failed_threads));
        printf("\n");
    }
    printf("%llu ticks, %llu instructions, %.0f s\n", static_cast<unsigned long long>(end),
           static_cast<unsigned long long>(instr), secs);
    return 0;
}

} // namespace nova2
