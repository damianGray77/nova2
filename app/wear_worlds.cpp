// nova2 worlds, gate 5's test worlds at full size (build-plan-gate5.md step
// 14; R7: evidence about the mechanism, not about life). worlds.test picks
// one; the config is the world, at full size (1,000 places), and each runs
// worlds.ticks ticks (each of its parts does, where it has parts), reporting
// every worlds.report_every. Each turns every gate 5 strength off
// (snapshot.h's gate5_off) and sets the ones it is about over the config,
// named in its first lines. The succession, hammerer, injection,
// bootblocks, flaky, islands, fanout and current make every place lax
// (os.strict_share = 0), so A.5's replicator runs anywhere. Those but the
// succession, and the partition, give their places gate 3's steady supply
// by place in place of the sources, so no cut-out ends a thread that runs
// on and on (the hammerer, the spinner) or a cold start the programs being
// watched; the scars, options, succession, storms and respawning keep gate
// 4's sources.
//
//   scars:       wear quickened (each kind's endurance 5,000 ticks at the
//                reference), visits at a mean of 2,000, 10,000 and 50,000
//                ticks, nothing respawning. A hot hub (of the places with
//                no cooling device, the one with the most data links, its
//                cores kept busy by hand) and a cool place (of those with
//                one, the best cooled, nothing placed). Reported: the share
//                of its device-ticks each spends with the device dead, and
//                the world's, against wait / (lifetime + wait), with the
//                visits' mean as the wait and the lifetime measured (7.8);
//   succession:  wear quickened (endurance 20,000), visits at a mean of
//                10,000, and A.5's replicator placed at the hot hub; then
//                the same with wear off. Reported: the hub's wear, the
//                lineage's births and members there, and when it loses
//                the place (12.5);
//   storms:      storms at 200 times the settings' rate and their
//                strength, wear at its settings, no visits. A place is
//                town or countryside by its catchment (the smaller half
//                town). Reported: the devices and links that fail in a
//                strike's tick or the next, each by the nearer strike's kind
//                and by proximity links from it, and the voltage there
//                (12.2);
//   partition:   gate 3's traffic at every place (traffic_world.h), the
//                supply by place (no sources: the cut leaves the island
//                lit), no visits. An island's bridges cut by hand for 500
//                ticks and mended, then for 3,000 (a message waits
//                net.lifetime, 1,000). Reported: the network's counts and
//                the messages waiting through each (11.6);
//   respawning:  wear quickened further (endurance 2,000), visits at a
//                mean of 50,000, respawning at the settings' share.
//                Reported: where places move (how far, onto which
//                landmass) and how the map's links change (7.8);
//   hammerer:    rowhammer at its settings and no cosmic rays. At 100
//                places a thread reading two pages of its own in turn
//                (test_hammer.cpp's), the pages either side of each held
//                by a program of its own code (kin) at half the places and
//                by a sleeper (a rival) at the rest; with the core error
//                floors as set, then off (test_hammer.cpp's), since they
//                end a thread that runs without a call within a few
//                hundred ticks. Reported: whose bytes the flips land in,
//                and when the hammerers end (9.8);
//   injection:   at 100 places a receiver whose buffer is the last 8 bytes
//                of its code's allocation, and whose next allocation, its
//                own, it jumps to after each RECV; and a sender beside it
//                SENDing it 40 bytes every 100 ticks, of which the 32 past
//                the buffer begin with a replicator (A.5's, finding its
//                start from IP). Half the receivers ask for 40 bytes, half
//                for 8. Reported: the overruns, whether the sent code runs
//                as the receiver, and what it makes (9.8);
//   bootblocks:  disk blocks wearing out at 200 writes, no visits; at 100
//                places a thread DWRITEing 64 bytes to its disk's boot
//                block for ever, with levelling off and on. Reported: how
//                long the disks last (7.4);
//   options:     the scars world with visits at a mean of 10,000, three
//                ways: as set (replacement at 1, respawning at 1);
//                replacement like for like at half a life; respawning at
//                half the devices failed. Reported: what each changes in
//                scars, replacements and moves;
//   flaky:       A.5's replicator placed at 100 places, flaky.share 0, no
//                visits (a visit draws flakiness again); at half of them
//                every core and memory device made flaky by hand, 20 times
//                its kind's floor. Reported: mutations, births and how
//                long the lineage holds each place (gap 12);
//   islands:     every strait parting, in spells of a third of the run:
//                joined, parted, joined. As they part, two of gate 3's
//                migrants (traffic_world.h; sense and pushy, two genomes)
//                are placed at the two ends of a bridge of the strait whose
//                smaller landmass is largest, one each side. Reported: who
//                holds each side as they join and a spell later, and who
//                crossed (gap 13);
//   fanout:      the killer and the watchdog at their settings; the
//                killer off; and the killer on with the core error floors
//                off, so that a thread looping without a call lasts long
//                enough for the watchdog. At 50 places one program
//                starting a thread on every record it can get
//                (test_gate5.cpp's crowd), given at tick 300 memory until
//                its place has under a twentieth free; at 50 others a
//                thread that loops without a call beside A.5's
//                replicator. Reported: how long the crowd lasts, what
//                fills its place after it, and whom the watchdog ends
//                (gap 14);
//   current:     A.5's replicator placed at the place furthest upstream on
//                the largest landmass and at its neighbours there, with the
//                current at 1 and 10 blocks a place a day and off, its
//                heading fixed; then at 10 with the heading turning over the
//                run (a season the run's length, turns of up to half a
//                turn). Reported: what comes alive downstream and how far,
//                what reaches another landmass, whether static still makes
//                lineages where the blocks land, and what the turn reaches
//                (gap 15).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h" // gate5_off
#include "nova2/world.h"
#include "traffic_world.h"

namespace nova2 {

namespace {

typedef unsigned long long ull;

struct Run {
    u64 ticks = 0, every = 0;
    u32 threads = 0;
};

// --- the programs placed by hand (R7) -----------------------------------------

const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// A.5's 14-byte replicator, on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// test_gate5.cpp's crowd: one program starting a thread at its sleeper
// again and again, for as long as the place has a thread to give.
const u8 kCrowd[14] = {encode(kMov, kC),   encode(kAdd, kImm), 8,      encode(kSys, kImm), kSpawn,
                       encode(kJmp, kImm), static_cast<u8>(-5), 0,     encode(kMov, kImm), 120,
                       encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
const u8 kSpin[2] = {encode(kJmp, kImm), 0}; // JMP # 0: never a call
// test_hammer.cpp's: the bytes B and D point at, read in turn, for ever.
const u8 kHammer[4] = {encode(kMov, kAtB), encode(kMov, kAtD), encode(kJmp, kImm), static_cast<u8>(-2)};
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm),
                        static_cast<u8>(-4)};
// test_blocks.cpp's: DWRITE for ever, its offset in D, its buffer in B and
// its length in C.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
// The injection's replicator: A.5's, finding its start from IP (it runs in
// a thread whose C isn't its start), 17 bytes (2 mod 3: "both") with one
// never run.
const u8 kInjected[17] = {encode(kMov, kIp),  encode(kSto, kC),   encode(kMov, kImm), 17,
                          encode(kSto, kB),   encode(kSys, kImm), kAlloc,             encode(kMov, kC),
                          encode(kSto, kB),   encode(kCpy, kImm), 17,                 encode(kMov, kD),
                          encode(kSys, kImm), kSpawn,             encode(kJmp, kImm), static_cast<u8>(-12),
                          0};

// --- shared -------------------------------------------------------------------

// A copy of the config with every gate 5 strength off, then `a` and `b`
// (key, value, ..., null) set over it, in that order.
void derive(Config& base, Config* out, const char* const* a, const char* const* b = nullptr) {
    TextBuf text;
    base.write(text);
    Config::ParseError pe;
    out->parse(text.view(), &pe);
    gate5_off(out, true);
    for (u32 i = 0; a && a[i]; i += 2)
        out->set(a[i], a[i + 1]);
    for (u32 i = 0; b && b[i]; i += 2)
        out->set(b[i], b[i + 1]);
}

bool build(World& w, Config& cfg, const Run& run) {
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return false;
    }
    if (!w.map().placed()) {
        fprintf(stderr, "nova2: gate 5's test worlds need a map (world.map)\n");
        return false;
    }
    w.start_threads(run.threads);
    return true;
}

void watch_world(Config& cfg, World& w, Observer* obs) {
    ObserverSettings os;
    os.sample_every = key_u32(cfg, "observe.sample_every");
    os.write_every = key_u32(cfg, "observe.write_every");
    os.genome_key_every = key_u32(cfg, "observe.genome_key_every");
    os.group_threshold = key_f64(cfg, "observe.group_threshold");
    os.min_genome = key_u32(cfg, "observe.min_genome");
    os.keep_generations = key_u32(cfg, "observe.keep_generations");
    os.lineage_generations = os.keep_generations;
    os.day = key_u32(cfg, "supply.day");
    os.keep_facts = false;
    obs->init(os);
    w.watch(obs);
}

bool reporting(const World& w, const Run& run, u64 end) {
    return w.tick_count() % run.every == 0 || w.tick_count() == end;
}

// A program placed by hand in a fresh allocation of `size` bytes holding
// `code`, its thread at its start: its holder, or 0; the address in *at.
u32 hand(Place& p, const u8* code, u32 n, u32 size, u32* at = nullptr) {
    const u32 a = p.mem.alloc(1, size, p.mem.strict() ? 1 : 2, 0);
    if (!a)
        return 0;
    memcpy(p.mem.bytes() + a, code, n);
    if (!p.os.place_by_hand(1, a, a, 0))
        return 0;
    if (at)
        *at = a;
    return p.mem.record(p.mem.alloc_at(a)).holder;
}

u64 uid_of(const Place& p, u32 holder) { return holder ? p.os.program(holder).uid : 0; }

bool lives(const Place& p, u32 holder, u64 uid) {
    return holder && p.os.program(holder).alive && p.os.program(holder).uid == uid;
}

// A program's first thread's registers B, C and D, set while it is off its
// core (A.3).
void set_regs(Place& p, u32 holder, u32 b, u32 c, u32 d) {
    const u32 t = p.os.program(holder).first_thread;
    if (!t)
        return;
    ExecContext x;
    p.os.load(t - 1, x);
    x.r.b = b;
    x.r.c = c;
    x.r.d = d;
    p.os.save(t - 1, x);
}

// A lineage's members living at a place, by the observer's records.
u32 members_at(const Observer& obs, u64 origin, u32 place) {
    const LineageRec* L = obs.lineage(origin);
    if (!L)
        return 0;
    if (L->place == place)
        return L->home_alive;
    const ColonyRec* c = obs.colony(origin, place);
    return c ? c->alive : 0;
}

// The lineage with the most programs living at place i: its origin (0:
// none), its programs there in *most.
u64 leader_at(World& w, const Observer& obs, u32 i, u32* most) {
    const Place& p = w.place(i);
    Array<u64> origins;
    Array<u32> counts;
    for (u32 h = 0; h < p.os.program_capacity(); ++h) {
        const Program& g = p.os.program(kFirstProgramHolder + h);
        const ProgramRec* r = g.alive ? obs.program(g.uid) : nullptr;
        if (!r)
            continue;
        usize k = 0;
        while (k < origins.size() && origins[k] != r->origin)
            ++k;
        if (k == origins.size()) {
            origins.push(r->origin);
            counts.push(0);
        }
        ++counts[k];
    }
    u64 best = 0;
    *most = 0;
    for (usize k = 0; k < origins.size(); ++k)
        if (counts[k] > *most) {
            *most = counts[k];
            best = origins[k];
        }
    return best;
}

const char* cause_name(BirthCause c) {
    static const char* const kNames[] = {"a thread's", "static", "an arrival", "a boot", "placed by hand"};
    return kNames[static_cast<u32>(c)];
}

// Each place's links from the places with from[i] set (~0u: no way), over
// the data links (power: the proximity links), whatever their state.
void hops_from(const Map& m, const u8* from, bool power, Array<u32>* hops) {
    const u32 n = m.place_count();
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
        const u32 deg = power ? m.power_degree(v) : m.degree(v);
        for (u32 j = 0; j < deg; ++j) {
            u32 u;
            if (power) {
                const PowerLink& l = m.power_link(m.power_link_at(v, j));
                u = l.a == v ? l.b : l.a;
            } else {
                const MapLink& l = m.link(m.link_at(v, j));
                u = l.a == v ? l.b : l.a;
            }
            if ((*hops)[u] == ~0u) {
                (*hops)[u] = (*hops)[v] + 1;
                queue.push(u);
            }
        }
    }
}

// The hot hub: of the places with no cooling device, the one with the most
// data links (the least cooling among those level).
u32 hot_hub(World& w) {
    const Map& m = w.map();
    u32 best = ~0u;
    for (u32 i = 0; i < w.place_count(); ++i) {
        if (w.place(i).has_cooler)
            continue;
        if (best == ~0u || m.degree(i) > m.degree(best) ||
            (m.degree(i) == m.degree(best) && m.cooling(i) < m.cooling(best)))
            best = i;
    }
    return best == ~0u ? 0 : best;
}

// The cool place: of those with a cooling device, the best cooled (the
// fewest data links among those level).
u32 cool_place(World& w) {
    const Map& m = w.map();
    u32 best = ~0u;
    for (u32 i = 0; i < w.place_count(); ++i) {
        if (!w.place(i).has_cooler)
            continue;
        if (best == ~0u || m.cooling(i) > m.cooling(best) ||
            (m.cooling(i) == m.cooling(best) && m.degree(i) < m.degree(best)))
            best = i;
    }
    return best == ~0u ? 1 : best;
}

// Counting threads placed by hand until each core has one.
void keep_busy(Place& p) {
    for (u32 k = p.os.live_threads(); k < p.core_count(); ++k)
        if (!hand(p, kCounter, sizeof kCounter, 64))
            return;
}

u64 sum_kinds(const u64* v) {
    u64 s = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        s += v[k];
    return s;
}

int cmp_u64(const void* a, const void* b) {
    const u64 x = *static_cast<const u64*>(a), y = *static_cast<const u64*>(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

int cmp_u32(const void* a, const void* b) {
    const u32 x = *static_cast<const u32*>(a), y = *static_cast<const u32*>(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

// Wear quickened: each kind's endurance, in ticks at the reference.
void quicken(Config* cfg, const char* endurance) {
    static const char* const kKinds[] = {"p", "e", "memory", "disk", "network", "cooler"};
    cfg->set("wear.rate", "1");
    for (u32 k = 0; k < 6; ++k) {
        char key[48];
        snprintf(key, sizeof key, "wear.%s.endurance", kKinds[k]);
        cfg->set(key, endurance);
    }
}

// --- scars, and the options ----------------------------------------------------

// A place's devices, or the world's, over a run: the share of their
// device-ticks spent dead; the device-ticks alive and dead for each failure;
// the failures, those replaced fresh at a visit, and those replaced like
// for like; and the temperature on average.
struct Scars {
    double share = 0, life = 0, wait = 0, temp = 0;
    u64 failures = 0, replaced = 0, renewed = 0;
};

void scars_of(World& w, const Array<u64>& dev_ticks, u32 only, Scars* s) {
    u64 ticks = 0, dead = 0;
    double temp = 0;
    u32 n = 0;
    *s = Scars{};
    for (u32 i = 0; i < w.place_count(); ++i) {
        if (only != ~0u && i != only)
            continue;
        const Place& p = w.place(i);
        ticks += dev_ticks[i];
        dead += sum_kinds(p.failed_ticks);
        s->failures += sum_kinds(p.failures);
        s->replaced += sum_kinds(p.replaced);
        s->renewed += sum_kinds(p.renewed);
        temp += w.tick_count() ? static_cast<double>(p.temp_total) / 1000.0 / static_cast<double>(w.tick_count()) : 0;
        ++n;
    }
    s->share = ticks ? static_cast<double>(dead) / static_cast<double>(ticks) : 0;
    s->life = s->failures ? static_cast<double>(ticks - dead) / static_cast<double>(s->failures) : 0;
    s->wait = s->failures ? static_cast<double>(dead) / static_cast<double>(s->failures) : 0;
    s->temp = n ? temp / n : 0;
}

struct ScarEnd {
    Scars world, hub, cool;
    u64 respawns = 0, moved = 0, torn = 0;
    u32 links = 0, plinks = 0;
};

// The world's devices' scars under the settings `sets`, with the hot hub
// kept busy.
bool scars_one(Config& base, const Run& run, const char* name, const char* const* sets, ScarEnd* end) {
    Config cfg;
    derive(base, &cfg, sets);
    quicken(&cfg, "5000");
    World w;
    if (!build(w, cfg, run))
        return false;
    const u32 n = w.place_count(), hub = hot_hub(w), cool = cool_place(w);
    printf("%s: the hot hub is place %u (%u data links, cooling %.2f W a degree, no cooling device), the cool "
           "place %u (%u data links, cooling %.2f W a degree, a cooling device)\n",
           name, hub, w.map().degree(hub), w.map().cooling(hub), cool, w.map().degree(cool), w.map().cooling(cool));
    fflush(stdout);
    Array<u64> dev_ticks;
    dev_ticks.resize(n);
    while (w.tick_count() < run.ticks) {
        keep_busy(w.place(hub));
        w.tick();
        for (u32 i = 0; i < n; ++i)
            dev_ticks[i] += w.place(i).device_count();
        if (!reporting(w, run, run.ticks))
            continue;
        scars_of(w, dev_ticks, ~0u, &end->world);
        scars_of(w, dev_ticks, hub, &end->hub);
        scars_of(w, dev_ticks, cool, &end->cool);
        printf("  tick %-7llu the world %.4f dead (%llu failures, %llu replaced fresh, %llu like for like, %llu "
               "respawns); the hub %.4f at %.1f degrees (%llu failures); the cool place %.4f at %.1f degrees "
               "(%llu)\n",
               static_cast<ull>(w.tick_count()), end->world.share, static_cast<ull>(end->world.failures),
               static_cast<ull>(end->world.replaced), static_cast<ull>(end->world.renewed),
               static_cast<ull>(w.respawns), end->hub.share, end->hub.temp, static_cast<ull>(end->hub.failures),
               end->cool.share, end->cool.temp, static_cast<ull>(end->cool.failures));
        fflush(stdout);
    }
    end->respawns = w.respawns;
    end->moved = w.respawns_moved;
    end->torn = w.torn_programs;
    end->links = w.map().link_count();
    end->plinks = w.map().power_link_count();
    return true;
}

void print_scars(const char* what, const Scars& s, double mean) {
    if (!s.failures) {
        printf("    %s: no device failed\n", what);
        return;
    }
    printf("    %s: %.4f of its device-ticks dead; %llu failures, a life of %.0f device-ticks and a wait of %.0f "
           "each; wait / (life + wait) with the mean as the wait: %.4f\n",
           what, s.share, static_cast<ull>(s.failures), s.life, s.wait, s.life + mean > 0 ? mean / (s.life + mean) : 0);
}

int scars(Config& base, const Run& run) {
    static const char* const kMeans[3] = {"2000", "10000", "50000"};
    ScarEnd end[3];
    for (u32 v = 0; v < 3; ++v) {
        const char* const sets[] = {"visit.mean", kMeans[v], "visit.respawn_share", "2", nullptr};
        char name[64];
        snprintf(name, sizeof name, "visits every %s ticks on average", kMeans[v]);
        if (!scars_one(base, run, name, sets, &end[v]))
            return 2;
        printf("\n");
    }
    printf("the scars, by the visits' mean (each kind's endurance 5,000 ticks at the reference; random gaps, so "
           "a failed device waits the mean for its visit):\n");
    for (u32 v = 0; v < 3; ++v) {
        const double mean = atof(kMeans[v]);
        printf("  visits every %s ticks:\n", kMeans[v]);
        print_scars("the world", end[v].world, mean);
        print_scars("the hot hub", end[v].hub, mean);
        print_scars("the cool place", end[v].cool, mean);
    }
    return 0;
}

int options(Config& base, const Run& run) {
    static const char* const kSets[3][5] = {{"visit.mean", "10000", nullptr, nullptr, nullptr},
                                            {"visit.mean", "10000", "visit.replace_at", "0.5", nullptr},
                                            {"visit.mean", "10000", "visit.respawn_share", "0.5", nullptr}};
    static const char* const kNames[3] = {"as set (replacement at 1, respawning at 1)",
                                          "replacement like for like at half a life",
                                          "respawning at half the devices failed"};
    ScarEnd end[3];
    for (u32 v = 0; v < 3; ++v) {
        if (!scars_one(base, run, kNames[v], kSets[v], &end[v]))
            return 2;
        printf("\n");
    }
    printf("the options (each kind's endurance 5,000 ticks at the reference, visits every 10,000 on average):\n");
    for (u32 v = 0; v < 3; ++v) {
        const ScarEnd& e = end[v];
        printf("  %s: the world %.4f dead, %llu failures, %llu replaced fresh and %llu like for like; the hub %.4f "
               "dead (%llu failures, %llu like for like), the cool place %.4f (%llu, %llu); %llu respawns, %llu of "
               "them moved, %llu programs torn down; %u data links and %u proximity links at the end\n",
               kNames[v], e.world.share, static_cast<ull>(e.world.failures), static_cast<ull>(e.world.replaced),
               static_cast<ull>(e.world.renewed), e.hub.share, static_cast<ull>(e.hub.failures),
               static_cast<ull>(e.hub.renewed), e.cool.share, static_cast<ull>(e.cool.failures),
               static_cast<ull>(e.cool.renewed), static_cast<ull>(e.respawns), static_cast<ull>(e.moved),
               static_cast<ull>(e.torn), e.links, e.plinks);
    }
    return 0;
}

// --- succession ------------------------------------------------------------------

// The share of their lives the place's cores and memory devices have worn.
void worn_share(const Place& p, double* cores, double* memory) {
    double c = 0, m = 0;
    for (u32 k = 0; k < p.core_count(); ++k)
        c += p.core_worn[k].life ? static_cast<double>(p.core_worn[k].wear) / p.core_worn[k].life : 0;
    const u32 nm = p.mem.device_count();
    for (u32 d = 0; d < nm; ++d)
        m += p.memory_worn[d].life ? static_cast<double>(p.memory_worn[d].wear) / p.memory_worn[d].life : 0;
    *cores = p.core_count() ? c / p.core_count() : 0;
    *memory = nm ? m / nm : 0;
}

bool succession_one(Config& base, const Run& run, const char* name, bool wear) {
    static const char* const kSets[] = {"visit.mean", "10000", "os.strict_share", "0", nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    if (wear)
        quicken(&cfg, "20000");
    World w;
    if (!build(w, cfg, run))
        return false;
    Observer obs;
    watch_world(cfg, w, &obs);
    const u32 hub = hot_hub(w);
    Place& p = w.place(hub);
    const u32 holder = hand(p, kLax, sizeof kLax, 14);
    const u64 origin = uid_of(p, holder);
    printf("%s: A.5's replicator placed at the hot hub, place %u (%u data links, cooling %.2f W a degree, %u "
           "cores, %u memory devices)\n",
           name, hub, w.map().degree(hub), w.map().cooling(hub), p.core_count(), p.mem.device_count());
    fflush(stdout);
    u64 gone = ~0ull, first_lost = ~0ull, last_led = 0;
    while (w.tick_count() < run.ticks) {
        w.tick();
        const LineageRec* L = obs.lineage(origin);
        const u32 home = L ? L->home_alive : 0;
        if (!home && gone == ~0ull)
            gone = w.tick_count();
        if (!reporting(w, run, run.ticks))
            continue;
        u32 most = 0;
        const u64 lead = leader_at(w, obs, hub, &most);
        if (lead == origin)
            last_led = w.tick_count();
        else if (first_lost == ~0ull)
            first_lost = w.tick_count();
        const Place& q = w.place(hub);
        double cw = 0, mw = 0;
        worn_share(q, &cw, &mw);
        const LineageRec* lead_rec = lead ? obs.lineage(lead) : nullptr;
        printf("  tick %-7llu %4u of the lineage's %5u members there, of %4u programs; %s leads (%u programs); "
               "%7llu births in the lineage; %.1f degrees; cores %.2f of their lives worn, memory %.2f; %u devices "
               "dead now, %llu failures and %llu replaced so far; incarnation %u\n",
               static_cast<ull>(w.tick_count()), home, L ? L->alive : 0, q.os.live_programs(),
               lead == origin ? "it" : lead_rec ? cause_name(lead_rec->cause) : "nobody", most,
               static_cast<ull>(L ? L->births : 0), static_cast<double>(q.temp) / kDegree, cw, mw,
               q.failed_devices(), static_cast<ull>(sum_kinds(q.failures)), static_cast<ull>(sum_kinds(q.replaced)),
               q.incarnation);
        fflush(stdout);
    }
    const LineageRec* L = obs.lineage(origin);
    printf("  %s: it led the hub at every report to tick %llu", name, static_cast<ull>(last_led));
    if (first_lost != ~0ull)
        printf(", first did not at tick %llu", static_cast<ull>(first_lost));
    if (gone != ~0ull)
        printf(", and its last member there died at tick %llu", static_cast<ull>(gone));
    printf("; %llu births in it, %u alive at the end\n\n", static_cast<ull>(L ? L->births : 0), L ? L->alive : 0);
    return true;
}

int succession(Config& base, const Run& run) {
    return succession_one(base, run, "wear quickened (endurance 20,000 at the reference)", true) &&
                   succession_one(base, run, "wear off", false)
               ? 0
               : 2;
}

// --- storms ------------------------------------------------------------------------

int storms(Config& base, const Run& run) {
    static const char* const kSets[] = {"wear.rate", "1", "patch.storm.rate", "100", nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    World w;
    if (!build(w, cfg, run))
        return 2;
    const Map& m = w.map();
    const u32 n = w.place_count();
    Array<u32> catch_sorted;
    catch_sorted.resize(n);
    for (u32 i = 0; i < n; ++i)
        catch_sorted[i] = m.catchment(i);
    qsort(catch_sorted.data(), n, sizeof(u32), cmp_u32);
    const u32 median = catch_sorted[n / 2];
    printf("storms at 200 times the settings' rate (%g formed in each %llu ticks on average), at their strength "
           "(%g a tick at a storm's centre); "
           "a place is town with a catchment of %u raster cells or fewer (the median), countryside above\n",
           100 / 1e6 * n / 100.0 * static_cast<double>(w.events().block(PatchType::storm)),
           static_cast<ull>(w.events().block(PatchType::storm)), key_f64(cfg, "patch.storm.strength.max"), median);
    fflush(stdout);
    constexpr u32 kHops = 6; // 6: six links or more, or none
    static const char* const kClass[2] = {"town", "countryside"};
    u64 fails[2][kHops + 1] = {}, kinds[2][kHeatKinds] = {}, strikes[2] = {}, ticks[2] = {}, links[2] = {};
    double mv[2][3] = {}, hot[2][3] = {};
    u64 mv_n[2][3] = {};
    u64 background = 0, background_links = 0;
    Array<u64> prev;
    prev.resize(static_cast<usize>(n) * kHeatKinds);
    // This tick's strikes by the kind of place struck, and last tick's: a
    // device a strike burns out fails at its place's next device states, so
    // in the tick after.
    Array<u8> struck[2], last[2], either[2], down;
    for (u32 c = 0; c < 2; ++c) {
        struck[c].resize(n);
        last[c].resize(n);
        either[c].resize(n);
    }
    down.resize(w.links().count());
    Array<u32> now_hops[2], hops[2];
    u32 last_count[2] = {};
    while (w.tick_count() < run.ticks) {
        w.tick();
        u32 count[2] = {};
        for (u32 i = 0; i < n; ++i) {
            const u32 c = m.catchment(i) <= median ? 0 : 1;
            struck[c][i] = w.struck(i) ? 1 : 0;
            struck[1 - c][i] = 0;
            count[c] += struck[c][i];
        }
        const bool now = count[0] || count[1], any = now || last_count[0] || last_count[1];
        for (u32 c = 0; c < 2; ++c) {
            strikes[c] += count[c];
            ticks[c] += count[c] ? 1 : 0;
            for (u32 i = 0; i < n; ++i)
                either[c][i] = struck[c][i] | last[c][i];
            if (count[c] || last_count[c])
                hops_from(m, either[c].data(), true, &hops[c]);
            else {
                hops[c].resize(n);
                hops[c].fill(~0u);
            }
            if (count[c])
                hops_from(m, struck[c].data(), true, &now_hops[c]);
            else {
                now_hops[c].resize(n);
                now_hops[c].fill(~0u);
            }
        }
        // Each failure goes to the nearer strike's kind of place (town on a
        // tie) among the strikes of this tick and the last, by the proximity
        // links from it; with none, to the background. The voltage and the
        // temperature are this tick's, by the links from its own strikes.
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            const u32 c = hops[0][i] <= hops[1][i] ? 0 : 1, hc = hops[c][i];
            const u32 h = hc < kHops ? hc : kHops;
            for (u32 k = 0; k < kHeatKinds; ++k) {
                const u64 d = p.failures[k] - prev[static_cast<usize>(i) * kHeatKinds + k];
                prev[static_cast<usize>(i) * kHeatKinds + k] = p.failures[k];
                if (!any) {
                    background += d;
                    continue;
                }
                fails[c][h] += d;
                kinds[c][k] += d;
            }
            const u32 cn = now_hops[0][i] <= now_hops[1][i] ? 0 : 1, hn = now_hops[cn][i];
            if (now && hn < 3) {
                mv[cn][hn] += p.env.voltage_mv;
                hot[cn][hn] += static_cast<double>(p.temp) / kDegree;
                ++mv_n[cn][hn];
            }
        }
        for (u32 k = 0; k < w.links().count(); ++k) {
            const Line& l = w.links().line(k);
            const u8 gone = l.failed == LinkFail::worn ? 1 : 0;
            if (gone && !down[k]) {
                if (!any)
                    ++background_links;
                else
                    ++links[hops[0][l.a] <= hops[1][l.a] ? 0 : 1];
            }
            down[k] = gone;
        }
        for (u32 c = 0; c < 2; ++c) {
            last_count[c] = count[c];
            for (u32 i = 0; i < n; ++i)
                last[c][i] = struck[c][i];
        }
        if (!reporting(w, run, run.ticks))
            continue;
        printf("  tick %-7llu strikes %llu (town %llu in %llu ticks, countryside %llu in %llu); devices failed in a "
               "strike's tick or the next: nearer a town strike %llu, a countryside one %llu; links %llu and %llu; "
               "in the other ticks devices %llu, links %llu\n",
               static_cast<ull>(w.tick_count()), static_cast<ull>(w.strikes), static_cast<ull>(strikes[0]),
               static_cast<ull>(ticks[0]), static_cast<ull>(strikes[1]), static_cast<ull>(ticks[1]),
               static_cast<ull>(sum_kinds(kinds[0])), static_cast<ull>(sum_kinds(kinds[1])),
               static_cast<ull>(links[0]), static_cast<ull>(links[1]), static_cast<ull>(background),
               static_cast<ull>(background_links));
        fflush(stdout);
    }
    printf("\nin a strike's tick and the next, each failure by the nearer strike's kind of place:\n");
    for (u32 c = 0; c < 2; ++c) {
        printf("  %s: %llu strikes in %llu ticks; devices failed by proximity links from the strike:", kClass[c],
               static_cast<ull>(strikes[c]), static_cast<ull>(ticks[c]));
        for (u32 h = 0; h <= kHops; ++h)
            if (h == kHops)
                printf(" %u+ or none: %llu", h, static_cast<ull>(fails[c][h]));
            else
                printf(" %u: %llu", h, static_cast<ull>(fails[c][h]));
        printf("\n    by kind:");
        for (u32 k = 0; k < kHeatKinds; ++k)
            printf(" %s %llu", kHeatKindNames[k], static_cast<ull>(kinds[c][k]));
        printf("; links failed %llu\n    in the tick, on average: ", static_cast<ull>(links[c]));
        for (u32 h = 0; h < 3; ++h)
            printf("%s%u links out %.0f mV, %.1f degrees", h ? "; " : "", h,
                   mv_n[c][h] ? mv[c][h] / mv_n[c][h] : 0, mv_n[c][h] ? hot[c][h] / mv_n[c][h] : 0);
        printf("\n");
    }
    return 0;
}

// --- partition -----------------------------------------------------------------------

void net_line(World& w, const char* when) {
    const NetCounts& c = w.net().counts();
    printf("  tick %-7llu %-28s sent %llu, delivered %llu, lost %llu, cut %llu, stranded %llu, full %llu; began a "
           "wait %llu, returned %llu, rerouted %llu, expired %llu; waiting now %u\n",
           static_cast<ull>(w.tick_count()), when, static_cast<ull>(c.sent), static_cast<ull>(c.delivered),
           static_cast<ull>(c.lost), static_cast<ull>(c.cut), static_cast<ull>(c.stranded),
           static_cast<ull>(c.full), static_cast<ull>(c.waited), static_cast<ull>(c.returned),
           static_cast<ull>(c.rerouted), static_cast<ull>(c.expired), w.net().waiting());
    fflush(stdout);
}

int partition(Config& base, const Run& run) {
    static const char* const kSets[] = {"supply.model", "place", "link.cuts", "1e-12", "net.lifetime", "1000",
                                        nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    World w;
    if (!build(w, cfg, run))
        return 2;
    const Map& m = w.map();
    const u32 n = w.place_count();
    // The island: the most peopled landmass after the largest with a bridge.
    Array<u32> size, bridged;
    size.resize(m.landmasses() + 1);
    bridged.resize(m.landmasses() + 1);
    for (u32 i = 0; i < n; ++i)
        ++size[m.landmass(i)];
    for (u32 k = 0; k < m.link_count(); ++k)
        if (m.landmass(m.link(k).a) != m.landmass(m.link(k).b)) {
            ++bridged[m.landmass(m.link(k).a)];
            ++bridged[m.landmass(m.link(k).b)];
        }
    u32 largest = 0, island = ~0u;
    for (u32 l = 0; l < size.size(); ++l)
        largest = size[l] > size[largest] ? l : largest;
    for (u32 l = 0; l < size.size(); ++l)
        if (l != largest && bridged[l] && (island == ~0u || size[l] > size[island]))
            island = l;
    if (island == ~0u) {
        fprintf(stderr, "nova2: no island with a bridge on this map\n");
        return 2;
    }
    Array<u32> cut;
    for (u32 k = 0; k < m.link_count(); ++k)
        if ((m.landmass(m.link(k).a) == island) != (m.landmass(m.link(k).b) == island))
            cut.push(k);
    u32 placed = 0;
    for (u32 i = 0; i < n; ++i)
        placed += place_traffic(w.place(i), n, i);
    printf("the island: landmass %u, %u places, %u bridges to the rest; gate 3's traffic placed at every place (%u "
           "programs); the supply by place; a message waits %llu ticks for a way on\n",
           island, size[island], static_cast<u32>(cut.size()), placed, static_cast<ull>(key_u64(cfg, "net.lifetime")));
    fflush(stdout);
    // Warm for 2,000 ticks; cut for 500; joined for 2,500; cut for 3,000;
    // joined for 3,000.
    static const u64 kAt[5] = {2000, 2500, 5000, 8000, 11000};
    static const char* const kWhat[5] = {"cut", "mended (after 500)", "cut", "mended (after 3,000)", "the end"};
    u32 phase = 0;
    net_line(w, "warm");
    while (phase < 5) {
        w.tick();
        if (w.tick_count() == kAt[phase]) {
            net_line(w, kWhat[phase]);
            if (phase == 0 || phase == 2)
                for (usize j = 0; j < cut.size(); ++j)
                    w.links().fail(w.links().of_data(cut[j]), LinkFail::cut);
            if (phase == 1 || phase == 3)
                for (usize j = 0; j < cut.size(); ++j)
                    w.links().mend(w.links().of_data(cut[j]));
            ++phase;
            continue;
        }
        if (w.tick_count() % run.every == 0)
            net_line(w, phase == 1 || phase == 3 ? "the island cut off" : "joined");
    }
    return 0;
}

// --- respawning ----------------------------------------------------------------------

int respawning(Config& base, const Run& run) {
    static const char* const kSets[] = {"visit.mean", "50000", nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    quicken(&cfg, "2000");
    World w;
    if (!build(w, cfg, run))
        return 2;
    const u32 n = w.place_count();
    Array<double> x0, y0;
    Array<u32> mass0;
    x0.resize(n);
    y0.resize(n);
    mass0.resize(n);
    const Map& m = w.map();
    u32 deg0 = 0, lonely0 = 0;
    for (u32 i = 0; i < n; ++i) {
        x0[i] = m.x(i);
        y0[i] = m.y(i);
        mass0[i] = m.landmass(i);
        deg0 = m.degree(i) > deg0 ? m.degree(i) : deg0;
        lonely0 += m.degree(i) == 0 ? 1 : 0;
    }
    printf("wear quickened (endurance 2,000 ticks at the reference), visits every 50,000 ticks on average, a "
           "place respawning when a visit finds every device dead; at the start: %u data links (%u bridges), %u "
           "proximity links, %u landmasses, the most links at a place %u, %u places with none\n",
           m.link_count(), m.bridges(), m.power_link_count(), m.landmasses(), deg0, lonely0);
    fflush(stdout);
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (!reporting(w, run, run.ticks))
            continue;
        const Map& mm = w.map();
        u32 again = 0, other_mass = 0, most = 0, lonely = 0, most_inc = 0;
        double dist = 0, far = 0;
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            most = mm.degree(i) > most ? mm.degree(i) : most;
            lonely += mm.degree(i) == 0 ? 1 : 0;
            most_inc = p.incarnation > most_inc ? p.incarnation : most_inc;
            if (!p.incarnation)
                continue;
            ++again;
            const double dx = mm.x(i) - x0[i], dy = mm.y(i) - y0[i], d = sqrt(dx * dx + dy * dy);
            dist += d;
            far = d > far ? d : far;
            other_mass += mm.landmass(i) != mass0[i] ? 1 : 0;
        }
        printf("  tick %-7llu %llu respawns (%llu to a new point, %llu programs torn down); %u places built again "
               "(at most %u times), %.3f of the map's height from where they began on average and %.3f at most, "
               "%u on another landmass; %u data links (%u bridges), %u proximity links, the most at a place %u, "
               "%u places with none\n",
               static_cast<ull>(w.tick_count()), static_cast<ull>(w.respawns), static_cast<ull>(w.respawns_moved),
               static_cast<ull>(w.torn_programs), again, most_inc, again ? dist / again : 0, far, other_mass,
               mm.link_count(), mm.bridges(), mm.power_link_count(), most, lonely);
        fflush(stdout);
    }
    return 0;
}

// --- the hammerer ------------------------------------------------------------------------

// Whose bytes the flips landed in: the neighbour's pages beside the
// hammered ones, the neighbour's others, the hammerer's own, another
// program's, and memory no program holds (free, or an idle run's).
enum Whose : u32 { kNextPages = 0, kNeighbour, kOwn, kOtherProgram, kNoProgram, kWhose };

struct Hammered {
    u32 place = 0, page0 = 0, hammerer = 0, neighbour = 0;
    u64 h_uid = 0, n_uid = 0, ended = ~0ull;
    bool kin = false;
};

bool hammerer_one(Config& base, const Run& run, const char* name, const char* const* more) {
    static const char* const kSets[] = {"hammer.strength", "0.01", "noise.cosmic", "0", "os.strict_share", "0",
                                        "supply.model", "place", nullptr};
    Config cfg;
    derive(base, &cfg, kSets, more);
    World w;
    if (!build(w, cfg, run))
        return false;
    const u32 n = w.place_count();
    Array<Hammered> hs;
    u32 skipped = 0;
    for (u32 i = 0; i < n && hs.size() < 100; i += 10) {
        Place& p = w.place(i);
        const u32 ps = p.mem.page_size();
        u32 a[5];
        bool ok = true;
        for (u32 j = 0; j < 5; ++j) {
            a[j] = p.mem.alloc(1, ps, 2, 1); // the fastest device, as the hammerer's own would be
            ok = ok && a[j] && a[j] == a[0] + j * ps && p.mem.device_of(a[j]) == p.mem.device_of(a[0]);
        }
        Hammered h;
        h.place = i;
        h.page0 = a[0];
        h.kin = hs.size() % 2 == 0;
        u32 h_at = 0, n_at = 0;
        h.hammerer = ok ? hand(p, kHammer, sizeof kHammer, 64, &h_at) : 0;
        h.neighbour = !h.hammerer ? 0
                      : h.kin     ? hand(p, kHammer, sizeof kHammer, 64, &n_at)
                                  : hand(p, kSleeper, sizeof kSleeper, 64, &n_at);
        if (!h.neighbour) {
            ++skipped;
            continue;
        }
        for (u32 j = 0; j < 5; ++j) {
            memset(p.mem.bytes() + a[j], 0, ps);
            p.mem.give(p.mem.alloc_at(a[j]), j % 2 ? h.hammerer : h.neighbour);
        }
        set_regs(p, h.hammerer, a[1] + 8, 0, a[3] + 8);
        if (h.kin) // its own code reading one page, which flips nothing
            set_regs(p, h.neighbour, a[0] + 8, 0, a[0] + 8);
        h.h_uid = uid_of(p, h.hammerer);
        h.n_uid = uid_of(p, h.neighbour);
        hs.push(h);
    }
    printf("%s: rowhammer at strength 0.01, a window of %llu ticks; at %u places (%u more skipped: no five "
           "pages in a "
           "row on one device) a thread reads two pages of its own in turn, each between two pages of its "
           "neighbour's: a program of its own code (kin) at %u of them, a sleeper (a rival) at the rest\n",
           name, static_cast<ull>(key_u64(cfg, "hammer.window")), static_cast<u32>(hs.size()), skipped,
           static_cast<u32>((hs.size() + 1) / 2));
    fflush(stdout);
    u64 hit[2][kWhose] = {};
    while (w.tick_count() < run.ticks) {
        w.tick();
        for (usize k = 0; k < hs.size(); ++k) {
            Hammered& e = hs[k];
            if (e.ended == ~0ull && !lives(w.place(e.place), e.hammerer, e.h_uid))
                e.ended = w.tick_count();
            const Hammered& h = hs[k];
            const Place& p = w.place(h.place);
            const u32 ps = p.mem.page_size();
            const Array<u32>& f = p.failed_bytes();
            for (usize j = 0; j < f.size(); ++j) {
                const u32 at = f[j];
                u32 whose;
                if (at >= h.page0 && at < h.page0 + 5 * ps)
                    whose = (at - h.page0) / ps % 2 ? kOwn : kNextPages;
                else {
                    const u32 id = p.mem.alloc_at(at);
                    const u32 holder = id ? p.mem.record(id).holder : 0;
                    whose = holder == h.hammerer   ? kOwn
                            : holder == h.neighbour ? kNeighbour
                            : holder >= kFirstProgramHolder ? kOtherProgram
                                                            : kNoProgram;
                }
                ++hit[h.kin ? 0 : 1][whose];
            }
        }
        if (!reporting(w, run, run.ticks))
            continue;
        u64 flipped[2] = {}, past[2] = {};
        u32 alive[2][2] = {};
        for (usize k = 0; k < hs.size(); ++k) {
            const Hammered& h = hs[k];
            const Place& p = w.place(h.place);
            const u32 s = h.kin ? 0 : 1;
            flipped[s] += p.noise.hammer;
            past[s] += p.noise.hammered;
            alive[s][0] += lives(p, h.hammerer, h.h_uid) ? 1 : 0;
            alive[s][1] += lives(p, h.neighbour, h.n_uid) ? 1 : 0;
        }
        for (u32 s = 0; s < 2; ++s)
            printf("  tick %-7llu beside %-5s activations past the threshold %llu, bits flipped %llu; failed bytes "
                   "in the neighbour's pages beside the hammered ones %llu, in its others %llu, in the hammerer's "
                   "%llu, another program's %llu, no program's %llu; hammerers alive %u, neighbours %u\n",
                   static_cast<ull>(w.tick_count()), s ? "a rival" : "kin", static_cast<ull>(past[s]),
                   static_cast<ull>(flipped[s]), static_cast<ull>(hit[s][kNextPages]),
                   static_cast<ull>(hit[s][kNeighbour]), static_cast<ull>(hit[s][kOwn]),
                   static_cast<ull>(hit[s][kOtherProgram]), static_cast<ull>(hit[s][kNoProgram]), alive[s][0],
                   alive[s][1]);
        fflush(stdout);
    }
    // When the hammerers ended, beside kin and beside a rival.
    for (u32 s = 0; s < 2; ++s) {
        Array<u64> ended;
        u32 of = 0;
        for (usize k = 0; k < hs.size(); ++k) {
            if (hs[k].kin != (s == 0))
                continue;
            ++of;
            if (hs[k].ended != ~0ull)
                ended.push(hs[k].ended);
        }
        if (ended.size())
            qsort(ended.data(), ended.size(), sizeof(u64), cmp_u64);
        printf("  %s: beside %s, hammerers ended %u of %u", name, s ? "a rival" : "kin",
               static_cast<u32>(ended.size()), of);
        if (ended.size())
            printf(", the first at tick %llu, the median at %llu, the last at %llu", static_cast<ull>(ended[0]),
                   static_cast<ull>(ended[ended.size() / 2]), static_cast<ull>(ended[ended.size() - 1]));
        printf("\n");
    }
    printf("\n");
    fflush(stdout);
    return true;
}

// The core error floors kill a thread that runs without a call within a
// few hundred ticks, so the hammerer runs as set and with them off, as
// test_hammer.cpp has them.
int hammerer(Config& base, const Run& run) {
    const char* const off[] = {"noise.p_floor", "0", "noise.e_floor", "0", nullptr};
    return hammerer_one(base, run, "the core error floors as set", nullptr) &&
                   hammerer_one(base, run, "the core error floors off", off)
               ? 0
               : 2;
}

// --- injection ---------------------------------------------------------------------------

struct Receiver {
    u32 place = 0, at = 0, holder = 0;
    u64 uid = 0;
    bool over = false; // asks for 40 bytes into 8
};

int injection(Config& base, const Run& run) {
    static const char* const kSets[] = {"os.strict_share", "0", "supply.model", "place", nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    World w;
    if (!build(w, cfg, run))
        return 2;
    Observer obs;
    watch_world(cfg, w, &obs);
    const u32 n = w.place_count();
    constexpr u32 kPort = 5, kSize = 128;
    u8 msg[40];
    for (u32 i = 0; i < 40; ++i)
        msg[i] = static_cast<u8>(0xE0 + i % 16);
    memcpy(msg + 8, kInjected, sizeof kInjected);
    Array<Receiver> rs;
    u32 skipped = 0;
    for (u32 i = 5; i < n && rs.size() < 100; i += 10) {
        Place& p = w.place(i);
        Receiver r;
        r.place = i;
        r.over = rs.size() % 2 == 0;
        // Its two allocations first, one after the other, then the program.
        const u32 a = p.mem.alloc(1, kSize, 2, 0), b = p.mem.alloc(1, kSize, 2, 0);
        if (!a || b != a + kSize) {
            ++skipped;
            continue;
        }
        HandCode code;
        code.addr(kB, kSize - 8).set(kC, r.over ? 40 : 8).set(kD, -1).imm(kMov, kPort).sys(kRecv);
        const u32 jmp = code.n;
        code.imm(kJmp, static_cast<i32>(kSize - jmp)); // into its second allocation
        memcpy(p.mem.bytes() + a, code.b, code.n);
        const u8 back[2] = {encode(kJmp, kImm), static_cast<u8>(-static_cast<i32>(kSize))};
        memcpy(p.mem.bytes() + b, back, 2);
        if (!p.os.place_by_hand(1, a, a, 0)) {
            ++skipped;
            continue;
        }
        r.at = a;
        r.holder = p.mem.record(p.mem.alloc_at(a)).holder;
        r.uid = uid_of(p, r.holder);
        p.mem.give(p.mem.alloc_at(b), r.holder);
        // The sender beside it: the message to this place's port, again
        // every 100 ticks.
        HandCode s;
        s.addr(kC, 64).set(kD, 40).set(kB, kPort);
        s.imm(kMov, static_cast<i32>(i / 100)).imm(kMul, 100).imm(kAdd, static_cast<i32>(i % 100)).sys(kSend);
        s.imm(kMov, 100).sys(kSleep).back(0);
        u8 sender[104] = {};
        memcpy(sender, s.b, s.n);
        memcpy(sender + 64, msg, 40);
        if (s.bad || !hand(p, sender, sizeof sender, kSize)) {
            ++skipped;
            continue;
        }
        rs.push(r);
    }
    printf("%u receivers, each with a sender beside it (%u places skipped); half ask for 40 bytes into an 8-byte "
           "buffer at their allocation's end, half for 8\n",
           static_cast<u32>(rs.size()), skipped);
    fflush(stdout);
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (!reporting(w, run, run.ticks))
            continue;
        u32 written[2] = {}, alive[2] = {}, made[2] = {};
        u64 births[2] = {};
        for (usize k = 0; k < rs.size(); ++k) {
            const Receiver& r = rs[k];
            const Place& p = w.place(r.place);
            const u32 s = r.over ? 0 : 1;
            written[s] += memcmp(p.mem.bytes() + r.at + kSize, kInjected, sizeof kInjected) == 0 ? 1 : 0;
            alive[s] += lives(p, r.holder, r.uid) ? 1 : 0;
            const LineageRec* L = obs.lineage(r.uid);
            const u64 b = L && L->births > 1 ? L->births - 1 : 0;
            births[s] += b;
            made[s] += b ? 1 : 0;
        }
        printf("  tick %-7llu overruns %llu (into its own %llu, kin %llu, another lineage %llu, a stack %llu, free "
               "memory %llu); injections seen %llu, programs injected %llu, their births %llu\n",
               static_cast<ull>(w.tick_count()), static_cast<ull>(obs.overruns),
               static_cast<ull>(obs.reached[kReachOwn]), static_cast<ull>(obs.reached[kReachKin]),
               static_cast<ull>(obs.reached[kReachOther]), static_cast<ull>(obs.reached[kReachStack]),
               static_cast<ull>(obs.reached[kReachFree]), static_cast<ull>(obs.injections_seen),
               static_cast<ull>(obs.injected_programs), static_cast<ull>(obs.injection_births));
        for (u32 s = 0; s < 2; ++s)
            printf("               asking for %2u: the replicator written over the code after its RECV at %u of "
                   "%u, the receiver alive at %u, a lineage from it at %u (%llu births)\n",
                   s ? 8 : 40, written[s], static_cast<u32>(s ? rs.size() / 2 : (rs.size() + 1) / 2), alive[s],
                   made[s],
                   static_cast<ull>(births[s]));
        fflush(stdout);
    }
    return 0;
}

// --- boot blocks ---------------------------------------------------------------------------

bool blocks_one(Config& base, const Run& run, const char* name, const char* levelling) {
    static const char* const kSets[] = {"disk.block_endurance", "200", "os.strict_share", "0", "supply.model",
                                        "place", nullptr};
    const char* const more[] = {"os.levelling", levelling, nullptr};
    Config cfg;
    derive(base, &cfg, kSets, more);
    World w;
    if (!build(w, cfg, run))
        return false;
    const u32 n = w.place_count();
    Array<u32> at;
    for (u32 i = 3; i < n && at.size() < 100; i += 10) {
        Place& p = w.place(i);
        u32 a = 0;
        const u32 holder = hand(p, kWriter, sizeof kWriter, 1024, &a);
        if (!holder)
            continue;
        set_regs(p, holder, a + 64, 64, 0);
        at.push(i);
    }
    if (at.empty()) {
        fprintf(stderr, "nova2: no writer could be placed\n");
        return false;
    }
    printf("%s: at %u places a thread DWRITEs 64 bytes at its disk's offset 0 for ever; blocks of %u bytes, %u to "
           "a disk, wearing out at 200 writes (each block's drawn from half to twice that)\n",
           name, static_cast<u32>(at.size()), w.place(at[0]).os.block_size(), w.place(at[0]).os.block_count());
    fflush(stdout);
    Array<u64> life, writes;
    Array<u8> done;
    done.resize(at.size());
    while (w.tick_count() < run.ticks && life.size() < at.size()) {
        w.tick();
        for (usize k = 0; k < at.size(); ++k) {
            const Place& p = w.place(at[k]);
            if (!done[k] && p.os.disk_failed()) {
                done[k] = 1;
                life.push(w.tick_count());
                writes.push(p.os.calls_ok[kDwrite]);
            }
        }
        if (!reporting(w, run, run.ticks))
            continue;
        u64 calls = 0, swaps = 0;
        for (usize k = 0; k < at.size(); ++k) {
            calls += w.place(at[k]).os.calls_ok[kDwrite];
            swaps += w.place(at[k]).os.level_swaps;
        }
        printf("  tick %-7llu disks failed %u of %u; DWRITEs done %llu a writer on average; levelling's swaps %llu\n",
               static_cast<ull>(w.tick_count()), static_cast<u32>(life.size()), static_cast<u32>(at.size()),
               static_cast<ull>(calls / at.size()),
               static_cast<ull>(swaps));
        fflush(stdout);
    }
    if (life.size()) {
        qsort(life.data(), life.size(), sizeof(u64), cmp_u64);
        u64 wsum = 0;
        for (usize k = 0; k < writes.size(); ++k)
            wsum += writes[k];
        printf("  %s: %u disks of %u failed by tick %llu; the first at tick %llu, the median at %llu, the last at "
               "%llu; %llu DWRITEs before failing on average\n",
               name, static_cast<u32>(life.size()), static_cast<u32>(at.size()), static_cast<ull>(w.tick_count()),
               static_cast<ull>(life[0]),
               static_cast<ull>(life[life.size() / 2]), static_cast<ull>(life[life.size() - 1]),
               static_cast<ull>(wsum / writes.size()));
    }
    if (life.size() < at.size()) {
        u64 worst = 0, sum = 0, boot = 0;
        u32 alive = 0, boot_most = 0;
        for (usize k = 0; k < at.size(); ++k) {
            if (done[k])
                continue;
            const Os& os = w.place(at[k]).os;
            double most = 0;
            u32 most_at = 0;
            for (u32 b = 0; b < os.block_count(); ++b)
                if (os.block_life(b)) {
                    const double s = static_cast<double>(os.block_writes(b)) / os.block_life(b);
                    most_at = s > most ? b : most_at;
                    most = s > most ? s : most;
                }
            const u32 b0 = os.block_holding(0);
            boot += os.block_life(b0) ? 1000 * static_cast<u64>(os.block_writes(b0)) / os.block_life(b0) : 0;
            boot_most += most_at == b0 ? 1 : 0;
            sum += static_cast<u64>(most * 1000);
            worst = static_cast<u64>(most * 1000) > worst ? static_cast<u64>(most * 1000) : worst;
            ++alive;
        }
        printf("  %s: %u disks alive at the end; their most worn block %.3f of its life on average, %.3f at most "
               "(the boot block at %u of them); their boot blocks %.3f on average\n",
               name, alive, alive ? sum / 1000.0 / alive : 0, worst / 1000.0, boot_most,
               alive ? boot / 1000.0 / alive : 0);
    }
    printf("\n");
    return true;
}

int bootblocks(Config& base, const Run& run) {
    return blocks_one(base, run, "levelling off", "false") && blocks_one(base, run, "levelling on", "true") ? 0 : 2;
}

// --- flaky ------------------------------------------------------------------------------------

struct Settled {
    u32 place = 0;
    u64 origin = 0, gone = ~0ull;
    bool flaky = false;
};

int flaky(Config& base, const Run& run) {
    static const char* const kSets[] = {"os.strict_share", "0", "supply.model", "place", nullptr};
    Config cfg;
    derive(base, &cfg, kSets);
    World w;
    if (!build(w, cfg, run))
        return 2;
    Observer obs;
    watch_world(cfg, w, &obs);
    const u32 n = w.place_count();
    constexpr u32 kFactor = 20;
    Array<Settled> ls;
    for (u32 i = 1; i < n && ls.size() < 100; i += 10) {
        Place& p = w.place(i);
        Settled l;
        l.place = i;
        l.flaky = ls.size() % 2 == 0;
        const u32 holder = hand(p, kLax, sizeof kLax, 14);
        if (!holder)
            continue;
        l.origin = uid_of(p, holder);
        if (l.flaky) {
            for (u32 c = 0; c < p.core_count(); ++c)
                p.noise.set_core_floor(c, kFactor * kFlakyOne, 0, true);
            for (u32 d = 0; d < p.mem.device_count(); ++d) {
                p.noise.set_memory_flaky(d, kFactor * kFlakyOne);
                p.noise.renew_memory(d, p.memory_worn[d].fresh);
            }
        }
        ls.push(l);
    }
    printf("A.5's replicator placed at %u places; at %u of them every core and memory device flaky by hand, 20 "
           "times its kind's floor (the cores' %g and %g a result, memory's cosmic rays %g a bit a tick)\n",
           static_cast<u32>(ls.size()), static_cast<u32>((ls.size() + 1) / 2), key_f64(cfg, "noise.p_floor"),
           key_f64(cfg, "noise.e_floor"), key_f64(cfg, "noise.cosmic"));
    fflush(stdout);
    while (w.tick_count() < run.ticks) {
        w.tick();
        for (usize k = 0; k < ls.size(); ++k)
            if (ls[k].gone == ~0ull && !members_at(obs, ls[k].origin, ls[k].place))
                ls[k].gone = w.tick_count();
        if (!reporting(w, run, run.ticks))
            continue;
        u32 held[2] = {}, members[2] = {};
        u64 births[2] = {}, faithful[2] = {};
        for (usize k = 0; k < ls.size(); ++k) {
            const u32 s = ls[k].flaky ? 1 : 0;
            const LineageRec* L = obs.lineage(ls[k].origin);
            const u32 there = members_at(obs, ls[k].origin, ls[k].place);
            held[s] += there ? 1 : 0;
            members[s] += there;
            births[s] += L ? L->births : 0;
            faithful[s] += L ? L->faithful : 0;
        }
        for (u32 s = 0; s < 2; ++s)
            printf("  tick %-7llu %-8s the lineage alive at %2u places, %5u members; %8llu births, %8llu not "
                   "faithful (%.4f)\n",
                   static_cast<ull>(w.tick_count()), s ? "flaky:" : "plain:", held[s], members[s],
                   static_cast<ull>(births[s]), static_cast<ull>(births[s] - faithful[s]),
                   births[s] ? static_cast<double>(births[s] - faithful[s]) / births[s] : 0);
        printf("               the observer's tally: births made by a parent %llu (plain) and %llu (flaky), "
               "mutations %llu and %llu\n",
               static_cast<ull>(obs.tally5.flaky_births[0]), static_cast<ull>(obs.tally5.flaky_births[1]),
               static_cast<ull>(obs.tally5.flaky_mutations[0]), static_cast<ull>(obs.tally5.flaky_mutations[1]));
        fflush(stdout);
    }
    for (u32 s = 0; s < 2; ++s) {
        Array<u64> gone;
        u32 held = 0;
        for (usize k = 0; k < ls.size(); ++k)
            if (ls[k].flaky == (s == 1)) {
                if (ls[k].gone == ~0ull)
                    ++held;
                else
                    gone.push(ls[k].gone);
            }
        if (gone.size())
            qsort(gone.data(), gone.size(), sizeof(u64), cmp_u64);
        printf("%s places: the lineage held %u to the end; %u lost theirs, the first at tick %llu, the median at "
               "%llu\n",
               s ? "flaky" : "plain", held, static_cast<u32>(gone.size()), static_cast<ull>(gone.size() ? gone[0] : 0),
               static_cast<ull>(gone.size() ? gone[gone.size() / 2] : 0));
    }
    return 0;
}

// --- islands ----------------------------------------------------------------------------------

void sides_line(World& w, const Observer& obs, u32 ma, u32 mb, const u64* origin, bool leaders) {
    const Map& m = w.map();
    u32 held[2][2] = {}, led[2][3] = {}, places[2] = {};
    for (u32 i = 0; i < w.place_count(); ++i) {
        const u32 side = m.landmass(i) == ma ? 0 : m.landmass(i) == mb ? 1 : 2;
        if (side == 2)
            continue;
        ++places[side];
        for (u32 x = 0; x < 2; ++x)
            held[side][x] += members_at(obs, origin[x], i) ? 1 : 0;
        if (leaders) {
            u32 most = 0;
            const u64 lead = leader_at(w, obs, i, &most);
            ++led[side][lead == origin[0] ? 0 : lead == origin[1] ? 1 : 2];
        }
    }
    u32 across[2] = {};
    for (u32 c = 0; c < obs.colony_count(); ++c) {
        const ColonyRec& r = obs.colony_at(c);
        for (u32 x = 0; x < 2; ++x)
            if (r.origin == origin[x] && m.landmass(r.place) == (x ? ma : mb))
                ++across[x];
    }
    printf("  tick %-7llu the sense lineage's side (%u places): sense at %u, pushy at %u; the pushy lineage's "
           "side (%u): sense at %u, pushy at %u; colonies founded across: sense %u, pushy %u\n",
           static_cast<ull>(w.tick_count()), places[0], held[0][0], held[0][1], places[1], held[1][0], held[1][1],
           across[0], across[1]);
    if (leaders)
        printf("               leading each place: on the sense side sense %u, pushy %u, another %u; on the "
               "pushy side sense %u, pushy %u, another %u\n",
               led[0][0], led[0][1], led[0][2], led[1][0], led[1][1], led[1][2]);
    fflush(stdout);
}

int islands(Config& base, const Run& run) {
    const u64 spell = run.ticks / 3;
    char sp[32];
    snprintf(sp, sizeof sp, "%llu", static_cast<ull>(spell));
    const char* const sets[] = {"strait.share", "1", "strait.spell.min", sp, "strait.spell.max", sp,
                                "os.strict_share", "0", "supply.model", "place", nullptr};
    Config cfg;
    derive(base, &cfg, sets);
    World w;
    if (!build(w, cfg, run))
        return 2;
    Observer obs;
    watch_world(cfg, w, &obs);
    const Map& m = w.map();
    Array<u32> size;
    size.resize(m.landmasses() + 1);
    for (u32 i = 0; i < w.place_count(); ++i)
        ++size[m.landmass(i)];
    u32 best = ~0u;
    for (u32 s = 0; s < w.links().strait_count(); ++s) {
        const Strait& st = w.links().strait(s);
        const u32 small = size[st.ma] < size[st.mb] ? size[st.ma] : size[st.mb];
        const Strait* b = best == ~0u ? nullptr : &w.links().strait(best);
        if (!b || small > (size[b->ma] < size[b->mb] ? size[b->ma] : size[b->mb]))
            best = s;
    }
    if (best == ~0u) {
        fprintf(stderr, "nova2: no strait on this map\n");
        return 2;
    }
    const u32 ma = w.links().strait(best).ma, mb = w.links().strait(best).mb;
    u32 a = ~0u, b = ~0u;
    for (u32 k = 0; k < m.link_count() && a == ~0u; ++k) {
        const MapLink& l = m.link(k);
        if (m.landmass(l.a) == ma && m.landmass(l.b) == mb) {
            a = l.a;
            b = l.b;
        } else if (m.landmass(l.a) == mb && m.landmass(l.b) == ma) {
            a = l.b;
            b = l.a;
        }
    }
    printf("every strait parting in spells of %llu ticks; the strait between landmass %u (%u places) and %u (%u), "
           "%u bridges; the migrants go to place %u and place %u, the ends of one, as the straits part\n",
           static_cast<ull>(spell), ma, size[ma], mb, size[mb], w.links().strait(best).bridges, a, b);
    fflush(stdout);
    u64 origin[2] = {};
    while (w.tick_count() < 3 * spell) {
        if (w.tick_count() == spell) {
            origin[0] = place_migrant(w.place(a), migrant(MigrantKind::sense));
            origin[1] = place_migrant(w.place(b), migrant(MigrantKind::pushy));
            printf("  tick %-7llu placed: sense at place %u, pushy at place %u\n", static_cast<ull>(w.tick_count()),
                   a, b);
        }
        w.tick();
        if (w.tick_count() == spell + 1 || w.tick_count() == 2 * spell + 1)
            printf("  tick %-7llu straits parted now %u of %u\n", static_cast<ull>(w.tick_count()),
                   w.links().parted_straits(), w.links().strait_count());
        const bool edge = w.tick_count() == 2 * spell || w.tick_count() == 3 * spell;
        if (w.tick_count() > spell && (edge || w.tick_count() % run.every == 0))
            sides_line(w, obs, ma, mb, origin, edge);
    }
    printf("the network's messages lost crossing a bridge as its strait parted: %llu; partings %llu, joinings "
           "%llu\n",
           static_cast<ull>(w.net().counts().parted), static_cast<ull>(w.links().partings),
           static_cast<ull>(w.links().joinings));
    return 0;
}

// --- fan-out ----------------------------------------------------------------------------------

struct Guarded {
    u32 place = 0, holder = 0, spin = 0;
    u64 uid = 0, spin_uid = 0, lax = 0, ended = ~0ull, spin_ended = ~0ull, given = 0;
    u32 most_threads = 0;
};

// The tick the crowds are given most of their places, once their threads
// have filled the device their stacks go on (a thread's stack goes on its
// start's device).
const u64 kFill = 300;

// Memory for a holder until its place has under a twentieth free: the
// largest allocations that fit, then smaller. The bytes given.
u64 fill_place(Place& p, u32 holder) {
    const u32 keep = p.mem.size() / 20;
    u64 given = 0;
    for (u32 size = p.mem.max_alloc(); size >= p.mem.page_size() && p.mem.free_bytes() > keep;) {
        if (p.mem.free_bytes() - keep < size || !p.mem.alloc(holder, size, 0, 0)) {
            size /= 2;
            continue;
        }
        given += size;
    }
    return given;
}

bool fanout_one(Config& base, const Run& run, const char* name, const char* killer, bool floors) {
    static const char* const kSets[] = {"os.watchdog", "1000000", "os.strict_share", "0", "supply.model", "place",
                                        nullptr};
    const char* const more[] = {"os.killer", killer, floors ? nullptr : "noise.p_floor", "0", "noise.e_floor", "0",
                                nullptr};
    Config cfg;
    derive(base, &cfg, kSets, more);
    World w;
    if (!build(w, cfg, run))
        return false;
    Observer obs;
    watch_world(cfg, w, &obs);
    const u32 n = w.place_count();
    Array<Guarded> crowds, spins;
    for (u32 i = 7; i < n; i += 20) {
        Place& p = w.place(i);
        Guarded g;
        g.place = i;
        g.holder = hand(p, kCrowd, sizeof kCrowd, 64);
        g.uid = uid_of(p, g.holder);
        if (g.holder)
            crowds.push(g);
    }
    for (u32 i = 17; i < n; i += 20) {
        Place& p = w.place(i);
        Guarded g;
        g.place = i;
        g.spin = hand(p, kSpin, sizeof kSpin, 64);
        g.spin_uid = uid_of(p, g.spin);
        g.lax = uid_of(p, hand(p, kLax, sizeof kLax, 14));
        if (g.spin && g.lax)
            spins.push(g);
    }
    if (crowds.empty() || spins.empty()) {
        fprintf(stderr, "nova2: the crowds or the spinners could not be placed\n");
        return false;
    }
    printf("%s: the crowd at %u places (a place has %u thread records); a spinning thread beside A.5's replicator "
           "at %u places\n",
           name, static_cast<u32>(crowds.size()), w.place(crowds[0].place).os.thread_capacity(),
           static_cast<u32>(spins.size()));
    fflush(stdout);
    u64 given = 0;
    u32 filled = 0;
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (w.tick_count() == kFill) {
            for (usize k = 0; k < crowds.size(); ++k)
                if (crowds[k].ended == ~0ull) {
                    crowds[k].given = fill_place(w.place(crowds[k].place), crowds[k].holder);
                    given += crowds[k].given;
                    ++filled;
                }
            printf("  tick %-7llu the %u crowds alive given %.1f KB each, their places now under a twentieth free\n",
                   static_cast<ull>(w.tick_count()), filled, filled ? given / 1024.0 / filled : 0);
        }
        for (usize k = 0; k < crowds.size(); ++k) {
            Guarded& g = crowds[k];
            const Place& p = w.place(g.place);
            if (g.ended != ~0ull)
                continue;
            if (!lives(p, g.holder, g.uid))
                g.ended = w.tick_count();
            else if (p.os.program(g.holder).threads > g.most_threads)
                g.most_threads = p.os.program(g.holder).threads;
        }
        for (usize k = 0; k < spins.size(); ++k)
            if (spins[k].spin_ended == ~0ull && !lives(w.place(spins[k].place), spins[k].spin, spins[k].spin_uid))
                spins[k].spin_ended = w.tick_count();
        if (!reporting(w, run, run.ticks))
            continue;
        u32 alive = 0, spinning = 0, lax_there = 0;
        u64 threads = 0, kills = 0, killed = 0, faults = 0, programs_after = 0;
        double spare = 0;
        u32 after = 0;
        for (usize k = 0; k < crowds.size(); ++k) {
            const Place& p = w.place(crowds[k].place);
            kills += p.os.kills;
            spare += static_cast<double>(p.mem.free_bytes()) / p.mem.size();
            killed += p.os.killed_bytes;
            if (crowds[k].ended == ~0ull) {
                ++alive;
                threads += p.os.program(crowds[k].holder).threads;
            } else {
                ++after;
                programs_after += p.os.live_programs();
            }
        }
        for (usize k = 0; k < spins.size(); ++k) {
            spinning += spins[k].spin_ended == ~0ull ? 1 : 0;
            lax_there += members_at(obs, spins[k].lax, spins[k].place) ? 1 : 0;
            faults += w.place(spins[k].place).os.watchdog_faults;
        }
        printf("  tick %-7llu crowds alive %u (%llu threads in them), their places %.2f free, the killer's kills "
               "there %llu (%llu KB); programs at the %u places whose crowd has gone %.1f each; spinners alive "
               "%u, the replicator's lineage at %u of their places, the watchdog's faults there %llu; deaths by "
               "the watchdog %llu, by the killer %llu, in the world\n",
               static_cast<ull>(w.tick_count()), alive, static_cast<ull>(threads),
               crowds.size() ? spare / static_cast<double>(crowds.size()) : 0, static_cast<ull>(kills),
               static_cast<ull>(killed / 1024), after, after ? static_cast<double>(programs_after) / after : 0,
               spinning, lax_there, static_cast<ull>(faults),
               static_cast<ull>(obs.deaths_by_cause[static_cast<u32>(DeathCause::watchdog)]),
               static_cast<ull>(obs.deaths_by_cause[static_cast<u32>(DeathCause::oom)]));
        fflush(stdout);
    }
    // How long the crowds lasted, and who leads their places now.
    Array<u64> ended;
    u32 by_cause[5] = {}, empty = 0, most = 0;
    for (usize k = 0; k < crowds.size(); ++k) {
        most = crowds[k].most_threads > most ? crowds[k].most_threads : most;
        if (crowds[k].ended == ~0ull)
            continue;
        ended.push(crowds[k].ended);
        u32 count = 0;
        const u64 lead = leader_at(w, obs, crowds[k].place, &count);
        const LineageRec* L = lead ? obs.lineage(lead) : nullptr;
        if (L)
            ++by_cause[static_cast<u32>(L->cause)];
        else
            ++empty;
    }
    if (ended.size())
        qsort(ended.data(), ended.size(), sizeof(u64), cmp_u64);
    printf("  %s: the crowds reached %u threads at most; %u of %u ended", name, most,
           static_cast<u32>(ended.size()), static_cast<u32>(crowds.size()));
    if (ended.size())
        printf(", the first at tick %llu, the median at %llu, the last at %llu", static_cast<ull>(ended[0]),
               static_cast<ull>(ended[ended.size() / 2]), static_cast<ull>(ended[ended.size() - 1]));
    printf("; leading their places now, lineages begun by");
    for (u32 c = 0; c < 5; ++c)
        printf(" %s %u%s", cause_name(static_cast<BirthCause>(c)), by_cause[c], c < 4 ? "," : "");
    printf("; nobody at %u\n", empty);
    u32 spin_gone = 0, lax_left = 0;
    for (usize k = 0; k < spins.size(); ++k) {
        spin_gone += spins[k].spin_ended != ~0ull ? 1 : 0;
        lax_left += members_at(obs, spins[k].lax, spins[k].place) ? 1 : 0;
    }
    printf("  %s: spinners ended %u of %u; the replicator's lineage still at %u of their places; the programs the "
           "watchdog ended by size at birth (under 256 bytes, 1 KB, 4 KB, 16 KB, more):",
           name, spin_gone, static_cast<u32>(spins.size()), lax_left);
    for (u32 s = 0; s < kSizeBins; ++s)
        printf(" %llu", static_cast<ull>(obs.tally5.ended[1][s]));
    printf(", %llu of them of their place's leading lineage\n\n", static_cast<ull>(obs.tally5.ended_leader[1]));
    return true;
}

int fanout(Config& base, const Run& run) {
    return fanout_one(base, run, "the killer on (2,000 crowded barriers)", "2000", true) &&
                   fanout_one(base, run, "the killer off", "0", true) &&
                   fanout_one(base, run, "the killer on, the core error floors off", "2000", false)
               ? 0
               : 2;
}

// --- the current --------------------------------------------------------------------------------

struct Upstream {
    u32 up = 0;
    Array<u8> settled;
    Array<u32> hops;
    double dx = 1, dy = 0;
};

// The place furthest upstream on the largest landmass (by the heading at
// tick 0), and its neighbours there.
bool find_upstream(Config& base, const Run& run, Upstream* u) {
    static const char* const kProbe[] = {"current.strength", "1", "current.turn", "0", "supply.model", "place",
                                         nullptr};
    Config cfg;
    derive(base, &cfg, kProbe);
    World w;
    if (!build(w, cfg, run))
        return false;
    w.current().seek(0);
    u->dx = w.current().dx();
    u->dy = w.current().dy();
    const Map& m = w.map();
    const u32 n = w.place_count();
    Array<u32> size;
    size.resize(m.landmasses() + 1);
    for (u32 i = 0; i < n; ++i)
        ++size[m.landmass(i)];
    u32 largest = 0;
    for (u32 l = 0; l < size.size(); ++l)
        largest = size[l] > size[largest] ? l : largest;
    u32 up = ~0u;
    for (u32 i = 0; i < n; ++i)
        if (m.landmass(i) == largest &&
            (up == ~0u || m.x(i) * u->dx + m.y(i) * u->dy < m.x(up) * u->dx + m.y(up) * u->dy))
            up = i;
    u->up = up;
    u->settled.resize(n);
    u->settled[up] = 1;
    for (u32 j = 0; j < m.degree(up); ++j) {
        const MapLink& l = m.link(m.link_at(up, j));
        const u32 o = l.a == up ? l.b : l.a;
        if (m.landmass(o) == largest)
            u->settled[o] = 1;
    }
    hops_from(m, u->settled.data(), false, &u->hops);
    printf("the heading at tick 0: %.3f turns; the upstream place %u on landmass %u (%u places)\n",
           w.current().heading(), up, largest, size[largest]);
    return true;
}

bool current_one(Config& base, const Run& run, const char* name, const char* const* sets, const Upstream& u) {
    static const char* const kSets[] = {"os.strict_share", "0", "supply.model", "place", nullptr};
    Config cfg;
    derive(base, &cfg, kSets, sets);
    World w;
    if (!build(w, cfg, run))
        return false;
    Observer obs;
    watch_world(cfg, w, &obs);
    const Map& m = w.map();
    const u32 n = w.place_count();
    Array<u64> origins;
    for (u32 i = 0; i < n; ++i)
        if (u.settled[i]) {
            const u64 o = uid_of(w.place(i), hand(w.place(i), kLax, sizeof kLax, 14));
            if (o)
                origins.push(o);
        }
    const double up_at = m.x(u.up) * u.dx + m.y(u.up) * u.dy;
    printf("%s: A.5's replicator placed at %u places upstream\n", name, static_cast<u32>(origins.size()));
    fflush(stdout);
    Array<u64> first;
    first.resize(n);
    first.fill(~0ull);
    while (w.tick_count() < run.ticks) {
        w.tick();
        if (!reporting(w, run, run.ticks))
            continue;
        u32 reached = 0, island = 0, far_hops = 0, by_current = 0, half = 0, by_arrival = 0;
        double far = 0;
        for (u32 i = 0; i < n; ++i) {
            if (u.settled[i])
                continue;
            u32 there = 0;
            for (usize k = 0; k < origins.size() && !there; ++k)
                there = members_at(obs, origins[k], i);
            if (!there)
                continue;
            ++reached;
            // Founded by an arrival: a colonist some program sent there.
            for (usize k = 0; k < origins.size(); ++k) {
                const ColonyRec* c = obs.colony(origins[k], i);
                if (c && c->alive && c->colonist) {
                    ++by_arrival;
                    break;
                }
            }
            if (first[i] == ~0ull)
                first[i] = w.tick_count();
            half += first[i] > run.ticks / 2 ? 1 : 0;
            island += m.landmass(i) != m.landmass(u.up) ? 1 : 0;
            if (u.hops[i] != ~0u && u.hops[i] > far_hops)
                far_hops = u.hops[i];
            const double d = m.x(i) * u.dx + m.y(i) * u.dy - up_at;
            far = d > far ? d : far;
        }
        for (u32 c = 0; c < obs.colony_count(); ++c) {
            const ColonyRec& r = obs.colony_at(c);
            if (!r.alive || !r.by_current())
                continue;
            for (usize k = 0; k < origins.size(); ++k)
                by_current += r.origin == origins[k] ? 1 : 0;
        }
        u64 blocks = 0, runs = 0, made = 0;
        u32 landed_places = 0;
        for (u32 i = 0; i < n; ++i) {
            const Region& r = w.place(i).region;
            blocks += r.drift_blocks;
            runs += r.carried_runs;
            made += r.carried_made;
            landed_places += r.drift_blocks ? 1 : 0;
        }
        u32 static_landed = 0, static_dry = 0;
        for (u32 k = 0; k < obs.lineage_count(); ++k) {
            const LineageRec& L = obs.lineage_at(k);
            if (!L.alive || L.cause != BirthCause::static_bytes)
                continue;
            if (w.place(L.place).region.drift_blocks)
                ++static_landed;
            else
                ++static_dry;
        }
        printf("  tick %-7llu heading %.3f; the settlers' lineages at %u places beyond them (%u by the current "
               "alone, %u founded by an arrival), %u data links out at most and %.3f of the map's height "
               "downstream, %u on another landmass%s; blocks landed %llu at %u places, static runs in them %llu, "
               "programs made %llu; lineages begun by static and alive, at places blocks landed on %u (%.2f each) "
               "and at the rest %u "
               "(%.2f each)",
               static_cast<ull>(w.tick_count()), w.current().heading(), reached, by_current, by_arrival, far_hops,
               far, island,
               w.drifting() ? "" : " (the current off)", static_cast<ull>(blocks), landed_places,
               static_cast<ull>(runs), static_cast<ull>(made), static_landed,
               landed_places ? static_cast<double>(static_landed) / landed_places : 0, static_dry,
               n > landed_places ? static_cast<double>(static_dry) / (n - landed_places) : 0);
        if (half)
            printf("; first reached after tick %llu: %u", static_cast<ull>(run.ticks / 2), half);
        printf("\n");
        fflush(stdout);
    }
    printf("\n");
    return true;
}

int current(Config& base, const Run& run) {
    Upstream u;
    if (!find_upstream(base, run, &u))
        return 2;
    char season[32];
    snprintf(season, sizeof season, "%llu", static_cast<ull>(run.ticks));
    static const char* const kOne[] = {"current.strength", "1", "current.turn", "0", nullptr};
    static const char* const kTen[] = {"current.strength", "10", "current.turn", "0", nullptr};
    static const char* const kOff[] = {"current.strength", "0", nullptr};
    const char* const turning[] = {"current.strength", "10", "current.turn", "0.5", "current.season", season, nullptr};
    return current_one(base, run, "the current at 1 block a place a day, its heading fixed", kOne, u) &&
                   current_one(base, run, "at 10, its heading fixed", kTen, u) &&
                   current_one(base, run, "off", kOff, u) &&
                   current_one(base, run, "at 10, its heading turning over the run", turning, u)
               ? 0
               : 2;
}

} // namespace

int cmd_wear_worlds(Config& cfg, Str test) {
    Run run;
    run.ticks = key_u64(cfg, "worlds.ticks");
    run.every = key_u64(cfg, "worlds.report_every");
    run.threads = key_u32(cfg, "run.threads");
    if (test.eq("partition"))
        run.ticks = 11000; // its own timeline: cut, mended, cut, mended, the end
    if (!run.every)
        run.every = run.ticks ? run.ticks : 1;
    key_accept_known(cfg);
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    printf("nova2 worlds: %.*s (gate 5), seed %llu, %u places, %llu ticks, %u host threads\n\n",
           static_cast<int>(test.n), test.p, static_cast<ull>(key_u64(cfg, "world.seed")),
           key_u32(cfg, "world.places"), static_cast<ull>(run.ticks),
           run.threads ? run.threads : ThreadPool::hardware_threads());
    fflush(stdout);
    if (test.eq("scars"))
        return scars(cfg, run);
    if (test.eq("succession"))
        return succession(cfg, run);
    if (test.eq("storms"))
        return storms(cfg, run);
    if (test.eq("partition"))
        return partition(cfg, run);
    if (test.eq("respawning"))
        return respawning(cfg, run);
    if (test.eq("hammerer"))
        return hammerer(cfg, run);
    if (test.eq("injection"))
        return injection(cfg, run);
    if (test.eq("bootblocks"))
        return bootblocks(cfg, run);
    if (test.eq("options"))
        return options(cfg, run);
    if (test.eq("flaky"))
        return flaky(cfg, run);
    if (test.eq("islands"))
        return islands(cfg, run);
    if (test.eq("fanout"))
        return fanout(cfg, run);
    if (test.eq("current"))
        return current(cfg, run);
    fprintf(stderr, "nova2: worlds.test = %.*s is not one of gate 5's\n", static_cast<int>(test.n), test.p);
    return 2;
}

} // namespace nova2
