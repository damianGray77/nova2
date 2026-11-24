// Heat and cooling devices (nova2/heat.h; design.md 5, 7.7, appendix C 7;
// build-plan-gate4.md steps 5 and 6): the laws one by one; a heat trip's
// rule against the cut-out's; a place warming step by step beside the
// equations in doubles, and resting where hand values put it; the clamp
// with absurd rates; leakage; a core tripping, its thread ending with the
// cause "heat", and coming back at its cool-down point with no off-time; the
// reset region's memory tripping and the cold start when it cools; hot
// memory fading in half the time for each 10 degrees; a world at rest
// against its temperatures solved in doubles, the heat leaving it against
// the power its sources give less what the sea takes, and the same hash on
// 1, 2 and 24 host threads; heat between places, the coast and the cooling
// field; and a cooling device against its twin without, by hand, and
// cooling nothing below its threshold.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/heat.h"
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

const char* const kSmall = "world.places = 1\n"
                           "world.map = none\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 1000000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "heat.on = true\n"
                           "cooler.share = 0\n"
                           // The worlds here were worked by hand at the cores' first heat numbers and
                           // the first link resistance, so they are written out: the defaults since
                           // agreed (2026-11-20; build-plan-gate4.md 11) are checked in test_settings.
                           "heat.p.sink = 0.55\n"
                           "heat.p.capacity = 11\n"
                           "heat.e.sink = 0.07\n"
                           "heat.e.capacity = 1.4\n"
                           "grid.link_resistance = 5\n";

// Sources on, but none drawn: only those placed by hand, flat.
const char* const kByHand = "supply.model = sources\n"
                            "grid.drain = 0\n"
                            "sources.start = 0\n"
                            "sources.chance = 0\n"
                            "source.steady.voltage = 1000\n"
                            "source.steady.swing = 0\n"
                            "source.steady.dropouts = 0\n";

bool build_world(World& w, Config& cfg, const char* a, const char* b, TextBuf* why) {
    TextBuf text;
    text.append(Str(kSmall, strlen(kSmall)));
    text.append(Str(a, strlen(a)));
    if (!parse_over(cfg, text.c_str(), b))
        return false;
    if (!w.init(cfg, why)) {
        printf("  heat: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

// Counts in A as it runs: ADD # 1; JMP back.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

u32 place_program(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 128, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    CHECK(false);
    return 0;
}

double deg(i64 t) { return static_cast<double>(t) / 1e6; }
i64 udeg(double d) { return static_cast<i64>(d * 1e6 + (d < 0 ? -0.5 : 0.5)); }

// --- the laws ----------------------------------------------------------------------

void test_laws() {
    // A device drawing 10 W through a heatsink of 0.5 W a degree, with a
    // capacity of 5 W-ticks a degree, in a place at 25: 27 after a tick,
    // 28.8 after two, and at rest at 45.
    const u64 p = 10000000, h = 500000, c = 5000000;
    const i64 around = 25 * kDegree;
    i64 passed = 0;
    i64 t = heat_step(around, around, p, h, c, &passed);
    CHECK(t == 27 * kDegree && passed == 0);
    t = heat_step(t, around, p, h, c, &passed);
    CHECK(t == 28800000 && passed == 1000000);
    // Step by step beside the equation in doubles, and the heat accounted
    // for: what it drew is what it passed on and what warmed it.
    double td = 28.8;
    i64 passed_sum = 1000000;
    double worst = 0;
    for (u32 k = 2; k < 400; ++k) {
        td += (10 - 0.5 * (td - 25)) / 5;
        t = heat_step(t, around, p, h, c, &passed);
        passed_sum += passed;
        worst = fabs(deg(t) - td) > worst ? fabs(deg(t) - td) : worst;
    }
    CHECK(worst < 1e-4);
    CHECK(fabs(deg(t) - 45) < 1e-4);
    const double kept = static_cast<double>(c) * (deg(t) - 25);
    CHECK(fabs(static_cast<double>(passed_sum) + kept - 400.0 * static_cast<double>(p)) < 1000);
    // The clamp: a heatsink far above the capacity lands on the balance in
    // one step, from below and from above, and stays.
    const u64 big = 1000000000000ull; // a megawatt a degree
    i64 x = heat_step(around, around, p, big, 1, &passed);
    CHECK(x >= around && x <= around + 10 + 1);
    CHECK(heat_step(x, around, p, big, 1, &passed) == x);
    x = heat_step(80 * kDegree, around, p, big, 1, &passed);
    CHECK(x >= around && x <= around + 10 + 1);
    // ... and one a little above it never passes the balance either.
    i64 y = 80 * kDegree;
    bool over = false;
    for (u32 k = 0; k < 50; ++k) {
        y = heat_step(y, around, p, 7000000, 5000000, &passed);
        over = over || y < around + udeg(10.0 / 7) - 2;
    }
    CHECK(!over && y <= around + udeg(10.0 / 7) + 2);
    // A place's step: the net heat over the larger of its capacity and its
    // rates out.
    CHECK(heat_step_net(around, 5000000, 2500000, 1250000000) == around + 4000);
    CHECK(heat_step_net(around, 5000000, 2500000, 1000) == around + 2 * kDegree);
    CHECK(heat_step_net(around, -5000000, 2500000, 1000) == around - 2 * kDegree);
    CHECK(heat_flow(2500000, 27 * kDegree, 25 * kDegree) == 5000000);
    CHECK(heat_flow(2500000, 25 * kDegree, 27 * kDegree) == -5000000);

    // Leakage, fading and the cooling device's share, at the settings.
    HeatSettings s;
    HeatLaw law;
    law.init(s);
    CHECK(law.outside == 25 * kDegree);
    CHECK_EQ_U64(law.leaky(10000000, 25 * kDegree), 10000000);
    CHECK_EQ_U64(law.leaky(10000000, 10 * kDegree), 10000000);
    CHECK(law.leaky(10000000, 35 * kDegree) >= 10999999 && law.leaky(10000000, 35 * kDegree) <= 11000001);
    CHECK(law.leaky(10000000, 125 * kDegree) >= 19999999 && law.leaky(10000000, 125 * kDegree) <= 20000001);
    CHECK(law.fade_factor(20 * kDegree) == 65536 && law.fade_factor(45 * kDegree) == 65536);
    CHECK(law.fade_factor(55 * kDegree) == 2 * 65536 && law.fade_factor(65 * kDegree) == 4 * 65536);
    CHECK(law.fade_factor(50 * kDegree) >= 92681 && law.fade_factor(50 * kDegree) <= 92683); // root 2
    CHECK(law.fade_factor(1000 * kDegree) == (1u << 31));
    CHECK(law.cooler_share(40 * kDegree) == 0 && law.cooler_share(30 * kDegree) == 0);
    CHECK(law.cooler_share(60 * kDegree) == (1u << 19) && law.cooler_share(80 * kDegree) == (1u << 20));
    CHECK(law.cooler_share(120 * kDegree) == (1u << 20));
    CHECK(law.limit[kHeatP] == 100 * kDegree && law.cool[kHeatP] == 90 * kDegree);
    CHECK(law.limit[kHeatDisk] == 100 * kDegree); // the struct's own default; the keys give 70 (below)
    // With heat off nothing leaks or fades faster.
    s.on = false;
    law.init(s);
    CHECK_EQ_U64(law.leaky(10000000, 90 * kDegree), 10000000);
    CHECK(law.fade_factor(90 * kDegree) == 65536);
    // The keys' numbers (build-plan-gate4.md 7).
    Config cfg;
    Config::ParseError pe;
    CHECK(cfg.parse(Str("heat.on = true\n"), &pe));
    HeatSettings k;
    read_heat_settings(cfg, &k);
    CHECK(cfg.ok() && k.on);
    CHECK(k.part[kHeatP].limit == 100 && k.part[kHeatE].limit == 100 && k.part[kHeatMemory].limit == 85);
    CHECK(k.part[kHeatDisk].limit == 70 && k.part[kHeatNetwork].limit == 85 && k.part[kHeatCooler].limit == 85);
    CHECK(k.part[kHeatP].sink == 0.98 && k.part[kHeatE].sink == 0.124 && k.part[kHeatMemory].sink == 0.1);
    CHECK(k.part[kHeatP].capacity == 19.6 && k.part[kHeatE].capacity == 2.48); // agreed 2026-11-20
    CHECK(k.cooldown == 10 && k.leakage == 0.01 && k.place_cooling == 2.5 && k.spread == 0.25);
    CHECK(k.fade_from == 45 && k.fade_halving == 10 && k.cooler_start == 40 && k.cooler_top == 80);
    CHECK(k.cooler_draw == 3 && k.cooler_threshold == 600 && k.cooler_maximum == 1300 && k.outside == 250);
}

// A heat trip's rule (power.h, step_heat) beside appendix B's cut-out.
void test_step_heat() {
    PowerSettings s; // restart gap 50 mV, off-time 20 ticks
    const i64 limit = 100 * kDegree, cool = 90 * kDegree;
    bool on = true, tripped = false;
    u64 off_at = 0;
    // On, under its limit, at a good voltage: nothing.
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, 99 * kDegree, limit, cool, true, s, 5) == PowerChange::none);
    CHECK(on && !tripped);
    // At its limit it trips, whatever the voltage.
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, limit, limit, cool, true, s, 6) == PowerChange::tripped);
    CHECK(!on && tripped);
    // It stays off while it's above its cool-down point.
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, 95 * kDegree, limit, cool, true, s, 7) == PowerChange::none);
    CHECK(!on && tripped);
    // At the cool-down point it's back at once: no off-time, no restart gap.
    CHECK(step_heat(&on, &tripped, &off_at, 860, 850, cool, limit, cool, true, s, 8) == PowerChange::restarted);
    CHECK(on && !tripped);
    // Tripped again, then cooled while the voltage is under its threshold:
    // it's a cut-out from that tick, and comes back by the cut-out's rule.
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, limit + 1, limit, cool, true, s, 9) == PowerChange::tripped);
    CHECK(step_heat(&on, &tripped, &off_at, 800, 850, 80 * kDegree, limit, cool, true, s, 30) == PowerChange::none);
    CHECK(!on && !tripped && off_at == 30);
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, 80 * kDegree, limit, cool, true, s, 40) == PowerChange::none);
    CHECK(step_heat(&on, &tripped, &off_at, 880, 850, 80 * kDegree, limit, cool, true, s, 50) == PowerChange::none);
    CHECK(step_heat(&on, &tripped, &off_at, 900, 850, 80 * kDegree, limit, cool, true, s, 50) ==
          PowerChange::restarted);
    CHECK(on);
    // A cut-out, as before heat.
    CHECK(step_heat(&on, &tripped, &off_at, 800, 850, 30 * kDegree, limit, cool, true, s, 60) == PowerChange::cut_out);
    CHECK(!on && !tripped && off_at == 60);
    // A cut-out device that would restart while past its limit stays off,
    // as tripped.
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, limit, limit, cool, true, s, 100) == PowerChange::none);
    CHECK(!on && tripped);
    // With heat off, the cut-out's rule and nothing more.
    on = true;
    tripped = false;
    CHECK(step_heat(&on, &tripped, &off_at, 1000, 850, 500 * kDegree, limit, cool, false, s, 200) ==
          PowerChange::none);
    CHECK(on && !tripped);
}

// --- a place warming ---------------------------------------------------------------

// A device's draw over the place's last tick, as account_draw shares it out.
struct Devices {
    u32 n = 0;
    u64 draw[kMaxCores + kMaxMemDevices + 3];
    i64 temp[kMaxCores + kMaxMemDevices + 3];
    u32 kind[kMaxCores + kMaxMemDevices + 3];
};

void read_devices(const Place& p, Devices* out) {
    Devices& d = *out;
    d.n = 0;
    for (u32 c = 0; c < p.core_count(); ++c) {
        d.kind[d.n] = p.cores[c].kind == DeviceKind::e_core ? kHeatE : kHeatP;
        d.temp[d.n] = p.core_temp[c];
        d.draw[d.n++] = p.core_draw[c];
    }
    u32 on = 0;
    for (u32 m = 0; m < p.mem.device_count(); ++m)
        on += p.memory_state[m].on ? 1 : 0;
    const u64 hold = static_cast<u64>(p.power().memory_hold_mw) * 1000;
    const u64 access = on ? static_cast<u64>(p.power().memory_access_uw) * p.activations : 0;
    bool first = true;
    for (u32 m = 0; m < p.mem.device_count(); ++m) {
        u64 dd = 0;
        if (p.memory_state[m].on) {
            dd = hold + access / on + (first ? access % on : 0);
            first = false;
        }
        d.kind[d.n] = kHeatMemory;
        d.temp[d.n] = p.memory_temp[m];
        d.draw[d.n++] = dd;
    }
    d.kind[d.n] = kHeatDisk;
    d.temp[d.n] = p.disk_temp;
    d.draw[d.n++] = p.draw.disk;
    d.kind[d.n] = kHeatNetwork;
    d.temp[d.n] = p.network_temp;
    d.draw[d.n++] = p.draw.network;
    if (p.has_cooler) {
        d.kind[d.n] = kHeatCooler;
        d.temp[d.n] = p.cooler_temp;
        d.draw[d.n++] = p.draw.cooling;
    }
}

void test_warming() {
    // One place with a 1,000 mV source of its own, a counter running, and
    // no leakage, so every draw is a number the equations can take as given.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build_world(w, cfg, kByHand, "heat.leakage = 0\nsources.fixed = 0 steady 60 30\n", &why));
    Place& p = w.place(0);
    CHECK(w.heated() && p.heat.on && !p.has_cooler);
    CHECK(p.temp == 25 * kDegree && p.env.temperature_dc == 250);
    CHECK_EQ_U64(w.cooling_rate(0), 2500000);
    place_program(p, kCounter, sizeof kCounter);
    const HeatLaw& law = p.heat;
    // The equations in doubles, a tick at a time, fed each tick's draws.
    double tp = 25;
    double td[kMaxCores + kMaxMemDevices + 3];
    Devices d;
    read_devices(p, &d);
    for (u32 k = 0; k < d.n; ++k)
        td[k] = 25;
    double worst = 0;
    for (u32 t = 0; t < 12000; ++t) {
        w.tick();
        read_devices(p, &d);
        double into = static_cast<double>(w.grid().place_heat(0)) / 1e6;
        const double before = tp;
        for (u32 k = 0; k < d.n; ++k) {
            const double h = static_cast<double>(law.sink[d.kind[k]]) / 1e6;
            const double c = static_cast<double>(law.capacity[d.kind[k]]) / 1e6;
            into += h * (td[k] - before); // what it passes its place
            td[k] += (static_cast<double>(d.draw[k]) / 1e6 - h * (td[k] - before)) / c;
            worst = fabs(deg(d.temp[k]) - td[k]) > worst ? fabs(deg(d.temp[k]) - td[k]) : worst;
        }
        tp += (into - 2.5 * (before - 25)) / 1250;
        worst = fabs(deg(p.temp) - tp) > worst ? fabs(deg(p.temp) - tp) : worst;
    }
    CHECK(worst < 0.005);
    // At rest, by hand: the place is above the outside by all that's made
    // in it over its cooling rate, and each device above the place by its
    // draw over its heatsink's rate.
    const double made = static_cast<double>(p.draw.total() + w.grid().place_heat(0)) / 1e6;
    CHECK(fabs(deg(p.temp) - (25 + made / 2.5)) < 0.01);
    bool rest = true;
    double hottest = 0;
    for (u32 k = 0; k < d.n; ++k) {
        const double want = deg(p.temp) + static_cast<double>(d.draw[k]) / static_cast<double>(law.sink[d.kind[k]]);
        rest = rest && fabs(deg(d.temp[k]) - want) < 0.01;
        hottest = deg(d.temp[k]) > hottest ? deg(d.temp[k]) : hottest;
    }
    CHECK(rest);
    CHECK(p.env.temperature_dc == static_cast<i32>((p.temp + 50000) / 100000));
    // The heat leaving the world is the power the source gives: what
    // reaches the place and what it loses inside.
    const double given = static_cast<double>(w.grid().given_uw + static_cast<i64>(w.grid().inside_uw));
    CHECK(fabs(static_cast<double>(w.heat_out_uw) - given) < given * 0.002);
    CHECK(p.trips[kHeatP] + p.trips[kHeatE] + p.trips[kHeatMemory] + p.trips[kHeatDisk] + p.trips[kHeatNetwork] == 0);
    printf("  heat: one place drawing %.2f W, its source losing %.2f W inside: 12,000 ticks beside the equations "
           "in doubles, never more than %.4f degrees apart; at rest the place at %.2f (by hand %.2f), its hottest "
           "device at %.2f; %.3f W leaves the world, the source gives %.3f W\n",
           static_cast<double>(p.draw.total()) / 1e6, static_cast<double>(w.grid().place_heat(0)) / 1e6, worst,
           deg(p.temp), 25 + made / 2.5, hottest, static_cast<double>(w.heat_out_uw) / 1e6, given / 1e6);
}

// The clamp in a world: rates set absurdly high against the capacities.
void test_clamp() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build_world(w, cfg, kByHand,
                      "heat.leakage = 0\nsources.fixed = 0 steady 60 30\nheat.place.cooling = 100000\n"
                      "heat.place.capacity = 0.000001\nheat.p.sink = 10000\nheat.e.sink = 10000\n"
                      "heat.memory.sink = 10000\nheat.disk.sink = 10000\nheat.network.sink = 10000\n"
                      "heat.p.capacity = 0.000001\nheat.e.capacity = 0.000001\nheat.memory.capacity = 0.000001\n"
                      "heat.disk.capacity = 0.000001\nheat.network.capacity = 0.000001\n",
                      &why));
    Place& p = w.place(0);
    place_program(p, kLax, sizeof kLax);
    bool sane = true;
    i64 high = 0;
    Devices d;
    for (u32 t = 0; t < 400; ++t) {
        w.tick();
        read_devices(p, &d);
        // Nothing under the outside, nothing under its place, and nothing
        // above its own balance: a watt through 10,000 W a degree is a ten
        // thousandth of a degree.
        sane = sane && p.temp >= 25 * kDegree && p.temp <= 25 * kDegree + 2000;
        for (u32 k = 0; k < d.n; ++k) {
            sane = sane && d.temp[k] >= 25 * kDegree && d.temp[k] <= 25 * kDegree + 2000 + 5000;
            high = d.temp[k] > high ? d.temp[k] : high;
        }
    }
    CHECK(sane && p.births > 20);
    printf("  heat: with every rate set 10,000 times too high and every capacity a millionth of a watt-tick, a "
           "busy place stayed between the outside and %.4f degrees for 400 ticks\n",
           deg(high));
}

// Leakage: a hot device draws more, by its own temperature.
void test_leakage() {
    World a, b;
    Config ca, cb;
    TextBuf why;
    const char* const one = "sources.fixed = 0 steady 60 30\nheat.place.cooling = 0.5\n";
    TextBuf off;
    off.append(Str(one, strlen(one)));
    off.append("heat.leakage = 0\n");
    CHECK(build_world(a, ca, kByHand, off.c_str(), &why));
    CHECK(build_world(b, cb, kByHand, one, &why));
    for (u32 t = 0; t < 15000; ++t) {
        a.tick();
        b.tick();
    }
    const Place& p = a.place(0);
    const Place& q = b.place(0);
    CHECK(q.draw.total() > p.draw.total() && q.temp > p.temp);
    // At rest a device's temperature holds still, so its draw is its draw
    // with no leakage, times 1 + 1% a degree it is above the outside.
    CHECK_EQ_U64(q.draw.disk, q.heat.leaky(p.draw.disk, q.disk_temp));
    CHECK_EQ_U64(q.draw.network, q.heat.leaky(p.draw.network, q.network_temp));
    const double want = static_cast<double>(p.draw.disk) * (1 + 0.01 * (deg(q.disk_temp) - 25));
    CHECK(fabs(static_cast<double>(q.draw.disk) - want) < 2);
    CHECK(deg(q.disk_temp) > 40);
    printf("  heat: leakage: a quiet place cooling at 0.5 W a degree rests at %.2f degrees drawing %.3f W without "
           "it, and at %.2f drawing %.3f W with it; its disk, at %.2f degrees, draws %.4f W for its 1 W\n",
           deg(p.temp), static_cast<double>(p.draw.total()) / 1e6, deg(q.temp),
           static_cast<double>(q.draw.total()) / 1e6, deg(q.disk_temp), static_cast<double>(q.draw.disk) / 1e6);
}

// --- trips ---------------------------------------------------------------------------

// A core past its limit trips: its running thread ends with the cause
// "heat", and it comes back at its cool-down point, with no off-time.
void test_trip() {
    World w;
    Config cfg;
    TextBuf why;
    // A place with its own source (the supply by place), driven tick by
    // tick here, so its own temperature stays at the outside; P cores
    // only, that trip at 40 degrees, with a small heat capacity so they
    // warm and cool within a few ticks.
    CHECK(build_world(w, cfg, "supply.model = place\n",
                      "hw.e_share = 0\nheat.p.limit = 40\nheat.cooldown = 10\nheat.p.capacity = 1.1\n"
                      "heat.leakage = 0\npower.off_time = 200\n",
                      &why));
    Place& p = w.place(0);
    const u32 t = place_program(p, kCounter, sizeof kCounter);
    const u64 uid = p.os.program(p.os.thread(t).program).uid;
    u32 tripped_at = 0, back_at = 0, core = 0;
    bool heat_death = false;
    i64 temp_at_trip = 0, temp_before_back = 0, temp_at_back = 0;
    for (u32 k = 1; k <= 200 && !back_at; ++k) {
        const u32 on = p.os.thread(t).state != ThreadState::free ? p.os.thread(t).core : 0;
        const i64 was = tripped_at ? p.core_temp[core] : 0;
        p.os.clear_facts();
        p.tick();
        if (!tripped_at) {
            if (on && p.core_tripped[on - 1]) {
                tripped_at = k;
                core = on - 1;
                temp_at_trip = p.core_temp[core];
                for (usize i = 0; i < p.os.deaths.size(); ++i)
                    heat_death = heat_death || (p.os.deaths[i].cause == DeathCause::heat && p.os.deaths[i].uid == uid);
                CHECK(!p.cores[core].working && p.cores[core].speed == 0 && p.core_draw[core] == 0);
                CHECK(p.os.thread(t).state == ThreadState::free);
            }
        } else if (p.cores[core].working) {
            back_at = k;
            temp_before_back = was;
            temp_at_back = p.core_temp[core];
        } else {
            CHECK(p.core_tripped[core] && p.core_draw[core] == 0);
            // Still above its cool-down point as the tick began.
            CHECK(was > 30 * kDegree);
        }
    }
    CHECK(tripped_at != 0 && back_at != 0 && heat_death);
    CHECK(p.trips[kHeatP] >= 1 && p.heat_threads == 1 && p.cut_threads == 0 && p.cutouts[0] == 0);
    CHECK(p.heat_ticks[0] >= back_at - tripped_at && p.off_ticks[0] == 0);
    // It came back the tick it had cooled to 30 degrees: far sooner than
    // the off-time of 200 ticks a cut-out would wait.
    CHECK(temp_before_back <= 30 * kDegree && back_at - tripped_at < 50);
    CHECK(p.env.voltage_mv == 1000);
    printf("  heat: a P core running a counter tripped at tick %u (%.2f degrees after the tick, limit 40), its "
           "thread ended with the cause \"heat\", and it was back at tick %u, the tick after it had cooled to "
           "%.2f (cool-down point 30; the off-time of a cut-out was set to 200)\n",
           tripped_at, deg(temp_at_trip), back_at, deg(temp_before_back));
    (void)temp_at_back;
}

// Gap 7: a heat trip of the memory device holding the reset region ends in
// a cold start when it cools.
void test_region_trip() {
    World w;
    Config cfg;
    TextBuf why;
    // Memory that trips at 28 degrees: a device's holding draw of 0.5 W
    // through 0.1 W a degree would rest 5 above the place.
    CHECK(build_world(w, cfg, "supply.model = place\n",
                      "heat.memory.limit = 28\nheat.cooldown = 2\nheat.leakage = 0\nregion.retry = 3\n", &why));
    Place& p = w.place(0);
    place_program(p, kLax, sizeof kLax);
    const u32 rd = p.mem.region_device();
    u32 tripped_at = 0, back_at = 0;
    u64 faded = 0;
    u32 alive_at_trip = 0, alive_before_back = 0;
    for (u32 k = 1; k <= 400 && !back_at; ++k) {
        const u32 alive = p.os.live_threads();
        p.os.clear_facts();
        p.tick();
        if (!tripped_at && p.memory_tripped[rd]) {
            tripped_at = k;
            alive_at_trip = alive;
            CHECK(!p.memory_state[rd].on && p.env.voltage_mv == 1000 && p.cold_starts == 0);
        } else if (tripped_at && p.memory_state[rd].on) {
            back_at = k;
            alive_before_back = alive;
        } else if (tripped_at) {
            faded = p.noise.faded(rd) > faded ? p.noise.faded(rd) : faded;
        }
    }
    CHECK(tripped_at != 0 && back_at != 0);
    // One cold start, the tick it was back; and nothing from before lives
    // on past it (most had ended already, as their memory faded).
    CHECK(p.cold_starts == 1 && alive_at_trip > 0 && p.os.live_threads() == 0);
    CHECK(p.trips[kHeatMemory] >= 1 && p.memory_cutouts == 0 && p.region_heat_ticks >= back_at - tripped_at);
    // While it was tripped it held nothing: its cells faded as with no
    // voltage, though the place was at 1,000 mV.
    CHECK(faded > 0);
    printf("  heat: the reset region's memory device tripped at tick %u at 1,000 mV with %u threads alive; %llu of "
           "its cells faded while it was off (%u threads were left by then), and at tick %u it had cooled: a cold "
           "start, and no thread from before it\n",
           tripped_at, alive_at_trip, static_cast<unsigned long long>(faded), alive_before_back, back_at);
}

// A hot memory device loses the share a cold one would in half the time
// for each 10 degrees.
void test_fade_hot() {
    // Three places of one seed at 500 mV, under every memory threshold, so
    // every device is cut out and fading. With no heatsink a device that
    // draws nothing keeps the temperature it's given.
    const double temps[3] = {45, 55, 65};
    Array<u64> cells[3];
    u64 all = 0;
    for (u32 k = 0; k < 3; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_world(w, cfg, "supply.model = place\n", "supply.voltage = 900\nheat.memory.sink = 0\n", &why));
        Place& p = w.place(0);
        // The voltage drops under the thresholds by hand: its source goes.
        for (u32 t = 0; t < 4; ++t)
            p.tick();
        CHECK(p.memory_state[0].on);
        const u32 devices = p.mem.device_count();
        for (u32 d = 0; d < devices; ++d)
            p.memory_temp[d] = udeg(temps[k]);
        p.supplied = true; // from here its voltage is what it's given
        p.given_mv = 500;
        all = static_cast<u64>(p.mem.device(0).size) * 8;
        for (u32 t = 0; t < 2400; ++t) {
            p.tick();
            cells[k].push(p.noise.faded(0));
        }
        CHECK(!p.memory_state[0].on && !p.memory_tripped[0] && p.memory_temp[0] == udeg(temps[k]));
    }
    // At 55 degrees after t ticks, what 45 loses in 2t; at 65, in 4t.
    u32 compared = 0;
    bool halves = true, quarters = true;
    for (u32 t = 1; t <= 600; ++t) {
        // cells[k][i] is the count after i + 1 ticks cut out (the first of
        // them the tick it cut out, which fades too).
        halves = halves && cells[1][t - 1] == cells[0][2 * t - 1];
        quarters = quarters && cells[2][t - 1] == cells[0][4 * t - 1];
        ++compared;
    }
    CHECK(halves && quarters);
    CHECK(cells[0][599] > 0 && cells[0][599] < cells[1][599] && cells[1][599] < cells[2][599] && cells[2][599] < all);
    printf("  heat: memory at 500 mV: after 600 ticks a device at 45 degrees had lost %llu of its %llu cells, one "
           "at 55 %llu and one at 65 %llu; tick for tick the device at 55 had what the one at 45 had after twice "
           "as long, and the one at 65 after four times (%u ticks compared)\n",
           static_cast<unsigned long long>(cells[0][599]), static_cast<unsigned long long>(all),
           static_cast<unsigned long long>(cells[1][599]), static_cast<unsigned long long>(cells[2][599]), compared);
}

// --- a world at rest -----------------------------------------------------------------

const char* const kLand = "world.places = 48\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "os.strict_share = 0\n"
                          "supply.model = sources\n"
                          "heat.on = true\n";

bool build_land(World& w, Config& cfg, u64 seed, const char* extra, TextBuf* why) {
    char head[48];
    snprintf(head, sizeof head, "world.seed = %llu\n", static_cast<unsigned long long>(seed));
    TextBuf text;
    text.append(Str(kLand, strlen(kLand)));
    text.append(Str(head, strlen(head)));
    if (!parse_over(cfg, text.c_str(), extra))
        return false;
    if (!w.init(cfg, why)) {
        printf("  heat: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

// Sources that hold still: no day, no swings, no drop-outs or swells, and
// lives far longer than the run.
const char* const kStill = "sources.life = 1000000000\nsources.chance = 0\n"
                           "source.steady.swing = 0\nsource.steady.dropouts = 0\n"
                           "source.daylight.swing = 0\nsource.daylight.dropouts = 0\nsource.daylight.night = 1\n"
                           "source.gusty.swing = 0\nsource.gusty.dropouts = 0\nsource.gusty.swells = 0\n"
                           "region.retry = 1000000\n";

// Each place's resting temperature, worked in doubles from what is made in
// it and the rates out of it: to the outside, and to its neighbours along
// the proximity links with no water under them.
void rest_exact(World& w, double spread, Array<double>* out) {
    const u32 n = w.place_count();
    const Map& map = w.map();
    Array<double>& t = *out;
    t.resize(0);
    t.resize(n);
    t.fill(25);
    for (u32 sweep = 0; sweep < 200000; ++sweep) {
        double move = 0;
        for (u32 i = 0; i < n; ++i) {
            const Place& p = w.place(i);
            const double q = (static_cast<double>(p.passed_uw) + static_cast<double>(w.grid().place_heat(i))) / 1e6;
            const double c = static_cast<double>(w.cooling_rate(i)) / 1e6 +
                             static_cast<double>(p.heat.cooler_rate) / 1e6 * static_cast<double>(p.cooler_work) /
                                 1048576.0;
            double num = q + c * 25, den = c;
            for (u32 j = 0; j < map.power_degree(i); ++j) {
                const PowerLink& l = map.power_link(map.power_link_at(i, j));
                if (l.water > 0 || !(spread > 0))
                    continue;
                const double k = spread * map.mean_length() / l.length;
                num += k * t[map.power_other(map.power_link_at(i, j), i)];
                den += k;
            }
            const double nt = num / den;
            move = fabs(nt - t[i]) > move ? fabs(nt - t[i]) : move;
            t[i] = nt;
        }
        if (move < 1e-12)
            break;
    }
}

void test_world_rest() {
    World w;
    Config cfg;
    TextBuf why;
    // Every source at 1,000 mV, far below the maxima, and nothing running:
    // a quiet world.
    TextBuf extra;
    extra.append(Str(kStill, strlen(kStill)));
    extra.append("source.steady.voltage = 1000\nsource.daylight.voltage = 1000\nsource.gusty.voltage = 1000\n"
                 "cooler.share = 0\n");
    CHECK(build_land(w, cfg, 5, extra.c_str(), &why));
    for (u32 t = 0; t < 30000; ++t)
        w.tick();
    const u32 n = w.place_count();
    // The cooling rates: the base rate x the place's cooling field x (1 +
    // the coast factor x its share of water).
    bool rates = true;
    u32 coastal = 0;
    double least = 1e9, most = 0;
    for (u32 i = 0; i < n; ++i) {
        const double want = 2500000.0 * w.map().cooling(i) * (1 + 1.0 * w.map().water_share(i));
        rates = rates && fabs(static_cast<double>(w.cooling_rate(i)) - want) < 1;
        coastal += w.map().water_share(i) > 0 ? 1 : 0;
        least = want < least ? want : least;
        most = want > most ? want : most;
    }
    CHECK(rates && coastal > 0 && most > least);
    // At rest against doubles.
    Array<double> exact;
    rest_exact(w, 0.25, &exact);
    double worst = 0, rise = 0, lit_rise = 0;
    u64 trips = 0;
    u32 land_links = 0, wet_links = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = w.place(i);
        worst = fabs(deg(p.temp) - exact[i]) > worst ? fabs(deg(p.temp) - exact[i]) : worst;
        rise = deg(p.temp) - 25 > rise ? deg(p.temp) - 25 : rise;
        if (p.env.voltage_mv > 0)
            lit_rise += deg(p.temp) - 25;
        for (u32 k = 0; k < kHeatKinds; ++k)
            trips += p.trips[k];
    }
    for (u32 k = 0; k < w.map().power_link_count(); ++k) {
        land_links += w.map().power_link(k).water > 0 ? 0 : 1;
        wet_links += w.map().power_link(k).water > 0 ? 1 : 0;
    }
    CHECK(worst < 0.01);
    CHECK(trips == 0);
    // The heat leaving the world is the power the sources give (what
    // reaches their places and what they lose inside), less what the sea
    // takes: the shore drains, and the water's share of the links' losses.
    const Grid& g = w.grid();
    const double gives = static_cast<double>(g.given_uw) + static_cast<double>(g.inside_uw);
    const double sea = static_cast<double>(g.drains_uw + g.links_sea_uw);
    CHECK(fabs(static_cast<double>(w.heat_out_uw) - (gives - sea)) < gives * 0.005);
    CHECK(sea > 0);
    // A quiet world's places sit a few degrees above the outside: each by
    // its idle draw over its cooling rate. Nothing is near a limit.
    CHECK(rise > 0 && rise < 10);
    printf("  heat: 48 quiet places at rest after 30,000 ticks: each within %.4f degrees of its temperature "
           "solved in doubles (%u links on land carry heat, %u over water none); cooling rates %.2f to %.2f W a "
           "degree, %u places on the coast; the sources give %.3f W, the sea takes %.3f W, and %.3f W leaves the "
           "world as heat; the warmest place is %.2f degrees above the outside, the average %.2f, and nothing "
           "tripped\n",
           worst, land_links, wet_links, least / 1e6, most / 1e6, coastal, gives / 1e6, sea / 1e6,
           static_cast<double>(w.heat_out_uw) / 1e6, rise, lit_rise / n);
    // With no heat between places the same world rests elsewhere: a place
    // by its own heat alone.
    World alone;
    Config ca;
    extra.append("heat.spread = 0\n");
    CHECK(build_land(alone, ca, 5, extra.c_str(), &why));
    for (u32 t = 0; t < 30000; ++t)
        alone.tick();
    double apart = 0, own = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = alone.place(i);
        const double q = (static_cast<double>(p.passed_uw) + static_cast<double>(alone.grid().place_heat(i))) / 1e6;
        const double want = 25 + q / (static_cast<double>(alone.cooling_rate(i)) / 1e6);
        own = fabs(deg(p.temp) - want) > own ? fabs(deg(p.temp) - want) : own;
        apart = fabs(deg(p.temp) - deg(w.place(i).temp)) > apart ? fabs(deg(p.temp) - deg(w.place(i).temp)) : apart;
    }
    CHECK(own < 0.01 && apart > 0.01);
    printf("  heat: with no heat between places each rests at its own heat over its own cooling rate (within "
           "%.4f degrees), up to %.2f degrees from where the links put it\n",
           own, apart);
}

void seed_programs(World& w, u32 every) {
    for (u32 i = 0; i < w.place_count(); i += every)
        if (w.place(i).env.voltage_mv >= 900)
            place_program(w.place(i), kLax, sizeof kLax);
}

void test_threads() {
    u64 hashes[3] = {};
    const u32 threads[3] = {1, 2, 24};
    for (u32 k = 0; k < 3; ++k) {
        World w;
        Config cfg;
        TextBuf why;
        // Places that cool badly, so heat matters within the run.
        CHECK(build_land(w, cfg, 5, "heat.place.cooling = 0.3\nheat.place.capacity = 60\n", &why));
        w.start_threads(threads[k]);
        seed_programs(w, 3);
        for (u32 t = 0; t < 400; ++t)
            w.tick();
        hashes[k] = w.hash();
        if (k == 0) {
            u64 trips = 0, births = 0;
            u32 coolers = 0, working = 0;
            double hot = 0;
            for (u32 i = 0; i < w.place_count(); ++i) {
                const Place& p = w.place(i);
                for (u32 j = 0; j < kHeatKinds; ++j)
                    trips += p.trips[j];
                births += p.births;
                coolers += p.has_cooler ? 1 : 0;
                working += p.cooler_work > 0 ? 1 : 0;
                hot = deg(p.temp) > hot ? deg(p.temp) : hot;
                TextBuf broken;
                CHECK(p.check(&broken));
            }
            CHECK(coolers == 16 && trips > 0 && births > 0);
            printf("  heat: 48 places cooling badly, with replicators: after 400 ticks the hottest place is at "
                   "%.1f degrees, %llu heat trips, %u of the %u cooling devices working, %llu births\n",
                   hot, static_cast<unsigned long long>(trips), working, coolers,
                   static_cast<unsigned long long>(births));
        }
    }
    CHECK_EQ_U64(hashes[0], hashes[1]);
    CHECK_EQ_U64(hashes[0], hashes[2]);
    printf("  heat: the same world hash on 1, 2 and 24 host threads\n");
}

// --- cooling devices (step 6) ----------------------------------------------------------

// The resting temperature of a place making q watts with a cooling device,
// by bisection on its balance: q + the device's draw = (the place's rate +
// the device's rate x its share) x (T - outside), its share rising evenly
// from 40 to 80 degrees.
double cooled_rest(double q, double rate, double cooler_rate, double cooler_draw) {
    double lo = 25, hi = 2000;
    for (u32 k = 0; k < 200; ++k) {
        const double t = 0.5 * (lo + hi);
        const double s = t <= 40 ? 0 : (t >= 80 ? 1 : (t - 40) / 40);
        const double net = q + cooler_draw * s - (rate + cooler_rate * s) * (t - 25);
        if (net > 0)
            lo = t;
        else
            hi = t;
    }
    return 0.5 * (lo + hi);
}

void test_cooler() {
    // Twins: a quiet place that cools at 0.2 W a degree, with a cooling
    // device and without. No leakage, so what's made doesn't follow the
    // temperature.
    const char* const one = "sources.fixed = 0 steady 60 30\nheat.place.cooling = 0.2\nheat.place.capacity = 100\n"
                            "heat.leakage = 0\n";
    World a, b;
    Config ca, cb;
    TextBuf why;
    CHECK(build_world(a, ca, kByHand, one, &why));
    TextBuf with;
    with.append(Str(one, strlen(one)));
    with.append("cooler.share = 1\n");
    CHECK(build_world(b, cb, kByHand, with.c_str(), &why));
    const Place& p = a.place(0);
    const Place& q = b.place(0);
    CHECK(!p.has_cooler && q.has_cooler);
    for (u32 t = 0; t < 40000; ++t) {
        a.tick();
        b.tick();
    }
    // The twin without: its heat over its rate.
    const double made = static_cast<double>(p.draw.total() + a.grid().place_heat(0)) / 1e6;
    const double bare = 25 + made / 0.2;
    CHECK(fabs(deg(p.temp) - bare) < 0.02);
    // The twin with: everything but the device's draw is the same heat
    // (the source's loss follows the draw a little, so it's read off).
    const double others = static_cast<double>(q.draw.total() - q.draw.cooling + b.grid().place_heat(0)) / 1e6;
    const double want = cooled_rest(others, 0.2, 2.5, 3);
    CHECK(fabs(deg(q.temp) - want) < 0.02);
    CHECK(deg(q.temp) < deg(p.temp) - 5);
    // And it draws more: the device's draw at its share of full.
    const double share = (want - 40) / 40;
    CHECK(q.draw.total() > p.draw.total());
    CHECK(fabs(static_cast<double>(q.draw.cooling) / 1e6 - 3 * share) < 0.002);
    CHECK(fabs(static_cast<double>(q.cooler_work) / 1048576.0 - share) < 0.001);
    CHECK(q.cooler_state.on && !q.cooler_tripped && q.energy.cooling > 0 && p.energy.cooling == 0);
    // The device's own temperature: above its place by its draw over its
    // heatsink's rate.
    CHECK(fabs(deg(q.cooler_temp) - (deg(q.temp) + static_cast<double>(q.draw.cooling) / 1e6 / 1.0)) < 0.02);
    printf("  cooling: a quiet place making %.2f W and cooling at 0.2 W a degree rests at %.2f degrees (by hand "
           "%.2f); its twin with a cooling device at %.2f (by hand %.2f), the device at %.1f%% of full, drawing "
           "%.3f W: %.2f degrees cooler for %.3f W more\n",
           made, deg(p.temp), bare, deg(q.temp), want, share * 100, static_cast<double>(q.draw.cooling) / 1e6,
           deg(p.temp) - deg(q.temp), static_cast<double>(q.draw.total() - p.draw.total()) / 1e6);

    // Below its threshold it cools nothing: the same twins at 800 mV, with
    // the device's threshold at 900. The twin with one rests where the
    // twin without does, and draws the same.
    const char* const dim = "source.steady.voltage = 800\nsources.fixed = 0 steady 60 30\n"
                            "heat.place.cooling = 0.15\nheat.place.capacity = 30\nheat.leakage = 0\n"
                            "cooler.threshold = 900\n";
    World c, d;
    Config cc, cd;
    CHECK(build_world(c, cc, kByHand, dim, &why));
    TextBuf dim_with;
    dim_with.append(Str(dim, strlen(dim)));
    dim_with.append("cooler.share = 1\n");
    CHECK(build_world(d, cd, kByHand, dim_with.c_str(), &why));
    for (u32 t = 0; t < 40000; ++t) {
        c.tick();
        d.tick();
    }
    const Place& r = c.place(0);
    const Place& s = d.place(0);
    CHECK(s.has_cooler && !s.cooler_state.on && s.cooler_cutouts == 1 && s.cooler_work == 0 && s.draw.cooling == 0);
    CHECK(s.temp - r.temp < 1000 && r.temp - s.temp < 1000); // within a thousandth of a degree
    CHECK(s.draw.total() == r.draw.total() && s.energy.cooling == 0);
    CHECK(deg(s.temp) > 45); // hot enough that a working device would have started
    // With the usual threshold of 600 the same place is cooled.
    World e;
    Config ce;
    CHECK(build_world(e, ce, kByHand,
                      "source.steady.voltage = 800\nsources.fixed = 0 steady 60 30\nheat.place.cooling = 0.15\n"
                      "heat.place.capacity = 30\nheat.leakage = 0\ncooler.threshold = 600\ncooler.share = 1\n",
                      &why));
    for (u32 t = 0; t < 40000; ++t)
        e.tick();
    CHECK(e.place(0).cooler_state.on && e.place(0).temp < r.temp - kDegree && e.place(0).draw.cooling > 0);
    printf("  cooling: at %d mV with the device's threshold at 900 it's cut out: the place rests at %.2f degrees, "
           "its twin without at %.2f, drawing the same; with the threshold at 600 it rests at %.2f\n",
           s.env.voltage_mv, deg(s.temp), deg(r.temp), deg(e.place(0).temp));
}

// The places with a cooling device: the share the setting asks for, where
// the coolers' field is highest.
void test_cooler_places() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build_land(w, cfg, 3, "world.places = 300\n", &why));
    Array<double> field;
    w.map().field_values(w.seed(), Field::coolers, w.map_settings(), &field);
    u32 have = 0;
    double least_with = 1e300, most_without = -1e300;
    for (u32 i = 0; i < 300; ++i) {
        if (w.place(i).has_cooler) {
            ++have;
            least_with = field[i] < least_with ? field[i] : least_with;
        } else {
            most_without = field[i] > most_without ? field[i] : most_without;
        }
    }
    CHECK(have == 100 && least_with >= most_without);
    // None with heat off, and none with the share at nothing.
    World off, zero;
    Config co, cz;
    CHECK(build_land(off, co, 3, "heat.on = false\n", &why));
    CHECK(build_land(zero, cz, 3, "cooler.share = 0\n", &why));
    u32 any = 0;
    for (u32 i = 0; i < 48; ++i)
        any += (off.place(i).has_cooler ? 1 : 0) + (zero.place(i).has_cooler ? 1 : 0);
    CHECK(any == 0);
    printf("  cooling: 300 places: %u have a cooling device, the %u the coolers' field puts highest\n", have, have);
}

} // namespace

void suite_heat() {
    test_laws();
    test_step_heat();
    test_warming();
    test_clamp();
    test_leakage();
    test_trip();
    test_region_trip();
    test_fade_hot();
    test_world_rest();
    test_threads();
    test_cooler();
    test_cooler_places();
}
