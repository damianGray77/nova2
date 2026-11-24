// Wear and failure (nova2/wear.h; design.md 7.4, 7.6, appendix C;
// build-plan-gate5.md step 2): the law by hand, with a doubling for each 10
// degrees and for each 100 mV over the maximum; lives drawn from half to
// twice their kind's; a place held at the reference conditions, every device
// failing at its drawn life to the tick, and in half the time 10 degrees
// hotter or 100 mV over; a worn core's threshold, speed, cut-out and
// low-voltage margin against a new one's; a worn memory device's weak
// cells, the same cells every time, at the set chance and only while it is
// on; what each kind's failure does; a world through a snapshot; and
// nothing at all with the base rate at 0.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/snapshot.h"
#include "nova2/wear.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 1\n"
                           "world.map = none\n"
                           "memory.size = 32768\n"
                           "memory.devices.min = 2\n"
                           "memory.devices.max = 2\n"
                           "memory.threshold.fast = 650\n"
                           "memory.threshold.slow = 600\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 1000000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 2\n"
                           "hw.e_share = 0\n"
                           "wear.rate = 1\n";

bool build(World& w, Config& cfg, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(Str(kSmall, strlen(kSmall)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  wear: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

void run_at(Place& p, i32 mv) {
    p.supplied = true;
    p.given_mv = mv;
    p.os.clear_facts();
    p.tick();
}

// Counts in A as it runs: ADD # 1; JMP back.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// Sets a low-voltage handler (B = its start + 12, A = 3), then counts in A.
// The handler sleeps 60 ticks and returns.
const u8 kGuarded[18] = {encode(kMov, kB),   encode(kAdd, kImm), 12,     encode(kSto, kB),
                         encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                         encode(kAdd, kImm), 1,                  encode(kJmp, kImm), static_cast<u8>(-2),
                         encode(kMov, kImm), 60,                 encode(kSys, kImm), kSleep,
                         encode(kSys, kImm), kHret};

// A program placed by hand on a memory device; its thread's record.
u32 place_on(Place& p, const u8* code, u32 len, u32 device) {
    const u32 at = p.mem.alloc_on(1, 128, Perm::both, device);
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

// A thread's A: on its core, or saved at the base of its stack (A.3).
u32 reg_a(const Place& p, u32 t) {
    const Thread& th = p.os.thread(t);
    if (th.core)
        return p.cores[th.core - 1].x.r.a;
    u32 v = 0;
    memcpy(&v, p.mem.bytes() + th.stack, 4);
    return v;
}

bool place_ok(const Place& p) {
    TextBuf why;
    const bool ok = p.check(&why);
    if (!ok)
        printf("  wear: %s\n", why.c_str());
    return ok;
}

// --- the law -------------------------------------------------------------------

void test_law() {
    WearSettings s;
    WearLaw w;
    w.init(s);
    const i32 vmax = 1300;
    const i64 ref = 45 * kDegree;
    CHECK(w.on && w.rate == kWearTick);
    // At the reference, inside the range: a tick a tick.
    CHECK_EQ_U64(w.step(ref, 1000, vmax), kWearTick);
    CHECK_EQ_U64(w.step(ref, vmax, vmax), kWearTick);
    // Each 10 degrees doubles it, or halves it below.
    CHECK_EQ_U64(w.step(ref + 10 * kDegree, 1000, vmax), 2 * kWearTick);
    CHECK_EQ_U64(w.step(ref + 50 * kDegree, 1000, vmax), 32 * kWearTick);
    CHECK_EQ_U64(w.step(ref - 10 * kDegree, 1000, vmax), kWearTick / 2);
    CHECK_EQ_U64(w.step(ref - 20 * kDegree, 1000, vmax), kWearTick / 4);
    // Each 100 mV over the maximum doubles it; under the maximum, nothing.
    CHECK_EQ_U64(w.step(ref, vmax + 100, vmax), 2 * kWearTick);
    CHECK_EQ_U64(w.step(ref, vmax + 300, vmax), 8 * kWearTick);
    CHECK_EQ_U64(w.step(ref, 600, vmax), kWearTick);
    // Both at once multiply.
    CHECK_EQ_U64(w.step(ref + 10 * kDegree, vmax + 100, vmax), 4 * kWearTick);
    // Between doublings it follows 2^x, within the table's step of a 32nd.
    for (i32 mv = vmax; mv <= vmax + 200; mv += 7) {
        const double want = pow(2.0, (mv - vmax) / 100.0);
        const double got = static_cast<double>(w.step(ref, mv, vmax)) / static_cast<double>(kWearTick);
        CHECK(got <= want * 1.0001 && got >= want / pow(2.0, 1 / 32.0) * 0.9999);
    }
    for (i64 t = 20 * kDegree; t <= 100 * kDegree; t += 1370000) {
        const double want = pow(2.0, static_cast<double>(t - ref) / 1e7);
        const double got = static_cast<double>(w.step(t, 1000, vmax)) / static_cast<double>(kWearTick);
        CHECK(got <= want * 1.0001 && got >= want / pow(2.0, 1 / 32.0) * 0.9999);
    }
    // A strike, 20 V on a device made for 1.3: past any life in one tick.
    CHECK(w.step(ref, 20000, vmax) == kWearMost);
    CHECK(w.step(25 * kDegree, 20000, vmax) == kWearMost);
    // A 1.8 times swell, 1,800 mV: 32 times, so 1,000 ticks of it are 32,000.
    CHECK_EQ_U64(w.step(ref, 1800, vmax), 32 * kWearTick);

    // A slope of 0 turns its cause off; a rate of 0 turns wear off.
    WearSettings none = s;
    none.heat = 0;
    w.init(none);
    CHECK_EQ_U64(w.step(ref + 40 * kDegree, 1000, vmax), kWearTick);
    CHECK_EQ_U64(w.step(ref, vmax + 100, vmax), 2 * kWearTick);
    none = s;
    none.voltage = 0;
    w.init(none);
    CHECK_EQ_U64(w.step(ref, 20000, vmax), kWearTick);
    CHECK_EQ_U64(w.step(ref + 10 * kDegree, 1000, vmax), 2 * kWearTick);
    none = s;
    none.rate = 0;
    w.init(none);
    CHECK(!w.on && w.step(ref + 40 * kDegree, 20000, vmax) == 0);
    // Two doublings for each 10 degrees, and a rate of a quarter.
    none = s;
    none.heat = 2;
    none.rate = 0.25;
    w.init(none);
    CHECK_EQ_U64(w.step(ref, 1000, vmax), kWearTick / 4);
    CHECK_EQ_U64(w.step(ref + 10 * kDegree, 1000, vmax), kWearTick);

    // The threshold's rise and the weak cells, in proportion.
    w.init(s);
    const u64 life = 1000 * kWearTick;
    CHECK(w.risen(0, life) == 0 && w.risen(life / 2, life) == 25 && w.risen(life, life) == 50);
    CHECK(w.risen(3 * life, life) == 50 && w.risen(life / 50 - 1, life) == 0 && w.risen(life / 50, life) == 1);
    // A device of 16,384 bytes has 131,072 cells: 131 weak at the end of its life.
    CHECK(w.weak_cells(life, life, 131072) == 131 && w.weak_cells(life / 2, life, 131072) == 65);
    CHECK(w.weak_cells(0, life, 131072) == 0 && w.weak_cells(2 * life, life, 131072) == 131);
}

void test_lives() {
    WearSettings s;
    s.endurance[kHeatP] = 1000000;
    WearLaw w;
    w.init(s);
    // Whole ticks from half to twice the kind's, evenly on a log scale: by
    // quarter of the range, about a quarter of the draws each.
    u32 part[4] = {};
    bool in_range = true, whole = true;
    const u32 n = 4000;
    for (u32 i = 0; i < n; ++i) {
        const u64 life = w.life(7, i / 64, i % 64, kHeatP, 0);
        whole = whole && (life & (kWearTick - 1)) == 0;
        const double t = static_cast<double>(life >> kWearBits);
        in_range = in_range && t >= 500000 && t <= 2000000;
        u32 q = static_cast<u32>((log2(t / 1e6) + 1) * 2);
        ++part[q > 3 ? 3 : q];
    }
    CHECK(in_range && whole);
    for (u32 q = 0; q < 4; ++q)
        CHECK(part[q] > n / 4 - 150 && part[q] < n / 4 + 150);
    // The same draw again; another for another slot, place, seed or replacement.
    const u64 a = w.life(7, 3, 5, kHeatP, 0);
    CHECK(a == w.life(7, 3, 5, kHeatP, 0));
    CHECK(a != w.life(7, 3, 6, kHeatP, 0) && a != w.life(7, 4, 5, kHeatP, 0) && a != w.life(8, 3, 5, kHeatP, 0));
    CHECK(a != w.life(7, 3, 5, kHeatP, 1));
}

// --- a place held at set conditions -----------------------------------------------

// Runs the place at mv until every device has failed or `most` ticks have
// passed; fills each device's first tick as failed (the cores, the memory
// devices, the disk, the network device).
void fail_ticks(Place& p, i32 mv, u64 most, u64* at, u32 n) {
    for (u32 i = 0; i < n; ++i)
        at[i] = 0;
    for (u64 t = 1; t <= most; ++t) {
        run_at(p, mv);
        // After t ticks: device_states of tick t - 1 has run.
        u32 i = 0, left = 0;
        for (u32 c = 0; c < p.core_count(); ++c, ++i)
            if (!at[i] && p.core_worn[c].failed)
                at[i] = t - 1;
        for (u32 d = 0; d < p.mem.device_count(); ++d, ++i)
            if (!at[i] && p.memory_worn[d].failed)
                at[i] = t - 1;
        if (!at[i] && p.disk_worn.failed)
            at[i] = t - 1;
        ++i;
        if (!at[i] && p.network_worn.failed)
            at[i] = t - 1;
        for (u32 k = 0; k < n; ++k)
            left += at[k] ? 0 : 1;
        if (!left)
            return;
    }
}

const char* const kShort = "wear.p.endurance = 1000\n"
                           "wear.e.endurance = 1000\n"
                           "wear.memory.endurance = 1500\n"
                           "wear.disk.endurance = 2000\n"
                           "wear.network.endurance = 2500\n"
                           "wear.weak.share = 0\n";

void test_to_the_tick() {
    // Heat is off, so every device sits at the outside temperature: the
    // reference, 45 degrees. Each fails on the tick its drawn life says: it
    // works for that many ticks and no more.
    u64 life[6] = {};
    {
        World w;
        Config cfg;
        TextBuf why;
        TextBuf extra;
        extra.append(kShort);
        extra.append("hw.temperature = 450\n");
        CHECK(build(w, cfg, extra.c_str(), &why));
        Place& p = w.place(0);
        CHECK(p.wear.on && p.core_count() == 2 && p.mem.device_count() == 2);
        u32 i = 0;
        for (u32 c = 0; c < 2; ++c)
            life[i++] = p.core_worn[c].life >> kWearBits;
        for (u32 d = 0; d < 2; ++d)
            life[i++] = p.memory_worn[d].life >> kWearBits;
        life[i++] = p.disk_worn.life >> kWearBits;
        life[i++] = p.network_worn.life >> kWearBits;
        CHECK(life[0] >= 500 && life[0] <= 2000 && life[0] != life[1]);
        CHECK(life[2] >= 750 && life[2] <= 3000 && life[4] >= 1000 && life[4] <= 4000 && life[5] >= 1250);
        u64 at[6];
        fail_ticks(p, 1000, 6000, at, 6);
        for (u32 k = 0; k < 6; ++k)
            CHECK_EQ_U64(at[k], life[k]);
        CHECK(p.failures[kHeatP] == 2 && p.failures[kHeatMemory] == 2 && p.failures[kHeatDisk] == 1 &&
              p.failures[kHeatNetwork] == 1 && p.failures[kHeatCooler] == 0);
        CHECK(place_ok(p));
        // A failed device wears no more.
        const u64 worn = p.core_worn[0].wear;
        run_at(p, 1000);
        CHECK(p.core_worn[0].wear == worn && worn == p.core_worn[0].life);
    }
    // Ten degrees hotter, in half the time (rounded up to a whole tick), and
    // ten cooler, in twice.
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        TextBuf extra;
        extra.append(kShort);
        extra.append(pass == 0 ? "hw.temperature = 550\n" : "hw.temperature = 350\n");
        CHECK(build(w, cfg, extra.c_str(), &why));
        u64 at[6];
        fail_ticks(w.place(0), 1000, 12000, at, 6);
        for (u32 k = 0; k < 6; ++k)
            CHECK_EQ_U64(at[k], pass == 0 ? (life[k] + 1) / 2 : life[k] * 2);
    }
    // And 100 mV over its maximum, in half the time: the memory devices' and
    // the disk's maximum is 1,300 mV here, and the cores' is put at 1,500.
    {
        World w;
        Config cfg;
        TextBuf why;
        TextBuf extra;
        extra.append(kShort);
        extra.append("hw.temperature = 450\nhw.p.maximum = 1500\nhw.network.maximum = 1200\n");
        CHECK(build(w, cfg, extra.c_str(), &why));
        u64 at[6];
        fail_ticks(w.place(0), 1400, 6000, at, 6);
        CHECK_EQ_U64(at[0], life[0]);           // the cores: inside their range
        CHECK_EQ_U64(at[1], life[1]);
        CHECK_EQ_U64(at[2], (life[2] + 1) / 2); // memory: one step over
        CHECK_EQ_U64(at[3], (life[3] + 1) / 2);
        CHECK_EQ_U64(at[4], (life[4] + 1) / 2); // the disk
        CHECK_EQ_U64(at[5], (life[5] + 3) / 4); // the network device: two steps over
    }
    // A spike far over the maximum breaks every device in the tick it lands.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "", &why));
        Place& p = w.place(0);
        run_at(p, 1000);
        CHECK(p.failures[kHeatP] == 0);
        run_at(p, 9000); // the wear of this tick: 77 doublings over the maximum
        run_at(p, 1000);  // and the failures at the next tick's device states
        CHECK(p.core_worn[0].failed && p.core_worn[1].failed && p.memory_worn[0].failed && p.memory_worn[1].failed &&
              p.disk_worn.failed && p.network_worn.failed);
        CHECK(place_ok(p));
    }
}

// --- a worn core -------------------------------------------------------------------

void test_worn_core() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const CorePower kp = p.power().p;
    // Core 0 at half its life, core 1 new. The tick's wear works the rise out.
    p.core_worn[0].wear = p.core_worn[0].life / 2;
    run_at(p, 1000);
    CHECK(p.cores[0].worn_mv == 25 && p.cores[1].worn_mv == 0);
    CHECK(p.core_threshold(0) == kp.threshold + 25 && p.core_threshold(1) == kp.threshold);
    run_at(p, 1000);
    // Its speed is the slope times what it has above its worn threshold
    // (appendix C): a sixth slower at 1,000 mV for a P core at 850.
    CorePower worn = kp;
    worn.threshold += 25;
    CHECK(p.cores[1].speed == core_speed(kp, 1000) && p.cores[0].speed == core_speed(worn, 1000));
    CHECK(p.cores[0].speed * 6 + 6 >= p.cores[1].speed * 5 && p.cores[0].speed * 6 <= p.cores[1].speed * 5);
    // SENSE reads the worn speed and the nominal threshold (R25).
    CHECK(p.os.reading(0, kDeviceSpeed, 0) == p.cores[0].speed && p.os.reading(0, kDeviceSpeed, 1) == p.cores[1].speed);
    CHECK(p.os.reading(0, kDeviceThreshold, 0) == static_cast<u32>(kp.threshold));
    // Above the maximum too it is slower by the same 25 mV.
    run_at(p, kp.maximum + 50);
    CHECK(p.cores[0].speed == core_speed(worn, kp.maximum) && p.cores[0].speed < p.cores[1].speed);
    // It draws by its speed.
    const u32 a = place_on(p, kCounter, sizeof kCounter, 0), b = place_on(p, kCounter, sizeof kCounter, 0);
    for (u32 k = 0; k < 5; ++k)
        run_at(p, 1000);
    CHECK(p.os.thread(a).core != 0 && p.os.thread(b).core != 0);
    CHECK(p.core_draw[0] * 600 > p.core_draw[1] * 495 && p.core_draw[0] * 600 < p.core_draw[1] * 505);
    // It cuts out 25 mV sooner: at 860 mV the new one runs and the worn one doesn't.
    run_at(p, kp.threshold + 30);
    CHECK(p.cores[0].working && p.cores[1].working);
    run_at(p, kp.threshold + 10);
    CHECK(!p.cores[0].working && p.cores[1].working && p.cutouts[0] == 1);
    // And it comes back at its worn threshold plus the restart gap, not before.
    const i32 back = kp.threshold + 25 + p.power().restart_gap;
    for (u32 k = 0; k < 40; ++k)
        run_at(p, back - 1);
    CHECK(!p.cores[0].working);
    run_at(p, back);
    CHECK(p.cores[0].working);
    CHECK(place_ok(p));
}

void test_worn_margin() {
    // The low-voltage interrupt is raised within the margin of the core's
    // worn cut-off: for a core at the end of its life, 50 mV sooner.
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "hw.cores.min = 1\nhw.cores.max = 1\npower.low_voltage_margin = 30\n", &why));
        Place& p = w.place(0);
        const i32 th = p.power().p.threshold;
        if (pass == 1)
            p.core_worn[0].wear = p.core_worn[0].life / 10 * 9; // nine tenths of its life: 44 or 45 mV
        const u32 t = place_on(p, kGuarded, sizeof kGuarded, 0);
        for (u32 k = 0; k < 10; ++k)
            run_at(p, 1000);
        CHECK(p.os.thread(t).handler[kOnLowVoltage] != 0 && p.sched.lv_raised == 0);
        const i32 rise = p.cores[0].worn_mv;
        CHECK(pass == 1 ? rise == 44 || rise == 45 : rise == 0);
        run_at(p, th + rise + 30); // at the margin: not yet
        CHECK(p.sched.lv_raised == 0);
        run_at(p, th + rise + 29);
        CHECK(p.sched.lv_raised == 1);
    }
}

// --- weak cells --------------------------------------------------------------------

u32 bits_differing(const u8* a, const u8* b, u32 n) {
    u32 k = 0;
    for (u32 i = 0; i < n; ++i)
        for (u8 x = static_cast<u8>(a[i] ^ b[i]); x; x = static_cast<u8>(x & (x - 1)))
            ++k;
    return k;
}

void test_weak_cells() {
    // At 720 mV the cores are cut out and memory is on, so nothing but the
    // weak cells changes a byte. Memory device 0 is at half its life, with
    // a hundredth of its cells weak at the end: 655 of its 131,072 now. A
    // weak cell loses its bit one tick in 50.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "wear.weak.share = 0.01\nwear.weak.chance = 0.02\n", &why));
    Place& p = w.place(0);
    const MemDevice dv = p.mem.device(0);
    CHECK(dv.size == 16384);
    p.memory_worn[0].wear = p.memory_worn[0].life / 2;
    run_at(p, 720);
    CHECK(p.noise.weak_cells(0) == 655 && p.noise.weak_cells(1) == 0);
    CHECK(!p.cores[0].working && !p.cores[1].working && p.memory_state[0].on && p.memory_state[1].on);
    Array<u8> before;
    before.append(p.mem.bytes(), p.mem.size());
    const u64 weak0 = p.noise.weak;
    const u32 ticks = 2000;
    for (u32 k = 0; k < ticks; ++k)
        run_at(p, 720);
    // The count: 655 cells x 2,000 ticks at 1 in 50 is 26,200, give or take.
    const u64 lost = p.noise.weak - weak0;
    CHECK(lost > 26200 - 800 && lost < 26200 + 800);
    // Where: only in device 0, and only at its weak cells, its 655 leakiest
    // by rank. Nearly every one of them has been hit, and about half read
    // back other than they were.
    Array<u8> is_weak;
    is_weak.resize(static_cast<usize>(dv.size) * 8);
    for (u64 r = 0; r < 655; ++r)
        is_weak[static_cast<usize>(p.noise.cell_of(0, r))] = 1;
    const u8* now = p.mem.bytes();
    u32 changed = 0, elsewhere = 0;
    for (u32 i = 0; i < p.mem.size(); ++i) {
        const u8 x = static_cast<u8>(before[i] ^ now[i]);
        for (u32 bit = 0; bit < 8; ++bit) {
            if (!((x >> bit) & 1))
                continue;
            ++changed;
            if (i < dv.base || i >= dv.base + dv.size || !is_weak[static_cast<usize>(i - dv.base) * 8 + bit])
                ++elsewhere;
        }
    }
    CHECK(elsewhere == 0);
    CHECK(changed > 655 / 2 - 80 && changed < 655 / 2 + 80);
    // The leakiest are the first to fade when the device cuts out: the same
    // ranks, the same cells.
    CHECK(p.noise.cell_of(0, 0) != p.noise.cell_of(0, 1));

    // Cut out, a device loses no bit to its weak cells (it fades instead).
    const u64 at_cut = p.noise.weak;
    for (u32 k = 0; k < 50; ++k)
        run_at(p, 300);
    CHECK(!p.memory_state[0].on && p.noise.weak == at_cut);
    // Back on, it does again; and as it wears, more cells are weak.
    p.memory_worn[0].wear = p.memory_worn[0].life - kWearTick * 400;
    for (u32 k = 0; k < 100; ++k)
        run_at(p, 800); // above its worn threshold and the restart gap; the cores stay cut out
    CHECK(p.memory_state[0].on && p.noise.weak > at_cut && p.noise.weak_cells(0) > 1200);
    CHECK(place_ok(p));

    // A share of 0: no weak cells however worn.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, "wear.weak.share = 0\n", &why));
    Place& q = w2.place(0);
    q.memory_worn[0].wear = q.memory_worn[0].life / 2;
    Array<u8> start;
    for (u32 k = 0; k < 300; ++k) {
        run_at(q, 720);
        if (k == 0)
            start.append(q.mem.bytes(), q.mem.size());
    }
    CHECK(q.noise.weak == 0 && bits_differing(start.data(), q.mem.bytes(), q.mem.size()) == 0);
}

void test_weak_states() {
    // With os.saved_state_fails, a thread a bit of whose saved state a weak
    // cell loses ends, cause "memory", as for a cosmic-ray flip (appendix C 8).
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "wear.weak.share = 1\nwear.weak.chance = 0.5\nos.saved_state_fails = true\n"
                        "hw.cores.min = 1\nhw.cores.max = 1\n", &why));
    Place& p = w.place(0);
    // Three counters on one core: two are always off it, their registers saved on device 0.
    for (u32 k = 0; k < 3; ++k)
        place_on(p, kCounter, sizeof kCounter, 0);
    for (u32 k = 0; k < 3; ++k)
        run_at(p, 1000);
    CHECK(p.os.live_threads() == 3 && p.failed_threads == 0);
    p.memory_worn[0].wear = p.memory_worn[0].life / 10 * 9;
    u32 by_memory = 0;
    for (u32 k = 0; k < 4; ++k) {
        run_at(p, 1000);
        for (usize i = 0; i < p.os.deaths.size(); ++i)
            by_memory += p.os.deaths[i].cause == DeathCause::memory_lost ? 1 : 0;
    }
    CHECK(p.noise.weak > 1000 && p.failed_threads >= 2 && by_memory >= 2);
    CHECK(place_ok(p));
}

// --- what each kind's failure does (7.6) -------------------------------------------

void test_core_fails() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const u32 a = place_on(p, kCounter, sizeof kCounter, 0), b = place_on(p, kCounter, sizeof kCounter, 0);
    for (u32 k = 0; k < 5; ++k)
        run_at(p, 1000);
    const u32 core_a = p.os.thread(a).core, core_b = p.os.thread(b).core;
    CHECK(core_a != 0 && core_b != 0 && core_a != core_b);
    const u64 uid_a = p.os.program(p.os.thread(a).program).uid;
    p.os.clear_facts();
    p.fail_core(core_a - 1);
    // Its thread ended, cause "failed device"; the other runs on.
    CHECK(p.os.thread(a).state == ThreadState::free && p.os.thread(b).state != ThreadState::free);
    CHECK(p.os.deaths.size() == 1 && p.os.deaths[0].uid == uid_a && p.os.deaths[0].cause == DeathCause::device_failed);
    CHECK(p.failure_threads == 1 && p.failures[kHeatP] == 1 && p.cut_threads == 0);
    CHECK(!p.cores[core_a - 1].working && p.cores[core_a - 1].speed == 0 && place_ok(p));
    // SENSE reads 0 for it in every reading but its kind.
    CHECK(p.os.reading(0, kDeviceKind, core_a - 1) == static_cast<u32>(DeviceKind::p_core));
    CHECK(p.os.reading(0, kDeviceSpeed, core_a - 1) == 0 && p.os.reading(0, kDeviceThreshold, core_a - 1) == 0);
    CHECK(p.os.reading(0, kDeviceSpeed, core_b - 1) != 0 && p.os.reading(0, kDeviceThreshold, core_b - 1) != 0);
    // The scheduler has one fewer: two more programs share the one core
    // left with the one still running, and the failed core never comes back.
    const u32 c = place_on(p, kCounter, sizeof kCounter, 0), d = place_on(p, kCounter, sizeof kCounter, 0);
    bool on_failed = false, ran[3] = {};
    const u64 off = p.off_ticks[0], heat = p.heat_ticks[0];
    const u32 th[3] = {b, c, d};
    const u32 counted[3] = {reg_a(p, b), reg_a(p, c), reg_a(p, d)};
    for (u32 k = 0; k < 60; ++k) {
        run_at(p, 1000);
        on_failed = on_failed || p.cores[core_a - 1].working || p.cores[core_a - 1].thread != 0 ||
                    p.cores[core_a - 1].idle_run;
    }
    for (u32 i = 0; i < 3; ++i)
        ran[i] = reg_a(p, th[i]) > counted[i] + 20000; // each has counted on: a third of the one core each
    CHECK(!on_failed && ran[0] && ran[1] && ran[2] && p.os.live_threads() == 3);
    // Its ticks are counted as failed, not as cut out or tripped; it draws nothing.
    CHECK(p.failed_ticks[kHeatP] == 60 && p.off_ticks[0] == off && p.heat_ticks[0] == heat);
    CHECK(p.core_draw[core_a - 1] == 0 && p.core_draw[core_b - 1] > 0 && place_ok(p));
}

void test_memory_fails() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const u32 region = p.mem.region_device(), other = 1 - region;
    // Three programs: one wholly on the device that will fail, one wholly on
    // the other, and one on the other that holds an allocation on the first.
    const u32 lost = place_on(p, kCounter, sizeof kCounter, other);
    const u32 kept = place_on(p, kCounter, sizeof kCounter, region);
    const u32 part = place_on(p, kCounter, sizeof kCounter, region);
    for (u32 k = 0; k < 5; ++k)
        run_at(p, 1000);
    const u32 part_holder = p.os.thread(part).program;
    const u32 extra = p.mem.alloc_on(part_holder, 64, Perm::both, other);
    CHECK(extra != 0 && p.mem.device_of(p.os.thread(lost).stack) == other);
    CHECK(p.mem.device_of(p.os.thread(kept).stack) == region && p.mem.allows(extra, part_holder, kRead));
    const u64 uid_lost = p.os.program(p.os.thread(lost).program).uid;
    const u32 free_before = p.mem.device_free(region);
    p.os.clear_facts();
    p.fail_memory(other);
    // The program with everything there is gone, cause "failed device".
    CHECK(p.os.thread(lost).state == ThreadState::free);
    CHECK(p.os.deaths.size() == 1 && p.os.deaths[0].uid == uid_lost && p.os.deaths[0].cause == DeathCause::device_failed);
    CHECK(p.failure_threads == 1 && p.failures[kHeatMemory] == 1);
    // The others live; the one with an allocation there has lost it, and touching it faults.
    CHECK(p.os.thread(kept).state != ThreadState::free && p.os.thread(part).state != ThreadState::free);
    CHECK(p.mem.alloc_at(extra) == 0 && !p.mem.allows(extra, part_holder, kRead));
    // Nothing is allocated on it, it has no free bytes, and SENSE reads 0 for it.
    CHECK(p.mem.device_failed(other) && !p.mem.device_failed(region) && p.mem.region_ok());
    CHECK(p.mem.alloc_on(part_holder, 64, Perm::both, other) == 0 && p.mem.device_free(other) == 0);
    CHECK(p.mem.device_free(region) == free_before);
    const u32 si = p.core_count() + other;
    CHECK(p.os.reading(0, kDeviceKind, si) == static_cast<u32>(DeviceKind::memory));
    CHECK(p.os.reading(0, kDeviceSpeed, si) == 0 && p.os.reading(0, kDeviceThreshold, si) == 0 &&
          p.os.reading(0, kDeviceFree, si) == 0);
    CHECK(p.os.reading(0, kDeviceFree, p.core_count() + region) == free_before);
    // An ALLOC with no device asked for goes to the one left.
    const u32 any = p.mem.alloc(part_holder, 64, 2, 0);
    CHECK(any != 0 && p.mem.device_of(any) == region);
    CHECK(place_ok(p));
    for (u32 k = 0; k < 50; ++k)
        run_at(p, 1000);
    CHECK(p.os.live_threads() == 2 && !p.memory_state[other].on && p.failed_ticks[kHeatMemory] == 50);
    CHECK(p.draw.memory == static_cast<u64>(p.power().memory_hold_mw) * 1000 + p.power().memory_access_uw * p.activations);
    CHECK(place_ok(p));

    // The device holding the reset region: idle cores halt, and port 0 waits.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, "region.retry = 1\n", &why));
    Place& q = w2.place(0);
    for (u32 k = 0; k < 20; ++k)
        run_at(q, 1000);
    CHECK(q.region.started > 0);
    const u32 rd = q.mem.region_device();
    const u32 survivor = place_on(q, kCounter, sizeof kCounter, 1 - rd);
    run_at(q, 1000);
    q.fail_memory(rd);
    CHECK(!q.mem.region_ok() && !q.cores[0].idle_run && !q.cores[1].idle_run);
    const u64 started = q.region.started;
    const u8 message[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(q.region.arrive(message, 8, BirthCause::arrival, 77, false, false));
    for (u32 k = 0; k < 200; ++k)
        run_at(q, 1000);
    CHECK(q.region.started == started && q.region.queued() == 1 && q.region.arrival_runs == 0);
    CHECK(q.os.thread(survivor).state != ThreadState::free && q.cold_starts == 0);
    CHECK(place_ok(q));
}

void test_other_fails() {
    // The network device goes dark; the disk is off; neither comes back.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    for (u32 k = 0; k < 3; ++k)
        run_at(p, 1000);
    CHECK(p.env.network_on && p.draw.network > 0 && p.draw.disk > 0);
    p.fail_network();
    p.fail_disk();
    for (u32 k = 0; k < 100; ++k)
        run_at(p, 1000);
    CHECK(!p.env.network_on && !p.network_state.on && p.draw.network == 0);
    CHECK(!p.disk_state.on && p.draw.disk == 0);
    CHECK(p.failed_ticks[kHeatNetwork] == 100 && p.failed_ticks[kHeatDisk] == 100 && p.network_cutouts == 0 &&
          p.disk_cutouts == 0);
    const u32 di = p.core_count() + p.mem.device_count();
    CHECK(p.os.reading(0, kDeviceKind, di) == static_cast<u32>(DeviceKind::disk));
    CHECK(p.os.reading(0, kDeviceSpeed, di) == 0 && p.os.reading(0, kDeviceFree, di) == 0);
    CHECK(place_ok(p));

    // A cooling device that fails cools nothing: the place runs as hot as
    // its twin with none.
    i64 temp[3] = {};
    for (u32 pass = 0; pass < 3; ++pass) {
        World wc;
        Config c;
        const char* const kHot = "heat.on = true\nsupply.model = place\nhw.temperature = 400\ncooler.start = 30\n"
                                 "cooler.top = 50\n";
        TextBuf extra;
        extra.append(kHot);
        extra.append(pass == 2 ? "cooler.share = 0\n" : "cooler.share = 1\n");
        CHECK(build(wc, c, extra.c_str(), &why));
        Place& q = wc.place(0);
        CHECK(q.has_cooler == (pass != 2));
        place_on(q, kCounter, sizeof kCounter, 0);
        place_on(q, kCounter, sizeof kCounter, 0);
        if (pass == 1)
            q.fail_cooler();
        for (u32 k = 0; k < 3000; ++k) {
            q.os.clear_facts();
            wc.tick(); // the world's tick moves the place's temperature
        }
        temp[pass] = q.temp;
        CHECK(pass != 1 || (q.draw.cooling == 0 && !q.cooler_state.on && q.failed_ticks[kHeatCooler] == 3000));
        CHECK(pass != 0 || q.draw.cooling > 0);
        CHECK(place_ok(q));
    }
    CHECK(temp[0] < temp[1] - kDegree / 2);
    CHECK(temp[1] > temp[2] - kDegree / 20 && temp[1] < temp[2] + kDegree / 20);
}

// --- a world ------------------------------------------------------------------------

const char* const kWorld = "world.places = 6\n"
                           "world.map = ring\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 4\n"
                           "hw.e_share = 0.5\n"
                           "memory.threshold.fast = 700\n"
                           "noise.p_floor = 0.00001\n"
                           "noise.e_floor = 0.00001\n"
                           "noise.cosmic = 0.000001\n"
                           "region.retry = 20\n"
                           "hw.temperature = 450\n" // the reference: a tick of wear a tick
                           "wear.p.endurance = 400\n"
                           "wear.e.endurance = 300\n"
                           "wear.memory.endurance = 500\n"
                           "wear.disk.endurance = 600\n"
                           "wear.network.endurance = 700\n"
                           "wear.weak.share = 0.01\n"
                           "wear.weak.chance = 0.01\n";

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

void test_world() {
    // Six places wearing out within a few hundred ticks: the same hash on 1
    // and 3 host threads, and through a snapshot taken while some devices
    // have failed and others are worn.
    u64 hash[2] = {}, mid = 0;
    ByteWriter saved;
    u64 failed_at_save = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, kWorld, &why));
        w.start_threads(pass == 0 ? 1 : 3);
        for (u32 t = 0; t < 450; ++t)
            w.tick();
        if (pass == 0) {
            mid = w.hash();
            snapshot(w, cfg, saved);
            for (u32 i = 0; i < w.place_count(); ++i) {
                const Place& p = w.place(i);
                for (u32 k = 0; k < kHeatKinds; ++k)
                    failed_at_save += p.failures[k];
                CHECK(place_ok(p));
            }
        } else {
            CHECK_EQ_U64(w.hash(), mid);
        }
        for (u32 t = 0; t < 1000; ++t)
            w.tick();
        hash[pass] = w.hash();
        u64 failed = 0, whole = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Place& p = w.place(i);
            for (u32 k = 0; k < kHeatKinds; ++k)
                failed += p.failures[k];
            whole += p.core_count() + p.mem.device_count() + 2;
            CHECK(place_ok(p));
        }
        CHECK(failed == whole); // by 1,450 ticks every device is past twice its kind's endurance
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(failed_at_save > 4);
    // The snapshot is version 6 and says wear is on; loaded, it goes on to the same end.
    SnapshotHeader h;
    TextBuf err;
    Config::ParseError pe;
    CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.version == 6 && h.tick == 450);
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
        CHECK_EQ_U64(w.hash(), mid);
        u64 failed = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Place& p = w.place(i);
            for (u32 c = 0; c < p.core_count(); ++c) {
                failed += p.core_worn[c].failed ? 1 : 0;
                // What follows from the wear is back: each rise, and what SENSE reads.
                CHECK(p.cores[c].worn_mv == p.wear.risen(p.core_worn[c].wear, p.core_worn[c].life));
                CHECK(p.env.devices[c].failed == p.core_worn[c].failed);
            }
            for (u32 d = 0; d < p.mem.device_count(); ++d) {
                failed += p.memory_worn[d].failed ? 1 : 0;
                CHECK(p.mem.device_failed(d) == p.memory_worn[d].failed);
            }
            failed += (p.disk_worn.failed ? 1 : 0) + (p.network_worn.failed ? 1 : 0);
            CHECK(place_ok(p));
        }
        CHECK(failed == failed_at_save);
        for (u32 t = 0; t < 1000; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
    }
}

void test_off() {
    // With the base rate at 0 nothing wears, nothing fails, and the place
    // saves what gate 4's did: not a byte about wear.
    u64 bytes_off = 0, hash_off = 0;
    for (u32 pass = 0; pass < 3; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        const char* const extra[3] = {"wear.rate = 0\n",
                                      "wear.rate = 0\nwear.heat = 5\nwear.voltage = 5\nwear.p.endurance = 3\n"
                                      "wear.threshold_rise = 400\nwear.weak.share = 1\n",
                                      "wear.rate = 1\n"};
        TextBuf text;
        text.append("world.places = 3\nworld.map = ring\nregion.retry = 20\nnoise.cosmic = 0.000001\n");
        text.append(extra[pass]);
        CHECK(build(w, cfg, text.c_str(), &why));
        for (u32 t = 0; t < 400; ++t)
            w.tick();
        ByteWriter out;
        w.save(out);
        bool any = false;
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Place& p = w.place(i);
            for (u32 c = 0; c < p.core_count(); ++c)
                any = any || p.core_worn[c].wear || p.core_worn[c].failed || p.cores[c].worn_mv;
            any = any || p.memory_worn[0].wear || p.disk_worn.wear || p.network_worn.wear || p.noise.weak;
        }
        if (pass == 0) {
            bytes_off = out.size();
            hash_off = w.hash();
            CHECK(!any);
        } else if (pass == 1) {
            // The other wear settings change nothing while the rate is 0.
            CHECK(!any && out.size() == bytes_off);
            CHECK_EQ_U64(w.hash(), hash_off);
        } else {
            // On, at the agreed numbers, 400 ticks wear every device a little
            // and move no threshold yet: the save is longer by the wear alone.
            CHECK(any && out.size() > bytes_off);
            u64 memory_hash[2] = {};
            World off;
            Config c0;
            TextBuf t0;
            t0.append("world.places = 3\nworld.map = ring\nregion.retry = 20\nnoise.cosmic = 0.000001\nwear.rate = 0\n");
            CHECK(build(off, c0, t0.c_str(), &why));
            for (u32 t = 0; t < 400; ++t)
                off.tick();
            for (u32 i = 0; i < w.place_count(); ++i) {
                memory_hash[0] ^= hash_bytes(w.place(i).mem.bytes(), w.place(i).mem.size(), i);
                memory_hash[1] ^= hash_bytes(off.place(i).mem.bytes(), off.place(i).mem.size(), i);
                CHECK(w.place(i).births == off.place(i).births && w.place(i).region.started == off.place(i).region.started);
            }
            CHECK_EQ_U64(memory_hash[0], memory_hash[1]);
        }
    }
    // A snapshot from before gate 5 (version 5) is read with every gate 5
    // strength off, where its settings don't say.
    SnapshotHeader h;
    h.version = 5;
    const char* const text = "world.places = 3\n";
    h.config_text = Str(text, strlen(text));
    Config cfg;
    Config::ParseError pe;
    CHECK(snapshot_config(h, &cfg, &pe) && cfg.has("wear.rate"));
    WorldSettings ws;
    read_world_settings(cfg, &ws);
    CHECK(cfg.ok() && ws.wear.rate == 0);
    // And the agreed numbers are the defaults (build-plan-gate5.md 7).
    Config plain;
    CHECK(plain.parse(Str("wear.rate = 1\n"), &pe));
    read_world_settings(plain, &ws);
    CHECK(plain.ok() && ws.wear.heat == 1 && ws.wear.reference == 45 && ws.wear.voltage == 1 && ws.wear.rise == 50);
    for (u32 k = 0; k < kHeatKinds; ++k)
        CHECK(ws.wear.endurance[k] == 200000000);
    CHECK(ws.wear.weak_share == 0.001 && ws.wear.weak_chance == 0.000125);
    // A rise that would leave a worn device unable to restart is refused.
    Config bad;
    CHECK(bad.parse(Str("wear.rate = 1\nwear.threshold_rise = 500\n"), &pe));
    read_world_settings(bad, &ws);
    CHECK(!bad.ok());
}

} // namespace

void suite_wear() {
    test_law();
    test_lives();
    test_to_the_tick();
    test_worn_core();
    test_worn_margin();
    test_weak_cells();
    test_weak_states();
    test_core_fails();
    test_memory_fails();
    test_other_fails();
    test_world();
    test_off();
}
