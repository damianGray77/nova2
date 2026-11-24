// Maintenance visits (nova2/visit.h, visit.cpp; design.md 7.8, 10.5;
// build-plan-gate5.md step 4): the gaps' mean and spread at each regularity;
// the same visit ticks whatever the programs do; the density tie; a failed
// device of each kind replaced at the next visit and not before, with its
// size and addresses, the values its place was dealt, a life of its own
// drawn again, and static in new memory and disks; like for like under the
// threshold, the contents kept and the programs running on; the reset
// region's device hot-swapped with no cold start; a world wearing out and
// being mended, on one host thread and three and through snapshots; and no
// visits at all with the mean at 0.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/keys.h"
#include "nova2/snapshot.h"
#include "nova2/visit.h"
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
                           "disk.latency = 1000\n"
                           "os.strict_share = 0\n"
                           "region.retry = 1000000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 2\n"
                           "hw.e_share = 0\n"
                           "hw.temperature = 450\n"
                           "wear.rate = 1\n"
                           "disk.block_endurance = 1000\n"
                           "visit.mean = 100\n"
                           "visit.regularity = 1\n";

bool build_on(World& w, Config& cfg, const char* base, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(Str(base, strlen(base)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  visit: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

bool build(World& w, Config& cfg, const char* extra, TextBuf* why) { return build_on(w, cfg, kSmall, extra, why); }

void run_at(Place& p, i32 mv) {
    p.supplied = true;
    p.given_mv = mv;
    p.os.clear_facts();
    p.tick();
}

bool place_ok(const Place& p) {
    TextBuf why;
    const bool ok = p.check(&why);
    if (!ok)
        printf("  visit: %s\n", why.c_str());
    return ok;
}

// Counts in A as it runs: ADD # 1; JMP back.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// DWRITE in a loop: its offset in D, buffer in B and length in C.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};

u32 thread_of(Place& p, u32 id) {
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    CHECK(false);
    return 0;
}

// A program placed by hand on a memory device; its thread's record.
u32 place_on(Place& p, const u8* code, u32 len, u32 device) {
    const u32 at = p.mem.alloc_on(1, 1024, Perm::both, device);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    return thread_of(p, id);
}

// ... a writer of `len` bytes at disk offset `offset`, from 64 bytes past its start.
u32 place_writer(Place& p, u32 device, u32 offset, u32 len) {
    const u32 t = place_on(p, kWriter, sizeof kWriter, device);
    ExecContext x;
    p.os.load(t, x);
    x.r.b = x.r.ip + 64;
    x.r.c = len;
    x.r.d = offset;
    p.os.save(t, x);
    return t;
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

u64 bits_set(const u8* p, u32 n) {
    u64 bits = 0;
    for (u32 i = 0; i < n; ++i)
        for (u32 b = 0; b < 8; ++b)
            bits += (p[i] >> b) & 1;
    return bits;
}

// --- the schedule --------------------------------------------------------------

void test_gaps() {
    // 20,000 gaps of a place at each regularity, mean 1,000 ticks.
    const u32 n = 20000;
    const double regs[3] = {0, 0.5, 1};
    double mean[3] = {}, dev[3] = {};
    for (u32 k = 0; k < 3; ++k) {
        double sum = 0, squares = 0;
        u64 least = ~0ull, under = 0;
        for (u32 i = 0; i < n; ++i) {
            const u64 g = visit_gap(7, 3, i, 1000, regs[k]);
            CHECK(g == visit_gap(7, 3, i, 1000, regs[k])); // a function of its arguments
            sum += static_cast<double>(g);
            squares += static_cast<double>(g) * static_cast<double>(g);
            least = g < least ? g : least;
            under += g < 1000 ? 1 : 0;
        }
        mean[k] = sum / n;
        dev[k] = sqrt(squares / n - mean[k] * mean[k]);
        if (k == 0) // random: exponential, so as wide as its mean, and 63% of gaps under it
            CHECK(least >= 1 && under > n * 0.61 && under < n * 0.65);
        if (k == 1) // half and half: never under half the mean
            CHECK(least >= 500);
        if (k == 2) // periodic
            CHECK(least == 1000 && dev[k] == 0);
    }
    CHECK(mean[0] > 975 && mean[0] < 1025 && dev[0] > 960 && dev[0] < 1040);
    CHECK(mean[1] > 985 && mean[1] < 1015 && dev[1] > 480 && dev[1] < 520);
    CHECK(mean[2] == 1000);
    // Other places and other seeds draw other gaps; a mean of 0 is never.
    CHECK(visit_gap(7, 3, 0, 1000, 0) != visit_gap(7, 4, 0, 1000, 0));
    CHECK(visit_gap(7, 3, 0, 1000, 0) != visit_gap(8, 3, 0, 1000, 0));
    CHECK(visit_gap(7, 3, 0, 0, 0) == kNoVisit && visit_gap(7, 3, 5, 1, 1) == 1);
    printf("  visit: 20,000 gaps at a mean of 1,000: mean %.0f and spread %.0f random, %.0f and %.0f half regular, "
           "%.0f and %.0f periodic\n",
           mean[0], dev[0], mean[1], dev[1], mean[2], dev[2]);
}

const char* const kRing = "world.places = 6\n"
                          "world.map = ring\n"
                          "memory.size = 32768\n"
                          "disk.size = 4096\n"
                          "disk.latency = 1000\n"
                          "os.strict_share = 0\n"
                          "hw.cores.min = 2\n"
                          "hw.cores.max = 4\n"
                          "region.retry = 20\n"
                          "visit.mean = 150\n";

// A.5's 14-byte replicator on a lax place.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

void test_schedule() {
    // Two worlds with one seed, one left as static and one crowded with
    // replicators: every place's visits come at the same ticks.
    const u32 ticks = 2000;
    Array<u64> at[2];
    u64 births[2] = {};
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_on(w, cfg, kRing, "", &why));
        if (pass == 1)
            for (u32 i = 0; i < w.place_count(); ++i)
                memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
        for (u32 i = 0; i < w.place_count(); ++i)
            CHECK(w.place(i).visit_mean() == 150 && w.place(i).next_visit != kNoVisit && w.place(i).visits == 0);
        for (u32 t = 0; t < ticks; ++t) {
            for (u32 i = 0; i < w.place_count(); ++i)
                if (w.place(i).next_visit == w.tick_count())
                    at[pass].push(w.tick_count() * 16 + i);
            w.tick();
        }
        u64 made = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            made += w.place(i).visits;
            births[pass] += w.place(i).births;
            CHECK(w.place(i).last_visit < w.tick_count() && w.place(i).next_visit >= w.tick_count());
            CHECK(place_ok(w.place(i)));
        }
        CHECK(made == at[pass].size());
    }
    CHECK(births[1] > births[0] + 1000); // the programs differ
    CHECK(at[0].size() == at[1].size() && at[0].size() > 50 && at[0].size() < 110); // about 6 x 2,000 / 150
    bool same = at[0].size() == at[1].size();
    for (usize i = 0; same && i < at[0].size(); ++i)
        same = at[0][i] == at[1][i];
    CHECK(same);
    // Each place's first visit is its gap 0 from the seed.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_on(w, cfg, kRing, "", &why));
        bool differ = false;
        for (u32 i = 0; i < w.place_count(); ++i) {
            CHECK(w.place(i).next_visit == visit_gap(w.seed(), i, 0, 150, 0));
            differ = differ || w.place(i).next_visit != w.place(0).next_visit;
        }
        CHECK(differ);
    }
    printf("  visit: %llu visits to 6 places in 2,000 ticks came at the same ticks in a world of static and one "
           "crowded with replicators\n",
           static_cast<unsigned long long>(at[0].size()));
}

void test_density_tie() {
    // On a landscape with the tie at 1, a place's mean follows its density:
    // the world's mean x (its density / the average)^-1, so towns are
    // visited more often; with the tie at 0 every place has the world's.
    const char* const land = "world.places = 30\nvisit.mean = 100000\n";
    double least = 1e30, most = 0, sum = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build_on(w, cfg, land, pass ? "visit.density_tie = 1\n" : "", &why));
        for (u32 i = 0; i < w.place_count(); ++i) {
            const double m = static_cast<double>(w.place(i).visit_mean());
            if (!pass) {
                CHECK(m == 100000);
                continue;
            }
            least = m < least ? m : least;
            most = m > most ? m : most;
            sum += 100000.0 / m; // its density over the average
        }
    }
    // The map's density runs from 1 to map.density (3): the densest place is
    // visited three times as often as the sparsest, and the densities
    // average 1 by construction.
    CHECK(most / least > 2.9 && most / least < 3.1 && sum > 29.9 && sum < 30.1);
    printf("  visit: with the density tie at 1, 30 places' mean intervals ran from %.0f to %.0f ticks around a "
           "world's 100,000\n",
           least, most);
}

// --- a failed device, replaced ---------------------------------------------------

void test_replaced() {
    // Visits every 100 ticks. Each kind of device fails at tick 130 and is
    // back at tick 200, not before.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const u32 region = p.mem.region_device(), other = 1 - region;
    const u32 kept = place_on(p, kCounter, sizeof kCounter, region);
    const u32 writer = place_writer(p, region, 0, 64);
    const MemDevice dv = p.mem.device(other);
    const u32 free0 = p.mem.device_free(other);
    Array<u8> first_static, first_disk;
    first_static.append(p.mem.bytes() + dv.base, dv.size);
    first_disk.append(p.os.disk(), p.os.disk_size());
    while (p.env.tick < 130)
        run_at(p, 1000);
    CHECK(p.visits == 1 && p.last_visit == 100 && p.next_visit == 200);
    CHECK(p.os.boot_writer() != 0 && p.os.block_writes(0) > 100);
    const u64 life_core = p.core_worn[1].life, life_mem = p.memory_worn[other].life, life_disk = p.disk_worn.life;
    const u64 life_net = p.network_worn.life;
    const u32 block_life = p.os.block_life(0);
    memset(p.mem.bytes() + dv.base, 0xAA, dv.size);
    p.fail_core(1);
    p.fail_memory(other);
    p.fail_disk();
    p.fail_network();
    const u64 deaths = p.deaths;
    while (p.env.tick < 200) {
        run_at(p, 1000);
        CHECK(p.core_worn[1].failed && p.memory_worn[other].failed && p.disk_worn.failed && p.network_worn.failed);
        CHECK(place_ok(p));
    }
    CHECK(p.failed_ticks[kHeatP] == 70 && p.failed_ticks[kHeatMemory] == 70 && p.failed_ticks[kHeatDisk] == 70);
    const u32 a_before = reg_a(p, kept);
    run_at(p, 1000); // tick 200: the visit
    CHECK(p.visits == 2 && p.last_visit == 200 && p.next_visit == 300);
    CHECK(!p.core_worn[1].failed && !p.memory_worn[other].failed && !p.disk_worn.failed && !p.network_worn.failed);
    CHECK(p.replaced[kHeatP] == 1 && p.replaced[kHeatMemory] == 1 && p.replaced[kHeatDisk] == 1 &&
          p.replaced[kHeatNetwork] == 1 && p.renewed[kHeatP] + p.renewed[kHeatMemory] + p.renewed[kHeatDisk] == 0);
    CHECK(p.region_swaps == 0 && p.cold_starts == 0 && place_ok(p));
    // Each is a new device at its slot: no wear but this tick's, a life of
    // its own drawn again, on and working in the tick of the visit.
    CHECK(p.core_worn[1].fresh == 1 && p.memory_worn[other].fresh == 1 && p.disk_worn.fresh == 1 &&
          p.network_worn.fresh == 1 && p.core_worn[0].fresh == 0 && p.memory_worn[region].fresh == 0);
    CHECK(p.core_worn[1].wear == kWearTick && p.memory_worn[other].wear == kWearTick &&
          p.disk_worn.wear == kWearTick && p.network_worn.wear == kWearTick);
    CHECK(p.core_worn[1].life != life_core && p.memory_worn[other].life != life_mem &&
          p.disk_worn.life != life_disk && p.network_worn.life != life_net);
    CHECK(p.core_worn[1].life == p.wear.life(w.seed(), 0, 1, kHeatP, 1));
    CHECK(p.cores[1].working && p.cores[1].speed == p.cores[0].speed && p.cores[1].kind == DeviceKind::p_core);
    CHECK(p.memory_state[other].on && p.disk_state.on && p.network_state.on && p.env.network_on);
    // The memory device: the size, addresses and threshold it had, every
    // page free again, and static in it: not the bytes that were there, not
    // its first static, about half its bits set.
    const MemDevice now = p.mem.device(other);
    CHECK(now.base == dv.base && now.size == dv.size && now.threshold == dv.threshold && now.latency16 == dv.latency16);
    CHECK(!p.mem.device_failed(other) && p.mem.device_free(other) == free0);
    const u64 set = bits_set(p.mem.bytes() + dv.base, dv.size);
    CHECK(set > dv.size * 4 - 1500 && set < dv.size * 4 + 1500);
    CHECK(memcmp(p.mem.bytes() + dv.base, first_static.data(), dv.size) != 0);
    const u32 again = p.mem.alloc_on(p.os.thread(kept).program, 64, Perm::both, other);
    CHECK(again != 0 && p.mem.device_of(again) == other);
    // SENSE reads it again.
    const u32 si = p.core_count() + other;
    CHECK(p.os.reading(0, kDeviceThreshold, si) == static_cast<u32>(dv.threshold));
    CHECK(p.os.reading(0, kDeviceSpeed, 1) == p.cores[1].speed && p.cores[1].speed > 0);
    // The disk: static, nobody's boot block, its blocks unwritten with
    // endurances drawn again, and its calls work: the writer, whose calls
    // answered 0 while it was failed, writes its block again.
    CHECK(!p.os.disk_failed() && p.os.block_life(0) != block_life);
    CHECK(memcmp(p.os.disk() + 512, first_disk.data() + 512, p.os.disk_size() - 512) != 0);
    const u64 dset = bits_set(p.os.disk() + 512, p.os.disk_size() - 512);
    CHECK(dset > (p.os.disk_size() - 512) * 4ull - 3000 && dset < (p.os.disk_size() - 512) * 4ull + 3000);
    const u64 ok = p.os.calls_ok[kDwrite];
    for (u32 k = 0; k < 10; ++k)
        run_at(p, 1000);
    CHECK(p.os.calls_ok[kDwrite] >= ok + 8 && p.os.boot_writer() != 0 && p.os.block_writes(0) >= 8 &&
          p.os.block_writes(0) <= 11);
    // Nothing paused, and no program was touched: the counter counted
    // through the visit's tick as through any other, and nobody died.
    CHECK(reg_a(p, kept) > a_before + 11 * 400 && p.deaths == deaths);
    CHECK(p.os.thread(kept).state != ThreadState::free && p.os.thread(writer).state != ThreadState::free);
    // The same seed makes the same new devices.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, "", &why));
    Place& q = w2.place(0);
    while (q.env.tick < 130)
        run_at(q, 1000);
    q.fail_memory(other);
    q.fail_disk();
    while (q.env.tick < 201)
        run_at(q, 1000);
    CHECK(memcmp(q.mem.bytes() + dv.base, p.mem.bytes() + dv.base, dv.size) == 0);
    CHECK(q.memory_worn[other].life == p.memory_worn[other].life && q.os.block_life(3) == p.os.block_life(3));
    CHECK(memcmp(q.os.disk() + 4096, p.os.disk() + 4096, 4096) == 0);
    printf("  visit: a core, a memory device, a disk and a network device failed at tick 130 waited 70 ticks and "
           "were replaced at the visit of tick 200, new and working, with static in the memory and on the disk\n");
}

// --- like for like ---------------------------------------------------------------

void test_like_for_like() {
    // With the threshold at a half, a visit replaces the devices worn past
    // half their lives and leaves the rest.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "visit.replace_at = 0.5\n", &why));
    Place& p = w.place(0);
    const u32 region = p.mem.region_device(), other = 1 - region;
    const u32 here = place_on(p, kCounter, sizeof kCounter, other);
    const u32 there = place_on(p, kCounter, sizeof kCounter, region);
    const u32 writer = place_writer(p, other, 0, 64);
    while (p.env.tick < 150)
        run_at(p, 1000);
    // Worn: both cores (0.6 and 0.4 of their lives), the memory device the
    // programs are on (0.7), the disk (0.9). A corrupted byte in memory and
    // one on the disk stay as they are.
    p.core_worn[0].wear = p.core_worn[0].life / 10 * 6;
    p.core_worn[1].wear = p.core_worn[1].life / 10 * 4;
    p.memory_worn[other].wear = p.memory_worn[other].life / 10 * 7;
    p.memory_worn[region].wear = p.memory_worn[region].life / 10 * 3;
    p.disk_worn.wear = p.disk_worn.life / 10 * 9;
    const MemDevice dv = p.mem.device(other);
    const u32 scratch = p.mem.alloc_on(p.os.thread(here).program, 2048, Perm::both, other);
    CHECK(scratch != 0);
    for (u32 i = 0; i < 2048; ++i)
        p.mem.bytes()[scratch + i] = static_cast<u8>(i * 7 + 3);
    p.mem.bytes()[scratch + 100] ^= 0x10; // a bit gone wrong
    p.os.disk()[5000] ^= 0x04;
    while (p.env.tick < 200)
        run_at(p, 1000);
    CHECK(p.cores[0].worn_mv == 30 && p.memory_threshold(other) == dv.threshold + 35 && p.replaced[kHeatP] == 0);
    Array<u8> mem_before, disk_before;
    mem_before.append(p.mem.bytes() + scratch, 2048);
    disk_before.append(p.os.disk() + 512, p.os.disk_size() - 512);
    const u64 writer_uid = p.os.boot_writer();
    const u32 a_here = reg_a(p, here), a_there = reg_a(p, there);
    const u32 free_before = p.mem.device_free(other);
    const u64 deaths = p.deaths, life0 = p.core_worn[0].life;
    CHECK(writer_uid != 0 && p.os.block_writes(0) > 150);
    run_at(p, 1000); // tick 200: the visit
    CHECK(p.visits == 2 && p.renewed[kHeatP] == 1 && p.renewed[kHeatMemory] == 1 && p.renewed[kHeatDisk] == 1);
    CHECK(p.renewed[kHeatNetwork] == 0 && p.replaced[kHeatP] + p.replaced[kHeatMemory] + p.replaced[kHeatDisk] == 0);
    // New devices: no wear but this tick's, new lives, the nominal thresholds.
    CHECK(p.core_worn[0].wear == kWearTick && p.core_worn[0].fresh == 1 && p.core_worn[0].life != life0);
    CHECK(p.core_worn[1].wear > p.core_worn[1].life / 10 * 4 && p.core_worn[1].fresh == 0);
    CHECK(p.memory_worn[other].wear == kWearTick && p.memory_worn[region].fresh == 0);
    CHECK(p.disk_worn.wear == kWearTick && p.disk_worn.fresh == 1);
    CHECK(p.cores[0].worn_mv == 0 && p.memory_threshold(other) == dv.threshold);
    // The contents came across, the wrong bits with them; the allocations
    // and free pages are as they were; the disk's bytes and its boot
    // block's writer too, on blocks with no writes but this tick's.
    CHECK(memcmp(mem_before.data(), p.mem.bytes() + scratch, 2048) == 0);
    CHECK(p.mem.bytes()[scratch + 100] == static_cast<u8>((100 * 7 + 3) ^ 0x10));
    CHECK(p.mem.device_free(other) == free_before && p.mem.alloc_at(scratch) != 0);
    CHECK(memcmp(disk_before.data(), p.os.disk() + 512, p.os.disk_size() - 512) == 0);
    CHECK(p.os.boot_writer() == writer_uid && p.os.block_writes(0) <= 1 && !p.os.disk_failed());
    // Nothing paused: both counters counted through the visit's tick, on the
    // replaced core and the replaced memory as on the others; nobody died,
    // no device went off, and nothing cold-started.
    CHECK(reg_a(p, here) > a_here + 400 && reg_a(p, there) > a_there + 400 && p.deaths == deaths);
    CHECK(p.cores[0].working && p.cores[1].working && p.memory_state[other].on && p.disk_state.on);
    CHECK(p.cutouts[0] == 0 && p.memory_cutouts == 0 && p.disk_cutouts == 0 && p.cold_starts == 0);
    CHECK(p.os.thread(writer).state != ThreadState::free && place_ok(p));
    for (u32 k = 0; k < 20; ++k)
        run_at(p, 1000);
    CHECK(p.deaths == deaths && place_ok(p));
    // At the default threshold of 1 the same visit replaces nothing.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, "", &why));
    Place& q = w2.place(0);
    q.core_worn[0].wear = q.core_worn[0].life / 10 * 9;
    q.disk_worn.wear = q.disk_worn.life / 10 * 9;
    while (q.env.tick < 101)
        run_at(q, 1000);
    CHECK(q.visits == 1 && q.renewed[kHeatP] == 0 && q.renewed[kHeatDisk] == 0 && q.core_worn[0].fresh == 0);
    printf("  visit: with the threshold at a half, a visit replaced a core, a memory device and a disk worn past "
           "it, their contents and wrong bits copied across and their programs running on, and left the rest\n");
}

// --- the reset region's device ---------------------------------------------------

void test_region_swap() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "region.retry = 1\n", &why));
    Place& p = w.place(0);
    const u32 rd = p.mem.region_device();
    const u32 survivor = place_on(p, kCounter, sizeof kCounter, 1 - rd);
    while (p.env.tick < 120)
        run_at(p, 1000);
    CHECK(p.region.started > 0);
    p.fail_memory(rd);
    CHECK(!p.mem.region_ok());
    const u8 message[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(p.region.arrive(message, 8, BirthCause::arrival, 77, false, false));
    const u64 started = p.region.started;
    while (p.env.tick < 200)
        run_at(p, 1000);
    // Gone until the visit: no idle run starts, and port 0's arrival waits.
    CHECK(p.region.started == started && p.region.queued() == 1 && p.region.arrival_runs == 0);
    const u32 a_before = reg_a(p, survivor);
    const u64 deaths = p.deaths, boots = p.region.boot_runs;
    run_at(p, 1000); // tick 200: the visit hot-swaps the device
    CHECK(p.visits == 2 && p.replaced[kHeatMemory] == 1 && p.region_swaps == 1);
    CHECK(p.mem.region_ok() && p.memory_state[rd].on && !p.memory_worn[rd].failed);
    // The region is back, and in the same tick the arrival that waited runs.
    CHECK(p.region.queued() == 0 && p.region.arrival_runs == 1);
    for (u32 k = 0; k < 20; ++k)
        run_at(p, 1000);
    CHECK(p.region.started > started + 10); // idle runs start again
    // No cold start: the boot block doesn't run, and the program on the
    // other device carried on through the visit.
    CHECK(p.cold_starts == 0 && p.region.boot_runs == boots && p.deaths == deaths);
    CHECK(p.os.thread(survivor).state != ThreadState::free && reg_a(p, survivor) > a_before + 21 * 400);
    CHECK(place_ok(p));
    printf("  visit: the reset region's device, failed at tick 120, was hot-swapped at the visit of tick 200: the "
           "arrival that had waited ran, idle runs started again, and nothing cold-started\n");
}

// --- worlds ------------------------------------------------------------------------

const char* const kWearing = "world.places = 6\n"
                             "world.map = ring\n"
                             "memory.size = 32768\n"
                             "disk.size = 4096\n"
                             "disk.latency = 1000\n"
                             "os.strict_share = 0\n"
                             "hw.cores.min = 2\n"
                             "hw.cores.max = 4\n"
                             "hw.e_share = 0.5\n"
                             "memory.threshold.fast = 700\n"
                             "noise.p_floor = 0.00001\n"
                             "noise.e_floor = 0.00001\n"
                             "noise.cosmic = 0.000001\n"
                             "region.retry = 20\n"
                             "hw.temperature = 450\n"
                             "wear.rate = 1\n"
                             "wear.p.endurance = 400\n"
                             "wear.e.endurance = 300\n"
                             "wear.memory.endurance = 500\n"
                             "wear.disk.endurance = 600\n"
                             "wear.network.endurance = 700\n"
                             "wear.weak.share = 0.01\n"
                             "wear.weak.chance = 0.01\n"
                             "disk.block_endurance = 60\n"
                             "disk.worn_flip = 0.05\n"
                             "visit.mean = 150\n";

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

struct Tally {
    u64 failures = 0, replaced[kHeatKinds] = {}, renewed = 0, visits = 0, swaps = 0, waiting = 0, boots = 0;
};

Tally tally(World& w) {
    Tally t;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k) {
            t.failures += p.failures[k];
            t.replaced[k] += p.replaced[k];
            t.renewed += p.renewed[k];
        }
        t.visits += p.visits;
        t.swaps += p.region_swaps;
        t.boots += p.boot_failures;
        for (u32 c = 0; c < p.core_count(); ++c)
            t.waiting += p.core_worn[c].failed ? 1 : 0;
        for (u32 d = 0; d < p.mem.device_count(); ++d)
            t.waiting += p.memory_worn[d].failed ? 1 : 0;
        t.waiting += (p.disk_worn.failed ? 1 : 0) + (p.network_worn.failed ? 1 : 0);
        CHECK(place_ok(p));
    }
    return t;
}

void place_crowd(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        place_writer(p, p.mem.region_device(), 0, 600);
    }
}

void test_worlds() {
    // Six places whose devices live a few hundred ticks, visited about
    // every 150: devices fail, wait and are replaced over and over, with
    // replicators and a disk writer at each. The same hash on 1 host thread
    // and 3; and from snapshots taken with devices failed and waiting, with
    // disks already replaced, part way to the next visits. The second world
    // replaces like for like at 0.7 of a life.
    for (u32 like = 0; like < 2; ++like) {
        const char* const extra = like ? "visit.replace_at = 0.7\n" : "";
        u64 hash[2] = {}, mid = 0;
        ByteWriter saved;
        Tally at_save, at_end;
        const u32 first = 700, rest = 1300;
        for (u32 pass = 0; pass < 2; ++pass) {
            World w;
            Config cfg;
            TextBuf why;
            CHECK(build_on(w, cfg, kWearing, extra, &why));
            place_crowd(w);
            w.start_threads(pass == 0 ? 1 : 3);
            for (u32 t = 0; t < first; ++t)
                w.tick();
            if (pass == 0) {
                mid = w.hash();
                snapshot(w, cfg, saved);
                at_save = tally(w);
            } else {
                CHECK_EQ_U64(w.hash(), mid);
            }
            for (u32 t = 0; t < rest; ++t) {
                w.tick();
                if (t % 8 == 0)
                    tally(w); // the invariants, as it goes
            }
            hash[pass] = w.hash();
            at_end = tally(w);
        }
        CHECK_EQ_U64(hash[0], hash[1]);
        CHECK(at_save.waiting > 0 && at_save.replaced[kHeatDisk] > 0 && at_save.replaced[kHeatMemory] > 0);
        CHECK(at_end.replaced[kHeatP] + at_end.replaced[kHeatE] > 6 && at_end.replaced[kHeatMemory] > 6 &&
              at_end.replaced[kHeatDisk] > 6 && at_end.replaced[kHeatNetwork] > 3 && at_end.swaps > 0);
        CHECK(at_end.visits > 50 && (like ? at_end.renewed > 10 : at_end.renewed == 0));
        SnapshotHeader h;
        TextBuf err;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.version == 6 && h.tick == first);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
        CHECK_EQ_U64(w.hash(), mid);
        const Tally loaded = tally(w);
        CHECK(loaded.waiting == at_save.waiting && loaded.visits == at_save.visits);
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
        printf("  visit: six places wearing out over 2,000 ticks%s: %llu failures, %llu visits; replaced %llu "
               "cores, %llu memory devices (%llu holding the reset region), %llu disks (%llu failed by their boot "
               "block) and %llu network devices, %llu like for like; %llu devices failed and waiting at the end; "
               "the same world on 1 host thread and 3 and from a snapshot of tick %u with %llu waiting\n",
               like ? ", replacing at 0.7 of a life" : "", static_cast<unsigned long long>(at_end.failures),
               static_cast<unsigned long long>(at_end.visits),
               static_cast<unsigned long long>(at_end.replaced[kHeatP] + at_end.replaced[kHeatE]),
               static_cast<unsigned long long>(at_end.replaced[kHeatMemory]),
               static_cast<unsigned long long>(at_end.swaps), static_cast<unsigned long long>(at_end.replaced[kHeatDisk]),
               static_cast<unsigned long long>(at_end.boots),
               static_cast<unsigned long long>(at_end.replaced[kHeatNetwork]),
               static_cast<unsigned long long>(at_end.renewed), static_cast<unsigned long long>(at_end.waiting),
               first, static_cast<unsigned long long>(at_save.waiting));
    }
}

// --- off ---------------------------------------------------------------------------

void test_off() {
    // With the mean at 0 no place is ever visited: what fails stays failed,
    // and the save's bytes and hash are those of a world that says nothing
    // about visits, whatever the other visit settings.
    u64 hash[2] = {};
    usize bytes[2] = {};
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg,
                    pass ? "visit.mean = 0\nvisit.regularity = 0.5\nvisit.replace_at = 0.1\nvisit.density_tie = 2\n"
                         : "visit.mean = 0\n",
                    &why));
        Place& p = w.place(0);
        CHECK(p.next_visit == kNoVisit && p.visit_mean() == 0);
        p.core_worn[0].wear = p.core_worn[0].life / 2;
        for (u32 t = 0; t < 50; ++t)
            run_at(p, 1000);
        p.fail_core(1);
        p.fail_disk();
        for (u32 t = 0; t < 400; ++t)
            run_at(p, 1000);
        CHECK(p.visits == 0 && p.core_worn[1].failed && p.disk_worn.failed && p.core_worn[0].fresh == 0);
        CHECK(place_ok(p));
        hash[pass] = w.hash();
        ByteWriter out;
        w.save(out);
        bytes[pass] = out.size();
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(bytes[0] == bytes[1]);
    // A snapshot from before gate 5 is read with no visits.
    SnapshotHeader h;
    h.version = 5;
    const char* const text = "world.places = 3\n";
    h.config_text = Str(text, strlen(text));
    Config cfg;
    Config::ParseError pe;
    CHECK(snapshot_config(h, &cfg, &pe) && cfg.has("visit.mean"));
    WorldSettings ws;
    read_world_settings(cfg, &ws);
    CHECK(cfg.ok() && ws.visit.mean == 0);
    // And the agreed numbers are the defaults (build-plan-gate5.md 7).
    CHECK(key_find(Str("visit.mean"))->def == 1000000 && key_find(Str("visit.regularity"))->def == 0);
    CHECK(key_find(Str("visit.density_tie"))->def == 0 && key_find(Str("visit.replace_at"))->def == 1);
    CHECK(key_find(Str("visit.respawn_share"))->def == 1);
}

} // namespace

void suite_visit() {
    test_gaps();
    test_schedule();
    test_density_tie();
    test_replaced();
    test_like_for_like();
    test_region_swap();
    test_worlds();
    test_off();
}
