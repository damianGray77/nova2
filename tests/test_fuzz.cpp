// The fuzz world (build-plan-gate1.md 6; build-plan-gate2.md step 7): places
// full of static, as an origin run starts, under gate 2's varying supply at
// its hardest, with the invariant checker (Place::check) after every tick.
// The supply has a short day with deep nights, fast swings, and drop-outs
// as often as the settings allow, half of them blackouts; the error floors
// are high, so static changes fast. It passes if the engine never stops on
// an assertion (the tests run in the checked build) and no invariant
// breaks. A second world crowds it: a replicator in every lax region and a
// booter, a disk writer and a disk reader in every place (R7), so threads
// are cut out, paused, faded and rebooted with work in hand, and the disk
// cuts out with I/O in flight; a third pauses threads at a cut-out instead
// of ending them. All three end a thread when any bit of its saved state
// fails (os.saved_state_fails, on by default; build-plan-gate2.md step 10),
// and a fourth crowded world turns that off, so only fading ends one. A
// fifth carries gate 3's traffic on its map (build-plan-gate3.md step 5):
// replicators SENT to port 0s, talkers, listeners with data handlers,
// PINGs, neighbour SENSEs and travellers in the reset regions, while network
// devices go dark at night; the network's own check runs after every tick
// too. Its idle runs may SEND (os.idle_send), so the travellers travel; a
// sixth carries the same traffic in the default world, where they can't
// (build-plan-gate3.md step 8). Both hold arrivals to their share of the
// cores (region.arrival_share). A seventh wears the fifth's devices out
// (build-plan-gate5.md step 2): lives a few hundred to a few thousand ticks
// long, thresholds rising and weak cells losing bits on the way, so cores,
// memory devices, disks, network devices and reset regions fail for good
// under threads, I/O and traffic. An eighth mends the seventh
// (build-plan-gate5.md step 4): visits every 250 ticks or so replace what
// has failed, and what is worn past 0.8 of its life like for like, so
// devices and reset regions go and come back again and again. Its links are
// cut too (step 5), about one in 300 ticks each, and mended at the visits,
// with messages waiting up to 40 ticks for a way on.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

const char* const kFuzz = "world.places = 8\n"
                          "memory.size = 32768\n"
                          "disk.size = 65536\n"
                          "hw.cores.max = 4\n"
                          "region.retry = 1\n"
                          "os.slots = 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 3 0 3 0 3 0 3\n"
                          "noise.p_floor = 1e-3\n"
                          "noise.e_floor = 1e-3\n"
                          "noise.cosmic = 1e-5\n"
                          "supply.sag = 50\n"
                          "supply.day = 400\n"
                          "supply.night = 0.1\n"
                          "supply.swing = 0.3\n"
                          "supply.swing_period = 150\n"
                          "supply.dropouts = 1000\n"
                          "supply.dropout.min = 2\n"
                          "supply.dropout.max = 300\n"
                          "supply.blackout_share = 0.5\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// test_boot.cpp's booter: as a thread it DWRITEs itself to offset 0 and
// waits; booted, it copies itself and SPAWNs the copy.
const u8 kBooter[34] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                        kDwrite,            encode(kJnz, kImm), 19,                 encode(kMov, kImm),
                        2,                  encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSys, kImm), kAlloc,             encode(kMov, kIp),  encode(kSub, kImm),
                        18,                 encode(kSto, kB),   encode(kCpy, kImm), 34,
                        encode(kMov, kD),   encode(kSys, kImm), kSpawn,             encode(kPush, kA),
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4)};

// DWRITE in a loop (test_cutout.cpp's): from itself, to the offset and for
// the length of its start, clamped at the disk's end and its memory's.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
// DREAD in a loop: 16 bytes from the offset of its start into itself, 32
// bytes past its start.
const u8 kReader[12] = {encode(kMov, kB),   encode(kAdd, kImm), 32, encode(kSto, kB), encode(kMov, kImm), 16,
                        encode(kSto, kC),   encode(kMov, kD),   encode(kSys, kImm), kDread, encode(kJmp, kImm),
                        static_cast<u8>(-3)};

// A spinner with a fault handler (as the guards suite's): B = the handler,
// at 14; D = the byte at 30, where the handler stores the code it was
// given; A = 0, a fault handler. It spins at 12, for the watchdog.
const u8 kHandled[17] = {encode(kMov, kIp),  encode(kAdd, kImm), 14, encode(kSto, kB),  encode(kMov, kIp),
                         encode(kAdd, kImm), 26,                 encode(kSto, kD),  encode(kMov, kImm), 0,
                         encode(kSys, kImm), kSetHandler,        encode(kJmp, kImm), 0, encode(kSto, kAtD),
                         encode(kSys, kImm), kHret};

// Sleeps for good, filling memory for the killer.
const u8 kSleeper[6] = {encode(kMov, kImm), 0, encode(kSys, kImm), kSleep, encode(kJmp, kImm),
                        static_cast<u8>(-4)};

// perm: ALLOC's permission code; 2, "both", is code on a strict place's
// pages only when 1 asks for it.
bool hand_place(Place& p, const u8* code, u32 len, u32 size = 64, u32 perm = 2) {
    const u32 at = p.mem.alloc(1, size, perm, 0);
    if (!at)
        return false;
    memcpy(p.mem.bytes() + at, code, len);
    return p.os.place_by_hand(1, at, at, 0) != 0;
}

bool build(World& w, Config& cfg, const char* extra) {
    // The run's settings over the fuzz world's: a key in both is the run's.
    Config::ParseError pe;
    Config over;
    if (!cfg.parse(kFuzz, &pe) || !over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    TextBuf why;
    const bool ok = w.init(cfg, &why);
    if (!ok)
        printf("  fuzz: %s %s\n", why.c_str(), cfg.value_error());
    return ok;
}

// What a fuzz run went through, summed over its places.
struct Seen {
    u64 checks = 0, broken = 0;
    u64 core_cutouts = 0, memory_cutouts = 0, disk_cutouts = 0, cold = 0, boots = 0, faded = 0;
    u64 births = 0, deaths = 0, cut_threads = 0, paused = 0, faded_threads = 0, failed_threads = 0, torn = 0;
    u64 failed_reads = 0;
    u64 strays = 0; // live threads whose stack isn't an allocation of their program (os.stack_held)
    // Gate 3's traffic.
    NetCounts net;
    u64 dark = 0; // place-ticks with the network device off
    u64 arrivals = 0, travellers = 0, inbox = 0, direct = 0, handled = 0;
    // Gate 5's wear.
    u64 failures[kHeatKinds] = {}, failure_threads = 0, weak = 0, regions_lost = 0;
    u64 block_writes = 0, worn_flips = 0, swaps = 0, boot_failures = 0;
    u64 visits = 0, replaced = 0, renewed = 0, region_swaps = 0;
    u64 link_cuts = 0, link_mends = 0;
    u64 respawns = 0, respawns_moved = 0, torn_down = 0;
    u64 strikes = 0, patch_cuts = 0, patch_ticks = 0;
    u64 hammered = 0, hammer = 0; // rowhammer: activations past a threshold, and the bits they flipped
    // Step 9's: flaky parts, straits, and the current.
    u32 places = 0, flaky = 0, parts = 0, straits = 0;
    u64 partings = 0, joinings = 0, gusts = 0; // gusts: patch-ticks of them
    CurrentCounts drift;
    // Step 10's: the out-of-memory killer and the watchdog.
    u64 kills = 0, killed_threads = 0, dog = 0, dog_handled = 0;
};

// guards: a spinner with a handler at each lax place, and sleepers filling
// every place they can to under the killer's line. Fading and flips end
// most of them within a few dozen ticks here, so the killer's ends are few.
Seen fuzz(const char* extra, u64 ticks, bool crowd, bool traffic = false, bool guards = false) {
    Seen s;
    World w;
    Config cfg;
    CHECK(build(w, cfg, extra));
    if (crowd)
        for (u32 i = 0; i < w.place_count(); ++i) {
            Place& p = w.place(i);
            if (!p.mem.strict())
                memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
            hand_place(p, kBooter, sizeof kBooter);
            hand_place(p, kWriter, sizeof kWriter);
            hand_place(p, kReader, sizeof kReader);
            if (traffic)
                CHECK(place_traffic(p, w.place_count(), i) == 3);
            if (guards && !p.mem.strict())
                CHECK(hand_place(p, kHandled, sizeof kHandled));
            while (guards && p.mem.free_bytes() + 1000 > p.mem.size() / 10 &&
                   hand_place(p, kSleeper, sizeof kSleeper, 512, p.mem.strict() ? 1 : 2)) {
            }
        }
    w.start_threads(4);
    TextBuf why;
    for (u64 t = 0; t < ticks && s.broken == 0; ++t) {
        w.tick();
        s.patch_ticks += w.events().count();
        for (u32 i = 0; i < w.events().count(); ++i)
            s.gusts += w.events().now(i).type == static_cast<u8>(PatchType::gust) ? 1 : 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            ++s.checks;
            why.clear();
            if (!w.place(i).check(&why)) {
                ++s.broken;
                printf("  fuzz: place %u at tick %llu: %s\n", i, static_cast<unsigned long long>(w.tick_count()),
                       why.c_str());
                break;
            }
            s.dark += w.place(i).network_state.on ? 0 : 1;
        }
        why.clear();
        if (s.broken == 0 && !w.net().check(&why)) {
            ++s.broken;
            printf("  fuzz: the network at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()),
                   why.c_str());
        }
        why.clear();
        if (s.broken == 0 && !w.links().check(w.tick_count(), &why)) {
            ++s.broken;
            printf("  fuzz: the links at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()),
                   why.c_str());
        }
        // The map after a respawn: both link sets by the rules, against
        // every pair.
        why.clear();
        if (s.broken == 0 && w.respawns != s.respawns) {
            s.respawns = w.respawns;
            if (!w.map().check_links(true, &why)) {
                ++s.broken;
                printf("  fuzz: the map at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()),
                       why.c_str());
            }
        }
    }
    s.strikes = w.strikes;
    s.places = w.place_count();
    s.straits = w.links().strait_count();
    s.partings = w.links().partings;
    s.joinings = w.links().joinings;
    s.drift = w.drift;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 c = 0; c < p.core_count(); ++c, ++s.parts)
            s.flaky += p.noise.core_flaky(c) != kFlakyOne ? 1 : 0;
        for (u32 d = 0; d < p.mem.device_count(); ++d, ++s.parts)
            s.flaky += p.noise.memory_flaky(d) != kFlakyOne ? 1 : 0;
    }
    s.patch_cuts = w.links().patch_cut;
    s.respawns = w.respawns;
    s.respawns_moved = w.respawns_moved;
    s.torn_down = w.torn_programs;
    s.net = w.net().counts();
    s.link_cuts = w.links().cut;
    s.link_mends = w.links().mended;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        s.core_cutouts += p.cutouts[0] + p.cutouts[1];
        s.memory_cutouts += p.memory_cutouts;
        s.disk_cutouts += p.disk_cutouts;
        s.cold += p.cold_starts;
        s.boots += p.region.boot_runs;
        s.faded += p.noise.cells_faded;
        s.births += p.births;
        s.deaths += p.deaths;
        s.cut_threads += p.cut_threads;
        s.paused += p.paused_threads;
        s.faded_threads += p.faded_threads;
        s.failed_threads += p.failed_threads;
        s.torn += p.os.torn_writes;
        s.failed_reads += p.os.failed_reads;
        s.arrivals += p.region.arrival_runs;
        s.travellers += p.region.travellers;
        s.inbox += p.os.inbox_joined;
        s.direct += p.os.recv_direct;
        s.handled += p.os.inbox_taken;
        for (u32 k = 0; k < kHeatKinds; ++k)
            s.failures[k] += p.failures[k];
        s.failure_threads += p.failure_threads;
        s.weak += p.noise.weak;
        s.hammered += p.noise.hammered;
        s.hammer += p.noise.hammer;
        s.regions_lost += p.mem.region_ok() ? 0 : 1;
        s.block_writes += p.os.block_writes_all;
        s.worn_flips += p.os.worn_flips;
        s.swaps += p.os.level_swaps;
        s.boot_failures += p.boot_failures;
        s.visits += p.visits;
        s.kills += p.os.kills;
        s.killed_threads += p.os.killed_threads;
        s.dog += p.os.watchdog_faults;
        s.dog_handled += p.os.watchdog_handled;
        s.region_swaps += p.region_swaps;
        for (u32 k = 0; k < kHeatKinds; ++k) {
            s.replaced += p.replaced[k];
            s.renewed += p.renewed[k];
        }
        for (u32 t = 0; t < p.os.thread_top(); ++t) {
            const Thread& th = p.os.thread(t);
            if (th.state == ThreadState::free)
                continue;
            const u32 id = p.mem.alloc_at(th.stack);
            s.strays += !id || p.mem.record(id).base != th.stack || p.mem.record(id).holder != th.program;
        }
    }
    return s;
}

void report(const char* what, u64 ticks, const Seen& s) {
    printf("  fuzz: %s, %llu ticks checked in %u places (%llu checks): %llu core, %llu memory and %llu disk "
           "cut-outs, %llu cold starts (%llu boots), %llu cells faded; %llu births, %llu deaths (%llu at cut-outs, "
           "%llu paused, %llu by fading, %llu by a flip or an edge error); %llu writes torn, %llu reads failed\n",
           what, static_cast<unsigned long long>(ticks), s.places, static_cast<unsigned long long>(s.checks),
           static_cast<unsigned long long>(s.core_cutouts), static_cast<unsigned long long>(s.memory_cutouts),
           static_cast<unsigned long long>(s.disk_cutouts), static_cast<unsigned long long>(s.cold),
           static_cast<unsigned long long>(s.boots), static_cast<unsigned long long>(s.faded),
           static_cast<unsigned long long>(s.births), static_cast<unsigned long long>(s.deaths),
           static_cast<unsigned long long>(s.cut_threads), static_cast<unsigned long long>(s.paused),
           static_cast<unsigned long long>(s.faded_threads), static_cast<unsigned long long>(s.failed_threads),
           static_cast<unsigned long long>(s.torn),
           static_cast<unsigned long long>(s.failed_reads));
}

// Static, as an origin run starts.
void test_static() {
    const u64 ticks = 4000;
    const Seen s = fuzz("", ticks, false);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.core_cutouts > 0 && s.memory_cutouts > 0 && s.cold > 0 && s.faded > 0);
    report("static", ticks, s);
}

// Crowded with replicators, booters, and a disk writer and reader, the
// disk cutting out at 900 mV (above the cores) so it cuts out with I/O in
// flight while they live.
void test_crowded() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\n", ticks, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.cut_threads > 0 && s.cold > 0 && s.boots > 0 && s.births > 1000 && s.torn + s.failed_reads > 0);
    CHECK(s.failed_threads > 0);
    report("crowded", ticks, s);
}

// Crowded, with threads paused at a cut-out instead of ended.
void test_paused() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\nos.cutout_pause = true\n", ticks, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.paused > 0 && s.cut_threads == 0);
    report("crowded, pausing at cut-outs", ticks, s);
}

// Crowded, with the saved-state rule narrowed to fading, as before gate 2
// widened it: flips and edge errors end no one.
void test_fading_only() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\nos.saved_state_fails = false\n", ticks, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.failed_threads == 0 && s.births > 1000);
    report("crowded, only fading ending threads", ticks, s);
}

// Crowded, with gate 3's traffic on its map: messages queued, crossing,
// looping back and waiting in inboxes as network devices go dark.
void test_traffic() {
    const u64 ticks = 3000;
    CHECK(!colonist_sender(0).bad && !talker().bad && !listener().bad && !traveller().bad);
    const Seen s = fuzz("disk.threshold = 900\nos.idle_send = true\n", ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    const NetCounts& n = s.net;
    CHECK(n.sent > 0 && n.delivered > 0 && n.looped > 0 && n.pings > 0 && n.echoes > 0);
    CHECK(s.dark > 0 && n.cut + n.lost + n.stranded > 0);
    CHECK(s.arrivals > 0 && s.travellers > 0 && s.inbox > 0 && s.handled > 0);
    report("crowded, with traffic", ticks, s);
    printf("  fuzz: its traffic: %llu sent (%llu to their own place), %llu delivered, %llu PINGs answered; dropped "
           "%llu full, %llu stranded, %llu lost, %llu cut, with network devices dark %llu place-ticks; %llu "
           "arrivals ran (%llu travellers); %llu joined inboxes, %llu taken there, %llu went straight to a RECV\n",
           static_cast<unsigned long long>(n.sent), static_cast<unsigned long long>(n.looped),
           static_cast<unsigned long long>(n.delivered), static_cast<unsigned long long>(n.pings),
           static_cast<unsigned long long>(n.full), static_cast<unsigned long long>(n.stranded),
           static_cast<unsigned long long>(n.lost), static_cast<unsigned long long>(n.cut),
           static_cast<unsigned long long>(s.dark), static_cast<unsigned long long>(s.arrivals),
           static_cast<unsigned long long>(s.travellers), static_cast<unsigned long long>(s.inbox),
           static_cast<unsigned long long>(s.handled), static_cast<unsigned long long>(s.direct));
}

// The same traffic in the default world (build-plan-gate3.md step 8): an
// idle run's SEND fails, so the travellers' arrivals only fault, and
// programs' arrivals still land and run.
void test_traffic_default() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\n", ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.net.sent > 0 && s.net.delivered > 0 && s.arrivals > 0 && s.travellers == 0);
    report("crowded, with traffic, idle runs not SENDing", ticks, s);
}

// The fifth world wearing out: every kind of device fails for good during
// the run, with work in hand.
void test_wearing() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\nos.idle_send = true\nhw.temperature = 450\nwear.rate = 1\n"
                        "wear.p.endurance = 1500\nwear.e.endurance = 1500\nwear.memory.endurance = 1500\n"
                        "wear.disk.endurance = 1200\nwear.network.endurance = 1200\nwear.cooler.endurance = 1200\n"
                        "wear.weak.share = 0.01\nwear.weak.chance = 0.01\ndisk.block = 8192\ndisk.block_endurance = 2\n"
                        "disk.worn_flip = 0.05\nos.levelling = true\nos.levelling_gap = 1\n",
                        ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    const u64* f = s.failures;
    CHECK(f[0] + f[1] > 8 && f[2] > 4 && f[3] > 4 && f[4] > 4);
    CHECK(s.failure_threads > 0 && s.weak > 0 && s.regions_lost > 0 && s.births > 1000);
    CHECK(s.block_writes > 20 && s.swaps > 0 && s.worn_flips > 0);
    report("crowded, with traffic, wearing out", ticks, s);
    printf("  fuzz: its failures: %llu P and %llu E cores, %llu memory devices (%llu of them with the reset "
           "region), %llu disks, %llu network devices, %llu coolers; %llu threads ended by one; %llu bits lost "
           "in weak cells; %llu block writes with %llu swaps by levelling, %llu bits landed wrong in worn "
           "blocks, %llu of the disks failed by their boot block\n",
           static_cast<unsigned long long>(f[0]), static_cast<unsigned long long>(f[1]),
           static_cast<unsigned long long>(f[2]), static_cast<unsigned long long>(s.regions_lost),
           static_cast<unsigned long long>(f[3]), static_cast<unsigned long long>(f[4]),
           static_cast<unsigned long long>(f[5]), static_cast<unsigned long long>(s.failure_threads),
           static_cast<unsigned long long>(s.weak), static_cast<unsigned long long>(s.block_writes),
           static_cast<unsigned long long>(s.swaps), static_cast<unsigned long long>(s.worn_flips),
           static_cast<unsigned long long>(s.boot_failures));
}

// The seventh world with visits: what fails is replaced, and what is worn
// past 0.8 of its life, so every kind of device goes and comes back under
// load.
void test_mended() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\nos.idle_send = true\nhw.temperature = 450\nwear.rate = 1\n"
                        "wear.p.endurance = 500\nwear.e.endurance = 500\nwear.memory.endurance = 500\n"
                        "wear.disk.endurance = 400\nwear.network.endurance = 400\nwear.cooler.endurance = 400\n"
                        "wear.weak.share = 0.01\nwear.weak.chance = 0.01\ndisk.block = 8192\ndisk.block_endurance = 2\n"
                        "disk.worn_flip = 0.05\nos.levelling = true\nos.levelling_gap = 1\n"
                        "visit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\n"
                        "link.cuts = 0.003\nnet.lifetime = 40\n",
                        ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.link_cuts > 20 && s.link_mends > 10 && s.net.waited > 10 && s.net.rerouted > 0);
    CHECK(s.visits > 60 && s.replaced > 40 && s.renewed > 10 && s.region_swaps > 2 && s.births > 1000);
    report("crowded, with traffic, wearing out and mended", ticks, s);
    const u64* f = s.failures;
    printf("  fuzz: its visits: %llu, replacing %llu failed devices (%llu of them holding the reset region) and "
           "%llu worn ones like for like, of %llu failures (%llu cores, %llu memory devices, %llu disks, %llu "
           "network devices)\n",
           static_cast<unsigned long long>(s.visits), static_cast<unsigned long long>(s.replaced),
           static_cast<unsigned long long>(s.region_swaps), static_cast<unsigned long long>(s.renewed),
           static_cast<unsigned long long>(f[0] + f[1] + f[2] + f[3] + f[4] + f[5]),
           static_cast<unsigned long long>(f[0] + f[1]), static_cast<unsigned long long>(f[2]),
           static_cast<unsigned long long>(f[3]), static_cast<unsigned long long>(f[4]));
    printf("  fuzz: its links: %llu cut and %llu mended; %llu messages began a wait, %llu took a new route, %llu "
           "ran out of time\n",
           static_cast<unsigned long long>(s.link_cuts), static_cast<unsigned long long>(s.link_mends),
           static_cast<unsigned long long>(s.net.waited), static_cast<unsigned long long>(s.net.rerouted),
           static_cast<unsigned long long>(s.net.expired));
}

// The eighth world, with places torn down at their visits once 3 in 10 of
// their devices have failed, and built again where the map puts them: the
// network, the routes and the link table follow the map through it all.
void test_respawning() {
    const u64 ticks = 3000;
    const Seen s = fuzz("disk.threshold = 900\nos.idle_send = true\nhw.temperature = 450\nwear.rate = 1\n"
                        "wear.p.endurance = 500\nwear.e.endurance = 500\nwear.memory.endurance = 500\n"
                        "wear.disk.endurance = 400\nwear.network.endurance = 400\nwear.cooler.endurance = 400\n"
                        "wear.weak.share = 0.01\nwear.weak.chance = 0.01\ndisk.block = 8192\ndisk.block_endurance = 2\n"
                        "disk.worn_flip = 0.05\nos.levelling = true\nos.levelling_gap = 1\n"
                        "visit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\nvisit.respawn_share = 0.3\n"
                        "link.cuts = 0.003\nnet.lifetime = 40\n",
                        ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.respawns > 5 && s.respawns_moved > 5 && s.visits > 60 && s.replaced > 10 && s.births > 300);
    report("crowded, with traffic, wearing out, mended and respawning", ticks, s);
    printf("  fuzz: its respawns: %llu places torn down at a visit (%llu built again at a new point), ending %llu "
           "programs; %llu messages lost with gone wires and %llu with a torn-down or dark place's device; the "
           "map's rules held after each, against every pair\n",
           static_cast<unsigned long long>(s.respawns), static_cast<unsigned long long>(s.respawns_moved),
           static_cast<unsigned long long>(s.torn_down), static_cast<unsigned long long>(s.net.unlinked),
           static_cast<unsigned long long>(s.net.cut));
}

// The ninth world, with the sources on the map and every type of patch over
// it: storms striking, swells, brownouts, heat waves and outages, with
// places wearing out, mended and torn down as the eighth's are, and
// rowhammer with thresholds low enough for ordinary programs to pass.
void test_patched() {
    const u64 ticks = 3000;
    const Seen s = fuzz("supply.model = sources\nheat.on = true\ndisk.threshold = 900\nos.idle_send = true\n"
                        "wear.rate = 1\nwear.p.endurance = 500\nwear.e.endurance = 500\nwear.memory.endurance = 500\n"
                        "wear.disk.endurance = 400\nwear.network.endurance = 400\nwear.cooler.endurance = 400\n"
                        "wear.weak.share = 0.01\nwear.weak.chance = 0.01\ndisk.block = 8192\ndisk.block_endurance = 2\n"
                        "disk.worn_flip = 0.05\nos.levelling = true\nos.levelling_gap = 1\n"
                        "visit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\nvisit.respawn_share = 0.3\n"
                        "link.cuts = 0.003\nnet.lifetime = 40\nramp.lead.min = 10\nramp.lead.max = 100\n"
                        "patch.life.min = 100\npatch.life.max = 500\n"
                        "patch.storm.rate = 30000\npatch.storm.strength.min = 0.005\npatch.storm.strength.max = 0.02\n"
                        "patch.swell.rate = 30000\npatch.brownout.rate = 30000\npatch.heatwave.rate = 30000\n"
                        "patch.outage.rate = 30000\nhammer.strength = 0.01\nhammer.window = 500\n"
                        "hammer.threshold.fast = 200\nhammer.threshold.slow = 600\n",
                        ticks, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 8);
    CHECK(s.strikes > 5 && s.patch_cuts > 5 && s.patch_ticks > ticks && s.births > 100);
    CHECK(s.hammer > 100 && s.hammer < s.hammered);
    report("crowded, with traffic, the sources on the map and every type of patch", ticks, s);
    printf("  fuzz: its patches: %.1f there at a time; %llu strikes; %llu links cut by outages; %llu places torn "
           "down at a visit; %llu row activations past a rowhammer threshold flipped %llu bits\n",
           static_cast<double>(s.patch_ticks) / ticks, static_cast<unsigned long long>(s.strikes),
           static_cast<unsigned long long>(s.patch_cuts), static_cast<unsigned long long>(s.respawns),
           static_cast<unsigned long long>(s.hammered), static_cast<unsigned long long>(s.hammer));
}

// The tenth world: the ninth's on a map of 24 places, with a third of the
// cores and memory devices flaky (and cores' floors rising tenfold as they
// wear), every strait parting and joining every 50 to 400 ticks, and the
// replication current with gusts: blocks of memory copied downstream into
// reset regions while places wear out, black out, are torn down and built
// again, and links are cut, part and are mended.
void test_adrift() {
    const u64 ticks = 2000;
    const Seen s = fuzz("world.places = 24\nsupply.model = sources\nheat.on = true\ndisk.threshold = 900\n"
                        "os.idle_send = true\n"
                        "wear.rate = 1\nwear.p.endurance = 500\nwear.e.endurance = 500\nwear.memory.endurance = 500\n"
                        "wear.disk.endurance = 400\nwear.network.endurance = 400\nwear.cooler.endurance = 400\n"
                        "wear.weak.share = 0.01\nwear.weak.chance = 0.01\ndisk.block = 8192\ndisk.block_endurance = 2\n"
                        "disk.worn_flip = 0.05\nos.levelling = true\nos.levelling_gap = 1\n"
                        "visit.mean = 250\nvisit.regularity = 0.3\nvisit.replace_at = 0.8\nvisit.respawn_share = 0.3\n"
                        "link.cuts = 0.003\nnet.lifetime = 40\nramp.lead.min = 10\nramp.lead.max = 100\n"
                        "patch.life.min = 100\npatch.life.max = 500\n"
                        "patch.storm.rate = 30000\npatch.storm.strength.min = 0.005\npatch.storm.strength.max = 0.02\n"
                        "patch.swell.rate = 30000\npatch.brownout.rate = 30000\npatch.heatwave.rate = 30000\n"
                        "patch.outage.rate = 30000\nhammer.strength = 0.01\nhammer.window = 500\n"
                        "hammer.threshold.fast = 200\nhammer.threshold.slow = 600\n"
                        "flaky.share = 0.3\nflaky.worn = 10\nstrait.share = 1\nstrait.spell.min = 50\n"
                        "strait.spell.max = 400\ncurrent.strength = 60\ncurrent.season = 700\n"
                        "patch.gust.rate = 30000\nos.killer = 10\nos.watchdog = 5000\n",
                        ticks, true, true, true);
    CHECK(s.broken == 0 && s.strays == 0 && s.checks == ticks * 24);
    CHECK(s.flaky * 6 > s.parts && s.flaky * 2 < s.parts && s.straits > 0 && s.partings > 3 && s.joinings > 3);
    CHECK(s.gusts > 100 && s.drift.drawn > 1000 && s.drift.sent > 500 && s.drift.landed > 300);
    CHECK(s.drift.unlit > 0 && s.drift.down > 0 && s.drift.dark > 0 && s.net.adrift > 0 && s.respawns > 5);
    CHECK(s.kills > 0 && s.killed_threads >= s.kills && s.dog > 0 && s.dog_handled > 0);
    report("crowded, with traffic, every patch, flaky parts, straits parting, the current, the killer and the "
           "watchdog",
           ticks, s);
    printf("  fuzz: its flaky parts: %u of %u cores and memory devices at the end; its %u straits parted %llu "
           "times and joined %llu; %.1f gusts there at a time; the current drew %llu blocks: %llu becalmed, "
           "%llu at a place with its network device off, %llu from memory off or failed, %llu at a failed or "
           "parted link, %llu toward a dark place, %llu with no room, %llu sent; %llu let go on the way, "
           "%llu landed, %llu at a region off or failed; %llu places torn down at a visit\n",
           s.flaky, s.parts, s.straits, static_cast<unsigned long long>(s.partings),
           static_cast<unsigned long long>(s.joinings), static_cast<double>(s.gusts) / ticks,
           static_cast<unsigned long long>(s.drift.drawn),
           static_cast<unsigned long long>(s.drift.becalmed), static_cast<unsigned long long>(s.drift.unlit),
           static_cast<unsigned long long>(s.drift.unread), static_cast<unsigned long long>(s.drift.down),
           static_cast<unsigned long long>(s.drift.dark), static_cast<unsigned long long>(s.drift.full),
           static_cast<unsigned long long>(s.drift.sent), static_cast<unsigned long long>(s.net.adrift),
           static_cast<unsigned long long>(s.drift.landed), static_cast<unsigned long long>(s.drift.unlanded),
           static_cast<unsigned long long>(s.respawns));
    printf("  fuzz: its killer and watchdog: %llu programs ended out of memory (%llu threads); %llu watchdog "
           "faults, %llu of them taken by a handler\n",
           static_cast<unsigned long long>(s.kills), static_cast<unsigned long long>(s.killed_threads),
           static_cast<unsigned long long>(s.dog), static_cast<unsigned long long>(s.dog_handled));
}

} // namespace

void suite_fuzz() {
    test_static();
    test_crowded();
    test_paused();
    test_fading_only();
    test_traffic();
    test_traffic_default();
    test_wearing();
    test_mended();
    test_respawning();
    test_patched();
    test_adrift();
}
