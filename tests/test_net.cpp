// The network (design.md 11.3-11.5; build-plan-gate3.md step 3), on test
// maps and landscapes: a message's delay is the sum of its hops, each its
// queue's wait plus its size over the link's bandwidth, rounded up, plus the
// link's delay; a full queue drops the newcomer and keeps the rest;
// corruption's measured rate matches the setting, scaled by each link's
// length; a dark place is routed around once the routes are worked out
// again, and what was queued at it, waiting for it, or crossing to it is
// dropped and counted; the routes match a brute-force search; and in a
// world, a replicator's bytes sent to a place's port 0 run there as a
// prompt run, while each network device draws for its bytes.
#include <intrin.h> // __popcnt

#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/map.h"
#include "nova2/net.h"
#include "nova2/observe.h"
#include "nova2/rng.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

bool build_map(Map& m, u64 seed, u32 places, const char* text) {
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(text, &pe));
    MapSettings ms;
    read_map_settings(cfg, &ms);
    CHECK(cfg.ok());
    TextBuf why;
    return m.build(seed, places, ms, &why);
}

NetSettings net_settings(u32 queue_ticks, double corruption, u32 message_max = 1024) {
    NetSettings s;
    s.queue_ticks = queue_ticks;
    s.corruption = corruption;
    s.message_max = message_max;
    s.lifetime = 0; // gate 3's network: nothing waits (gate 5's is test_links.cpp's)
    return s;
}

// The place at the other end of s's link toward d, or ~0.
u32 hop(Network& n, const Map& m, u32 s, u32 d) {
    const u32 j = n.routes().next(s, d);
    return j == ~0u ? ~0u : m.other(m.link_at(s, j), s);
}

u32 link_between(const Map& m, u32 a, u32 b) {
    for (u32 j = 0; j < m.degree(a); ++j)
        if (m.other(m.link_at(a, j), a) == b)
            return m.link_at(a, j);
    return ~0u;
}

// The barriers it takes an unhindered message of `size` bytes to go from s
// to d, hop by hop along the routes: each hop's size over its bandwidth,
// rounded up, plus its delay.
u64 route_time(Network& n, const Map& m, u32 s, u32 d, u32 size, u32* hops) {
    u64 t = 0;
    *hops = 0;
    for (u32 at = s; at != d;) {
        const u32 to = hop(n, m, at, d);
        if (to == ~0u)
            return ~0ull;
        const MapLink& l = m.link(link_between(m, at, to));
        t += (size + l.bandwidth - 1) / l.bandwidth + l.delay;
        ++*hops;
        at = to;
    }
    return t;
}

// Steps the network from tick `from` until it delivers, or `limit`; the
// tick it delivered at, or ~0.
u64 step_until_delivered(Network& n, const u8* dark, u64 from, u64 limit, u32 count) {
    u32 got = 0;
    for (u64 t = from; t < limit; ++t) {
        n.step(t, dark);
        got += n.delivered_count();
        if (got >= count)
            return t;
    }
    return ~0ull;
}

const char* const kGrid = "world.map = grid\nmap.bandwidth.min = 16\nmap.bandwidth.max = 16\n";
const char* const kRing = "world.map = ring\nmap.bandwidth.min = 16\nmap.bandwidth.max = 16\n";

// On a 4 x 4 grid, every link 16 bytes a tick with a delay of 2: a 40-byte
// message from corner to corner takes 6 hops of 3 + 2 ticks; one sent just
// after it waits for it at the first link, then follows it a link behind.
void test_delay() {
    Map m;
    CHECK(build_map(m, 1, 16, kGrid));
    Network n;
    TextBuf why;
    CHECK(n.init(&m, net_settings(8, 0), 1, &why));
    u8 dark[16] = {};
    n.step(0, dark); // the routes, with every device on
    u8 bytes[40];
    for (u32 i = 0; i < 40; ++i)
        bytes[i] = static_cast<u8>(i * 7);
    u32 hops = 0;
    const u64 expect = route_time(n, m, 0, 15, 40, &hops);
    CHECK(hops == 6 && expect == 30);
    // Ties go to the lowest id: from 0, east to 1 before south to 4.
    CHECK(hop(n, m, 0, 15) == 1 && hop(n, m, 0, 5) == 1 && hop(n, m, 4, 1) == 0);
    CHECK(n.send(0, 15, 3, bytes, 40, 77, 1) == SendFail::none);
    CHECK(n.send(0, 15, 3, bytes, 40, 78, 1) == SendFail::none);
    u64 first = ~0ull, second = ~0ull;
    for (u64 t = 1; t < 100 && second == ~0ull; ++t) {
        n.step(t, dark);
        for (u32 i = 0; i < n.delivered_count(); ++i) {
            const Message& g = n.delivered(i);
            CHECK(g.dst == 15 && g.src == 0 && g.port == 3 && g.hops == 6 && g.flips == 0);
            CHECK(memcmp(n.data(g), bytes, 40) == 0);
            if (g.sender == 77)
                first = t;
            else
                second = t;
        }
    }
    CHECK(first == 1 + expect);
    CHECK(second == first + 3); // its wait at the first link, its size over the bandwidth
    CHECK(n.counts().delivered == 2 && n.counts().hops == 12 && n.in_flight() == 0);
    // The sender's and receiver's devices draw for the bytes; forwarding is free.
    CHECK(n.take_bytes(0) == 80 && n.take_bytes(15) == 80 && n.take_bytes(1) == 0 && n.take_bytes(0) == 0);
    // On landscapes, each pair's message, sent alone, arrives in its route's
    // time.
    u32 checked = 0, wrong = 0;
    u8 big[100] = {};
    for (u64 seed = 1; seed <= 3; ++seed) {
        Map L;
        CHECK(build_map(L, seed, 48, ""));
        Network ln;
        CHECK(ln.init(&L, net_settings(8, 0), seed, &why));
        u8 none[48] = {};
        u64 t = 0;
        for (u32 s = 0; s < 48; s += 5)
            for (u32 d = 0; d < 48; d += 7) {
                if (s == d)
                    continue;
                u32 h = 0;
                const u64 want = route_time(ln, L, s, d, 100, &h);
                CHECK(want != ~0ull);
                ++t;
                CHECK(ln.send(s, d, 1, big, 100, 0, t) == SendFail::none);
                const u64 at = step_until_delivered(ln, none, t, t + 2000, 1);
                ++checked;
                wrong += at != t + want ? 1 : 0;
                t = at;
            }
    }
    CHECK(wrong == 0);
    printf("  net: a 40-byte message across a 4 x 4 grid took 30 ticks, 6 hops of 3 + 2; the next, sent with it, "
           "3 more; %u messages across landscapes each took their route's time\n",
           checked);
}

// A full queue drops the newcomer, and what's waiting keeps its place. On a
// ring of 4, place 1 keeps its queue toward 2 full; place 0's message to 2,
// routed through 1, finds no room there and is dropped; all of 1's arrive,
// in the order sent.
void test_tail_drop() {
    Map m;
    CHECK(build_map(m, 1, 4, kRing));
    Network n;
    TextBuf why;
    CHECK(n.init(&m, net_settings(8, 0, 128), 1, &why)); // 128 bytes a queue
    u8 dark[4] = {};
    n.step(0, dark);
    CHECK(hop(n, m, 0, 2) == 1 && n.capacity(link_between(m, 1, 2)) == 128);
    u8 bytes[32] = {};
    CHECK(n.send(0, 2, 1, bytes, 32, 1000, 1) == SendFail::none);
    u32 own = 0, sent_own = 0;
    u64 last_seq = 0;
    bool in_order = true;
    for (u64 t = 1; t < 60; ++t) {
        // Place 1's SENDs this tick: as many as there's room for. The last
        // that can't fit fails at once, for a local reason.
        while (t < 20 && n.can_send(1, 2, 32) == SendFail::none) {
            CHECK(n.send(1, 2, 1, bytes, 32, 1, t) == SendFail::none);
            ++sent_own;
        }
        if (t < 20)
            CHECK(n.send(1, 2, 1, bytes, 32, 1, t) == SendFail::full);
        n.step(t, dark);
        for (u32 i = 0; i < n.delivered_count(); ++i) {
            const Message& g = n.delivered(i);
            CHECK(g.sender == 1); // 0's never arrives
            in_order = in_order && (own == 0 || g.seq > last_seq);
            last_seq = g.seq;
            ++own;
        }
    }
    CHECK(n.counts().full == 1 && own == sent_own && in_order && n.in_flight() == 0);
    printf("  net: a full queue dropped the one message arriving at it; the %u waiting there all arrived, in order\n",
           sent_own);
}

// A queue is never smaller than the message limit (net.message_max), so a
// message of any allowed size can cross any link. On a ring of 4 at 16
// bytes a tick, 8 ticks' worth would be 128 bytes; each queue holds 1 KB
// instead, and a 1 KB message crosses 2 hops of 64 + 2 ticks. A second,
// sent with it, finds no room until the first has left its queue.
void test_big() {
    Map m;
    CHECK(build_map(m, 1, 4, kRing));
    Network n;
    TextBuf why;
    CHECK(n.init(&m, net_settings(8, 0), 1, &why));
    for (u32 k = 0; k < m.link_count(); ++k)
        CHECK(m.link(k).bandwidth == 16 && n.capacity(k) == 1024);
    static u8 bytes[1025];
    for (u32 i = 0; i < 1025; ++i)
        bytes[i] = static_cast<u8>(i * 13);
    u8 dark[4] = {};
    CHECK(n.send(0, 2, 1, bytes, 1025, 0, 1) == SendFail::too_big);
    CHECK(n.send(0, 2, 1, bytes, 1024, 5, 1) == SendFail::none);
    CHECK(n.send(0, 2, 1, bytes, 1024, 6, 1) == SendFail::full);
    n.step(1, dark);
    CHECK(n.can_send(0, 2, 1024) == SendFail::none);
    const u64 at = step_until_delivered(n, dark, 2, 400, 1);
    CHECK(at == 1 + 132 && n.delivered(0).sender == 5 && n.delivered(0).size == 1024);
    CHECK(memcmp(n.data(n.delivered(0)), bytes, 1024) == 0 && n.counts().full == 0);
    printf("  net: a 1 KB message crossed two 16-byte-a-tick links in %llu ticks; their queues hold 1 KB, not 128 "
           "bytes\n",
           static_cast<unsigned long long>(at - 1));
}

// Corruption: each hop flips each bit at net.corruption for each average
// link length it crosses. Measured over many messages on landscapes, by the
// bits that actually differ, against the rate summed along each route.
void test_corruption() {
    const double rate = 2e-4;
    double expect = 0, measured = 0, differ = 0;
    u32 messages = 0;
    TextBuf why;
    for (u64 seed = 1; seed <= 2; ++seed) {
        Map m;
        CHECK(build_map(m, seed, 48, ""));
        Network n;
        CHECK(n.init(&m, net_settings(64, rate), seed, &why));
        u8 dark[48] = {};
        n.step(0, dark);
        Rng r(seed, 0, 99, 0);
        u8 bytes[256];
        for (u32 i = 0; i < 256; ++i)
            bytes[i] = static_cast<u8>(r.next());
        for (u64 t = 1; t < 4000; ++t) {
            const u32 s = static_cast<u32>(r.below(48)), d = static_cast<u32>(r.below(48));
            if (s != d)
                n.send(s, d, 1, bytes, 256, 0, t);
            n.step(t, dark);
            for (u32 i = 0; i < n.delivered_count(); ++i) {
                const Message& g = n.delivered(i);
                // The bits that differ: those flipped an odd number of times.
                u32 diff = 0;
                for (u32 b = 0; b < 256; ++b)
                    diff += static_cast<u32>(__popcnt(static_cast<u32>(n.data(g)[b] ^ bytes[b])));
                CHECK(diff <= g.flips && (g.flips - diff) % 2 == 0);
                measured += g.flips;
                differ += diff;
                ++messages;
                // Its expected flips: along its route, for each hop.
                for (u32 at = g.src; at != g.dst;) {
                    const u32 to = hop(n, m, at, g.dst);
                    expect += rate * m.link(link_between(m, at, to)).length / m.mean_length() * 256 * 8;
                    at = to;
                }
            }
        }
    }
    const double sigma = sqrt(expect);
    CHECK(messages > 1000 && fabs(measured - expect) < 4 * sigma && differ > 0.99 * measured);
    printf("  net: corruption at %g a bit per average link length: %.0f bits flipped in %u messages, %.0f expected "
           "(sigma %.0f)\n",
           rate, measured, messages, expect, sigma);
}

// A dark place: what's queued at it and what it was sending are dropped
// (cut); a message whose next hop is it is dropped at its queue's head
// (stranded); one crossing to it is lost; and once the routes are worked
// out again, messages go around it.
void test_dark() {
    Map m;
    CHECK(build_map(m, 1, 6, kRing));
    Network n;
    TextBuf why;
    CHECK(n.init(&m, net_settings(8, 0), 1, &why));
    u8 dark[6] = {};
    n.step(0, dark);
    CHECK(hop(n, m, 0, 3) == 1); // a tie, both ways round 3 hops: to the lower id
    u8 bytes[64] = {};
    // A: 0 to 3 through 1, sent at tick 1: it reaches 1 at barrier 4.
    CHECK(n.send(0, 3, 1, bytes, 16, 1, 1) == SendFail::none);
    // B1 and B2: 1 to 2, 64 bytes each, a full queue; B1 starts at barrier 1.
    CHECK(n.send(1, 2, 1, bytes, 64, 2, 1) == SendFail::none);
    CHECK(n.send(1, 2, 1, bytes, 64, 2, 1) == SendFail::none);
    n.step(1, dark);
    // C: 0 to 3, sent at tick 2, waits at 0 for its link to 1.
    CHECK(n.send(0, 3, 1, bytes, 16, 3, 2) == SendFail::none);
    // Place 1 goes dark in tick 2.
    dark[1] = 1;
    n.step(2, dark);
    CHECK(n.counts().cut == 2);      // B2 waiting, B1 being sent
    CHECK(n.counts().stranded == 1); // C, its next hop dark at its queue's head
    CHECK(n.routes().changes == 1);  // now: before, every device was on
    CHECK(hop(n, m, 0, 3) == 5 && hop(n, m, 2, 0) == 3 && hop(n, m, 0, 1) == ~0u && hop(n, m, 1, 4) == ~0u);
    CHECK(n.can_send(0, 1, 16) == SendFail::no_route && n.can_send(1, 4, 16) == SendFail::dark);
    n.step(3, dark);
    n.step(4, dark); // A reaches 1: lost
    CHECK(n.counts().lost == 1);
    // D, sent at tick 5, goes round the other way: 3 hops of 1 + 2.
    CHECK(n.send(0, 3, 1, bytes, 16, 4, 5) == SendFail::none);
    const u64 at = step_until_delivered(n, dark, 5, 100, 1);
    CHECK(at == 5 + 9 && n.delivered(0).sender == 4 && n.delivered(0).hops == 3);
    // Back on: the routes go through 1 again.
    dark[1] = 0;
    n.step(at + 1, dark);
    CHECK(n.routes().changes == 2 && hop(n, m, 0, 3) == 1);
    CHECK(n.counts().delivered == 1 && n.in_flight() == 0);
    printf("  net: a dark place's queue was cut (2), a message waiting for it stranded (1), one crossing to it lost "
           "(1); the next went round it in 9 ticks\n");
}

// The routes against a brute-force search (Floyd-Warshall) on landscapes,
// some places dark: every route's next hop is the neighbour with the least
// delay plus distance, ties to the lowest id, and following them reaches
// the destination at the least total delay.
// The routes against a brute-force search (all pairs' distances, then each
// place's neighbour with the least delay + distance, ties to the lowest id),
// on landscapes with a tenth of their places dark: with the map's delays; a
// hop-only network with no delay at all, where every reachable place ties;
// and delays long enough for the search's heap. And a batch of columns,
// worked out as the barrier does in tasks, matches those worked out as
// asked.
void test_routes() {
    u32 pairs = 0, bad = 0, batched = 0, long_delay = 0;
    for (u32 trial = 0; trial < 7; ++trial) {
        const u32 n = trial < 4 ? 48 : trial == 4 ? 200 : 120;
        const char* text = trial == 5   ? "map.delay.hop = 0\nmap.delay.length = 0\n"
                           : trial == 6 ? "map.delay.length = 100000\n"
                                        : "";
        Map m;
        CHECK(build_map(m, trial + 1, n, text));
        for (u32 k = 0; trial == 6 && k < m.link_count(); ++k)
            long_delay += m.link(k).delay >= 4096 ? 1 : 0;
        Network net, ahead;
        TextBuf why;
        CHECK(net.init(&m, net_settings(8, 0), trial + 1, &why));
        CHECK(ahead.init(&m, net_settings(8, 0), trial + 1, &why));
        Array<u8> dark;
        dark.resize(n);
        Rng r(trial, 0, 98, 0);
        for (u32 i = 0; i < n; ++i)
            dark[i] = r.chance(0.1) ? 1 : 0;
        net.step(0, dark.data());
        ahead.step(0, dark.data());
        Array<u32> all;
        for (u32 d = 0; d < n; ++d)
            all.push(d);
        all.push(0); // twice: worked out once
        const u32 tasks = ahead.routes().begin_batch(all.data(), static_cast<u32>(all.size()), 3);
        CHECK(tasks == 3);
        for (u32 k = 0; k < tasks; ++k)
            ahead.routes().run_batch(k);
        CHECK(ahead.routes().columns == n);
        for (u32 s = 0; s < n; ++s)
            for (u32 d = 0; d < n; ++d) {
                batched += ahead.routes().next(s, d) == net.routes().next(s, d) ? 1 : 0;
                bad += ahead.routes().next(s, d) == net.routes().next(s, d) ? 0 : 1;
            }
        CHECK(ahead.routes().columns == n); // all were current
        const u64 kFar = ~0ull >> 2;
        Array<u64> dist;
        dist.resize(static_cast<usize>(n) * n);
        dist.fill(kFar);
        for (u32 i = 0; i < n; ++i)
            dist[static_cast<usize>(i) * n + i] = 0;
        for (u32 k = 0; k < m.link_count(); ++k) {
            const MapLink& l = m.link(k);
            if (dark[l.a] || dark[l.b])
                continue;
            dist[static_cast<usize>(l.a) * n + l.b] = l.delay;
            dist[static_cast<usize>(l.b) * n + l.a] = l.delay;
        }
        for (u32 k = 0; k < n; ++k)
            for (u32 i = 0; i < n; ++i)
                for (u32 j = 0; j < n; ++j) {
                    const u64 v = dist[static_cast<usize>(i) * n + k] + dist[static_cast<usize>(k) * n + j];
                    if (v < dist[static_cast<usize>(i) * n + j])
                        dist[static_cast<usize>(i) * n + j] = v;
                }
        for (u32 s = 0; s < n; ++s)
            for (u32 d = 0; d < n; ++d) {
                if (s == d)
                    continue;
                ++pairs;
                const u64 sd = dist[static_cast<usize>(s) * n + d];
                const u32 to = hop(net, m, s, d);
                if (dark[s] || dark[d] || sd >= kFar) {
                    bad += to != ~0u ? 1 : 0;
                    continue;
                }
                // The brute-force choice.
                u32 want = ~0u;
                u64 best = kFar;
                for (u32 j = 0; j < m.degree(s); ++j) {
                    const u32 k = m.link_at(s, j), u = m.other(k, s);
                    if (dark[u])
                        continue;
                    const u64 c = m.link(k).delay + dist[static_cast<usize>(u) * n + d];
                    if (c < best) {
                        best = c;
                        want = u;
                    }
                }
                bad += to != want ? 1 : 0;
                if (trial == 5)
                    continue; // with no delay, every way is the least, and the routes may go round
                // Following the routes: the least total delay.
                u64 total = 0;
                u32 at = s, steps = 0;
                while (at != d && at != ~0u && steps++ < n) {
                    const u32 nx = hop(net, m, at, d);
                    total += nx == ~0u ? 0 : m.link(link_between(m, at, nx)).delay;
                    at = nx;
                }
                bad += at != d || total != sd ? 1 : 0;
            }
    }
    CHECK(bad == 0 && long_delay > 0);
    printf("  net: %u routes matched a brute-force search on 7 landscapes with a tenth of their places dark (one "
           "with no delays, one with %u links of 4,096 ticks or more); %u lookups matched between columns worked out "
           "in a batch and as asked\n",
           pairs, long_delay, batched);
}

// In a world on a grid: a replicator's bytes sent to place 1's port 0 cross
// at the barrier, join its reset region's queue with their sender, and run
// as a prompt run there, making programs from the arrival. The sending and
// receiving places' network devices each draw for the bytes.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

void test_world() {
    World w;
    Config cfg;
    Config::ParseError pe;
    TextBuf why;
    CHECK(cfg.parse("world.places = 4\nworld.map = grid\nmemory.size = 32768\nos.strict_share = 0\n"
                    "noise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\nnet.corruption = 0\n",
                    &pe));
    CHECK(w.init(cfg, &why));
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size());
    }
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(2);
    w.tick(); // the routes
    CHECK(w.net().send(0, 1, 0, kLax, 14, 0, w.tick_count()) == SendFail::none);
    const u64 sent_at = w.tick_count();
    w.tick(); // the barrier sends it: 1 tick at 14 bytes, then the link's delay
    CHECK(w.place(0).net_bytes == 14);
    w.tick();
    // The next tick's draw has the bytes in it, then they're done with.
    CHECK(w.place(0).draw.network == static_cast<u64>(w.settings().power.network_mw) * 1000 + 14 &&
          w.place(0).net_bytes == 0);
    u64 born = 0;
    for (u32 t = 0; t < 20 && !born; ++t) {
        w.tick();
        born = obs.births_by_cause[static_cast<u8>(BirthCause::arrival)];
    }
    CHECK(born > 0 && w.place(1).region.prompt_runs == 1);
    CHECK(w.net().counts().delivered == 1 && w.net().counts().delivered_bytes == 14);
    printf("  net: in a world, a replicator sent at tick %llu ran as a prompt run at place 1 (births from arrivals: "
           "%llu); its sender's device drew for its 14 bytes\n",
           static_cast<unsigned long long>(sent_at), static_cast<unsigned long long>(born));
}

} // namespace

void suite_net() {
    test_delay();
    test_tail_drop();
    test_big();
    test_corruption();
    test_dark();
    test_routes();
    test_world();
}
