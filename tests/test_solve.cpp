// The voltage solve (nova2/solve.h; design.md 4.1, 6.1, appendix C 2-3;
// build-plan-gate4.md step 4): its small laws; one source at one place
// against gate 2's supply, as a law and as a world run beside gate 2's; a
// chain and a small grid against values worked by hand; random maps against
// the same sweeps in doubles, and what the tolerance and the cap leave; the
// power balance; a landmass with no source and no crossing at 0 V, and the
// trickle across the water falling off with its reach; a world with the
// sources on the map giving the same hash on 1, 2 and 24 host threads; the
// same sources whatever the programs do (step 3's world-level check); a
// crowd cut off raising its neighbours' voltage; and the constant draws
// above a device's maximum (gap 5).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/solve.h"
#include "nova2/sources.h"
#include "nova2/world.h"

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

// A map and a grid on it, from settings.
struct Net {
    Map map;
    Grid grid;
    MapSettings ms;
    SolveSettings gs;
};

bool build_net(Net& n, u64 seed, u32 places, const char* extra, TextBuf* why) {
    Config cfg;
    char head[64];
    snprintf(head, sizeof head, "world.places = %u\n", places);
    if (!parse_over(cfg, head, extra))
        return false;
    read_map_settings(cfg, &n.ms);
    read_solve_settings(cfg, &n.gs);
    if (!cfg.ok())
        return false;
    return n.map.build(seed, places, n.ms, why) && n.grid.init(n.gs, &n.map, why);
}

// Flat test links: every link of a test map one usual link, no borders. A
// usual link is 5 milliohms in the sums worked by hand here (the default is
// 2.5 since 2026-11-20).
const char* const kFlat = "grid.link_resistance = 5\n"
                          "map.resistance.min = 1\n"
                          "map.resistance.max = 1\n"
                          "map.border.strength = 0\n"
                          "grid.drain = 0\n"
                          "grid.tolerance = 0.000001\n"
                          "grid.sweeps = 100000\n";

struct SrcIn {
    u32 place;
    i64 uv;
    u64 g;
};

// The same sweeps in doubles, on the grid's own conductances, until nothing
// moves: what the integers should come to.
void solve_exact(const Map& map, const Grid& g, const Array<u64>& load, const Array<SrcIn>& src, Array<double>* out) {
    const u32 n = map.place_count();
    Array<double> gsum, isum;
    gsum.resize(n);
    isum.resize(n);
    for (usize k = 0; k < src.size(); ++k) {
        gsum[src[k].place] += static_cast<double>(src[k].g);
        isum[src[k].place] += static_cast<double>(src[k].g) * static_cast<double>(src[k].uv);
    }
    Array<double>& v = *out;
    v.resize(0);
    v.resize(n);
    for (u32 sweep = 0; sweep < 2000000; ++sweep) {
        double move = 0;
        for (u32 i = 0; i < n; ++i) {
            if (!g.lit(i))
                continue;
            double num = isum[i], den = static_cast<double>(load[i]) + static_cast<double>(g.drain_g(i)) + gsum[i];
            for (u32 j = 0; j < map.power_degree(i); ++j) {
                const u32 k = map.power_link_at(i, j);
                if (!g.link_g(k))
                    continue;
                num += static_cast<double>(g.link_g(k)) * v[map.power_other(k, i)];
                den += static_cast<double>(g.link_g(k));
            }
            const double nv = den > 0 ? num / den : 0;
            move = fabs(nv - v[i]) > move ? fabs(nv - v[i]) : move;
            v[i] = nv;
        }
        if (move < 1e-9)
            break;
    }
}

void give(Grid& g, const Array<u64>& load, const Array<SrcIn>& src) {
    g.begin();
    for (u32 i = 0; i < load.size(); ++i)
        g.load(i, load[i]);
    for (usize k = 0; k < src.size(); ++k)
        g.source(src[k].place, src[k].uv, src[k].g);
}

u64 siemens(double s) { return static_cast<u64>(s * 1048576.0 + 0.5); }

// --- the small laws -------------------------------------------------------------

void test_definitions() {
    // G = P / V^2: 1 W at 1 V is 1 S; 4 W at 2 V the same; nothing with no
    // draw or no voltage.
    CHECK_EQ_U64(load_conductance(1000000, 1000), kSiemens);
    CHECK_EQ_U64(load_conductance(4000000, 2000), kSiemens);
    CHECK_EQ_U64(load_conductance(0, 1000), 0);
    CHECK_EQ_U64(load_conductance(1000000, 0), 0);
    // V^2 x G: a volt across a siemens is a watt.
    CHECK_EQ_U64(power_uw(1000000, kSiemens), 1000000);
    CHECK_EQ_U64(power_uw(-500000, 4 * kSiemens), 1000000);
    CHECK_EQ_U64(power_uw(0, kSiemens), 0);
    // Above a device's maximum a constant draw rises as (V / Vmax)^2 (gap 5).
    CHECK_EQ_U64(over_maximum(1000000, 1300, 1300), 1000000);
    CHECK_EQ_U64(over_maximum(1000000, 900, 1300), 1000000);
    CHECK_EQ_U64(over_maximum(1000000, 2600, 1300), 4000000);
    CHECK_EQ_U64(over_maximum(1690000, 1400, 1300), 1960000);
}

// --- one source at one place: gate 2's supply ------------------------------------

void test_one_place_law() {
    Net n;
    TextBuf why;
    CHECK(build_net(n, 1, 1, "world.map = none\n", &why));
    CHECK(n.grid.parts() == 1 && n.map.power_link_count() == 0);
    // Gate 2's source: 1,000 mV, sagging 50 mV at a full draw of 37 W.
    SupplySettings ss;
    ss.voltage = 1000;
    ss.sag = 50;
    Supply sup;
    CHECK(sup.init(ss, 1, 0, 37000000, &why));
    Rng r(11, 0, kPurposeHardware, 0);
    u32 same = 0, off_by_one = 0, worse = 0, tried = 0;
    for (u32 k = 0; k < 20000; ++k) {
        const u32 cap = k % 7 == 0 ? kCapacityOne : static_cast<u32>(r.below(kCapacityOne + 1));
        const u64 draw = r.below(60000000);
        const i32 last = static_cast<i32>(300 + r.below(800));
        const i32 want = sup.voltage(cap, draw, last);
        n.grid.begin();
        n.grid.load(0, load_conductance(draw, last));
        n.grid.source(0, static_cast<i64>(sup.source_mv()) * 1000, mul_div_u64(sup.conductance(), cap, kCapacityOne));
        n.grid.solve();
        const i32 got = n.grid.mv(0);
        ++tried;
        if (got == want)
            ++same;
        else if (got - want == 1 || want - got == 1)
            ++off_by_one;
        else
            ++worse;
    }
    CHECK(worse == 0 && off_by_one == 0);
    printf("  solve: one source at one place gave gate 2's voltage in %u of %u draws (%u a millivolt off, %u worse)\n",
           same, tried, off_by_one, worse);
}

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "heat.on = false\n";

// Sources on, but none drawn: only those placed by hand, flat.
const char* const kByHand = "supply.model = sources\n"
                            "sources.start = 0\n"
                            "sources.chance = 0\n"
                            "source.steady.voltage = 1000\n"
                            "source.steady.swing = 0\n"
                            "source.steady.dropouts = 0\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build_world(World& w, Config& cfg, const char* a, const char* b, TextBuf* why) {
    TextBuf text;
    text.append(Str(kSmall, strlen(kSmall)));
    text.append(Str(a, strlen(a)));
    if (!parse_over(cfg, text.c_str(), b))
        return false;
    return w.init(cfg, why);
}

void place_program(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 128, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    CHECK(p.os.place_by_hand(1, at, at, 0) != 0);
}

void test_one_place_world() {
    // Gate 2's world: a source at the place, sagging 50 mV at its full draw.
    World a;
    Config ca;
    TextBuf why;
    CHECK(build_world(a, ca, "", "supply.model = place\nsupply.sag = 50\nworld.map = none\n", &why));
    CHECK(!a.powered());
    // Gate 4's: the same source, placed by hand at the place.
    char fixed[160];
    snprintf(fixed, sizeof fixed, "world.map = none\ngrid.drain = 0\nsources.fixed = 0 steady %.6f 50\n",
             static_cast<double>(a.place(0).supply.full_draw()) / 1e6);
    World b;
    Config cb;
    CHECK(build_world(b, cb, kByHand, fixed, &why));
    CHECK(b.powered() && b.sources().fixed_count() == 1);
    b.sources().seek(0);
    CHECK(b.sources().count() == 1);
    CHECK_EQ_U64(b.sources().source(0).gs, a.place(0).supply.conductance());
    place_program(a.place(0), kLax, sizeof kLax);
    place_program(b.place(0), kLax, sizeof kLax);
    u32 differ = 0;
    i32 lowest = 1000, highest = 0;
    for (u32 t = 0; t < 600; ++t) {
        a.tick();
        b.tick();
        const i32 va = a.place(0).env.voltage_mv, vb = b.place(0).env.voltage_mv;
        differ += va != vb ? 1 : 0;
        lowest = va < lowest ? va : lowest;
        highest = va > highest ? va : highest;
    }
    CHECK(differ == 0);
    CHECK(lowest < 990 && highest == 1000); // it sagged, from the open-circuit voltage
    CHECK_EQ_U64(a.place(0).births, b.place(0).births);
    CHECK_EQ_U64(a.place(0).draw.total(), b.place(0).draw.total());
    CHECK(a.place(0).births > 20);
    printf("  solve: a world with one source placed by hand ran 600 ticks at gate 2's voltages, tick for tick "
           "(%d to %d mV, %llu births)\n",
           lowest, highest, static_cast<unsigned long long>(a.place(0).births));
}

// --- a chain and a small grid, by hand -------------------------------------------

bool near_uv(i64 got, double want_v, double within_uv) { return fabs(static_cast<double>(got) - want_v * 1e6) <= within_uv; }

void test_by_hand() {
    TextBuf why;
    // Three places on a test grid make a chain, 1 - 0 - 2, each link 5
    // milliohms. A 1 V source behind 10 milliohms at place 1, and a 20
    // milliohm load at place 2: 40 milliohms in all, so 25 A. Place 1 is at
    // 0.75 V, place 0 at 0.625 V and place 2 at 0.5 V.
    {
        Net n;
        TextBuf extra;
        extra.append("world.map = grid\n");
        extra.append(Str(kFlat, strlen(kFlat)));
        CHECK(build_net(n, 1, 3, extra.c_str(), &why));
        CHECK(n.map.power_link_count() == 2);
        CHECK(n.map.power_link(0).a == 0 && n.map.power_link(0).b == 1 && n.map.power_link(1).a == 0 &&
              n.map.power_link(1).b == 2);
        CHECK_EQ_U64(n.grid.link_g(0), 209715200); // 200 S
        Array<u64> load;
        load.resize(3);
        load[2] = siemens(50);
        Array<SrcIn> src;
        src.push(SrcIn{1, 1000000, siemens(100)});
        give(n.grid, load, src);
        const u32 sweeps = n.grid.solve();
        CHECK(sweeps < 100000 && n.grid.capped == 0);
        CHECK(near_uv(n.grid.uv(1), 0.75, 10) && near_uv(n.grid.uv(0), 0.625, 10) && near_uv(n.grid.uv(2), 0.5, 10));
        CHECK(n.grid.mv(1) == 750 && n.grid.mv(0) == 625 && n.grid.mv(2) == 500);
        // The powers: the source gives its place 18.75 W; the load takes
        // 12.5 W; each link loses 3.125 W; the source loses 6.25 W inside.
        CHECK(fabs(static_cast<double>(n.grid.given_uw) - 18750000) < 500);
        CHECK(fabs(static_cast<double>(n.grid.loads_uw) - 12500000) < 500);
        CHECK(fabs(static_cast<double>(n.grid.links_uw) - 6250000) < 500);
        CHECK(fabs(static_cast<double>(n.grid.inside_uw) - 6250000) < 500);
        CHECK(n.grid.drains_uw == 0 && n.grid.links_sea_uw == 0);
        CHECK(fabs(static_cast<double>(n.grid.link_loss(0)) - 3125000) < 300);
        // The link from 0 to 1 carries power toward 0: 25 A leaving place 1
        // at 0.75 V.
        CHECK(fabs(static_cast<double>(n.grid.link_flow(0)) + 18750000) < 500);
        CHECK(fabs(static_cast<double>(n.grid.link_flow(1)) - 15625000) < 500);
        // What heats each place besides its devices: half of each of its
        // links' losses, and the source's own loss at its place.
        CHECK(fabs(static_cast<double>(n.grid.place_heat(0)) - 3125000) < 500);
        CHECK(fabs(static_cast<double>(n.grid.place_heat(1)) - (1562500 + 6250000)) < 500);
        CHECK(fabs(static_cast<double>(n.grid.place_heat(2)) - 1562500) < 500);
        printf("  solve: a chain of three by hand: %lld, %lld and %lld microvolts (0.75, 0.625 and 0.5 V) in %u "
               "sweeps\n",
               static_cast<long long>(n.grid.uv(1)), static_cast<long long>(n.grid.uv(0)),
               static_cast<long long>(n.grid.uv(2)), sweeps);
    }
    // Four places in a square: 0 - 1, 0 - 2, 1 - 3, 2 - 3. The source at
    // place 0 and the load at place 3, so two paths of two links side by
    // side: 5 milliohms between them, 35 in all, 28.571 A. Place 0 is at
    // 0.714286 V, place 3 at 0.571429 V, and places 1 and 2 half way.
    {
        Net n;
        TextBuf extra;
        extra.append("world.map = grid\n");
        extra.append(Str(kFlat, strlen(kFlat)));
        CHECK(build_net(n, 1, 4, extra.c_str(), &why));
        CHECK(n.map.power_link_count() == 4);
        Array<u64> load;
        load.resize(4);
        load[3] = siemens(50);
        Array<SrcIn> src;
        src.push(SrcIn{0, 1000000, siemens(100)});
        give(n.grid, load, src);
        n.grid.solve();
        CHECK(near_uv(n.grid.uv(0), 5.0 / 7, 10) && near_uv(n.grid.uv(3), 4.0 / 7, 10));
        CHECK(near_uv(n.grid.uv(1), 4.5 / 7, 10) && near_uv(n.grid.uv(2), 4.5 / 7, 10));
        CHECK(n.grid.uv(1) - n.grid.uv(2) <= 2 && n.grid.uv(2) - n.grid.uv(1) <= 2);
        // A second source, at place 3, at a lower voltage than its place
        // would have: it takes power, and what the sources give in all is
        // still what the load and the links take.
        src.push(SrcIn{3, 400000, siemens(10)});
        give(n.grid, load, src);
        n.grid.solve();
        const double out = static_cast<double>(n.grid.loads_uw + n.grid.links_uw);
        CHECK(fabs(static_cast<double>(n.grid.given_uw) - out) < 1000);
        CHECK(n.grid.uv(3) > 400000);
        // With no source nothing is lit: every place at 0 V.
        src.resize(0);
        give(n.grid, load, src);
        CHECK(n.grid.solve() == 0);
        CHECK(n.grid.uv(0) == 0 && n.grid.uv(3) == 0 && !n.grid.lit(0) && n.grid.given_uw == 0);
    }
}

// --- random maps against doubles -------------------------------------------------

void random_inputs(const Map& map, u64 seed, u32 sources, Array<u64>* load, Array<SrcIn>* src) {
    const u32 n = map.place_count();
    Rng r(seed, 5, kPurposeHardware, 0);
    load->resize(0);
    load->resize(n);
    for (u32 i = 0; i < n; ++i)
        (*load)[i] = siemens(1 + static_cast<double>(r.below(2000)) / 100.0); // 1 to 21 W at a volt
    src->resize(0);
    for (u32 k = 0; k < sources; ++k) {
        SrcIn s;
        s.place = static_cast<u32>(r.below(n));
        const i32 mv = static_cast<i32>(1000 + r.below(400));
        const i32 sag = static_cast<i32>(20 + r.below(280));
        s.uv = static_cast<i64>(mv) * 1000;
        s.g = source_conductance(mv, sag, 50000000 + r.below(350000000));
        src->push(s);
    }
}

double worst_error(const Grid& g, const Array<double>& exact) {
    double worst = 0;
    for (u32 i = 0; i < g.place_count(); ++i) {
        const double d = fabs(static_cast<double>(g.uv(i)) - exact[i]);
        worst = d > worst ? d : worst;
    }
    return worst;
}

void test_random() {
    TextBuf why;
    for (u32 round = 0; round < 3; ++round) {
        const u32 places = round == 0 ? 48 : (round == 1 ? 300 : 1000);
        Net n;
        CHECK(build_net(n, 20 + round, places, "grid.tolerance = 0.000001\n", &why));
        Array<u64> load;
        Array<SrcIn> src;
        random_inputs(n.map, 100 + round, places / 8 + 2, &load, &src);
        give(n.grid, load, src);
        const u32 sweeps = n.grid.solve(2000000);
        Array<double> exact;
        solve_exact(n.map, n.grid, load, src, &exact);
        const double worst = worst_error(n.grid, exact);
        // The integers come to the doubles' answer within a tenth of a
        // millivolt: each sweep rounds down to the microvolt, and a sweep
        // that would move a place by less than one moves nothing.
        CHECK(worst < 100);
        // The power balance: what the sources give their places is what the
        // loads, the links and the drains take.
        const double given = static_cast<double>(n.grid.given_uw);
        const double taken = static_cast<double>(n.grid.loads_uw + n.grid.links_uw + n.grid.drains_uw);
        CHECK(fabs(given - taken) < given * 1e-3);
        CHECK(n.grid.links_sea_uw <= n.grid.links_uw);
        // And each place's heat from the network adds up to the links' loss
        // on land and the sources' own.
        u64 heat = 0;
        for (u32 i = 0; i < places; ++i)
            heat += n.grid.place_heat(i);
        const double want = static_cast<double>(n.grid.links_uw - n.grid.links_sea_uw + n.grid.inside_uw);
        CHECK(fabs(static_cast<double>(heat) - want) < static_cast<double>(n.map.power_link_count()) * 2 + 10);
        // The setting's solve, from nothing: what the tolerance and the cap
        // leave, against the exact answer.
        Net d;
        CHECK(build_net(d, 20 + round, places, "", &why));
        give(d.grid, load, src);
        const u32 cold = d.grid.solve();
        const double cold_err = worst_error(d.grid, exact);
        // Then as a tick finds it: the last tick's voltages, and every load
        // a twentieth heavier.
        for (u32 i = 0; i < places; ++i) {
            d.grid.set_uv(i, n.grid.uv(i));
            load[i] += load[i] / 20;
        }
        give(d.grid, load, src);
        const u32 warm = d.grid.solve();
        Net e;
        CHECK(build_net(e, 20 + round, places, "grid.tolerance = 0.000001\n", &why));
        give(e.grid, load, src);
        e.grid.solve(2000000);
        double warm_err = 0;
        for (u32 i = 0; i < places; ++i) {
            const double x = fabs(static_cast<double>(d.grid.uv(i) - e.grid.uv(i)));
            warm_err = x > warm_err ? x : warm_err;
        }
        const double warm_given = static_cast<double>(d.grid.given_uw);
        const double warm_taken = static_cast<double>(d.grid.loads_uw + d.grid.links_uw + d.grid.drains_uw);
        CHECK(warm_err < 5000); // within 5 mV of the exact answer at the settings' tolerance
        CHECK(fabs(warm_given - warm_taken) < warm_given * 0.01);
        printf("  solve: %u places, %u links (%u left out as too weak, %u parts), %u sources: the exact solve took "
               "%u sweeps and came within %.1f microvolts of doubles; power given %.3f W, taken %.3f W. At the "
               "settings (0.1 mV, 64 sweeps): from nothing %u sweeps, %.3f mV off%s; from the last tick's "
               "voltages with loads 5%% heavier %u sweeps, %.3f mV off, power %.2f%% out of balance\n",
               places, n.map.power_link_count(), n.grid.dropped(), n.grid.parts(), static_cast<u32>(src.size()),
               sweeps, worst, given / 1e6, taken / 1e6, cold, cold_err / 1000, d.grid.capped ? " (capped)" : "",
               warm, warm_err / 1000, warm_given > 0 ? fabs(warm_given - warm_taken) / warm_given * 100 : 0.0);
    }
}

// --- water -----------------------------------------------------------------------

// A landscape with places on several landmasses, a source at place 0, and a
// small load at every place.
struct Water {
    Net n;
    Array<u64> load;
    Array<SrcIn> src;
};

bool build_water(Water& w, u64 seed, u32 places, double reach, TextBuf* why) {
    char extra[200];
    snprintf(extra, sizeof extra, "map.water.reach = %.4f\nmap.border.strength = 0\ngrid.tolerance = 0.000001\n",
             reach);
    if (!build_net(w.n, seed, places, extra, why))
        return false;
    w.load.resize(0);
    w.load.resize(places);
    w.load.fill(siemens(1));
    w.src.resize(0);
    w.src.push(SrcIn{0, 1200000, siemens(2000)});
    give(w.n.grid, w.load, w.src);
    w.n.grid.solve(2000000);
    return true;
}

void test_water() {
    TextBuf why;
    const u32 kPlaces = 120;
    // With the water's reach so short that no crossing is worth keeping, the
    // map falls into parts that no power crosses between: the land joined to
    // the source's place without water, and the rest. A seed where at least
    // ten places are in the rest.
    u64 seed = 0;
    for (u64 s = 1; s <= 60 && !seed; ++s) {
        Net n;
        if (!build_net(n, s, kPlaces, "map.water.reach = 0.01\nmap.border.strength = 0\n", &why))
            continue;
        u32 off = 0;
        for (u32 i = 0; i < kPlaces; ++i)
            off += n.grid.part(i) != n.grid.part(0) ? 1 : 0;
        if (off >= 10 && off + 10 <= kPlaces && n.grid.dropped() >= 2)
            seed = s;
    }
    CHECK(seed != 0);
    if (!seed)
        return;
    // Every place across the water is in a part with no source and no
    // crossing: 0 V exactly, and nothing drawn there.
    Water none;
    CHECK(build_water(none, seed, kPlaces, 0.01, &why));
    Array<u8> far;
    far.resize(kPlaces);
    u32 dark = 0, lit = 0, dry_crossings = 0;
    bool zero = true, on = true;
    for (u32 i = 0; i < kPlaces; ++i) {
        far[i] = none.n.grid.part(i) != none.n.grid.part(0) ? 1 : 0;
        if (far[i]) {
            ++dark;
            zero = zero && none.n.grid.uv(i) == 0 && !none.n.grid.lit(i);
        } else {
            ++lit;
            on = on && none.n.grid.uv(i) > 0 && none.n.grid.lit(i);
        }
    }
    for (u32 k = 0; k < none.n.map.power_link_count(); ++k) {
        const PowerLink& l = none.n.map.power_link(k);
        dry_crossings += !(l.water > 0) && none.n.map.landmass(l.a) != none.n.map.landmass(l.b) ? 1 : 0;
    }
    CHECK(zero && on && dark >= 10 && none.n.grid.parts() >= 2);
    printf("  solve: seed %llu, %u places on %u landmasses: with no crossing kept (%u links left out, %u parts), "
           "the %u places joined to the source's by land are lit and the %u across the water sit at 0 V (%u links "
           "join two landmasses with no water found under them)\n",
           static_cast<unsigned long long>(seed), kPlaces, none.n.map.landmasses(), none.n.grid.dropped(),
           none.n.grid.parts(), lit, dark, dry_crossings);
    // With the usual reach the crossings carry a trickle, and with half the
    // reach less: a link's conductance falls as e^(its water / the reach).
    Water full, half;
    CHECK(build_water(full, seed, kPlaces, 1, &why));
    CHECK(build_water(half, seed, kPlaces, 0.5, &why));
    u32 wet = 0, checked = 0;
    bool law = true, weaker = true;
    for (u32 k = 0; k < full.n.map.power_link_count(); ++k) {
        const PowerLink& l = full.n.map.power_link(k);
        if (!(l.water > 0)) {
            // A link on land is the same whatever the reach.
            law = law && full.n.grid.link_g(k) == half.n.grid.link_g(k);
            continue;
        }
        ++wet;
        const u64 a = full.n.grid.link_g(k), b = half.n.grid.link_g(k);
        weaker = weaker && b <= a;
        if (a < 100000 || b < 100000)
            continue;
        // e^(w / 0.5) / e^(w / 1) = e^w, with w its water in average links.
        const double want = exp(l.water / full.n.map.mean_length());
        law = law && fabs(static_cast<double>(a) / static_cast<double>(b) / want - 1) < 1e-3;
        ++checked;
    }
    CHECK(wet >= 2 && checked >= 1 && law && weaker);
    // What reaches the places across the water: something at the usual
    // reach, less at half of it, and nothing with no crossing.
    double p_full = 0, p_half = 0, best_full = 0, best_half = 0;
    u32 trickle = 0;
    for (u32 i = 0; i < kPlaces; ++i) {
        if (!far[i])
            continue;
        trickle += full.n.grid.uv(i) > 0 ? 1 : 0;
        p_full += static_cast<double>(power_uw(full.n.grid.uv(i), full.load[i]));
        p_half += static_cast<double>(power_uw(half.n.grid.uv(i), half.load[i]));
        best_full = static_cast<double>(full.n.grid.uv(i)) > best_full ? static_cast<double>(full.n.grid.uv(i)) : best_full;
        best_half = static_cast<double>(half.n.grid.uv(i)) > best_half ? static_cast<double>(half.n.grid.uv(i)) : best_half;
    }
    CHECK(trickle > 0 && p_full > 0 && p_half < p_full);
    CHECK(best_full < static_cast<double>(full.n.grid.uv(0)));
    // The water's share of the crossings' loss goes into the sea, not the
    // places at their ends.
    CHECK(full.n.grid.links_sea_uw > 0 && full.n.grid.links_sea_uw < full.n.grid.links_uw);
    printf("  solve: with the water's reach at 1, %u of those %u places get a trickle: %.3f W in all, the best "
           "place at %.1f mV (the source's place %.1f mV); at half the reach %.3f W, the best %.1f mV. %u wet "
           "links, %u checked against e^(water / reach); %.3f W of the links' %.3f W loss went into the sea\n",
           trickle, dark, p_full / 1e6, best_full / 1000, static_cast<double>(full.n.grid.uv(0)) / 1000, p_half / 1e6,
           best_half / 1000, wet, checked, static_cast<double>(full.n.grid.links_sea_uw) / 1e6,
           static_cast<double>(full.n.grid.links_uw) / 1e6);
}

// --- worlds ------------------------------------------------------------------------

const char* const kLand = "world.places = 48\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "os.strict_share = 0\n"
                          "supply.model = sources\n"
                          "heat.on = false\n";

bool build_land(World& w, Config& cfg, u64 seed, const char* extra, TextBuf* why) {
    char head[48];
    snprintf(head, sizeof head, "world.seed = %llu\n", static_cast<unsigned long long>(seed));
    TextBuf text;
    text.append(Str(kLand, strlen(kLand)));
    text.append(Str(head, strlen(head)));
    if (!parse_over(cfg, text.c_str(), extra))
        return false;
    return w.init(cfg, why);
}

void seed_programs(World& w, u32 every) {
    for (u32 i = 0; i < w.place_count(); i += every)
        if (w.place(i).env.voltage_mv >= 900)
            place_program(w.place(i), kLax, sizeof kLax);
}

void test_threads() {
    u64 hashes[3] = {};
    u64 births[3] = {};
    const u32 threads[3] = {1, 2, 24};
    for (u32 k = 0; k < 3; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_land(w, cfg, 5, "", &why));
        w.start_threads(threads[k]);
        seed_programs(w, 3);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        hashes[k] = w.hash();
        for (u32 i = 0; i < w.place_count(); ++i)
            births[k] += w.place(i).births;
        if (k == 0) {
            u32 lit = 0, dark = 0;
            i32 lo = 100000, hi = 0;
            for (u32 i = 0; i < w.place_count(); ++i) {
                const i32 v = w.place(i).env.voltage_mv;
                lit += v > 0 ? 1 : 0;
                dark += v == 0 ? 1 : 0;
                lo = v > 0 && v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
            CHECK(lit > 0 && hi > 900 && w.grid().solves == 300);
            printf("  solve: 48 places, %u sources alive at tick 300: %u places lit (%d to %d mV), %u at 0 V; %llu "
                   "births; %.1f sweeps a solve, %llu capped\n",
                   w.sources().count(), lit, lo, hi, dark, static_cast<unsigned long long>(births[0]),
                   static_cast<double>(w.grid().sweeps) / static_cast<double>(w.grid().solves),
                   static_cast<unsigned long long>(w.grid().capped));
        }
    }
    CHECK(births[0] > 0);
    CHECK_EQ_U64(hashes[0], hashes[1]);
    CHECK_EQ_U64(hashes[0], hashes[2]);
    CHECK_EQ_U64(births[0], births[1]);
    CHECK_EQ_U64(births[0], births[2]);
    printf("  solve: the same world hash after 300 ticks on 1, 2 and 24 host threads\n");
}

// Step 3's check at the world's level: nothing a program does changes a
// source. Two worlds of one seed, one with replicators and one without,
// have the same sources in the same states at every tick, though their
// voltages part.
void test_same_sources() {
    // Short lives and frequent events, so sources come and go in the run.
    const char* const extra = "sources.life = 400\nsource.steady.dropouts = 1000\nsource.daylight.dropouts = 1000\n"
                              "source.steady.swells = 1000\nsource.daylight.swells = 1000\n"
                              "source.gusty.swells = 1000\nsupply.dropout.max = 60\nsources.swell.longest = 60\n";
    World a, b;
    Config ca, cb;
    TextBuf why;
    CHECK(build_land(a, ca, 9, extra, &why));
    CHECK(build_land(b, cb, 9, extra, &why));
    seed_programs(b, 2);
    u32 differ = 0, parted = 0, states = 0, swelled = 0, dropped = 0;
    for (u32 t = 0; t < 1200; ++t) {
        a.tick();
        b.tick();
        // Each world's sources as its last tick's solve had them.
        const Sources& sa = a.sources();
        const Sources& sb = b.sources();
        if (sa.count() != sb.count() || sa.tick() != sb.tick() || sa.begun() != sb.begun() ||
            sa.ended() != sb.ended()) {
            ++differ;
            continue;
        }
        for (u32 k = 0; k < sa.count(); ++k) {
            const Source& x = sa.source(k);
            const Source& y = sb.source(k);
            const SourceState& p = sa.state(k);
            const SourceState& q = sb.state(k);
            differ += x.index != y.index || x.place != y.place || x.type != y.type || x.gs != y.gs ||
                              x.birth != y.birth || x.life != y.life || p.capacity != q.capacity ||
                              p.swell != q.swell || p.g != q.g || p.uv != q.uv
                          ? 1
                          : 0;
            ++states;
            swelled += p.swell > kCapacityOne ? 1 : 0;
            dropped += p.capacity == 0 ? 1 : 0;
        }
        for (u32 i = 0; i < a.place_count(); ++i)
            parted += a.place(i).env.voltage_mv != b.place(i).env.voltage_mv ? 1 : 0;
    }
    CHECK(differ == 0);
    CHECK(parted > 0 && a.hash() != b.hash());
    CHECK(a.sources().ended() > 5 && swelled > 0 && dropped > 0);
    printf("  solve: two worlds of one seed, one with replicators: %u source states over 1,200 ticks the same in "
           "both (%llu sources begun, %llu ended, %u states in a swell, %u dropped out), while %u place voltages "
           "differed\n",
           states, static_cast<unsigned long long>(a.sources().begun()),
           static_cast<unsigned long long>(a.sources().ended()), swelled, dropped, parted);
}

// Load rejection: a crowd's draw holds its neighbours' voltage down, and
// when the crowd is cut off the voltage rises.
void test_rejection() {
    // The law first: a chain 1 - 0 - 2, the source at 1, loads at 0 and 2.
    {
        Net n;
        TextBuf why, extra;
        extra.append("world.map = grid\n");
        extra.append(Str(kFlat, strlen(kFlat)));
        CHECK(build_net(n, 1, 3, extra.c_str(), &why));
        Array<u64> load;
        load.resize(3);
        load[0] = siemens(80);
        load[2] = siemens(10);
        Array<SrcIn> src;
        src.push(SrcIn{1, 1000000, siemens(100)});
        give(n.grid, load, src);
        n.grid.solve();
        const i64 before1 = n.grid.uv(1), before2 = n.grid.uv(2);
        load[0] = 0;
        give(n.grid, load, src);
        n.grid.solve();
        CHECK(n.grid.uv(1) > before1 + 100000 && n.grid.uv(2) > before2 + 100000);
    }
    // Then a world: three places in the same chain, a 60 W source at place
    // 1, and a crowd of replicators at place 0, in the middle.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build_world(w, cfg, kByHand,
                      "world.places = 3\nworld.map = grid\nmap.resistance.min = 1\nmap.resistance.max = 1\n"
                      "map.border.strength = 0\ngrid.drain = 0\ngrid.link_resistance = 2\nregion.retry = 100000\n"
                      "sources.fixed = 1 steady 150 50\n",
                      &why));
    place_program(w.place(0), kLax, sizeof kLax);
    for (u32 t = 0; t < 400; ++t)
        w.tick();
    i64 crowded[3] = {};
    for (u32 t = 0; t < 50; ++t) {
        w.tick();
        for (u32 i = 0; i < 3; ++i)
            crowded[i] += w.place(i).env.voltage_mv;
    }
    const u64 crowd_draw = w.place(0).draw.total();
    CHECK(w.place(0).births > 50);
    // Cut off by hand: every thread at place 0 ends.
    Place& p = w.place(0);
    for (u32 t = 0; t < p.os.thread_top(); ++t)
        if (p.os.thread(t).state != ThreadState::free)
            p.os.end_thread(t, DeathCause::exit);
    for (u32 t = 0; t < 20; ++t)
        w.tick();
    i64 after[3] = {};
    for (u32 t = 0; t < 50; ++t) {
        w.tick();
        for (u32 i = 0; i < 3; ++i)
            after[i] += w.place(i).env.voltage_mv;
    }
    CHECK(w.place(0).draw.total() < crowd_draw);
    // (50 ticks summed: each rose by more than a millivolt on average.)
    CHECK(after[0] > crowded[0] + 50 && after[1] > crowded[1] + 50 && after[2] > crowded[2] + 50);
    printf("  solve: load rejection: with a crowd at the middle place drawing %.1f W the three places averaged "
           "%.1f, %.1f and %.1f mV; with the crowd cut off (%.1f W) %.1f, %.1f and %.1f mV\n",
           static_cast<double>(crowd_draw) / 1e6, static_cast<double>(crowded[0]) / 50,
           static_cast<double>(crowded[1]) / 50, static_cast<double>(crowded[2]) / 50,
           static_cast<double>(w.place(0).draw.total()) / 1e6, static_cast<double>(after[0]) / 50,
           static_cast<double>(after[1]) / 50, static_cast<double>(after[2]) / 50);
}

// Gap 5: above its maximum a device's constant draw rises as (V / Vmax)^2.
void test_over_maximum() {
    World lo, hi;
    Config cl, ch;
    TextBuf why;
    const char* const one = "world.map = none\ngrid.drain = 0\nregion.retry = 100000\nsources.fixed = 0 steady 100 1\n";
    CHECK(build_world(lo, cl, kByHand, one, &why));
    TextBuf high;
    high.append(Str(one, strlen(one)));
    high.append("source.steady.voltage = 1500\n");
    CHECK(build_world(hi, ch, kByHand, high.c_str(), &why));
    for (u32 t = 0; t < 30; ++t) {
        lo.tick();
        hi.tick();
    }
    const Place& a = lo.place(0);
    const Place& b = hi.place(0);
    const i32 mv = b.env.voltage_mv;
    CHECK(a.env.voltage_mv > 990 && a.env.voltage_mv <= 1000 && mv > 1480 && mv <= 1500);
    const PowerSettings& pw = b.power();
    CHECK_EQ_U64(a.draw.disk, static_cast<u64>(pw.disk_mw) * 1000);
    CHECK_EQ_U64(b.draw.disk, over_maximum(static_cast<u64>(pw.disk_mw) * 1000, mv, pw.disk_maximum));
    CHECK_EQ_U64(a.draw.network, static_cast<u64>(pw.network_mw) * 1000);
    CHECK_EQ_U64(b.draw.network, over_maximum(static_cast<u64>(pw.network_mw) * 1000, mv, pw.network_maximum));
    CHECK(b.draw.disk > a.draw.disk * 13 / 10 && b.draw.memory > a.draw.memory * 13 / 10);
    // A halted core's draw too: with nothing running, each core's draw is
    // its idle draw, raised.
    u32 halted = 0;
    for (u32 c = 0; c < b.core_count(); ++c) {
        const CorePower& k = b.cores[c].kind == DeviceKind::e_core ? pw.e : pw.p;
        if (b.core_draw[c] == over_maximum(static_cast<u64>(k.idle_mw) * 1000, mv, k.maximum))
            ++halted;
    }
    CHECK(halted == b.core_count());
    printf("  solve: at %d mV, above every maximum, the disk draws %.3f W (%.3f W at %d mV), and a halted core, "
           "memory and the network device rise the same way\n",
           mv, static_cast<double>(b.draw.disk) / 1e6, static_cast<double>(a.draw.disk) / 1e6, a.env.voltage_mv);
}

} // namespace

void suite_solve() {
    test_definitions();
    test_one_place_law();
    test_one_place_world();
    test_by_hand();
    test_random();
    test_water();
    test_threads();
    test_same_sources();
    test_rejection();
    test_over_maximum();
}
