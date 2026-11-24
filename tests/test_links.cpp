// Links that fail (nova2/links.h, net.h, solve.h; design.md 7.4, 11.6;
// build-plan-gate5.md step 5): a link's wear by its loss, four times as fast
// at twice the reference current, faster as its resistance rises, then
// failing and carrying nothing, power or data, until a visit; cuts landing
// at the rate times the length, on a schedule nothing in the run moves;
// messages held for a failed link or a dark place taking the new route
// ahead of the barrier's arrivals, waiting up to their lifetime with no
// route, and going on if one comes back; a lifetime of 0 dropping them as
// gate 3 does; the routes against a brute-force search after random cuts;
// the solve with failed and worn links against a solve in doubles; and
// worlds with all of it, on one host thread and three and through a
// snapshot.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/keys.h"
#include "nova2/links.h"
#include "nova2/net.h"
#include "nova2/rng.h"
#include "nova2/snapshot.h"
#include "nova2/wear.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

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

// A map with its solve (if powered), its links and its network.
struct Net5 {
    Map map;
    Grid grid;
    Links links;
    Network net;
    MapSettings ms;
    SolveSettings gs;
    LinkSettings ls;
    NetSettings ns;
};

bool build5(Net5& n, u64 seed, u32 places, const char* extra, bool powered, TextBuf* why) {
    Config cfg;
    char head[64];
    snprintf(head, sizeof head, "world.places = %u\n", places);
    if (!parse_over(cfg, head, extra))
        return false;
    read_map_settings(cfg, &n.ms);
    read_solve_settings(cfg, &n.gs);
    read_link_settings(cfg, &n.ls);
    read_net_settings(cfg, &n.ns);
    if (!cfg.ok())
        return false;
    if (!n.map.build(seed, places, n.ms, why))
        return false;
    if (powered && !n.grid.init(n.gs, &n.map, why))
        return false;
    n.links.init(n.ls, 1.0, &n.map, powered ? &n.grid : nullptr, seed);
    n.ns.outages = n.links.on();
    return n.net.init(&n.map, n.ns, seed, why);
}

bool links_ok(const Links& l, u64 tick) {
    TextBuf why;
    const bool ok = l.check(tick, &why);
    if (!ok)
        printf("  links: %s\n", why.c_str());
    return ok;
}

bool net_ok(const Network& n) {
    TextBuf why;
    const bool ok = n.check(&why);
    if (!ok)
        printf("  links: %s\n", why.c_str());
    return ok;
}

u64 siemens(double s) { return static_cast<u64>(s * 1048576.0 + 0.5); }

u32 link_between(const Map& m, u32 a, u32 b) {
    for (u32 j = 0; j < m.degree(a); ++j)
        if (m.other(m.link_at(a, j), a) == b)
            return m.link_at(a, j);
    return ~0u;
}

// The place at the other end of s's link toward d, or ~0.
u32 hop(Network& n, const Map& m, u32 s, u32 d) {
    const u32 j = n.routes().next(s, d);
    return j == ~0u ? ~0u : m.other(m.link_at(s, j), s);
}

// --- wear by loss ------------------------------------------------------------------

// Two places and one link of the usual resistance, a stiff source at place
// 0 and a load at place 1.
const char* const kPair = "world.map = grid\n"
                          "map.resistance.min = 1\n"
                          "map.resistance.max = 1\n"
                          "map.border.strength = 0\n"
                          "grid.drain = 0\n"
                          "grid.tolerance = 0.000001\n"
                          "grid.sweeps = 100000\n"
                          "wear.link.endurance = 3000\n"
                          "wear.link.current = 30\n"
                          "wear.link.rise = 2\n";

double amps(const Net5& n, u32 pk) {
    const PowerLink& l = n.map.power_link(pk);
    return fabs(static_cast<double>(n.grid.uv(l.a) - n.grid.uv(l.b))) * 1e-6 * static_cast<double>(n.grid.link_g(pk)) /
           1048576.0;
}

void solve_pair(Net5& n, u64 load) {
    n.links.sync();
    n.grid.begin();
    n.grid.load(1, load);
    n.grid.source(0, 1000000, siemens(100000));
    n.grid.solve();
}

// A tick with the load: the link's wear in it, in ticks; its current, and
// its resistance over its new one, as the tick's solve had them.
double wear_tick(Net5& n, u64 load, double* current, double* risen) {
    solve_pair(n, load);
    *current = amps(n, 0);
    *risen = static_cast<double>(n.grid.link_g0(0)) / static_cast<double>(n.grid.link_g(0));
    const u64 before = n.links.line(0).wear;
    n.links.wear_step();
    return static_cast<double>(n.links.line(0).wear - before) / static_cast<double>(kWearTick);
}

void test_wear() {
    TextBuf why;
    // A load that draws the reference current through the link, 30 A: the
    // source's 1 V over the link's 2.5 milliohms and the load's 30.83; and
    // one that draws twice it.
    const u64 one = siemens(1.0 / (1.0 / 30 - 0.0025)), two = siemens(1.0 / (1.0 / 60 - 0.0025));
    double i1 = 0, i2 = 0, r1 = 0, r2 = 0, w1 = 0, w2 = 0;
    {
        Net5 n;
        CHECK(build5(n, 1, 2, kPair, true, &why));
        CHECK(n.links.count() == 1 && n.links.wears() && n.links.line(0).power == 0 && n.links.line(0).data == 0);
        w1 = wear_tick(n, one, &i1, &r1);
    }
    {
        Net5 n;
        CHECK(build5(n, 1, 2, kPair, true, &why));
        w2 = wear_tick(n, two, &i2, &r2);
    }
    CHECK(fabs(i1 - 30) < 0.02 && fabs(i2 - 60) < 0.04 && r1 == 1 && r2 == 1);
    CHECK(fabs(w1 - (i1 / 30) * (i1 / 30)) < 1e-3 && fabs(w2 - (i2 / 30) * (i2 / 30)) < 4e-3);
    CHECK(fabs(w1 - 1) < 2e-3 && fabs(w2 / w1 - 4) < 0.01);

    // Half worn, its resistance is half as much again: the same load draws
    // a little less, and the link wears faster all the same.
    Net5 n;
    CHECK(build5(n, 1, 2, kPair, true, &why));
    const u64 life = n.links.line(0).life;
    CHECK(life >= (1500ull << kWearBits) && life <= (6000ull << kWearBits));
    n.links.set_wear(0, life / 2);
    double ih = 0, rh = 0;
    const double wh = wear_tick(n, one, &ih, &rh);
    CHECK(fabs(rh - 1.5) < 1e-3 && ih < i1 && fabs(wh - (ih / 30) * (ih / 30) * rh) < 2e-3 && wh > 1.3);
    CHECK(links_ok(n.links, 0));

    // From new to the end at that load: faster and faster, so it fails
    // before its endurance in ticks; then it carries nothing.
    n.links.set_wear(0, 0);
    double first = 0, last = 0, c = 0, r = 0;
    u64 ticks = 0;
    while (n.links.line(0).failed == LinkFail::none && ticks < 100000) {
        const double w = wear_tick(n, one, &c, &r);
        if (n.links.line(0).failed != LinkFail::none)
            break;
        first = ticks == 0 ? w : first;
        last = w;
        ++ticks;
    }
    const u64 whole = life >> kWearBits;
    CHECK(n.links.line(0).failed == LinkFail::worn && n.links.worn == 1 && n.links.failed_count() == 1);
    CHECK(fabs(first - 1) < 2e-3 && last > 1.6 && last < 2 && ticks < whole && ticks > whole / 2);
    solve_pair(n, one);
    CHECK(n.grid.link_g(0) == 0 && !n.grid.lit(1) && n.grid.uv(1) == 0 && n.grid.uv(0) > 999000);
    CHECK(n.grid.loads_uw == 0 && n.grid.links_uw == 0 && n.links.data_down()[0] == 1 && n.links.power_down()[0] == 1);
    u8 dark[2] = {};
    n.net.step(0, dark, n.links.data_down());
    CHECK(hop(n.net, n.map, 0, 1) == ~0u && n.net.can_send(0, 1, 16) == SendFail::no_route);
    CHECK(links_ok(n.links, 0) && net_ok(n.net));
    // It wears no more while failed.
    const u64 at_end = n.links.line(0).wear;
    n.links.wear_step();
    CHECK(n.links.line(0).wear == at_end);

    // A visit to either end: a new link, with no wear and a life of its own.
    n.links.visit(1);
    const Line& l = n.links.line(0);
    CHECK(l.failed == LinkFail::none && l.wear == 0 && l.fresh == 1 && l.life != life && n.links.mended == 1);
    solve_pair(n, one);
    CHECK(n.grid.link_g(0) == n.grid.link_g0(0) && n.grid.lit(1) && fabs(amps(n, 0) - 30) < 0.02);
    n.net.step(1, dark, n.links.data_down());
    CHECK(hop(n.net, n.map, 0, 1) == 1 && links_ok(n.links, 0));

    // The lives, on a landscape: each link's own, from half to twice the
    // endurance; the same from the same seed.
    Net5 a, b;
    CHECK(build5(a, 7, 48, "wear.link.endurance = 1000\n", true, &why));
    CHECK(build5(b, 7, 48, "wear.link.endurance = 1000\n", true, &why));
    u64 lo = ~0ull, hi = 0;
    u32 wearing = 0, idle = 0;
    for (u32 i = 0; i < a.links.count(); ++i) {
        const Line& x = a.links.line(i);
        CHECK(x.life == b.links.line(i).life);
        if (!x.life) {
            ++idle; // a data link that is no proximity link, or one the solve leaves out as too weak
            continue;
        }
        ++wearing;
        lo = x.life < lo ? x.life : lo;
        hi = x.life > hi ? x.life : hi;
    }
    CHECK(wearing > 60 && lo >= (500ull << kWearBits) && hi <= (2000ull << kWearBits));
    CHECK(lo < (650ull << kWearBits) && hi > (1500ull << kWearBits));
    printf("  links: one link, new: %.2f A wears %.4f ticks a tick and %.2f A %.4f (%.2f times); half worn, its "
           "resistance %.3f times its new one, the same load draws %.2f A and wears %.4f. To the end at that load: "
           "%llu ticks for a life of %llu, the last tick's wear %.3f; then no power crossed it and no route did, "
           "until a visit put a new link there\n",
           i1, w1, i2, w2, w2 / w1, rh, ih, wh, static_cast<unsigned long long>(ticks),
           static_cast<unsigned long long>(whole), last);
    printf("  links: 48 places: %u links carry current, their lives %llu to %llu ticks around 1,000; %u carry none "
           "and don't wear\n",
           wearing, static_cast<unsigned long long>(lo >> kWearBits), static_cast<unsigned long long>(hi >> kWearBits),
           idle);
}

// --- cuts --------------------------------------------------------------------------

void test_cuts() {
    TextBuf why;
    Net5 n;
    CHECK(build5(n, 3, 48, "link.cuts = 0.001\n", false, &why));
    CHECK(n.links.on() && !n.links.wears());
    const u32 count = n.links.count();
    const u64 ticks = 300000;
    Array<u64> cuts;
    cuts.resize(count);
    for (u64 t = 0; t < ticks; ++t) {
        n.links.cuts_due(t);
        if (!n.links.failed_count())
            continue;
        for (u32 i = 0; i < count; ++i)
            if (n.links.line(i).failed == LinkFail::cut) {
                ++cuts[i];
                n.links.mend(i); // at once, so every cut lands
            }
    }
    CHECK(links_ok(n.links, ticks) && n.links.missed == 0);
    // Each link's cuts against its chance: the rate x its length over the
    // average, in all and for the shorter and the longer links apart.
    double want[3] = {}, got[3] = {};
    u32 bridges = 0, power_only = 0;
    for (u32 i = 0; i < count; ++i) {
        const Line& l = n.links.line(i);
        const double length = l.data != kNoLink ? n.map.link(l.data).length : n.map.power_link(l.power).length;
        const double w = 0.001 * length / n.map.mean_length() * static_cast<double>(ticks);
        const u32 half = length > n.map.mean_length() ? 2 : 1;
        want[0] += w;
        got[0] += static_cast<double>(cuts[i]);
        want[half] += w;
        got[half] += static_cast<double>(cuts[i]);
        bridges += l.data != kNoLink && n.map.link(l.data).bridge ? 1 : 0;
        power_only += l.data == kNoLink ? 1 : 0;
        CHECK(cuts[i] == l.cut_count);
    }
    for (u32 k = 0; k < 3; ++k)
        CHECK(fabs(got[k] - want[k]) < 4 * sqrt(want[k]));
    CHECK(got[2] / want[2] > 0.97 && got[1] / want[1] > 0.97 && want[2] > want[1] * 0.3);
    // The same schedule from the same seed, and another from another.
    Net5 same, other;
    CHECK(build5(same, 3, 48, "link.cuts = 0.001\n", false, &why));
    CHECK(build5(other, 4, 48, "link.cuts = 0.001\n", false, &why));
    Net5 again;
    CHECK(build5(again, 3, 48, "link.cuts = 0.001\n", false, &why));
    u32 differ = 0;
    for (u64 t = 0; t < 20000; ++t) {
        same.links.cuts_due(t);
        again.links.cuts_due(t);
        // One mended as it goes, one left: the schedule is the same.
        for (u32 i = 0; same.links.failed_count() && i < count; ++i)
            same.links.mend(i);
    }
    for (u32 i = 0; i < count; ++i) {
        CHECK(same.links.line(i).cut_at == again.links.line(i).cut_at);
        CHECK(same.links.line(i).cut_count == again.links.line(i).cut_count);
        differ += i < other.links.count() && other.links.line(i).cut_at != n.links.line(i).cut_at ? 1 : 0;
    }
    CHECK(differ > 0 && again.links.missed > 0 && again.links.cut + again.links.missed == same.links.cut);
    CHECK(again.links.failed_count() > count / 2);
    printf("  links: 48 places, %u links (%u bridges, %u power only) over %llu ticks at 1 cut in 1,000 for an "
           "average link: %.0f cuts for %.0f expected; the shorter links %.0f for %.0f, the longer %.0f for %.0f. "
           "The same schedule from the same seed, mended or not\n",
           count, bridges, power_only, static_cast<unsigned long long>(ticks), got[0], want[0], got[1], want[1],
           got[2], want[2]);
}

// --- rerouting and lifetimes ---------------------------------------------------------

const char* const kGrid = "world.map = grid\nmap.bandwidth.min = 16\nmap.bandwidth.max = 16\nnet.lifetime = 20\n"
                          "link.cuts = 0.000000001\n";
const char* const kRing = "world.map = ring\nmap.bandwidth.min = 16\nmap.bandwidth.max = 16\nnet.lifetime = 20\n"
                          "link.cuts = 0.000000001\n";

// Steps the network over [from, to], noting the barrier each sender's
// message was delivered at (senders 1 to 15).
void step_noting(Network& n, const u8* dark, const u8* down, u64 from, u64 to, u64* at, u32* hops) {
    for (u64 t = from; t <= to; ++t) {
        n.step(t, dark, down);
        for (u32 i = 0; i < n.delivered_count(); ++i) {
            const Message& g = n.delivered(i);
            if (g.sender < 16) {
                at[g.sender] = t;
                hops[g.sender] = g.hops;
            }
        }
        CHECK(net_ok(n));
    }
}

void test_reroute() {
    TextBuf why;
    u8 bytes[64] = {};
    // A 3 x 3 grid, every link 16 bytes a tick with a delay of 2:
    //   0 1 2
    //   3 4 5
    //   6 7 8
    // Three messages wait at 1 for its link to 2 (one of them crossing it)
    // when the link is cut. A newcomer from 0, bound for 7 through 1 and 4,
    // reaches 1 at the barrier the three take their new route, through 4:
    // it joins that queue behind them.
    {
        Net5 n;
        CHECK(build5(n, 1, 9, kGrid, false, &why));
        const u32 k12 = link_between(n.map, 1, 2);
        CHECK(k12 != ~0u && link_between(n.map, 1, 4) != ~0u && link_between(n.map, 4, 7) != ~0u);
        u8 dark[9] = {};
        Array<u8> down;
        down.resize(n.map.link_count());
        u64 at[16] = {};
        u32 hops[16] = {};
        CHECK(n.net.send(0, 7, 1, bytes, 16, 9, 0) == SendFail::none);
        step_noting(n.net, dark, down.data(), 0, 0, at, hops);
        CHECK(hop(n.net, n.map, 0, 7) == 1 && hop(n.net, n.map, 1, 7) == 4 && hop(n.net, n.map, 1, 2) == 2);
        for (u32 s = 1; s <= 3; ++s)
            CHECK(n.net.send(1, 2, 1, bytes, 16, s, 1) == SendFail::none);
        step_noting(n.net, dark, down.data(), 1, 1, at, hops);
        down[k12] = 1; // cut in tick 2
        step_noting(n.net, dark, down.data(), 2, 2, at, hops);
        CHECK(n.net.waiting() == 3 && n.net.counts().waited == 3 && n.net.counts().returned == 1);
        CHECK(n.net.waiting_at(k12, 1) == 48 && n.net.queued(k12, 1) == 48 && n.net.on_links() == 1);
        CHECK(hop(n.net, n.map, 1, 2) == 4 && hop(n.net, n.map, 2, 1) == 5);
        step_noting(n.net, dark, down.data(), 3, 3, at, hops);
        CHECK(n.net.waiting() == 0 && n.net.counts().rerouted == 3 && n.net.queued(k12, 1) == 0);
        // The first is being sent toward 4; the other two and the newcomer wait.
        CHECK(n.net.queued(link_between(n.map, 1, 4), 1) == 48);
        step_noting(n.net, dark, down.data(), 4, 20, at, hops);
        CHECK(at[1] == 12 && at[2] == 13 && at[3] == 14 && hops[1] == 3 && hops[3] == 3);
        CHECK(at[9] == 12 && hops[9] == 3); // ahead of them it would have been there at 9
        const NetCounts& c = n.net.counts();
        CHECK(c.delivered == 4 && c.stranded == 0 && c.lost == 0 && c.full == 0 && c.expired == 0);
        printf("  links: a cut link's three messages (one crossing it) took the new route at the next barrier, "
               "ahead of a newcomer for the same queue: there at ticks %llu, %llu and %llu, the newcomer at %llu\n",
               static_cast<unsigned long long>(at[1]), static_cast<unsigned long long>(at[2]),
               static_cast<unsigned long long>(at[3]), static_cast<unsigned long long>(at[9]));
    }
    // A ring of 6. Place 1 is cut off, both its links: a message for it
    // waits at 0 for its lifetime of 20 ticks, and is then dropped. Another
    // goes on when the link comes back at tick 10.
    for (u32 back = 0; back < 2; ++back) {
        Net5 n;
        CHECK(build5(n, 1, 6, kRing, false, &why));
        const u32 k01 = link_between(n.map, 0, 1), k12 = link_between(n.map, 1, 2);
        u8 dark[6] = {};
        Array<u8> down;
        down.resize(n.map.link_count());
        u64 at[16] = {};
        u32 hops[16] = {};
        step_noting(n.net, dark, down.data(), 0, 0, at, hops);
        CHECK(n.net.send(0, 1, 1, bytes, 16, 1, 1) == SendFail::none);
        down[k01] = down[k12] = 1;
        step_noting(n.net, dark, down.data(), 1, 1, at, hops);
        CHECK(n.net.waiting() == 1 && n.net.can_send(0, 1, 16) == SendFail::no_route);
        if (back) {
            step_noting(n.net, dark, down.data(), 2, 9, at, hops);
            down[k01] = 0;
            step_noting(n.net, dark, down.data(), 10, 10, at, hops);
            CHECK(n.net.waiting() == 1); // the routes follow at this barrier's end
            step_noting(n.net, dark, down.data(), 11, 30, at, hops);
            CHECK(at[1] == 14 && hops[1] == 1 && n.net.counts().rerouted == 1 && n.net.counts().stranded == 0);
        } else {
            step_noting(n.net, dark, down.data(), 2, 20, at, hops);
            CHECK(n.net.waiting() == 1 && n.net.counts().stranded == 0);
            step_noting(n.net, dark, down.data(), 21, 21, at, hops);
            CHECK(n.net.waiting() == 0 && n.net.counts().stranded == 1 && n.net.counts().expired == 1);
            CHECK(n.net.in_flight() == 0 && at[1] == 0);
        }
    }
    printf("  links: with no route a message waited its lifetime of 20 ticks and was dropped at the 20th; another "
           "went on when its link came back at tick 10, and was there at 14\n");
    // A dark next place is a failed link (gate 3's test of a dark place,
    // with a lifetime): what waited at 0 for place 1, and what was crossing
    // to it, go round the other way; only what was queued at 1 is lost.
    {
        Net5 n;
        CHECK(build5(n, 1, 6, kRing, false, &why));
        u8 dark[6] = {};
        u64 at[16] = {};
        u32 hops[16] = {};
        step_noting(n.net, dark, nullptr, 0, 0, at, hops);
        CHECK(hop(n.net, n.map, 0, 3) == 1);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 1, 1) == SendFail::none);
        CHECK(n.net.send(1, 2, 1, bytes, 64, 2, 1) == SendFail::none);
        CHECK(n.net.send(1, 2, 1, bytes, 64, 2, 1) == SendFail::none);
        step_noting(n.net, dark, nullptr, 1, 1, at, hops);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 3, 2) == SendFail::none);
        dark[1] = 1;
        step_noting(n.net, dark, nullptr, 2, 2, at, hops);
        CHECK(n.net.counts().cut == 2 && n.net.counts().stranded == 0 && n.net.waiting() == 2);
        CHECK(n.net.counts().returned == 1);
        step_noting(n.net, dark, nullptr, 3, 30, at, hops);
        CHECK(at[1] == 12 && at[3] == 13 && hops[1] == 3 && hops[3] == 3);
        CHECK(n.net.counts().lost == 0 && n.net.counts().stranded == 0 && n.net.counts().rerouted == 2);
        printf("  links: a place gone dark: the message crossing to it and the one waiting for it went round the "
               "other way (there at ticks 12 and 13); the 2 queued at it were lost\n");
    }
    // A message that reaches a place with no route on waits there, in the
    // room of the end it came in by, and goes on when there is one.
    {
        Net5 n;
        CHECK(build5(n, 1, 6, kRing, false, &why));
        const u32 k01 = link_between(n.map, 0, 1);
        u8 dark[6] = {};
        u64 at[16] = {};
        u32 hops[16] = {};
        step_noting(n.net, dark, nullptr, 0, 0, at, hops);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 1, 1) == SendFail::none);
        step_noting(n.net, dark, nullptr, 1, 1, at, hops);
        dark[3] = 1;
        step_noting(n.net, dark, nullptr, 2, 4, at, hops);
        CHECK(n.net.waiting() == 1 && n.net.waiting_at(k01, 1) == 16 && n.net.waiting_at(k01, 0) == 0);
        dark[3] = 0;
        step_noting(n.net, dark, nullptr, 5, 30, at, hops);
        CHECK(at[1] == 12 && hops[1] == 3 && n.net.counts().stranded == 0 && n.net.counts().lost == 0);
        printf("  links: a message that reached a place with no route on waited there, and went on when its "
               "destination came back\n");
    }
    // A lifetime of 0: a cut link's messages are dropped as gate 3 drops a
    // dark place's: the one waiting stranded at once, the one crossing lost
    // when it would have arrived. Nothing waits.
    {
        Net5 n;
        CHECK(build5(n, 1, 6, "world.map = ring\nmap.bandwidth.min = 16\nmap.bandwidth.max = 16\nnet.lifetime = 0\n"
                              "link.cuts = 0.000000001\n",
                     false, &why));
        const u32 k01 = link_between(n.map, 0, 1);
        u8 dark[6] = {};
        Array<u8> down;
        down.resize(n.map.link_count());
        u64 at[16] = {};
        u32 hops[16] = {};
        step_noting(n.net, dark, down.data(), 0, 0, at, hops);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 1, 1) == SendFail::none);
        step_noting(n.net, dark, down.data(), 1, 1, at, hops);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 3, 2) == SendFail::none);
        down[k01] = 1;
        step_noting(n.net, dark, down.data(), 2, 2, at, hops);
        CHECK(n.net.counts().stranded == 1 && n.net.waiting() == 0 && hop(n.net, n.map, 0, 3) == 5);
        step_noting(n.net, dark, down.data(), 3, 4, at, hops);
        CHECK(n.net.counts().lost == 1 && n.net.in_flight() == 0);
        CHECK(n.net.send(0, 3, 1, bytes, 16, 4, 5) == SendFail::none);
        step_noting(n.net, dark, down.data(), 5, 30, at, hops);
        CHECK(at[4] == 14 && at[1] == 0 && at[3] == 0 && n.net.counts().waited == 0 && n.net.counts().rerouted == 0);
        printf("  links: with a lifetime of 0, a cut link's waiting message was stranded at once and the one "
               "crossing it lost, as at a dark place before gate 5\n");
    }
}

// --- the routes after cuts -----------------------------------------------------------

void test_routes() {
    u32 pairs = 0, bad = 0, cut = 0, none = 0;
    for (u32 trial = 0; trial < 5; ++trial) {
        const u32 n = trial < 3 ? 48 : trial == 3 ? 200 : 120;
        const char* text = trial == 4 ? "map.delay.length = 100000\nlink.cuts = 0.000000001\n"
                                      : "link.cuts = 0.000000001\n";
        Net5 w;
        TextBuf why;
        CHECK(build5(w, trial + 11, n, text, false, &why));
        const Map& m = w.map;
        Array<u8> dark, down;
        dark.resize(n);
        down.resize(m.link_count());
        Rng r(trial, 0, 97, 0);
        for (u32 i = 0; i < n; ++i)
            dark[i] = r.chance(0.05) ? 1 : 0;
        for (u32 k = 0; k < m.link_count(); ++k) {
            down[k] = r.chance(0.2) ? 1 : 0;
            cut += down[k];
        }
        w.net.step(0, dark.data(), down.data());
        const u64 kFar = ~0ull >> 2;
        Array<u64> dist;
        dist.resize(static_cast<usize>(n) * n);
        dist.fill(kFar);
        for (u32 i = 0; i < n; ++i)
            dist[static_cast<usize>(i) * n + i] = 0;
        for (u32 k = 0; k < m.link_count(); ++k) {
            const MapLink& l = m.link(k);
            if (dark[l.a] || dark[l.b] || down[k])
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
                const u32 to = hop(w.net, m, s, d);
                if (dark[s] || dark[d] || sd >= kFar) {
                    bad += to != ~0u ? 1 : 0;
                    none += dark[s] || dark[d] ? 0 : 1;
                    continue;
                }
                u32 want = ~0u;
                u64 best = kFar;
                for (u32 j = 0; j < m.degree(s); ++j) {
                    const u32 k = m.link_at(s, j), u = m.other(k, s);
                    if (dark[u] || down[k])
                        continue;
                    const u64 c = m.link(k).delay + dist[static_cast<usize>(u) * n + d];
                    if (c < best) {
                        best = c;
                        want = u;
                    }
                }
                bad += to != want ? 1 : 0;
                // Following the routes: the least total delay, over no cut link.
                u64 total = 0;
                u32 at = s, steps = 0;
                while (at != d && at != ~0u && steps++ < n) {
                    const u32 nx = hop(w.net, m, at, d);
                    if (nx != ~0u) {
                        const u32 k = link_between(m, at, nx);
                        total += m.link(k).delay;
                        bad += down[k] ? 1 : 0;
                    }
                    at = nx;
                }
                bad += at != d || total != sd ? 1 : 0;
            }
    }
    CHECK(bad == 0 && cut > 100 && none > 100);
    printf("  links: %u routes matched a brute-force search on 5 landscapes with a fifth of their links cut (%u) "
           "and a twentieth of their places dark; %u pairs of lit places had no route left\n",
           pairs, cut, none);
}

// --- the solve with failed and worn links ----------------------------------------------

void test_solve() {
    TextBuf why;
    u32 unlit = 0, failed = 0, worn = 0;
    double worst = 0;
    for (u32 round = 0; round < 3; ++round) {
        const u32 places = round == 2 ? 300 : 48;
        Net5 n;
        CHECK(build5(n, 30 + round, places, "grid.tolerance = 0.000001\nwear.link.endurance = 1000\n", true, &why));
        const Map& map = n.map;
        Rng r(round, 0, 96, 0);
        for (u32 i = 0; i < n.links.count(); ++i) {
            const Line& l = n.links.line(i);
            if (r.chance(round == 1 ? 0.5 : 0.25)) {
                n.links.fail(i, LinkFail::cut);
                failed += l.power != kNoLink ? 1 : 0;
            } else if (l.life && r.chance(0.7)) {
                n.links.set_wear(i, l.life / 1000 * r.below(1000));
                ++worn;
            }
        }
        n.links.sync();
        CHECK(links_ok(n.links, 0));
        Array<u64> load;
        load.resize(places);
        Array<double> gsum, isum;
        gsum.resize(places);
        isum.resize(places);
        n.grid.begin();
        for (u32 i = 0; i < places; ++i) {
            load[i] = siemens(1 + static_cast<double>(r.below(2000)) / 100.0);
            n.grid.load(i, load[i]);
        }
        Array<u8> fed;
        fed.resize(places);
        for (u32 k = 0; k < places / 8 + 2; ++k) {
            const u32 at = static_cast<u32>(r.below(places));
            const i64 uv = static_cast<i64>(1000 + r.below(400)) * 1000;
            const u64 g = siemens(100 + static_cast<double>(r.below(900)));
            n.grid.source(at, uv, g);
            gsum[at] += static_cast<double>(g);
            isum[at] += static_cast<double>(g) * static_cast<double>(uv);
            fed[at] = 1;
        }
        n.grid.solve(2000000);
        // In doubles, with the failed links read from the link table: a
        // place is lit if links that haven't failed join it to a source.
        Array<u32> stack;
        for (u32 i = 0; i < places; ++i)
            if (fed[i])
                stack.push(i);
        while (!stack.empty()) {
            const u32 a = stack[stack.size() - 1];
            stack.resize(stack.size() - 1);
            for (u32 j = 0; j < map.power_degree(a); ++j) {
                const u32 pk = map.power_link_at(a, j), b = map.power_other(pk, a);
                if (n.links.line(n.links.of_power(pk)).failed != LinkFail::none || !n.grid.link_g0(pk) || fed[b])
                    continue;
                fed[b] = 1;
                stack.push(b);
            }
        }
        Array<double> v;
        v.resize(places);
        for (u32 sweep = 0; sweep < 2000000; ++sweep) {
            double move = 0;
            for (u32 i = 0; i < places; ++i) {
                if (!fed[i])
                    continue;
                double num = isum[i], den = static_cast<double>(load[i]) + static_cast<double>(n.grid.drain_g(i)) + gsum[i];
                for (u32 j = 0; j < map.power_degree(i); ++j) {
                    const u32 pk = map.power_link_at(i, j);
                    const Line& l = n.links.line(n.links.of_power(pk));
                    if (l.failed != LinkFail::none || !n.grid.link_g0(pk))
                        continue;
                    // Its conductance as worn, worked here from its wear.
                    const double g = static_cast<double>(n.grid.link_g0(pk)) /
                                     (1 + floor(static_cast<double>(l.wear) / static_cast<double>(l.life / 4096)) / 4096);
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
            const double d = fabs(static_cast<double>(n.grid.uv(i)) - v[i]);
            worst = d > worst ? d : worst;
            CHECK(n.grid.lit(i) == (fed[i] != 0));
            if (!fed[i]) {
                ++unlit;
                CHECK(n.grid.uv(i) == 0);
            }
        }
        const double given = static_cast<double>(n.grid.given_uw);
        const double taken = static_cast<double>(n.grid.loads_uw + n.grid.links_uw + n.grid.drains_uw);
        CHECK(fabs(given - taken) < given * 1e-3);
    }
    CHECK(worst < 100 && unlit >= 5 && failed > 100 && worn > 100);
    printf("  links: the solve on 3 landscapes with %u proximity links failed and %u worn: every place within %.0f "
           "microvolts of a solve in doubles; %u places cut off from every source sat at 0 V; the power balanced\n",
           failed, worn, worst, unlit);
}

// --- worlds --------------------------------------------------------------------------

const char* const kLand = "world.places = 40\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "supply.model = sources\n"
                          "heat.on = true\n"
                          "wear.rate = 1\n"
                          "wear.link.endurance = 400\n"
                          "wear.link.current = 12\n"
                          "link.cuts = 0.0006\n"
                          "net.lifetime = 30\n"
                          "visit.mean = 150\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build_world(World& w, Config& cfg, const char* extra, TextBuf* why) {
    if (!parse_over(cfg, kLand, extra))
        return false;
    if (!w.init(cfg, why)) {
        printf("  links: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
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

// The invariants, and the power balance, after a tick. The worst the
// balance was out, as a share of what the sources gave.
double world_ok(World& w) {
    TextBuf why;
    if (!w.links().check(w.tick_count(), &why) || !w.net().check(&why)) {
        printf("  links: at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()), why.c_str());
        CHECK(false);
    }
    const Grid& g = w.grid();
    const double given = static_cast<double>(g.given_uw);
    const double taken = static_cast<double>(g.loads_uw + g.links_uw + g.drains_uw);
    return given > 1e6 ? fabs(given - taken) / given : 0;
}

bool has_tag(const ByteWriter& b, u32 tag) {
    for (usize i = 0; i + 4 <= b.size(); ++i)
        if (memcmp(b.data() + i, &tag, 4) == 0)
            return true;
    return false;
}

void test_worlds() {
    const u32 first = 500, rest = 700;
    u64 hash[2] = {}, mid = 0;
    ByteWriter saved;
    u32 failed_at_save = 0, waiting_at_save = 0;
    double balance = 0;
    Array<u64> cut_at, cut_count;
    u64 worn = 0, cut = 0, mended = 0, missed = 0;
    NetCounts nc;
    u64 births = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, "", &why));
        CHECK(w.links().on() && w.links().wears());
        crowd(w);
        w.start_threads(pass == 0 ? 1 : 3);
        for (u32 t = 0; t < first; ++t) {
            w.tick();
            if (pass == 0) {
                const double b = world_ok(w);
                balance = b > balance ? b : balance;
            }
        }
        if (pass == 0) {
            mid = w.hash();
            snapshot(w, cfg, saved);
            failed_at_save = w.links().failed_count();
            waiting_at_save = w.net().waiting();
        } else {
            CHECK_EQ_U64(w.hash(), mid);
        }
        for (u32 t = 0; t < rest; ++t) {
            w.tick();
            if (pass == 0) {
                const double b = world_ok(w);
                balance = b > balance ? b : balance;
            }
        }
        hash[pass] = w.hash();
        if (pass == 0) {
            worn = w.links().worn;
            cut = w.links().cut;
            mended = w.links().mended;
            missed = w.links().missed;
            nc = w.net().counts();
            for (u32 i = 0; i < w.links().count(); ++i) {
                cut_at.push(w.links().line(i).cut_at);
                cut_count.push(w.links().line(i).cut_count);
            }
            for (u32 i = 0; i < w.place_count(); ++i)
                births += w.place(i).births;
        }
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(worn > 3 && cut > 20 && mended > 20 && failed_at_save > 0 && births > 1000);
    CHECK(nc.waited > 20 && nc.rerouted > 10 && nc.delivered > 1000 && balance < 0.02);
    // From the snapshot, taken with links failed and messages waiting.
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
        CHECK_EQ_U64(w.hash(), mid);
        CHECK(w.links().failed_count() == failed_at_save && w.net().waiting() == waiting_at_save);
        world_ok(w);
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
    }
    // The cuts are the same whatever the programs do: the same world with
    // nothing living in it has every link's cuts at the same ticks.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, "", &why));
        w.start_threads(3);
        for (u32 t = 0; t < first + rest; ++t)
            w.tick();
        CHECK(w.hash() != hash[0] && w.links().count() == cut_at.size());
        u64 total = 0;
        for (u32 i = 0; i < w.links().count(); ++i) {
            CHECK(w.links().line(i).cut_at == cut_at[i] && w.links().line(i).cut_count == cut_count[i]);
            total += cut_count[i];
        }
        CHECK(total == cut + missed && w.links().cut + w.links().missed == total);
    }
    printf("  links: 40 places with sources, crowded, over %u ticks: %llu links failed by wear and %llu by cuts "
           "(%llu more cuts found their link already failed), %llu mended at visits; %llu messages began a wait "
           "(%llu of them came back from a link), %llu took a new route, %llu ran out of time; %llu delivered. The "
           "power balanced within %.3f%% every tick. The same world on 1 host thread and 3, and from a snapshot of "
           "tick %u with %u links failed and %u messages waiting; and the same cuts with nothing living\n",
           first + rest, static_cast<unsigned long long>(worn), static_cast<unsigned long long>(cut),
           static_cast<unsigned long long>(missed), static_cast<unsigned long long>(mended),
           static_cast<unsigned long long>(nc.waited), static_cast<unsigned long long>(nc.returned),
           static_cast<unsigned long long>(nc.rerouted), static_cast<unsigned long long>(nc.expired),
           static_cast<unsigned long long>(nc.delivered), balance * 100, first, failed_at_save, waiting_at_save);
}

// Off is off: with no link wear, no cuts and no lifetime, nothing fails or
// waits, and the save says nothing of links.
void test_off() {
    ByteWriter off, on;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, pass ? "" : "wear.link.endurance = 0\nlink.cuts = 0\nnet.lifetime = 0\n", &why));
        CHECK(w.links().on() == (pass == 1));
        crowd(w);
        w.start_threads(3);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        if (!pass) {
            const NetCounts& c = w.net().counts();
            CHECK(w.links().worn == 0 && w.links().cut == 0 && w.links().failed_count() == 0);
            CHECK(c.waited == 0 && c.rerouted == 0 && c.expired == 0 && c.returned == 0 && w.net().waiting() == 0);
        }
        w.save(pass ? on : off);
    }
    const u32 kLnk1 = 0x4C4E4B31, kNet2 = 0x4E455432;
    CHECK(!has_tag(off, kLnk1) && !has_tag(off, kNet2) && has_tag(on, kLnk1) && has_tag(on, kNet2));
    printf("  links: with link wear, cuts and the lifetime at 0, nothing failed or waited in 300 ticks, and the "
           "save has no link section\n");
}

} // namespace

void suite_links() {
    test_wear();
    test_cuts();
    test_reroute();
    test_routes();
    test_solve();
    test_worlds();
    test_off();
}
