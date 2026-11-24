// Flaky parts (design.md 7.5; build-plan-gate5.md step 9, gap 12).
//
// - The draws: the share of devices that are flaky and their factors, from
//   the seed, the place, the device's slot and how often it was replaced.
// - The law, without a place: a flaky core's wrong results and a flaky
//   memory device's cosmic-ray flips at the factor times the kind's rate, a
//   falling rate included; a worn core's floor rising with its wear.
// - In worlds: the share over many seeds, the same whatever the programs do;
//   a hand-placed thread's errors at the factor; replaced devices drawing
//   again; a snapshot taken part way; and the offs.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/noise.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

// ---------------------------------------------------------------------------
// The law, without a place
// ---------------------------------------------------------------------------

struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Noise noise;
    Array<u32> failed;

    // A P core and an E core; two memory devices, of 16 KB and 8 KB.
    Rig(const NoiseSettings& s, const FlakySettings& f, u32 core0, u32 core1, u32 mem0, u32 mem1) {
        static const MemDeviceSpec devs[] = {{16384, 64, 700}, {8192, 64, 600}};
        MemorySpec ms;
        ms.page_size = 256;
        ms.region_size = 1024;
        ms.devices = devs;
        ms.device_count = 2;
        CHECK(m.init(ms, nullptr));
        cores[0].kind = DeviceKind::p_core;
        cores[1].kind = DeviceKind::e_core;
        env.place_id = 3;
        CHECK(noise.init(&m, cores, 2, &env, 7, s, PowerSettings{}, nullptr));
        noise.init_flaky(f);
        const u32 cq[2] = {core0, core1}, mq[2] = {mem0, mem1};
        noise.start_flaky(cq, mq);
    }

    // The wrong results among a core's next n, as cpu.cpp counts them down.
    u64 errors(u32 core, u64 n) {
        ErrorClock& c = cores[core].clock;
        u64 wrong = 0;
        for (u64 i = 0; i < n; ++i) {
            if (i % 4096 == 0)
                ++env.tick;
            if (c.left != ~0ull && --c.left == 0) {
                c.next(&c);
                ++wrong;
            }
        }
        return wrong;
    }

    // The cosmic-ray flips in each device over `ticks` ticks.
    void cosmic(u64 ticks, u64* flips) {
        flips[0] = flips[1] = 0;
        for (u64 t = 0; t < ticks; ++t) {
            failed.resize(0);
            noise.cosmic_flips(&failed);
            for (usize i = 0; i < failed.size(); ++i)
                ++flips[failed[i] < 16384 ? 0 : 1];
            ++env.tick;
        }
    }
};

// A count within so many standard deviations of what was expected.
bool near(u64 got, double want, double sigmas = 5) {
    return fabs(static_cast<double>(got) - want) < sigmas * sqrt(want);
}

void test_law() {
    NoiseSettings s;
    s.p_floor = 1e-3;
    s.e_floor = 2e-3;
    s.cosmic = 0;
    FlakySettings f;
    f.share = 0.02;
    f.worn = 10;
    const u64 n = 2000000;
    // Cores: neither flaky; then the P core at 20 times its kind's floor.
    u64 plain[2], flaky20 = 0, worn_half = 0, worn_end = 0, both = 0;
    {
        Rig r(s, f, kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
        plain[0] = r.errors(0, n);
        plain[1] = r.errors(1, n);
        CHECK(near(plain[0], n * 1e-3) && near(plain[1], n * 2e-3));
    }
    {
        Rig r(s, f, 20 * kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
        CHECK(r.noise.rate(0, 10) == to_p64(1e-3) / kFlakyOne * 20 * kFlakyOne + to_p64(1e-3) % kFlakyOne * 20 ||
              fabs(static_cast<double>(r.noise.rate(0, 10)) / static_cast<double>(to_p64(1e-3)) - 20) < 1e-9);
        CHECK(r.noise.rate(1, 10) == to_p64(2e-3));
        flaky20 = r.errors(0, n);
        CHECK(near(flaky20, n * 20e-3) && near(r.errors(1, n), n * 2e-3));
        // A like-for-like setting changes nothing; a new factor redraws.
        const u64 left = r.cores[0].clock.left;
        r.noise.set_core_floor(0, 20 * kFlakyOne, 0, true);
        CHECK(r.cores[0].clock.left == left);
        r.noise.set_core_floor(0, kFlakyOne, 0, true);
        CHECK(r.noise.rate(0, 10) == to_p64(1e-3) && near(r.errors(0, n), n * 1e-3));
    }
    // A worn core: half way through its life at 5.5 times its own floor, at
    // the end at 10 times; a flaky one's own floor is its factor's.
    {
        Rig r(s, f, kFlakyOne, 3 * kFlakyOne, kFlakyOne, kFlakyOne);
        r.noise.set_core_floor(0, kFlakyOne, 128, true);
        worn_half = r.errors(0, n);
        r.noise.set_core_floor(0, kFlakyOne, 256, true);
        worn_end = r.errors(0, n);
        r.noise.set_core_floor(1, 3 * kFlakyOne, 256, true);
        both = r.errors(1, n);
        CHECK(near(worn_half, n * 5.5e-3) && near(worn_end, n * 10e-3) && near(both, n * 60e-3));
        CHECK(r.noise.worn_floor());
        // Without `again` (a load) the countdown stands.
        const u64 left = r.cores[0].clock.left;
        r.noise.set_core_floor(0, kFlakyOne, 0, false);
        CHECK(r.cores[0].clock.left == left && r.noise.rate(0, 10) == to_p64(1e-3));
    }
    // A rate can't pass a half.
    {
        Rig r(s, f, 5000 * kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
        CHECK(r.noise.rate(0, 10) == 1ull << 63);
    }
    // Memory: the second device at 30 times the cosmic-ray rate.
    u64 flips[2], fall[2];
    const u64 ticks = 20000;
    {
        NoiseSettings ms = s;
        ms.p_floor = ms.e_floor = 0;
        ms.cosmic = 1e-6;
        Rig r(ms, f, kFlakyOne, kFlakyOne, kFlakyOne, 30 * kFlakyOne);
        CHECK(r.noise.cosmic_rate(0) == to_p64(1e-6));
        CHECK(fabs(static_cast<double>(r.noise.cosmic_rate(1)) / static_cast<double>(to_p64(1e-6)) - 30) < 1e-9);
        r.cosmic(ticks, flips);
        CHECK(near(flips[0], 131072 * 1e-6 * ticks) && near(flips[1], 65536 * 30e-6 * ticks));
        CHECK(r.noise.cosmic == flips[0] + flips[1]);
    }
    // A falling rate: from 1 in 100,000 towards 1 in 10 million, its excess
    // halving every 2,000 ticks in eight steps. The flaky device follows it
    // at 30 times.
    double want = 0;
    {
        NoiseSettings ms = s;
        ms.p_floor = ms.e_floor = 0;
        ms.cosmic = 1e-7;
        ms.cosmic_start = 1e-5;
        ms.cosmic_half_life = 2000;
        Rig r(ms, f, kFlakyOne, kFlakyOne, kFlakyOne, 30 * kFlakyOne);
        r.cosmic(ticks, fall);
        for (u64 t = 0; t < ticks; ++t) {
            const u64 k = t / 2000 * 8 + t % 2000 * 8 / 2000;
            want += 1e-7 + (1e-5 - 1e-7) * pow(2.0, -static_cast<double>(k) / 8);
        }
        CHECK(near(fall[0], 131072 * want) && near(fall[1], 65536 * 30 * want));
    }
    printf("  flaky: the law: of 2,000,000 results, %llu and %llu wrong on a P and an E core at floors of 1 and 2 in "
           "1,000; %llu on a flaky P core at 20 times; %llu and %llu on a core half worn and worn out, at 5.5 and "
           "10 times; %llu on a flaky E core worn out, at 30 times\n",
           static_cast<ull>(plain[0]), static_cast<ull>(plain[1]), static_cast<ull>(flaky20),
           static_cast<ull>(worn_half), static_cast<ull>(worn_end), static_cast<ull>(both));
    printf("  flaky: memory, 20,000 ticks at 1 in a million a bit a tick: %llu flips in 16 KB, %llu in a flaky 8 KB "
           "at 30 times (%.0f and %.0f expected); with a falling rate, %llu and %llu (%.0f and %.0f)\n",
           static_cast<ull>(flips[0]), static_cast<ull>(flips[1]), 131072 * 1e-6 * ticks, 65536 * 30e-6 * ticks,
           static_cast<ull>(fall[0]), static_cast<ull>(fall[1]), 131072 * want, 65536 * 30 * want);
}

// The draws: the share and the factors, by place, slot and replacement.
void test_draws() {
    NoiseSettings s;
    FlakySettings f;
    f.share = 0.02;
    Rig r(s, f, kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
    u32 flaky = 0, low = 0, again = 0, total = 0;
    double logs = 0;
    u32 least = ~0u, most = 0;
    for (u32 place = 0; place < 4000; ++place) {
        r.env.place_id = place;
        for (u32 slot = 0; slot < 80; ++slot) {
            const u32 q = r.noise.flaky_factor(slot, 0);
            CHECK(q == r.noise.flaky_factor(slot, 0)); // the same draw every time
            ++total;
            if (q == kFlakyOne)
                continue;
            ++flaky;
            least = q < least ? q : least;
            most = q > most ? q : most;
            logs += log(q / 65536.0);
            low += q / 65536.0 < sqrt(300.0) ? 1 : 0;
            again += r.noise.flaky_factor(slot, 1) != kFlakyOne ? 1 : 0; // its replacement draws again
        }
    }
    r.env.place_id = 3;
    // 320,000 devices: 6,400 flaky, give or take 80.
    CHECK(near(flaky, total * 0.02));
    CHECK(least >= 3 * kFlakyOne && least < 3 * kFlakyOne * 102 / 100 && most <= 100 * kFlakyOne &&
          most > 100 * kFlakyOne / 102 * 100);
    // Evenly on a log scale: half under the middle factor, 17.3; the mean
    // of the logs half way between the ends'.
    CHECK(near(low, flaky * 0.5) && fabs(logs / flaky - (log(3.0) + log(100.0)) / 2) < 0.07);
    CHECK(again < flaky * 4 / 100); // a replacement is flaky as often as any device: 2%
    // A share of 0: none. A share of 1: every one.
    {
        FlakySettings none;
        Rig z(s, none, kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
        FlakySettings all;
        all.share = 1;
        all.factor_min = all.factor_max = 7;
        Rig a(s, all, kFlakyOne, kFlakyOne, kFlakyOne, kFlakyOne);
        for (u32 slot = 0; slot < 80; ++slot) {
            CHECK(z.noise.flaky_factor(slot, 0) == kFlakyOne && z.noise.flaky_factor(slot, 5) == kFlakyOne);
            CHECK(a.noise.flaky_factor(slot, 0) == 7 * kFlakyOne && a.noise.flaky_factor(slot, 5) == 7 * kFlakyOne);
        }
        CHECK(!z.noise.worn_floor());
    }
    printf("  flaky: the draws: %u of %u devices flaky at a share of 0.02, factors from %.2f to %.2f, %u under the "
           "middle factor of 17.3; %u of their replacements flaky too\n",
           flaky, total, least / 65536.0, most / 65536.0, low, again);
}

// ---------------------------------------------------------------------------
// In worlds
// ---------------------------------------------------------------------------

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "os.page_sizes = 256\n"
                           "region.retry = 3\n"
                           "supply.sag = 50\n";

// The 14-byte replicator for a lax place (as test_hammer.cpp's).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// Adds 1 to A for ever: a result every instruction.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-3)};

bool build(World& w, Config& cfg, const char* extra, TextBuf* why, bool say = true) {
    Config::ParseError pe;
    if (!cfg.parse(kSmall, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        if (say)
            printf("  flaky: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

// Over many seeds the share and the factors match the settings, and a
// seed's flaky devices are the same whatever the programs do.
void test_seeds() {
    u32 devices = 0, flaky = 0, low = 0, places_with = 0, places = 0;
    for (u32 seed = 1; seed <= 24; ++seed) {
        char extra[160];
        snprintf(extra, sizeof extra, "world.seed = %u\nworld.places = 48\nflaky.share = 0.02\n", seed);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        Array<u32> first;
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Place& p = w.place(i);
            bool any = false;
            for (u32 k = 0; k < p.core_count() + p.mem.device_count(); ++k) {
                const bool core = k < p.core_count();
                const u32 q = core ? p.noise.core_flaky(k) : p.noise.memory_flaky(k - p.core_count());
                CHECK(q == p.noise.flaky_factor(core ? k : kWearSlotMemory + k - p.core_count(), 0));
                first.push(q);
                ++devices;
                if (q == kFlakyOne)
                    continue;
                CHECK(q >= 3 * kFlakyOne && q <= 100 * kFlakyOne);
                ++flaky;
                low += q / 65536.0 < sqrt(300.0) ? 1 : 0;
                any = true;
            }
            places_with += any ? 1 : 0;
            ++places;
        }
        if (seed > 3)
            continue;
        // The same seed with replicators at work for 150 ticks.
        World w2;
        Config cfg2;
        CHECK(build(w2, cfg2, extra, &why));
        for (u32 i = 0; i < w2.place_count(); ++i)
            memcpy(w2.place(i).mem.bytes() + w2.place(i).mem.region_base(), kLax, sizeof kLax);
        w2.start_threads(3);
        for (u32 t = 0; t < 150; ++t)
            w2.tick();
        usize at = 0;
        u64 births = 0;
        for (u32 i = 0; i < w2.place_count(); ++i) {
            const Place& p = w2.place(i);
            births += p.births;
            for (u32 k = 0; k < p.core_count() + p.mem.device_count(); ++k) {
                const bool core = k < p.core_count();
                CHECK(first[at++] == (core ? p.noise.core_flaky(k) : p.noise.memory_flaky(k - p.core_count())));
            }
        }
        CHECK(births > 100);
    }
    CHECK(near(flaky, devices * 0.02) && devices > 7000);
    CHECK(fabs(low - flaky * 0.5) < 5 * sqrt(flaky * 0.25));
    printf("  flaky: 24 seeds of 48 places: %u of %u cores and memory devices flaky (%.2f%%), %u of them under the "
           "middle factor; %u of %u places have one; the same devices with replicators at work\n",
           flaky, devices, 100.0 * flaky / devices, low, places_with, places);
}

// A place's wrong results for the instructions it runs at its cores' factor,
// and its cosmic-ray flips at its memory's.
void test_place() {
    const u32 ticks = 20000;
    u64 errors[2] = {}, cosmic[2] = {}, ran[2] = {};
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg,
                    pass == 0 ? "noise.p_floor = 2e-4\nnoise.e_floor = 2e-4\nnoise.cosmic = 1e-8\nsupply.sag = 0\n"
                              : "noise.p_floor = 2e-4\nnoise.e_floor = 2e-4\nnoise.cosmic = 1e-8\nsupply.sag = 0\n"
                                "flaky.share = 1\nflaky.factor.min = 10\nflaky.factor.max = 10\n",
                    &why));
        Place& p = w.place(0);
        const u32 at = p.mem.alloc(1, 64, 2, 0);
        CHECK(at != 0);
        memcpy(p.mem.bytes() + at, kCounter, sizeof kCounter);
        CHECK(p.os.place_by_hand(1, at, at, 0) != 0);
        for (u32 c = 0; c < p.core_count(); ++c)
            CHECK(p.noise.core_flaky(c) == (pass ? 10 * kFlakyOne : kFlakyOne));
        for (u32 t = 0; t < ticks; ++t)
            w.tick();
        errors[pass] = p.noise.result_errors;
        cosmic[pass] = p.noise.cosmic;
        ran[pass] = p.sched.instructions + p.region.instructions;
    }
    // The wrong results follow the work done, which the errors themselves
    // change: for each instruction run, ten times as many, to within the
    // scatter and the mix of instructions. The flips are ten times as many.
    const double per[2] = {static_cast<double>(errors[0]) / ran[0], static_cast<double>(errors[1]) / ran[1]};
    CHECK(errors[0] > 200 && errors[1] > 200 && ran[0] > 200000 && ran[1] > 200000);
    CHECK(per[1] / per[0] > 8 && per[1] / per[0] < 12);
    CHECK(near(cosmic[1], 32768.0 * 8 * ticks * 1e-7, 5) && near(cosmic[0], 32768.0 * 8 * ticks * 1e-8, 5));
    printf("  flaky: a place for 20,000 ticks: %llu wrong results in %llu instructions and %llu cosmic-ray flips; "
           "with every device flaky at 10 times, %llu in %llu (%.1f times as many an instruction) and %llu\n",
           static_cast<ull>(errors[0]), static_cast<ull>(ran[0]), static_cast<ull>(cosmic[0]),
           static_cast<ull>(errors[1]), static_cast<ull>(ran[1]), per[1] / per[0], static_cast<ull>(cosmic[1]));
}

const char* const kWearing = "world.places = 6\nwear.rate = 1\nwear.p.endurance = 500\nwear.e.endurance = 500\n"
                             "wear.memory.endurance = 500\nwear.disk.endurance = 400\nwear.network.endurance = 400\n"
                             "wear.cooler.endurance = 400\nvisit.mean = 250\nvisit.regularity = 0.3\n"
                             "visit.replace_at = 0.8\nnoise.cosmic = 1e-7\n";

u32 step_of(const Worn& w) {
    const u64 part = w.life >> 8;
    const u64 k = part ? w.wear / part : 0;
    return k > 256 ? 256 : static_cast<u32>(k);
}

// Every device's factor is its slot's draw for the device there now, and a
// core's step its wear's. How many devices have been replaced, how many are
// flaky, and how many aren't what the first at their slot was.
void look(World& w, u32* replaced, u32* flaky, u32* changed, u32* stepped) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 c = 0; c < p.core_count(); ++c) {
            const Worn& wn = p.core_worn[c];
            const u32 q = p.noise.core_flaky(c);
            CHECK(q == p.noise.flaky_factor(c, wn.fresh));
            if (!wn.failed)
                CHECK(p.noise.core_step(c) == (p.noise.worn_floor() ? step_of(wn) : 0));
            *replaced += wn.fresh ? 1 : 0;
            *flaky += q != kFlakyOne ? 1 : 0;
            *changed += q != p.noise.flaky_factor(c, 0) ? 1 : 0;
            *stepped += p.noise.core_step(c) > 64 ? 1 : 0;
        }
        for (u32 d = 0; d < p.mem.device_count(); ++d) {
            const Worn& wn = p.memory_worn[d];
            const u32 q = p.noise.memory_flaky(d);
            CHECK(q == p.noise.flaky_factor(kWearSlotMemory + d, wn.fresh));
            *replaced += wn.fresh ? 1 : 0;
            *flaky += q != kFlakyOne ? 1 : 0;
            *changed += q != p.noise.flaky_factor(kWearSlotMemory + d, 0) ? 1 : 0;
        }
    }
}

// Devices wearing out and replaced: each replacement draws again, a worn
// core's floor follows its wear, and a snapshot carries on the same.
void test_replaced() {
    const u32 first = 700, rest = 800;
    char extra[600];
    snprintf(extra, sizeof extra, "%sflaky.share = 0.5\nflaky.factor.min = 3\nflaky.factor.max = 30\nflaky.worn = 10\n",
             kWearing);
    u64 hash[2] = {}, mid = 0;
    ByteWriter saved;
    u32 replaced = 0, flaky = 0, changed = 0, stepped = 0;
    u64 errors = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        for (u32 i = 0; i < w.place_count(); ++i)
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
        w.start_threads(pass == 0 ? 1 : 3);
        for (u32 t = 0; t < first; ++t)
            w.tick();
        if (pass == 0) {
            mid = w.hash();
            snapshot(w, cfg, saved);
            u32 r = 0, f = 0, c = 0;
            look(w, &r, &f, &c, &stepped);
        } else {
            CHECK_EQ_U64(w.hash(), mid);
        }
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        hash[pass] = w.hash();
        if (pass == 0) {
            u32 s = 0;
            look(w, &replaced, &flaky, &changed, &s);
            for (u32 i = 0; i < w.place_count(); ++i)
                errors += w.place(i).noise.result_errors;
        }
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(replaced > 10 && flaky > 5 && changed > 3 && stepped > 3);
    {
        SnapshotHeader sh;
        TextBuf err, why;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &sh, &err) && sh.tick == first);
        World w;
        Config cfg;
        CHECK(snapshot_config(sh, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + sh.world_offset, sh.world_size);
        CHECK(w.load(in, sh.version));
        CHECK_EQ_U64(w.hash(), mid);
        u32 r = 0, f = 0, c = 0, s = 0;
        look(w, &r, &f, &c, &s); // each factor and step is back as it was
        w.start_threads(3);
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
    }
    // The offs: a share of 0 and a worn floor of 1 are the tests' default
    // world, and each on its own makes another.
    u64 off[4] = {};
    for (u32 k = 0; k < 4; ++k) {
        snprintf(extra, sizeof extra, "%s%s", kWearing,
                 k == 0   ? ""
                 : k == 1 ? "flaky.share = 0\nflaky.worn = 1\n"
                 : k == 2 ? "flaky.share = 0.5\n"
                          : "flaky.worn = 10\n");
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, extra, &why));
        for (u32 i = 0; i < w.place_count(); ++i)
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
        w.start_threads(3);
        for (u32 t = 0; t < 600; ++t)
            w.tick();
        off[k] = w.hash();
    }
    CHECK_EQ_U64(off[0], off[1]);
    CHECK(off[2] != off[0] && off[3] != off[0] && off[2] != off[3]);
    // The settings refused.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(!build(w, cfg, "flaky.factor.min = 50\nflaky.factor.max = 40\n", &why, false));
    }
    printf("  flaky: 6 places wearing out over 1,500 ticks at a share of a half: %u devices replaced, %u flaky at "
           "the end, %u not as the first at their slot was; %u cores past a quarter of their life at tick 700, "
           "their floors risen; %llu wrong results; the same on 1 host thread and 3 and from a snapshot of tick "
           "%u; a share of 0 with a worn floor of 1 is the world without\n",
           replaced, flaky, changed, stepped, static_cast<ull>(errors), first);
}

} // namespace

void suite_flaky() {
    test_law();
    test_draws();
    test_seeds();
    test_place();
    test_replaced();
}
