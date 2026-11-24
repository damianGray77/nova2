// The supply before gate 4 (nova2/supply.h; design.md 2, 4.2, 4.3, appendix
// C; build-plan-gate2.md step 3): the day, swings and drop-outs against
// their definitions; the schedule the same whatever the place does; the
// voltage following the place's draw as the equation says; a voltage trace
// through a blackout and through a partial drop-out worked by hand; the
// setup checks; and the supply's state through threads and snapshots.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/snapshot.h"
#include "nova2/supply.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 4\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 1e-4\n"
                           "noise.e_floor = 1e-4\n";

// Section 7's supply, with its day, swings and drop-outs sped up so a short
// test sees all of them.
const char* const kFast = "supply.sag = 50\n"
                          "supply.night = 0.2\n"
                          "supply.day = 200\n"
                          "supply.swing = 0.25\n"
                          "supply.swing_period = 500\n"
                          "supply.dropouts = 1000\n"
                          "supply.dropout.min = 5\n"
                          "supply.dropout.max = 50\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
const u8 kSpin[2] = {encode(kJmp, kImm), 0}; // JMP # 0

// Parses the small world with `extra` on top and builds it.
bool build(World& w, Config& cfg, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(kSmall, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    return w.init(cfg, why);
}

// The replicator in every place's reset region.
void seed_replicators(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i)
        memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
}

// A spinning program on each of a place's cores, so each works all tick.
void spin_all(Place& p) {
    for (u32 c = 0; c < p.core_count(); ++c) {
        const u32 at = p.mem.alloc(c + 1, 64, 2, 0);
        CHECK(at != 0);
        memcpy(p.mem.bytes() + at, kSpin, 2);
        CHECK(p.os.place_by_hand(c + 1, at, at, 0) != 0);
    }
}

const double kOne = static_cast<double>(kCapacityOne);

// The day: the night's share at midnight, full at noon, sin^2 of the phase
// in between, each place at its own phase; off when the night is full.
void test_day() {
    SupplySettings s;
    s.night = 0.2;
    s.day = 2000;
    Supply a;
    CHECK(a.init(s, 1, 0, 10000000, nullptr));
    const u64 d = s.day, ph = a.phase();
    const u64 midnight = (d - ph) % d, noon = (d / 2 + d - ph) % d;
    CHECK(a.day(midnight) == 209715); // 0.2 x 2^20, rounded
    CHECK(a.day(noon) == kCapacityOne);
    double worst = 0;
    bool rising = true;
    for (u64 t = 0; t < d; ++t) {
        const double x = static_cast<double>((t + ph) % d) / static_cast<double>(d);
        const double want = 0.2 + 0.8 * sin(3.14159265358979 * x) * sin(3.14159265358979 * x);
        const double got = a.day(t) / kOne;
        if (fabs(got - want) > worst)
            worst = fabs(got - want);
        // From midnight to noon it only rises.
        const u64 since = (t + ph) % d;
        if (since > 0 && since <= d / 2 && a.day(t) < a.day(t - 1 + (t == 0 ? d : 0)))
            rising = false;
    }
    CHECK(worst < 0.003); // Bhaskara's sine is within 0.0016
    CHECK(rising);
    // A day repeats, and each place has its own phase.
    CHECK(a.day(midnight + 5 * d) == a.day(midnight));
    u32 distinct = 0;
    for (u32 p = 1; p < 10; ++p) {
        Supply b;
        CHECK(b.init(s, 1, p, 10000000, nullptr));
        distinct += b.phase() != a.phase() ? 1 : 0;
    }
    CHECK(distinct >= 8);
    // No day: full all the time.
    s.night = 1;
    Supply c;
    CHECK(c.init(s, 1, 0, 10000000, nullptr));
    bool full = true;
    for (u64 t = 0; t < 3000; ++t)
        full = full && c.day(t) == kCapacityOne;
    CHECK(full);
    printf("  supply: the day's capacity is within %.4f of sin^2 between a fifth and full\n", worst);
}

// Swings: between full and (1 - swing), changing smoothly, averaging halfway.
void test_swing() {
    SupplySettings s;
    s.swing = 0.25;
    s.swing_period = 2000;
    Supply a;
    CHECK(a.init(s, 1, 0, 10000000, nullptr));
    const u32 lo = kCapacityOne - 262144; // 1 - 0.25
    bool inside = true;
    u32 biggest_step = 0;
    double sum = 0;
    const u64 n = 400 * s.swing_period;
    for (u64 t = 0; t < n; ++t) {
        const u32 v = a.swing(t);
        inside = inside && v >= lo && v <= kCapacityOne;
        if (t > 0) {
            const u32 prev = a.swing(t - 1);
            const u32 step = v > prev ? v - prev : prev - v;
            if (step > biggest_step)
                biggest_step = step;
        }
        sum += v;
    }
    CHECK(inside);
    // A smoothstep's steepest is 1.5 times the straight line's.
    CHECK(biggest_step <= 262144 * 3 / 2 / s.swing_period + 2);
    const double mean = sum / static_cast<double>(n) / kOne;
    CHECK(fabs(mean - 0.875) < 0.02);
    // Another place drifts on its own.
    Supply b;
    CHECK(b.init(s, 1, 1, 10000000, nullptr));
    u32 differ = 0;
    for (u64 t = 0; t < 10 * s.swing_period; t += 100)
        differ += a.swing(t) != b.swing(t) ? 1 : 0;
    CHECK(differ > 90);
    s.swing = 0;
    Supply c;
    CHECK(c.init(s, 1, 0, 10000000, nullptr));
    CHECK(c.swing(12345) == kCapacityOne);
    printf("  supply: swings averaged %.3f of full over 400 periods\n", mean);
}

// Drop-outs: as many as the rate says, lengths evenly on a log scale, the
// blackout share, what the rest leave; and the capacity they give, tick by
// tick, agreeing with the list of them.
void test_dropouts() {
    SupplySettings s;
    s.dropouts = 500; // a million-tick average gap shortened to 2,000
    s.dropout_min = 10;
    s.dropout_max = 1000;
    s.blackout_share = 0.5;
    s.keep_min = 0;
    s.keep_max = 0.5;
    Supply a;
    CHECK(a.init(s, 7, 3, 10000000, nullptr));
    CHECK(a.block() == 1024);
    const u64 stretches = 4000; // about 4 million ticks
    u32 n = 0, short_ones = 0, blackouts = 0, partial = 0;
    double keep_sum = 0;
    bool lengths_ok = true, keeps_ok = true, starts_ok = true;
    for (u64 j = 0; j < stretches; ++j) {
        Dropout d;
        if (!a.dropout_in(j, &d))
            continue;
        ++n;
        lengths_ok = lengths_ok && d.length >= 10 && d.length <= 1000;
        starts_ok = starts_ok && d.start >= j * 1024 && d.start < (j + 1) * 1024;
        short_ones += d.length < 100 ? 1 : 0; // 100 is the log scale's middle
        if (d.blackout()) {
            ++blackouts;
        } else {
            ++partial;
            keeps_ok = keeps_ok && d.keep <= kCapacityOne / 2;
            keep_sum += d.keep / kOne;
        }
    }
    // 0.512 a stretch: 2,048 expected, give or take 32.
    CHECK(n > 1900 && n < 2200);
    CHECK(lengths_ok && keeps_ok && starts_ok);
    CHECK(fabs(static_cast<double>(short_ones) / n - 0.5) < 0.05);
    CHECK(fabs(static_cast<double>(blackouts) / n - 0.5) < 0.05);
    CHECK(fabs(keep_sum / partial - 0.25) < 0.02);
    // Tick by tick, the capacity is the deepest drop-out under way.
    const u64 ticks = 60 * 1024;
    u32* want = new u32[ticks];
    for (u64 t = 0; t < ticks; ++t)
        want[t] = kCapacityOne;
    for (u64 j = 0; j < 60; ++j) {
        Dropout d;
        if (!a.dropout_in(j, &d))
            continue;
        for (u64 t = d.start; t < d.start + d.length && t < ticks; ++t)
            if (d.keep < want[t])
                want[t] = d.keep;
    }
    bool same = true;
    u32 low = 0;
    for (u64 t = 0; t < ticks; ++t) {
        same = same && a.dropout(t) == want[t];
        low += want[t] < kCapacityOne ? 1 : 0;
    }
    delete[] want;
    CHECK(same && low > 0);
    // None at a rate of 0.
    s.dropouts = 0;
    Supply b;
    CHECK(b.init(s, 7, 3, 10000000, nullptr));
    Dropout d;
    CHECK(!b.dropout_in(5, &d) && b.dropout(5000) == kCapacityOne);
    printf("  supply: %u drop-outs in %llu stretches, %u blackouts, %.0f%% shorter than 100 ticks\n", n,
           static_cast<unsigned long long>(stretches), blackouts, 100.0 * short_ones / n);
}

// Done when (1): two worlds with the same seed and different programs have
// the same supply schedule; their voltages part where their draws do.
void test_schedule_independent() {
    World a, b;
    Config ca, cb;
    TextBuf why;
    CHECK(build(a, ca, kFast, &why));
    CHECK(build(b, cb, kFast, &why));
    seed_replicators(b);
    spin_all(b.place(0));
    a.start_threads(2);
    b.start_threads(2);
    bool same = true;
    u32 parted = 0, low = 0;
    for (u32 t = 0; t < 400; ++t) {
        a.tick();
        b.tick();
        for (u32 i = 0; i < a.place_count(); ++i) {
            same = same && a.place(i).capacity == b.place(i).capacity;
            low += a.place(i).capacity < kCapacityOne / 2 ? 1 : 0;
            parted += a.place(i).env.voltage_mv != b.place(i).env.voltage_mv ? 1 : 0;
        }
    }
    CHECK(same);
    CHECK(low > 0);    // nights and drop-outs happened
    CHECK(parted > 0); // and the draws told
}

// Done when (2): each tick, a place's voltage is Vs x Gs c / (Gs c + P / V^2),
// from the capacity, its last tick's draw P and the voltage V it drew at.
// And the source's conductance is what the place's full draw makes it.
void test_voltage_equation() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, kFast, &why));
    seed_replicators(w);
    w.start_threads(2);
    const u32 n = w.place_count();
    u64 last_p[8];
    i32 last_v[8];
    u32 checked = 0, off = 0, worst = 0;
    // The full draw at 950 mV, by hand, and the conductance it gives.
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        u64 full = 0;
        for (u32 c = 0; c < p.core_count(); ++c)
            full += core_work_uw(p.cores[c].kind == DeviceKind::e_core ? p.power().e : p.power().p, 950);
        full += 500000ull * p.mem.device_count() + 1000000 + 1000000; // memory holding, the disk, the network device
        CHECK(p.supply.full_draw() == full);
        const double gs = static_cast<double>(full) / (950.0 * 950.0) * 950.0 / 50.0; // siemens
        CHECK(fabs(p.supply.conductance() / kOne / gs - 1) < 1e-6);
    }
    for (u32 t = 0; t < 600; ++t) {
        for (u32 i = 0; i < n; ++i) {
            last_p[i] = w.place(i).draw.total();
            last_v[i] = w.place(i).env.voltage_mv;
        }
        w.tick();
        for (u32 i = 0; i < n; ++i) {
            Place& p = w.place(i);
            const double c = p.capacity / kOne;
            double want;
            if (p.capacity == 0) {
                want = 0;
            } else if (last_p[i] == 0 || last_v[i] == 0) {
                want = 1000;
            } else {
                const double gs = p.supply.conductance() / kOne * c;
                const double g = static_cast<double>(last_p[i]) / (static_cast<double>(last_v[i]) * last_v[i]);
                want = 1000 * gs / (gs + g);
            }
            const i32 got = p.env.voltage_mv;
            const double diff = fabs(got - want);
            if (diff > worst)
                worst = static_cast<u32>(diff + 0.5);
            checked += diff < 0.501 ? 1 : 0;
            off += got == 0 ? 1 : 0;
        }
    }
    CHECK(checked == 600 * n); // every tick, to the nearest millivolt
    CHECK(off > 0);            // blackouts were among them
    printf("  supply: %u place-ticks followed the equation to the millivolt, %u of them blacked out\n", checked, off);
}

// The hand-worked trace (done when, 3; and step 4's states): a place of two P
// cores, each running a spinning program, so it draws its full draw at
// whatever voltage it's at. So at full capacity it sits at the full-draw
// voltage, 950 mV. Its trace is worked tick by tick from the equation, with
// the draw and each device's state written out by hand, through a drop-out:
// - a blackout: nothing, every device cut out, then the full 1,000 mV the
//   first tick back, since nothing drew at 0 V, and every device back on.
//   There the place cold-starts (step 6), so its trace ends;
// - one leaving 0.6 of capacity, where it settles near 930 mV;
// - one leaving 0.2, a night's worth, and lasting three off-times. Without
//   cut-outs the one-tick lag wouldn't settle (section 8's risk): above the
//   P cores' threshold they'd draw and sag the voltage below it, and below
//   it they'd draw nothing and it would spring back, every tick. With them
//   (4.1, 4.4) the P cores cut out, stay off for the off-time, and come back
//   only once the voltage is at their threshold plus the gap: a slower
//   cycle, set by the off-time.
// The pause switch is on, so the spinners wait out each cut-out and the
// place stays busy.

// A device as worked by hand: off below its threshold, back on at its
// threshold plus 50 mV once 20 ticks have passed since it went off.
struct HandDevice {
    bool on = true;
    u64 off_at = 0;
    void step(double mv, double threshold, u64 t) {
        if (on && mv < threshold) {
            on = false;
            off_at = t;
        } else if (!on && t - off_at >= 20 && mv >= threshold + 50) {
            on = true;
        }
    }
};

// The place's draw at mv with its devices as they are, in microwatts.
double hand_draw(double mv, bool p_on, bool mem_on, bool rest_on, u32 devices) {
    double p = 0;
    if (p_on && mv > 850) // each P core: k V^2 s, k in nW per V^2 per cycle, s = slope x (V - Vth)
        p += 2 * 400000.0 * (166667.0 * (mv - 850) / 1000) / 1000 * (mv / 1000.0) * (mv / 1000.0);
    if (mem_on)
        p += 500000.0 * devices;
    if (rest_on) // the disk and the network device
        p += 2000000;
    return p;
}

// keep: the share a partial drop-out leaves, or below 0 for a blackout.
void trace_dropout(const char* kind, double keep, u32 length, u32 want_cuts) {
    const bool blackout = keep < 0;
    char extra[640];
    snprintf(extra, sizeof extra,
             "world.places = 1\nhw.cores.min = 2\nhw.cores.max = 2\nhw.e_share = 0\n"
             "noise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\nos.cutout_pause = true\n"
             "supply.sag = 50\nsupply.dropouts = 1000\nsupply.dropout.min = %u\nsupply.dropout.max = %u\n"
             "supply.blackout_share = %s\nsupply.dropout.keep_min = %g\nsupply.dropout.keep_max = %g\n",
             length, length, blackout ? "1" : "0", blackout ? 0.5 : keep, blackout ? 0.5 : keep);
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, extra, &why));
    Place& p = w.place(0);
    spin_all(p);
    w.start_threads(1);
    const u32 devices = p.mem.device_count();
    // The drop-outs, from the schedule. The one traced is the first to start
    // after tick 50 and at least 50 ticks after the one before it ended, so
    // the place has settled first.
    Dropout first;
    first.length = 0;
    Dropout list[64];
    u32 count = 0;
    u64 last_end = 0;
    for (u64 j = 0; count < 64 && first.length == 0; ++j) {
        Dropout d;
        if (!p.supply.dropout_in(j, &d))
            continue;
        list[count++] = d;
        if (d.start > 50 && d.start >= last_end + 50)
            first = d;
        last_end = d.start + d.length;
    }
    CHECK(first.length == length);
    // A blackout ends in a cold start (step 6): the first tick back reboots
    // the place, ending its spinners, so its trace stops there.
    const u64 end = first.start + first.length + (blackout ? 1 : 30);
    // And any that start before the trace ends.
    for (u64 j = first.start / p.supply.block() + 1; count < 64 && j * p.supply.block() < end; ++j) {
        Dropout d;
        if (p.supply.dropout_in(j, &d))
            list[count++] = d;
    }
    // The source's conductance by hand: Gs = (P_full / 950^2) x 950 / 50.
    const double gs = hand_draw(950, true, true, true, devices) / (950.0 * 950.0) * 19.0;
    double v = 1000; // tick 0: nothing drawn yet
    double drew = 0;
    // Memory's thresholds are 600 to 700 mV. Here the voltage is 0 or above
    // 800, so every device does the same as one at 700 would.
    HandDevice pc, memory, rest;
    u32 matched = 0, states = 0, before = 0, during = 0, back = 0, cuts = 0;
    i32 in_it = 0, after = 0, prev_level = 0; // the voltage on the drop-out's last tick, the first tick back, the tick before
    bool was_on = true;
    u64 last_cut = 0, shortest_off = ~0ull;
    for (u64 t = 0; t < end; ++t) {
        double c = 1;
        for (u32 k = 0; k < count; ++k)
            if (t >= list[k].start && t < list[k].start + list[k].length)
                c = list[k].keep / kOne;
        if (c == 0)
            v = 0;
        else if (drew == 0)
            v = 1000;
        else
            v = floor(1000 * gs * c / (gs * c + drew / (v * v)) + 0.5);
        pc.step(v, 850, t);
        memory.step(v, 700, t);
        rest.step(v, 600, t);
        w.tick();
        const i32 got = p.env.voltage_mv;
        matched += fabs(got - v) <= 1 ? 1 : 0;
        bool same = p.cores[0].working == pc.on && p.cores[1].working == pc.on;
        for (u32 d = 0; d < devices; ++d)
            same = same && p.memory_state[d].on == memory.on;
        same = same && p.disk_state.on == rest.on && p.network_state.on == rest.on;
        states += same ? 1 : 0;
        drew = hand_draw(v, pc.on, memory.on, rest.on, devices);
        // The engine's P cores: never back on within the off-time.
        const bool on = p.cores[0].working;
        if (was_on && !on) {
            last_cut = t;
            cuts += t >= first.start && t < first.start + first.length ? 1 : 0;
        }
        if (!was_on && on && t - last_cut < shortest_off)
            shortest_off = t - last_cut;
        was_on = on;
        if (t + 10 >= first.start && t < first.start)
            before += got >= 949 && got <= 951 ? 1 : 0;
        if (t >= first.start && t < first.start + first.length && t + 5 >= first.start + first.length)
            during += blackout     ? (got == 0 ? 1 : 0)
                      : keep > 0.5 ? (got >= 900 && got < 950 && on ? 1 : 0)
                                   : (got >= 900 && !on ? 1 : 0); // settled, the P cores out
        if (t == first.start + first.length)
            back = blackout ? (got == 1000 && on ? 1 : 0) : 1;
        if (t + 1 == first.start + first.length)
            in_it = got;
        if (t + 2 == first.start + first.length)
            prev_level = got;
        if (t == first.start + first.length)
            after = got;
    }
    CHECK(matched == end);
    CHECK(states == end);
    CHECK(before == 10); // at its full draw, at the full-draw voltage
    CHECK(during == 5);  // nothing, settled at 0.6 of capacity, or settled with the P cores out
    CHECK(back == 1);    // the full voltage the first tick back from a blackout, and every device on
    CHECK(cuts == want_cuts);
    CHECK(shortest_off >= 20); // no flickering within the off-time
    printf("  supply: a %s from tick %llu for %u ticks, worked by hand: %u of %llu ticks to the millivolt, devices "
           "in the same states; %d and %d mV on its last two ticks, %d mV the tick after; the P cores cut out %u "
           "time%s in it\n",
           kind, static_cast<unsigned long long>(first.start), length, matched, static_cast<unsigned long long>(end),
           prev_level, in_it, after, cuts, cuts == 1 ? "" : "s");
}

void test_dropout_trace() {
    trace_dropout("blackout", -1, 20, 1);
    trace_dropout("drop-out to 0.6", 0.6, 20, 0);
    trace_dropout("drop-out to 0.2", 0.2, 60, 3);
}

// Setup refuses a supply it can't make.
void test_supply_checks() {
    const char* const bad[] = {
        "supply.sag = 1000\n",                                           // the whole voltage
        "supply.dropout.min = 100\nsupply.dropout.max = 50\n",           // shortest above longest
        "supply.dropout.keep_min = 0.6\nsupply.dropout.keep_max = 0.5\n", // least above most
        "supply.dropouts = 1000\nsupply.dropout.max = 2000\n",           // 2.05 a stretch of 2,048
        "hw.network.maximum = 620\n",                                    // 600 + the 50 mV gap
    };
    for (u32 i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(!build(w, cfg, bad[i], &why) && !cfg.ok());
    }
    // Section 7's supply builds.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "supply.sag = 50\nsupply.night = 0.2\nsupply.swing = 0.25\nsupply.dropouts = 1\n", &why));
}

// The supply's state (the voltage, the last tick's draw) is in the hash and
// the snapshot: a world under a busy supply comes out the same on 1 and 3
// threads, and resumed from a snapshot taken mid-schedule.
void test_supply_state() {
    u64 h1 = 0, h3 = 0;
    for (u32 run = 0; run < 2; ++run) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, kFast, &why));
        seed_replicators(w);
        w.start_threads(run ? 3 : 1);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        (run ? h3 : h1) = w.hash();
    }
    CHECK_EQ_U64(h1, h3);
    World a;
    Config cfg;
    TextBuf why;
    CHECK(build(a, cfg, kFast, &why));
    seed_replicators(a);
    a.start_threads(2);
    for (u32 t = 0; t < 150; ++t)
        a.tick();
    TextBuf text;
    cfg.write(text);
    ByteWriter out;
    snapshot_begin(a.seed(), a.tick_count(), text.view(), out);
    a.save(out);
    snapshot_finish(out);
    SnapshotHeader h;
    TextBuf err;
    CHECK(snapshot_parse(out.data(), out.size(), &h, &err));
    Config cfg2;
    Config::ParseError pe;
    CHECK(cfg2.parse(h.config_text, &pe));
    World b;
    CHECK(b.init(cfg2, &why));
    ByteReader in(out.data() + h.world_offset, h.world_size);
    CHECK(b.load(in));
    CHECK_EQ_U64(b.hash(), a.hash());
    b.start_threads(2);
    for (u32 t = 0; t < 150; ++t)
        b.tick();
    CHECK_EQ_U64(b.hash(), h1);
}

} // namespace

void suite_supply() {
    test_day();
    test_swing();
    test_dropouts();
    test_schedule_independent();
    test_voltage_equation();
    test_dropout_trace();
    test_supply_checks();
    test_supply_state();
}
