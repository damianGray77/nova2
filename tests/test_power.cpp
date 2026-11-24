// Power on the device side (nova2/power.h; design.md 4.4, 4.5, 7.1,
// appendix C; build-plan-gate2.md step 2): speed against voltage, each
// device's draw against appendix C's equations, the setup checks, and gate
// 1's speeds from a steady supply.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/power.h"
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

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
const u8 kSpin[2] = {encode(kJmp, kImm), 0}; // JMP # 0

// Parses the small world with `extra` on top; false (with cfg's error) if
// the settings are refused.
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

// Speed is linear above the threshold and stops rising at the maximum; the
// slopes give gate 1's stock speeds at 1,000 mV.
void test_speed() {
    PowerSettings s;
    CHECK(core_speed(s.p, 1000) == 25000 && core_speed(s.e, 1000) == 18000);
    CHECK(core_speed(s.p, 850) == 0 && core_speed(s.p, 700) == 0 && core_speed(s.e, 750) == 0);
    CHECK(core_speed(s.p, 925) == 12500); // 166,667 x 75 mV
    CHECK(core_speed(s.e, 875) == 9000);  // 72,000 x 125 mV
    // Equal steps of voltage, equal steps of speed.
    CHECK(core_speed(s.e, 1100) - core_speed(s.e, 1000) == core_speed(s.e, 1000) - core_speed(s.e, 900));
    // Capped at the maximum.
    CHECK(core_speed(s.p, 1300) == 75000 && core_speed(s.p, 1600) == 75000);
    CHECK(core_speed(s.e, 1200) == 32400 && core_speed(s.e, 2000) == 32400);
}

// Appendix C: running, k x V^2 x s, scaled by the clock and the time it ran;
// halted, the idle draw.
void test_core_draw() {
    PowerSettings s;
    CHECK(core_work_uw(s.p, 1000) == 10000000); // 10 W at 1,000 mV
    CHECK(core_work_uw(s.e, 1000) == 2500002);  // 2.5 W
    CHECK(core_work_uw(s.p, 1100) == 16666400ull * 1100 / 1000 * 1100 / 1000); // 41,666 cycles a tick at 1.1 V
    CHECK(core_work_uw(s.p, 800) == 0);         // below the threshold: no speed, no working draw
    // 4.4's example: with a threshold of 0.7 V at 1.0 V, 10% more speed costs about 17% more power.
    CorePower k = s.p;
    k.threshold = 700;
    CHECK(core_speed(k, 1030) * 10 == core_speed(k, 1000) * 11);
    const double up = static_cast<double>(core_work_uw(k, 1030)) / static_cast<double>(core_work_uw(k, 1000));
    CHECK(up > 1.165 && up < 1.169);
    // Over a tick of T stock sixteenths.
    const i64 T = 25000 * 16;
    CHECK(core_draw_uw(s.p, 1000, T, 0, 0) == 1000000);                       // halted: 1 W
    CHECK(core_draw_uw(s.p, 1000, T, T, T * 10) == 10000000);                 // ran it all at stock
    CHECK(core_draw_uw(s.p, 1000, T, T / 2, T / 2 * 10) == 5500000);          // half each
    CHECK(core_draw_uw(s.p, 1000, T, T, T * 15) == 15000000);                 // at 1.5 times stock
    CHECK(core_draw_uw(s.p, 1000, T, T / 4, T / 4 * 5) == 750000 + 1250000);  // a quarter, at half stock
    CHECK(core_draw_uw(s.e, 1000, 18000 * 16, 0, 0) == 300000);               // an E core halted: 0.3 W
    // Memory: holding, per device, and per row activation.
    CHECK(memory_draw_uw(s, 3, 0) == 1500000);
    CHECK(memory_draw_uw(s, 3, 2500) == 1500000 + 2500ull * s.memory_access_uw);
}

// A steady supply at 1,000 mV gives every core gate 1's stock speed, and
// SENSE sees it.
void test_gate1_speeds() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "hw.cores.min = 4\nhw.e_share = 0.5\n", &why));
    u32 p = 0, e = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& pl = w.place(i);
        for (u32 c = 0; c < pl.core_count(); ++c) {
            const bool ecore = pl.cores[c].kind == DeviceKind::e_core;
            const u32 want = ecore ? 18000 : 25000;
            p += !ecore && pl.cores[c].stock_speed == want && pl.env.devices[c].speed == want ? 1 : 0;
            e += ecore && pl.cores[c].stock_speed == want && pl.env.devices[c].speed == want ? 1 : 0;
        }
    }
    CHECK(p > 0 && e > 0 && p + e == 16);
}

// Each place's draw over a tick: its devices' draws add up; a core spinning
// all tick draws its working draw exactly; memory's draw follows its row
// activations.
void test_place_draw() {
    World w;
    Config cfg;
    TextBuf why;
    // No noise, so nothing knocks the spinning programs off their loops.
    CHECK(build(w, cfg, "hw.cores.min = 2\nhw.cores.max = 2\nnoise.p_floor = 0\nnoise.e_floor = 0\nnoise.cosmic = 0\n",
                &why));
    // Place 0: a spinning program for each core, so both run all tick.
    Place& p0 = w.place(0);
    for (u32 c = 0; c < p0.core_count(); ++c) {
        const u32 at = p0.mem.alloc(c + 1, 64, 2, 0);
        CHECK(at != 0);
        memcpy(p0.mem.bytes() + at, kSpin, 2);
        CHECK(p0.os.place_by_hand(c + 1, at, at, 0) != 0);
    }
    // Place 1: the replicator in its reset region, so memory is busy.
    Place& p1 = w.place(1);
    memcpy(p1.mem.bytes() + p1.mem.region_base(), kLax, 14);
    w.start_threads(2);
    u64 acts = 0;
    for (u32 t = 0; t < 12; ++t) {
        w.tick();
        acts += p1.activations;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& pl = w.place(i);
        const PowerSettings& s = pl.power();
        u64 cores = 0;
        u32 in_range = 0;
        for (u32 c = 0; c < pl.core_count(); ++c) {
            const CorePower& k = pl.cores[c].kind == DeviceKind::e_core ? s.e : s.p;
            cores += pl.core_draw[c];
            in_range += pl.core_draw[c] >= k.idle_mw * 1000ull && pl.core_draw[c] <= core_work_uw(k, 1000) ? 1 : 0;
        }
        CHECK(in_range == pl.core_count());
        CHECK(pl.draw.cores == cores && pl.draw.disk == 1000000 && pl.draw.network == 1000000);
        CHECK(pl.draw.memory == memory_draw_uw(s, pl.mem.device_count(), pl.activations));
        CHECK(pl.draw.total() == pl.draw.cores + pl.draw.memory + pl.draw.disk + pl.draw.network);
        CHECK(pl.peak_draw >= pl.draw.total() && pl.energy.total() >= pl.draw.total());
    }
    // The spinning cores ran every sixteenth of the tick, at stock.
    for (u32 c = 0; c < p0.core_count(); ++c) {
        const CorePower& k = p0.cores[c].kind == DeviceKind::e_core ? p0.power().e : p0.power().p;
        CHECK(p0.core_draw[c] == core_work_uw(k, 1000));
    }
    CHECK(p0.activations == 0); // JMP touches no data
    CHECK(acts > 0);            // the replicator's copies do
    printf("  power: a place filling with replicators made %.1f row activations a tick, on %u cores\n",
           static_cast<double>(acts) / 12, p1.core_count());
}

// Setup refuses ranges a device couldn't restart in, and a supply below the
// cores' thresholds.
void test_setup_checks() {
    const char* const bad[] = {
        "hw.p.maximum = 870\n",                            // 850 + the 50 mV gap is above it
        "hw.e.maximum = 790\n",                            // 750 + 50
        "memory.maximum = 720\n",                          // fast memory's 700 + 50
        "disk.maximum = 620\n",                            // 600 + 50
        "supply.voltage = 800\n",                              // below the P cores' 850
        "power.off_time = 0\n",                            // must be at least a tick
        "power.restart_gap = 0\n",                         // must be at least 1 mV
        "hw.e.threshold = 690\n",                          // memory's thresholds must sit below the cores' (4.4)
        "power.restart_gap = 500\nhw.p.maximum = 1300\n",  // 850 + 500 is above 1,300
    };
    for (u32 i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        World w;
        Config cfg;
        TextBuf why;
        const bool ok = build(w, cfg, bad[i], &why);
        CHECK(!ok && !cfg.ok());
    }
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "hw.p.maximum = 900\nsupply.voltage = 880\n", &why)); // 850 + 50 fits exactly
}

} // namespace

void suite_power() {
    test_speed();
    test_core_draw();
    test_gate1_speeds();
    test_place_draw();
    test_setup_checks();
}
