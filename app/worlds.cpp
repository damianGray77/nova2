// nova2 worlds: gate 3's test worlds at full size (build-plan-gate3.md step 7;
// R7: evidence about the mechanism, not about life).
//
//   nova2 worlds [config] [--set key=value]...
//
// The config is the world: gate 3's, as an origin test has it, with
// world.places at full size. worlds.test picks the test, which runs
// worlds.ticks ticks and reports every worlds.report_every:
//   sense:     a sensing migrant (traffic_world.h) placed by hand at the place
//              nearest the map's middle. Reported: the places its lineage has
//              reached (a colony founded there, 13.3), and at the end, by hops
//              from where it started, how many it reached and when; the
//              other lineages that founded colonies, and where; where
//              arrivals landed, and where its lineage was born beyond its
//              first place (its colonies' births); its
//              genomes against the migrant's (noise changes its copies); and
//              how full the places it reached are and what became of the
//              arrivals beyond its front;
//   blind:     a blind migrant placed there instead, reported the same: where
//              its copies land is 11.1's prediction;
//   busy:      the places with even ids kept busy by spinning threads, one
//              more than their cores, topped up after every tick, and a pushy
//              migrant at every other place. Reported: the arrivals that
//              reach the busy places and how many ever run there, in ticks
//              where a core could have come free and in the others, against
//              the free places (10.4's hypothesis);
//   nightfall: step 5's traffic (traffic_world.h) at every place, with the
//              network device's threshold raised to 900 mV for this test
//              (kNightThreshold), so each place's device goes off for part of
//              every night, at the place's own phase, while its cores and
//              memory run on. Reported: the topology changes a tick, the
//              routes worked out and their cost, against the tick (11.3);
//   flood:     the world as it is, nothing placed (asked after
//              build-plan-gate3.md step 8's second pilot found port 0s
//              flooded). Every kLookEvery ticks it reads every place's port
//              0 queue and groups the arrivals waiting by their bytes.
//              Reported every worlds.report_every: how full the queues are,
//              the messages and arrivals, the runs started, static and
//              prompt, the cores' cycles each used, and the runs that
//              SENT; at the end the commonest arrivals waiting, and the
//              commonest code the runs that SENT ran from their start to the
//              SEND, each with its bytes disassembled.
// worlds.migrant_device picks the device the migrants' ALLOCs ask for: the
// one their landing spot names (the slowest, A.5), or any.
#include <cstdio>
#include <cstdlib> // qsort
#include <cstring>
#include <intrin.h> // __rdtsc: the routes' and the network step's timings are in its cycles

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/hashmap.h"
#include "nova2/world.h"
#include "arrivals.h"
#include "traffic_world.h"

namespace nova2 {

namespace {

constexpr u64 kNone = ~0ull;
const char* const kNightThreshold = "900"; // mV: the nightfall test's network device threshold

enum class Test : u8 { sense, blind, busy, nightfall, flood };

struct Run {
    Test test = Test::sense;
    bool any_device = false; // worlds.migrant_device
    u32 message_max = 0;     // net.message_max, for the flood test
    u64 ticks = 0, every = 0;
    u32 threads = 0;
};

// The place nearest the map's middle.
u32 middle_place(const Map& m) {
    u32 best = 0;
    double best_d = -1;
    for (u32 i = 0; i < m.place_count(); ++i) {
        const double dx = m.x(i) - m.width() / 2, dy = m.y(i) - 0.5;
        if (best_d < 0 || dx * dx + dy * dy < best_d) {
            best_d = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

// Each place's hops from `from` over the data links (~0: out of reach).
void hops_from(const Map& m, u32 from, Array<u32>* hops) {
    const u32 n = m.place_count();
    hops->resize(n);
    hops->fill(~0u);
    Array<u32> queue;
    queue.push(from);
    (*hops)[from] = 0;
    for (usize q = 0; q < queue.size(); ++q) {
        const u32 i = queue[q];
        for (u32 j = 0; j < m.degree(i); ++j) {
            const u32 o = m.other(m.link_at(i, j), i);
            if ((*hops)[o] == ~0u) {
                (*hops)[o] = (*hops)[i] + 1;
                queue.push(o);
            }
        }
    }
}

int by_u64(const void* a, const void* b) {
    const u64 x = *static_cast<const u64*>(a), y = *static_cast<const u64*>(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

struct Count {
    u64 n;
    u32 place;
};

int most_first(const void* a, const void* b) {
    const Count& x = *static_cast<const Count*>(a);
    const Count& y = *static_cast<const Count*>(b);
    if (x.n != y.n)
        return x.n > y.n ? -1 : 1;
    return x.place < y.place ? -1 : (x.place > y.place ? 1 : 0);
}

// How a genome stands to the migrant's code: shift 0 and the bytes that
// differ (at most 2 counted as near) if it lines up with the code; shift k
// (1 to 8) if it's the code from byte k on; ~0 otherwise.
struct GenomeKind {
    u32 shift, diffs;
};

GenomeKind genome_kind(const HandCode& code, const u8* g, u32 len) {
    u32 diffs = 0;
    for (u32 i = 0; i < code.n; ++i)
        diffs += i >= len || g[i] != code.b[i] ? 1 : 0;
    if (diffs <= 2)
        return GenomeKind{0, diffs};
    for (u32 k = 1; k <= 8; ++k) {
        bool same = len >= code.n - k;
        for (u32 i = 0; same && i + k < code.n; ++i)
            same = g[i] == code.b[i + k];
        if (same)
            return GenomeKind{k, 0};
    }
    return GenomeKind{~0u, 0};
}

// The sensing or blind migrant: placed at the middle place, its lineage
// followed through the observer's colonies.
int spread(World& w, Observer& obs, const Run& run) {
    const Map& m = w.map();
    const u32 n = w.place_count();
    const u32 start = middle_place(m);
    const bool blind = run.test == Test::blind;
    const HandCode code = migrant(blind ? MigrantKind::blind : MigrantKind::sense, run.any_device);
    const u64 origin = place_migrant(w.place(start), code);
    if (!origin || code.bad) {
        fprintf(stderr, "nova2: no room for the migrant at place %u\n", start);
        return 1;
    }
    Array<u32> hops;
    hops_from(m, start, &hops);
    u32 reachable = 0, far = 0;
    for (u32 i = 0; i < n; ++i)
        if (hops[i] != ~0u) {
            ++reachable;
            far = hops[i] > far ? hops[i] : far;
        }
    printf("the %s migrant (%u bytes, its ALLOC asking for %s) placed at place %u, near the map's middle (%s, %u "
           "links); %u places within reach, the farthest %u hops away\n",
           blind ? "blind" : "sensing", code.n, run.any_device ? "any device" : "its landing spot's device", start,
           w.place(start).mem.strict() ? "strict" : "lax", m.degree(start), reachable - 1, far);
    printf("its bytes:");
    for (u32 i = 0; i < code.n; ++i)
        printf(" %02x", code.b[i]);
    printf("\n\n");
    Array<u64> first;
    first.resize(n);
    first.fill(kNone);
    first[start] = 0;
    u32 reached = 0, seen = 0;
    u64 last = 0;
    const double t0 = clock_seconds();
    for (u64 t = 1; t <= run.ticks; ++t) {
        w.tick();
        if (t % run.every != 0 && t != run.ticks)
            continue;
        for (; seen < obs.colony_count(); ++seen) {
            const ColonyRec& C = obs.colony_at(seen);
            if (C.origin == origin && first[C.place] == kNone) {
                first[C.place] = C.first;
                last = C.first > last ? C.first : last;
                ++reached;
            }
        }
        const LineageRec* L = obs.lineage(origin);
        const NetCounts& nc = w.net().counts();
        const Arrivals a = arrivals(w);
        const double secs = clock_seconds() - t0;
        printf("  tick %7llu: %4u places reached, %6u of its programs alive, %7llu births; %8llu messages sent, "
               "%7llu landed at port 0s, %6llu made programs; %.0f s, %.0f ticks/s\n",
               static_cast<unsigned long long>(w.tick_count()), reached, L ? L->alive : 0,
               static_cast<unsigned long long>(L ? L->births : 0), static_cast<unsigned long long>(nc.sent),
               static_cast<unsigned long long>(a.landed), static_cast<unsigned long long>(a.made), secs,
               secs > 0 ? static_cast<double>(t) / secs : 0);
        fflush(stdout);
    }
    printf("\nat tick %llu:\n", static_cast<unsigned long long>(w.tick_count()));
    // How far, and how fast.
    Array<u64> ticks;
    for (u32 i = 0; i < n; ++i)
        if (i != start && first[i] != kNone)
            ticks.push(first[i]);
    if (ticks.size())
        qsort(ticks.data(), ticks.size(), sizeof(u64), by_u64);
    printf("  reached %u of the %u other places within reach (%.1f%%)", reached, reachable - 1,
           reachable > 1 ? 100.0 * reached / (reachable - 1) : 0);
    if (reached)
        printf(", the last at tick %llu", static_cast<unsigned long long>(last));
    printf("\n");
    const u32 shares[3] = {10, 50, 90};
    for (u32 k = 0; k < 3; ++k) {
        const usize want = (static_cast<usize>(reachable - 1) * shares[k] + 99) / 100;
        if (want && want <= ticks.size())
            printf("  %u%% of them reached by tick %llu\n", shares[k],
                   static_cast<unsigned long long>(ticks[want - 1]));
        else
            printf("  %u%% of them: not reached\n", shares[k]);
    }
    printf("  by hops from place %u: hops, places, reached, the first reached (tick), the median (tick)\n", start);
    Array<u64> at;
    for (u32 d = 1; d <= far; ++d) {
        at.clear();
        u32 places = 0;
        for (u32 i = 0; i < n; ++i)
            if (hops[i] == d) {
                ++places;
                if (first[i] != kNone)
                    at.push(first[i]);
            }
        if (at.size())
            qsort(at.data(), at.size(), sizeof(u64), by_u64);
        if (at.size())
            printf("    %3u %5u %5u %8llu %8llu\n", d, places, static_cast<u32>(at.size()),
                   static_cast<unsigned long long>(at[0]), static_cast<unsigned long long>(at[at.size() / 2]));
        else
            printf("    %3u %5u %5u        -        -\n", d, places, 0u);
    }
    // Other lineages that founded colonies (13.3), and where (asked after
    // build-plan-gate3.md step 7): a static run that SPAWNs begins a lineage
    // of its own, unless it started in an arrival's landed bytes, all as
    // they landed (13.2).
    Array<u8> other_at;
    other_at.resize(n);
    other_at.fill(0);
    for (u32 i = 0; i < obs.colony_count(); ++i)
        if (obs.colony_at(i).origin != origin)
            other_at[obs.colony_at(i).place] = 1;
    u32 others = 0, others_static = 0, others_alive = 0, also = 0, only_others = 0;
    for (u32 i = 0; i < obs.lineage_count(); ++i) {
        const LineageRec& L = obs.lineage_at(i);
        if (L.origin != origin && L.colonies) {
            ++others;
            others_static += L.cause == BirthCause::static_bytes ? 1 : 0;
            others_alive += L.alive;
        }
    }
    for (u32 i = 0; i < n; ++i)
        if (i != start && other_at[i]) {
            if (first[i] != kNone)
                ++also;
            else
                ++only_others;
        }
    const LineageRec* its = obs.lineage(origin);
    printf("  other lineages with colonies: %u (%u begun by a static run), %u of their programs alive against its %u; "
           "they reached %u of the places it reached and %u it didn't\n",
           others, others_static, others_alive, its ? its->alive : 0, also, only_others);
    // Where its copies landed.
    Array<Count> dest;
    u64 all = 0;
    for (u32 i = 0; i < n; ++i) {
        const u64 k = landed(w.place(i));
        all += k;
        if (k)
            dest.push(Count{k, i});
    }
    if (dest.size())
        qsort(dest.data(), dest.size(), sizeof(Count), most_first);
    printf("  arrivals landed at %u places' port 0s, %llu in all; the most:\n", static_cast<u32>(dest.size()),
           static_cast<unsigned long long>(all));
    u64 top = 0;
    for (usize k = 0; k < dest.size() && k < 10; ++k) {
        top += dest[k].n;
        const u32 h = hops[dest[k].place];
        printf("    place %5u (%u hops away): %llu (%.1f%%)\n", dest[k].place, h,
               static_cast<unsigned long long>(dest[k].n), all ? 100.0 * dest[k].n / all : 0);
    }
    if (dest.size() > 10)
        printf("    the top 10 took %.1f%%\n", all ? 100.0 * top / all : 0);
    // Its births beyond its first place, place by place: its colonies'
    // (13.3), the colonists and all born of them there. The arrivals above
    // include any other code's.
    Array<Count> own;
    u64 own_all = 0, at01 = 0;
    for (u32 c = 0; c < obs.colony_count(); ++c) {
        const ColonyRec& C = obs.colony_at(c);
        if (C.origin != origin || !C.births)
            continue;
        own_all += C.births;
        at01 += C.place < 2 ? C.births : 0;
        own.push(Count{C.births, C.place});
    }
    if (own.size())
        qsort(own.data(), own.size(), sizeof(Count), most_first);
    printf("  its births beyond its first place (its colonies'): %llu at %u places; %llu (%.1f%%) at places 0 and "
           "1; the most:\n",
           static_cast<unsigned long long>(own_all), static_cast<u32>(own.size()),
           static_cast<unsigned long long>(at01), own_all ? 100.0 * at01 / own_all : 0);
    top = 0;
    for (usize k = 0; k < own.size() && k < 10; ++k) {
        top += own[k].n;
        printf("    place %5u (%u hops away): %llu (%.1f%%)\n", own[k].place, hops[own[k].place],
               static_cast<unsigned long long>(own[k].n), own_all ? 100.0 * own[k].n / own_all : 0);
    }
    if (own.size() > 10)
        printf("    the top 10 took %.1f%%\n", own_all ? 100.0 * top / own_all : 0);
    // Its genomes (13.2: the bytes each birth's parent wrote): the migrant's
    // own, and the variants gate 3's noise made of it as it copied.
    u64 g_births = 0, g_orig = 0, g_near = 0, g_shift = 0, g_other = 0;
    Array<Count> gtop;
    for (u32 g = 0; g < obs.genome_count(); ++g) {
        const GenomeRec& G = obs.genome_at(g);
        if (G.state == GenomeState::free)
            continue;
        if (G.origin != origin)
            continue;
        g_births += G.count;
        gtop.push(Count{G.count, g});
        const GenomeKind k = genome_kind(code, obs.genome_bytes(G), G.len);
        (k.shift == 0 && k.diffs == 0 ? g_orig : k.shift == 0 ? g_near : k.shift != ~0u ? g_shift : g_other) += G.count;
    }
    if (gtop.size())
        qsort(gtop.data(), gtop.size(), sizeof(Count), most_first);
    printf("  its genomes: %u among %llu births: the migrant's own %llu (%.1f%%), the same with a byte or two "
           "changed %llu, a copy of it starting 1 to 8 bytes in %llu, others %llu; the most:\n",
           static_cast<u32>(gtop.size()), static_cast<unsigned long long>(g_births),
           static_cast<unsigned long long>(g_orig), g_births ? 100.0 * g_orig / g_births : 0,
           static_cast<unsigned long long>(g_near), static_cast<unsigned long long>(g_shift),
           static_cast<unsigned long long>(g_other));
    for (usize k = 0; k < gtop.size() && k < 5; ++k) {
        const GenomeRec& G = obs.genome_at(gtop[k].place);
        const GenomeKind g = genome_kind(code, obs.genome_bytes(G), G.len);
        printf("    %llu births (%.1f%%), %u bytes: ", static_cast<unsigned long long>(G.count),
               g_births ? 100.0 * G.count / g_births : 0, G.len);
        if (g.shift == 0 && g.diffs == 0)
            printf("the migrant's own\n");
        else if (g.shift == 0) {
            printf("the migrant's, %u %s changed:", g.diffs, g.diffs == 1 ? "byte" : "bytes");
            const u8* b = obs.genome_bytes(G);
            for (u32 i = 0; i < code.n; ++i)
                if (i >= G.len || b[i] != code.b[i])
                    printf(" byte %u %02x to %02x", i, code.b[i], i < G.len ? b[i] : 0);
            printf("\n");
        }
        else if (g.shift != ~0u)
            printf("a copy of the migrant starting %u bytes in\n", g.shift);
        else
            printf("another\n");
    }
    // How full the places it reached are: an arrival that runs makes nothing
    // if its ALLOC finds no room or its SPAWN no thread record.
    u64 threads = 0, free_kb = 0, runs = 0, made = 0;
    u32 full_threads = 0, full_memory = 0, most = 0, counted = 0;
    for (u32 i = 0; i < n; ++i) {
        if (first[i] == kNone)
            continue;
        const Place& p = w.place(i);
        ++counted;
        threads += p.os.live_threads();
        most = p.os.live_threads() > most ? p.os.live_threads() : most;
        full_threads += p.os.live_threads() == p.os.thread_capacity() ? 1 : 0;
        free_kb += p.mem.free_bytes() / 1024;
        full_memory += p.mem.free_bytes() < 1024 ? 1 : 0;
        runs += p.region.arrival_runs;
        made += p.region.arrival_made;
    }
    if (counted)
        printf("  the %u places reached and place %u: threads alive %.1f a place (the most %u, of %u records; %u "
               "places with every record taken), %.1f KB of memory free a place (%u with under 1 KB); %llu "
               "arrivals ran there, %llu made a program (%.1f%%)\n",
               counted - 1, start, static_cast<double>(threads) / counted, most, w.place(start).os.thread_capacity(),
               full_threads, static_cast<double>(free_kb) / counted, full_memory,
               static_cast<unsigned long long>(runs), static_cast<unsigned long long>(made),
               runs ? 100.0 * static_cast<double>(made) / runs : 0);
    // The front: the places reached with a neighbour not reached, and those
    // neighbours, as they stand at the end.
    Array<u8> beyond;
    beyond.resize(n);
    beyond.fill(0);
    u32 front = 0, nbeyond = 0, landed_at = 0, ran_at = 0, dark = 0;
    u64 f_threads = 0, f_idle = 0, b_landed = 0, b_runs = 0, b_made = 0, b_neither = 0, b_dropped = 0, b_idle = 0;
    u64 b_programs = 0, b_free = 0, b_preempted = 0, b_overwritten = 0;
    Arrivals b_all;
    u32 b_no_page = 0;
    for (u32 i = 0; i < n; ++i) {
        if (first[i] == kNone)
            continue;
        bool edge = false;
        for (u32 j = 0; j < m.degree(i); ++j) {
            const u32 o = m.other(m.link_at(i, j), i);
            if (first[o] != kNone)
                continue;
            edge = true;
            if (beyond[o])
                continue;
            beyond[o] = 1;
            ++nbeyond;
            const Place& q = w.place(o);
            b_landed += landed(q);
            b_runs += q.region.arrival_runs;
            b_made += q.region.arrival_made;
            b_neither += q.region.arrival_neither;
            b_preempted += q.region.arrival_preempted;
            b_overwritten += q.region.arrival_overwritten;
            b_dropped += q.region.arrival_dropped;
            b_all.add(q);
            landed_at += landed(q) ? 1 : 0;
            ran_at += q.region.arrival_runs ? 1 : 0;
            dark += q.network_state.on ? 0 : 1;
            b_programs += q.os.live_programs();
            b_free += q.mem.free_bytes();
            b_no_page += q.mem.free_bytes() < q.mem.page_size() ? 1 : 0;
            for (u32 c = 0; c < q.core_count(); ++c)
                b_idle += q.cores[c].working && !q.cores[c].thread ? 1 : 0;
        }
        if (!edge)
            continue;
        const Place& p = w.place(i);
        ++front;
        f_threads += p.os.live_threads();
        for (u32 c = 0; c < p.core_count(); ++c)
            f_idle += p.cores[c].working && !p.cores[c].thread ? 1 : 0;
    }
    if (front)
        printf("  the front: %u places reached beside %u not reached; at the front %.1f threads alive and %.2f idle "
               "cores a place; beyond it %.2f idle cores a place (%u with the network device off), and %llu arrivals "
               "landed (at %u of them): %llu ran (at %u), %llu made a program, %llu did neither; %llu dropped there "
               "for want of room\n",
               front, nbeyond, static_cast<double>(f_threads) / front, static_cast<double>(f_idle) / front,
               nbeyond ? static_cast<double>(b_idle) / nbeyond : 0, dark, static_cast<unsigned long long>(b_landed),
               landed_at, static_cast<unsigned long long>(b_runs), ran_at, static_cast<unsigned long long>(b_made),
               static_cast<unsigned long long>(b_neither), static_cast<unsigned long long>(b_dropped));
    if (nbeyond)
        printf("  beyond the front: %.1f programs alive and %.1f KB of memory free a place; %u with less free than "
               "a page (%u bytes); of the arrivals that did neither there, %llu had their core taken by a ready "
               "thread (10.2), %llu their bytes written over before they ended\n",
               static_cast<double>(b_programs) / nbeyond, static_cast<double>(b_free) / 1024.0 / nbeyond, b_no_page,
               w.place(start).mem.page_size(), static_cast<unsigned long long>(b_preempted),
               static_cast<unsigned long long>(b_overwritten));
    if (nbeyond)
        print_rest("beyond the front:", b_all);
    print_network(w.net().counts(), w.net().in_flight());
    print_arrivals(arrivals(w));
    return 0;
}

// A busy place: its spinning threads, one more than its cores, so a thread is
// always ready when one ends or its turn does. Gate 3's noise (7.5: a result
// goes wrong about once in 10,000 at the floor) sends a spinner's jump astray
// within a tick or two, and a cut-out or a cold start ends them too. One sent
// astray can also live on and leave its core (a SLEEP, a wait, or a pin to
// one kind of core): the test ends any that isn't at its jump, unpinned and
// running or ready ("stalled", cause exit). After every tick it
// places new ones for those gone: the cores are kept busy by hand-placed
// threads. A core can come free for an arrival only in a tick where two or
// more went, counting any it was short of from the start (one leaves a
// thread for every core), or the place cold-started: an "open" tick. An
// arrival run in any other tick would be one run while every core was busy.
struct BusyPlace {
    u32 thread[65]; // the spinners' records
    u32 id[65];     // and their ids
    u32 ip[65];     // and their jumps' addresses
    u32 threads = 0;
    u64 lost = 0;        // spinners that ended or stalled (and were replaced)
    u64 stalled = 0;     // of those, the ones that stalled
    u64 short_ticks = 0; // ticks it ended short of cores + 1 (no room for a new one)
    u64 open_ticks = 0;
    u64 runs_open = 0, runs_shut = 0; // arrivals run in open ticks, and in the others
    u64 cold = 0, runs = 0;           // its cold starts and arrival runs at the last look
};

// Thread t's IP: on its core, or saved at the base of its stack (A.3).
u32 ip_of(const Place& p, u32 t) {
    const Thread& th = p.os.thread(t);
    if (th.core)
        return p.cores[th.core - 1].x.r.ip;
    const u8* s = p.mem.bytes() + th.stack + 16; // little-endian, as memory is
    return static_cast<u32>(s[0]) | static_cast<u32>(s[1]) << 8 | static_cast<u32>(s[2]) << 16 |
           static_cast<u32>(s[3]) << 24;
}

// Places spinners until `p` has its cores + 1 (at most 65). False if it ran
// out of room.
bool top_up(Place& p, BusyPlace& b, const HandCode& spin) {
    while (b.threads <= p.core_count() && b.threads < 65) {
        const u32 t = place_hand_code(p, spin);
        if (t == ~0u)
            return false;
        b.thread[b.threads] = t;
        b.ip[b.threads] = ip_of(p, t);
        b.id[b.threads++] = p.os.thread(t).id;
    }
    return true;
}

// Forgets the spinners that ended, and ends and forgets those that stalled;
// how many went.
u32 drop_gone(Place& p, BusyPlace& b) {
    u32 kept = 0;
    for (u32 k = 0; k < b.threads; ++k) {
        const Thread& th = p.os.thread(b.thread[k]);
        if (th.state == ThreadState::free || th.id != b.id[k])
            continue;
        if ((th.state != ThreadState::running && th.state != ThreadState::ready) || th.pin != kPinAny ||
            ip_of(p, b.thread[k]) != b.ip[k]) {
            p.os.end_thread(b.thread[k], DeathCause::exit);
            ++b.stalled;
            continue;
        }
        b.thread[kept] = b.thread[k];
        b.ip[kept] = b.ip[k];
        b.id[kept++] = b.id[k];
    }
    const u32 ended = b.threads - kept;
    b.threads = kept;
    return ended;
}

int busy(World& w, Observer& obs, const Run& run) {
    const u32 n = w.place_count();
    Array<BusyPlace> places;
    places.resize(n);
    places.fill(BusyPlace{});
    HandCode spin;
    spin.imm(kJmp, 0);
    const HandCode pushy = migrant(MigrantKind::pushy, run.any_device);
    u32 nbusy = 0, nfree = 0, short_of = 0;
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        if (i % 2 == 0) {
            short_of += top_up(p, places[i], spin) ? 0 : 1;
            ++nbusy;
        } else {
            nfree += place_migrant(p, pushy) ? 1 : 0;
        }
    }
    printf("%u places kept busy (even ids: spinning threads, one more than their cores, topped up after every "
           "tick; %u short of that), a pushy migrant at %u of the others (its ALLOC asking for %s)\n\n",
           nbusy, short_of, nfree, run.any_device ? "any device" : "its landing spot's device");
    const double t0 = clock_seconds();
    for (u64 t = 1; t <= run.ticks; ++t) {
        w.tick();
        for (u32 i = 0; i < n; i += 2) {
            Place& p = w.place(i);
            BusyPlace& b = places[i];
            const u32 want = p.core_count() < 64 ? p.core_count() + 1 : 65;
            const u32 missing = want - b.threads; // no room for them after the last tick
            const u32 ended = drop_gone(p, b);
            const bool open = ended + missing >= 2 || p.cold_starts != b.cold;
            const u64 ran = p.region.arrival_runs - b.runs;
            b.lost += ended;
            b.open_ticks += open ? 1 : 0;
            (open ? b.runs_open : b.runs_shut) += ran;
            b.short_ticks += top_up(p, b, spin) ? 0 : 1;
            b.cold = p.cold_starts;
            b.runs = p.region.arrival_runs;
        }
        if (t % run.every != 0 && t != run.ticks)
            continue;
        u64 bl = 0, open = 0, shut = 0, lost = 0, fl = 0, fruns = 0;
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            if (i % 2 == 0) {
                bl += landed(p);
                open += places[i].runs_open;
                shut += places[i].runs_shut;
                lost += places[i].lost;
            } else {
                fl += landed(p);
                fruns += p.region.arrival_runs;
            }
        }
        const double secs = clock_seconds() - t0;
        printf("  tick %7llu: busy places: %8llu arrivals landed, %6llu ran in open ticks, %llu in the others "
               "(%9llu spinners replaced); free places: %8llu landed, %8llu ran; %.0f s\n",
               static_cast<unsigned long long>(w.tick_count()), static_cast<unsigned long long>(bl),
               static_cast<unsigned long long>(open), static_cast<unsigned long long>(shut),
               static_cast<unsigned long long>(lost), static_cast<unsigned long long>(fl),
               static_cast<unsigned long long>(fruns), secs);
        fflush(stdout);
    }
    printf("\nat tick %llu:\n", static_cast<unsigned long long>(w.tick_count()));
    u32 got = 0, ran_open = 0, ran_shut = 0, never_open = 0, went_short = 0;
    u64 open_ticks = 0, landed_busy = 0, runs_busy = 0, queued = 0, dropped = 0, lost = 0, stalled = 0;
    for (u32 i = 0; i < n; i += 2) {
        const Place& p = w.place(i);
        const BusyPlace& b = places[i];
        got += landed(p) ? 1 : 0;
        ran_open += b.runs_open ? 1 : 0;
        ran_shut += b.runs_shut ? 1 : 0;
        never_open += b.open_ticks ? 0 : 1;
        went_short += b.short_ticks ? 1 : 0;
        open_ticks += b.open_ticks;
        landed_busy += landed(p);
        runs_busy += p.region.arrival_runs;
        queued += p.region.arrivals_waiting();
        dropped += p.region.arrival_dropped;
        lost += b.lost;
        stalled += b.stalled;
    }
    printf("  of %u busy places, %u had arrivals land (%llu in all: %llu ran, %llu dropped for want of room, %llu "
           "still waiting)\n",
           nbusy, got, static_cast<unsigned long long>(landed_busy), static_cast<unsigned long long>(runs_busy),
           static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(queued));
    printf("  %u ran an arrival in an open tick (two or more spinners gone or missing, or a cold start), %u in a "
           "tick with every core kept busy\n",
           ran_open, ran_shut);
    printf("  open ticks: %llu of %llu place-ticks (%.2f%%); %u busy places never had one; %llu spinners replaced "
           "(%llu of them stalled), %.2f a place a tick; %u places at some point short of room for them\n",
           static_cast<unsigned long long>(open_ticks), static_cast<unsigned long long>(nbusy * run.ticks),
           nbusy ? 100.0 * static_cast<double>(open_ticks) / (static_cast<double>(nbusy) * run.ticks) : 0, never_open,
           static_cast<unsigned long long>(lost), static_cast<unsigned long long>(stalled),
           nbusy ? static_cast<double>(lost) / (static_cast<double>(nbusy) * run.ticks) : 0, went_short);
    printf("  deaths in the world, by cause (the stalled spinners under exit): exit %llu, fault %llu, cut-out %llu, "
           "device failed %llu, memory lost %llu, cold start %llu, heat %llu, out of memory %llu, watchdog %llu\n",
           static_cast<unsigned long long>(obs.deaths_by_cause[0]),
           static_cast<unsigned long long>(obs.deaths_by_cause[1]),
           static_cast<unsigned long long>(obs.deaths_by_cause[2]),
           static_cast<unsigned long long>(obs.deaths_by_cause[3]),
           static_cast<unsigned long long>(obs.deaths_by_cause[4]),
           static_cast<unsigned long long>(obs.deaths_by_cause[5]),
           static_cast<unsigned long long>(obs.deaths_by_cause[6]),
           static_cast<unsigned long long>(obs.deaths_by_cause[7]),
           static_cast<unsigned long long>(obs.deaths_by_cause[8]));
    print_network(w.net().counts(), w.net().in_flight());
    print_arrivals(arrivals(w));
    return 0;
}

int nightfall(World& w, const Run& run) {
    const u32 n = w.place_count();
    u32 placed = 0;
    for (u32 i = 0; i < n; ++i)
        placed += place_traffic(w.place(i), n, i);
    printf("step 5's traffic at every place (%u programs placed), the network device's threshold %s mV\n\n", placed,
           kNightThreshold);
    Array<u8> was;
    was.resize(n);
    for (u32 i = 0; i < n; ++i)
        was[i] = w.place(i).network_state.on ? 1 : 0;
    Routes& r = w.net().routes();
    u64 changes0 = r.changes, cols0 = r.columns, cyc0 = r.cycles, net0 = w.net_cycles;
    u64 switches = 0, off_sum = 0, changed_ticks = 0, last_changes = r.changes;
    u32 off_most = 0, off_least = n;
    const double t0 = clock_seconds();
    const u64 c0 = __rdtsc();
    double block_t = t0;
    u64 block_c = c0;
    for (u64 t = 1; t <= run.ticks; ++t) {
        w.tick();
        u32 off = 0;
        for (u32 i = 0; i < n; ++i) {
            const u8 on = w.place(i).network_state.on ? 1 : 0;
            off += on ? 0 : 1;
            switches += on != was[i] ? 1 : 0;
            was[i] = on;
        }
        off_sum += off;
        off_most = off > off_most ? off : off_most;
        off_least = off < off_least ? off : off_least;
        changed_ticks += r.changes != last_changes ? 1 : 0;
        last_changes = r.changes;
        if (t % run.every != 0 && t != run.ticks)
            continue;
        const double now = clock_seconds();
        const u64 c = __rdtsc();
        const double ms = c > block_c ? 1e3 * (now - block_t) / static_cast<double>(c - block_c) : 0; // a cycle
        const u64 ticks = t % run.every ? t % run.every : run.every;
        const double per = 1.0 / static_cast<double>(ticks);
        const u64 cols = r.columns - cols0;
        printf("  tick %7llu: %.1f devices off on average; %.2f topology changes a tick; %.1f columns worked out a "
               "tick, %.0f us each; routes %.2f ms of the threads' time a tick; the network step %.2f ms of a %.2f ms "
               "tick\n",
               static_cast<unsigned long long>(w.tick_count()), static_cast<double>(off_sum) * per,
               static_cast<double>(r.changes - changes0) * per, static_cast<double>(cols) * per,
               cols ? 1e3 * ms * static_cast<double>(r.cycles - cyc0) / static_cast<double>(cols) : 0,
               ms * static_cast<double>(r.cycles - cyc0) * per, ms * static_cast<double>(w.net_cycles - net0) * per,
               1e3 * (now - block_t) * per);
        fflush(stdout);
        changes0 = r.changes;
        cols0 = r.columns;
        cyc0 = r.cycles;
        net0 = w.net_cycles;
        off_sum = 0;
        block_t = now;
        block_c = c;
    }
    const double secs = clock_seconds() - t0;
    printf("\nover %llu ticks (%.0f s, %.1f ticks/s): %llu device switches (off or on), %.2f a tick; the topology "
           "changed in %llu ticks (%.1f%%); devices off at once: %u to %u of %u\n",
           static_cast<unsigned long long>(run.ticks), secs, secs > 0 ? static_cast<double>(run.ticks) / secs : 0,
           static_cast<unsigned long long>(switches), static_cast<double>(switches) / static_cast<double>(run.ticks),
           static_cast<unsigned long long>(changed_ticks), 100.0 * static_cast<double>(changed_ticks) / run.ticks,
           off_least, off_most, n);
    printf("  routes: %llu topology changes, %llu columns worked out in all; %.1f MB of columns kept at the end\n",
           static_cast<unsigned long long>(r.changes), static_cast<unsigned long long>(r.columns),
           r.kept_bytes() / (1024.0 * 1024.0));
    print_network(w.net().counts(), w.net().in_flight());
    return 0;
}

constexpr u64 kLookEvery = 1000; // ticks between the flood test's looks at the queues
constexpr u32 kSeenBytes = 64;   // of each arrival's bytes, the flood test keeps this many

// An arrival's bytes as the flood test saw them waiting at port 0s.
struct Seen {
    u64 hash;
    u64 count;     // the times seen, over every look
    u64 places;    // a bit for each place it was seen at (places 0 to 63)
    u64 from_runs; // the times its sender was an idle run (no program)
    u32 len;
    u8 b[kSeenBytes];
};

// The code an idle run ran from its start to a SEND (its bytes, when the
// SEND came at most kSeenBytes on from the start, and straight after it),
// as the flood test caught it: how often, how many from prompt runs, to a
// port 0, from its own start (C), and the bytes sent.
struct Sender {
    u64 hash;
    u64 count, prompt, port0, own, bytes;
    u32 len; // 0: the SEND came elsewhere (further on, or behind the start)
    u8 b[kSeenBytes];
};

// Every core whose idle run has just begun a SEND's wait (`until` holds each
// core's last wait), into `senders`.
void catch_senders(World& w, HashMap<u32>& index, Array<Sender>& senders, Array<u64>& until, u32 limit) {
    u32 k = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        for (u32 c = 0; c < p.core_count(); ++c, ++k) {
            if (k >= until.size())
                until.push(0);
            const Core& core = p.cores[c];
            if (!core.idle_run || !core.run_sent || core.net_until <= w.tick_count() || core.net_until == until[k])
                continue;
            until[k] = core.net_until;
            const u32 start = p.mem.region_base() + p.region.state(c).started_at;
            const Regs& r = core.x.r;
            const u32 n = r.ip > start && r.ip - start <= kSeenBytes ? r.ip - start : 0;
            const u64 h = n ? hash_bytes(p.mem.bytes() + start, n) : 0;
            bool fresh = false;
            u32& at = index.get_or_insert(h, &fresh);
            if (fresh) {
                at = static_cast<u32>(senders.size());
                Sender s{};
                s.hash = h;
                s.len = n;
                memcpy(s.b, p.mem.bytes() + start, n);
                senders.push(s);
            }
            Sender& s = senders[at];
            ++s.count;
            s.prompt += core.run_cause == BirthCause::arrival ? 1 : 0;
            s.port0 += r.b % 16 == 0 ? 1 : 0;
            s.own += r.c == start ? 1 : 0;
            s.bytes += r.d < limit ? r.d : limit;
        }
    }
}

// The counts the flood test reports by differences between its reports.
struct FloodSums {
    u64 started = 0, prompt = 0, static16 = 0, arrival16 = 0, static_sent = 0, travellers = 0;
    u64 landed = 0, dropped = 0, sent = 0, sent_bytes = 0;
};

FloodSums flood_sums(World& w) {
    FloodSums s;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Region& g = w.place(i).region;
        s.started += g.started;
        s.prompt += g.prompt_runs;
        s.static16 += g.static_cycles16;
        s.arrival16 += g.arrival_cycles16;
        s.static_sent += g.static_sent;
        s.travellers += g.travellers;
        s.landed += landed(w.place(i));
        s.dropped += g.arrival_dropped;
    }
    s.sent = w.net().counts().sent;
    s.sent_bytes = w.net().counts().sent_bytes;
    return s;
}

// Every place's port 0 queue now, into `seen`; the arrivals waiting, and
// the queues with less room than a message limit.
void look(World& w, HashMap<u32>& index, Array<Seen>& seen, u32 limit, u64* waiting, u32* tight) {
    *waiting = 0;
    *tight = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Region& g = w.place(i).region;
        *tight += g.settings().queue_bytes - g.queued_bytes() < limit ? 1 : 0;
        for (u32 k = 0; k < g.queued(); ++k) {
            const Arrival& a = g.waiting(k);
            if (a.cause != BirthCause::arrival)
                continue;
            ++*waiting;
            const u8* b = g.waiting_bytes(k);
            const u64 h = hash_bytes(b, a.len);
            bool fresh = false;
            u32& at = index.get_or_insert(h, &fresh);
            if (fresh) {
                at = static_cast<u32>(seen.size());
                Seen s{};
                s.hash = h;
                s.len = a.len;
                memcpy(s.b, b, a.len < kSeenBytes ? a.len : kSeenBytes);
                seen.push(s);
            }
            Seen& s = seen[at];
            ++s.count;
            s.places |= i < 64 ? 1ull << i : 0;
            s.from_runs += a.sender ? 0 : 1;
        }
    }
}

int flood(World& w, const Run& run) {
    typedef unsigned long long ull;
    const u32 n = w.place_count();
    printf("the world as it is, nothing placed; every place's port 0 queue read every %llu ticks\n\n",
           static_cast<ull>(kLookEvery));
    HashMap<u32> index, sindex;
    Array<Seen> seen;
    Array<Sender> senders;
    Array<u64> until;
    u64 sightings = 0, waiting = 0;
    u32 tight = 0;
    FloodSums last;
    const double t0 = clock_seconds();
    for (u64 t = 1; t <= run.ticks; ++t) {
        w.tick();
        catch_senders(w, sindex, senders, until, run.message_max);
        if (t % kLookEvery == 0 || t == run.ticks) {
            look(w, index, seen, run.message_max, &waiting, &tight);
            sightings += waiting;
        }
        if (t % run.every != 0 && t != run.ticks)
            continue;
        const FloodSums s = flood_sums(w);
        const u64 sent = s.sent - last.sent, landed = s.landed - last.landed;
        const u64 started = s.started - last.started, prompt = s.prompt - last.prompt;
        const u64 c_static = s.static16 - last.static16, c_arr = s.arrival16 - last.arrival16;
        printf("  tick %7llu: %5llu arrivals waiting, %u of %u queues with less room than a message limit (%u "
               "bytes); since the last: %llu messages (%.0f bytes each), %llu arrivals landed, %.1f%% dropped; runs "
               "started: %llu static, %llu prompt; their cycles %.1f%% static, %.1f%% arrivals'; %llu static runs "
               "SENT, %llu arrivals only SENT; %.0f s\n",
               static_cast<ull>(w.tick_count()), static_cast<ull>(waiting), tight, n, run.message_max,
               static_cast<ull>(sent), sent ? static_cast<double>(s.sent_bytes - last.sent_bytes) / sent : 0,
               static_cast<ull>(landed),
               landed ? 100.0 * static_cast<double>(s.dropped - last.dropped) / landed : 0,
               static_cast<ull>(started - prompt), static_cast<ull>(prompt),
               c_static + c_arr ? 100.0 * c_static / (c_static + c_arr) : 0,
               c_static + c_arr ? 100.0 * c_arr / (c_static + c_arr) : 0,
               static_cast<ull>(s.static_sent - last.static_sent), static_cast<ull>(s.travellers - last.travellers),
               clock_seconds() - t0);
        fflush(stdout);
        last = s;
    }
    Array<Count> order;
    for (usize k = 0; k < seen.size(); ++k)
        order.push(Count{seen[k].count, static_cast<u32>(k)});
    if (order.size())
        qsort(order.data(), order.size(), sizeof(Count), most_first);
    printf("\nat tick %llu: %u distinct arrivals seen waiting, in %llu sightings; the commonest (20 at most):\n",
           static_cast<ull>(w.tick_count()), static_cast<u32>(seen.size()), static_cast<ull>(sightings));
    for (usize k = 0; k < order.size() && k < 20; ++k) {
        const Seen& s = seen[order[k].place];
        printf("\n  %llu sightings (%.1f%%), %u bytes, at %u places; sent by an idle run %.0f%% of the times\n    ",
               static_cast<ull>(s.count), sightings ? 100.0 * s.count / sightings : 0, s.len,
               static_cast<u32>(__popcnt64(s.places)), s.count ? 100.0 * s.from_runs / s.count : 0);
        const u32 shown = s.len < kSeenBytes ? s.len : kSeenBytes;
        for (u32 i = 0; i < shown; ++i)
            printf("%02x ", s.b[i]);
        TextBuf text;
        disassemble(s.b, shown, &text);
        printf("%s\n    %s\n", shown < s.len ? "..." : "", shown ? text.c_str() : "(empty)");
    }
    order.clear();
    Sender all{};
    for (usize k = 0; k < senders.size(); ++k) {
        order.push(Count{senders[k].count, static_cast<u32>(k)});
        all.count += senders[k].count;
        all.prompt += senders[k].prompt;
        all.port0 += senders[k].port0;
        all.own += senders[k].own;
        all.bytes += senders[k].bytes;
    }
    const u64 caught = all.count;
    if (order.size())
        qsort(order.data(), order.size(), sizeof(Count), most_first);
    const double per = caught ? 100.0 / static_cast<double>(caught) : 0;
    printf("\nthe SENDs idle runs made: %llu caught, %.1f%% of them by prompt runs, %.1f%% to a port 0, %.1f%% from "
           "the run's own start, %.0f bytes each; the code each ran from its start to the SEND, %u kinds; the "
           "commonest (20 at most):\n",
           static_cast<ull>(caught), per * all.prompt, per * all.port0, per * all.own,
           caught ? static_cast<double>(all.bytes) / caught : 0, static_cast<u32>(senders.size()));
    for (usize k = 0; k < order.size() && k < 20; ++k) {
        const Sender& s = senders[order[k].place];
        const double c = static_cast<double>(s.count);
        printf("\n  %llu (%.1f%%): %.0f%% by prompt runs, %.0f%% to a port 0, %.0f%% from the run's own start, %.0f "
               "bytes each\n    ",
               static_cast<ull>(s.count), caught ? 100.0 * c / caught : 0, 100.0 * s.prompt / c,
               100.0 * s.port0 / c, 100.0 * s.own / c, s.bytes / c);
        if (!s.len) {
            printf("(the SEND further than %u bytes on from the start, or behind it)\n", kSeenBytes);
            continue;
        }
        for (u32 i = 0; i < s.len; ++i)
            printf("%02x ", s.b[i]);
        TextBuf text;
        disassemble(s.b, s.len, &text);
        printf("\n    %s\n", text.c_str());
    }
    return 0;
}

} // namespace

int cmd_worlds(Config& cfg) {
    Run run;
    const Str test = key_choice(cfg, "worlds.test");
    if (test.eq("band") || test.eq("heat") || test.eq("water") || test.eq("sleeper") || test.eq("rejection") ||
        test.eq("numbers") || test.eq("classes"))
        return cmd_power_worlds(cfg, test);
    if (test.eq("scars") || test.eq("succession") || test.eq("storms") || test.eq("partition") ||
        test.eq("respawning") || test.eq("hammerer") || test.eq("injection") || test.eq("bootblocks") ||
        test.eq("options") || test.eq("flaky") || test.eq("islands") || test.eq("fanout") || test.eq("current"))
        return cmd_wear_worlds(cfg, test);
    // Gate 3's test worlds are measured on gate 3's world: a supply at each
    // place, no heat and no low-voltage interrupt, unless the config says.
    if (!cfg.has("supply.model"))
        cfg.set("supply.model", "place");
    if (!cfg.has("heat.on"))
        cfg.set("heat.on", "false");
    if (!cfg.has("power.low_voltage_margin"))
        cfg.set("power.low_voltage_margin", "0");
    run.test = test.eq("blind")       ? Test::blind
               : test.eq("busy")      ? Test::busy
               : test.eq("nightfall") ? Test::nightfall
               : test.eq("flood")     ? Test::flood
                                      : Test::sense;
    const char* const names[5] = {"sense", "blind", "busy", "nightfall", "flood"};
    const char* name = names[static_cast<u32>(run.test)];
    run.any_device = key_choice(cfg, "worlds.migrant_device").eq("any");
    run.ticks = key_u64(cfg, "worlds.ticks");
    run.every = key_u64(cfg, "worlds.report_every");
    run.threads = key_u32(cfg, "run.threads");
    run.message_max = key_u32(cfg, "net.message_max");
    if (run.test == Test::nightfall)
        cfg.set("hw.network.threshold", kNightThreshold);
    ObserverSettings os;
    os.sample_every = key_u32(cfg, "observe.sample_every");
    os.write_every = key_u32(cfg, "observe.write_every");
    os.genome_key_every = key_u32(cfg, "observe.genome_key_every");
    os.group_threshold = key_f64(cfg, "observe.group_threshold");
    os.min_genome = key_u32(cfg, "observe.min_genome");
    os.keep_generations = key_u32(cfg, "observe.keep_generations");
    os.lineage_generations = os.keep_generations; // a lineage with a row is one whose genomes could be kept
    os.day = key_u32(cfg, "supply.day");
    os.keep_facts = false;
    World w;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return 2;
    }
    key_accept_known(cfg);
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    const Map& m = w.map();
    if (!m.placed()) {
        fprintf(stderr, "nova2: the test worlds need a map (world.map)\n");
        return 2;
    }
    Observer obs;
    if (run.test != Test::nightfall) {
        obs.init(os);
        w.watch(&obs);
    }
    w.start_threads(run.threads);
    const Place& p0 = w.place(0);
    printf("nova2 worlds: %s, seed %llu, %u places on a %s map (%u landmasses, %u links, %u bridges), %u host "
           "threads; %u KB of memory and %u KB of disk a place; %llu ticks, the supply's day %llu ticks\n",
           name, static_cast<unsigned long long>(w.seed()), w.place_count(),
           m.raster_w() ? "landscape" : "grid or ring", m.landmasses(), m.link_count(), m.bridges(),
           run.threads ? run.threads : ThreadPool::hardware_threads(), p0.mem.size() / 1024, p0.os.disk_size() / 1024,
           static_cast<unsigned long long>(run.ticks), static_cast<unsigned long long>(key_u64(cfg, "supply.day")));
    printf("noise (7.5): the floors %g (P cores) and %g (E cores) a result, cosmic rays %g a bit a tick\n\n",
           key_f64(cfg, "noise.p_floor"), key_f64(cfg, "noise.e_floor"), key_f64(cfg, "noise.cosmic"));
    fflush(stdout);
    if (run.test == Test::busy)
        return busy(w, obs, run);
    if (run.test == Test::nightfall)
        return nightfall(w, run);
    if (run.test == Test::flood)
        return flood(w, run);
    return spread(w, obs, run);
}

} // namespace nova2
