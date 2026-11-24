// Edge errors and memory fading (design.md 4.4, 7.5, 9.4, 10.2, appendix C;
// build-plan-gate2.md step 5): a core's error rate through its countdown at
// voltages across its range, against the settings; memory's bits failing to
// hold near its edges, at the rate set, reading back random; fading's share
// of a device after dips of known depth and length, the same cells first
// each time, visiting only the failing cells, and the reset region with its
// device; a sleeping thread whose saved state fades ending, cause "memory",
// through its saved registers or its handler's frame, and one whose saved
// state holds living on; the new state through threads and snapshots;
// a big fade cut into chunks on many host threads the same to the bit as
// one run straight through (optimization-plan.md pass 10); a fade looking
// for saved states only in the blocks of memory they reach finding the
// same hits, the states at the blocks' edges; and a batch's walks to their
// cells taken a step at a time together landing on cell_of's (pass 11).
#include <cmath>
#include <cstdio>
#include <cstdlib> // calloc, malloc, free
#include <cstring>
#include <intrin.h> // __popcnt

#include "check.h"
#include "nova2/noise.h"
#include "nova2/rng.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

// A memory, two cores and their noise, without a place (as test_noise.cpp's).
struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Noise noise;
    DeviceState states[2];

    explicit Rig(NoiseSettings s, u64 seed = 7) {
        // Fast memory at 700 mV and slow at 600; the reset region goes on the slow.
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
        CHECK(noise.init(&m, cores, 2, &env, seed, s, PowerSettings{}, nullptr));
    }

    // Counts n results through a core's countdown, as the interpreter does.
    u64 results(u32 core, u64 n) {
        ErrorClock& c = cores[core].clock;
        u64 wrong = 0;
        for (u64 i = 0; i < n; ++i)
            if (c.left != ~0ull && --c.left == 0) {
                c.next(&c);
                ++wrong;
            }
        return wrong;
    }

    // Holds memory at mv for `ticks` ticks, the devices in their states, as
    // appendix C's step 8 does.
    void hold(i32 mv, u64 ticks) {
        for (u64 t = 0; t < ticks; ++t) {
            if (noise.fade_check(states, mv))
                noise.fade(states, mv, nullptr, 0, nullptr);
            ++env.tick;
        }
    }
};

// Within 4 standard deviations of a count expected to be Poisson.
bool near_count(double got, double want) { return fabs(got - want) <= 4 * sqrt(want) + 1; }

// Done when (1): measured edge-error rates match the settings. Inside 50 mV
// of an edge the total doubles every 10 mV closer, so a P core (850 to
// 1,300 mV) at 880 mV makes 4 times its floor's errors, and at 850 mV 32.
void test_edge_rates() {
    NoiseSettings s;
    s.p_floor = s.e_floor = 1e-3;
    s.cosmic = 0;
    Rig g(s);
    struct Case {
        u32 core;
        i32 mv;
        double times; // the floor's
    };
    const Case cases[] = {
        {0, 1000, 1},  {0, 900, 1},  {0, 880, 4},  {0, 860, 16}, {0, 851, 29.857}, {0, 850, 32},
        {0, 820, 32}, // below the threshold, as at the edge
        {0, 1290, 16}, {1, 760, 16}, {1, 1195, 22.627}, {1, 900, 1},
    };
    double measured[11];
    for (u32 i = 0; i < 11; ++i) {
        const Case& k = cases[i];
        g.noise.core_edges(k.mv);
        const double rate = s.p_floor * k.times;
        const u64 n = static_cast<u64>(10000 / rate); // about 10,000 wrong
        const u64 wrong = g.results(k.core, n);
        CHECK(near_count(static_cast<double>(wrong), rate * n));
        measured[i] = static_cast<double>(wrong) / n / s.p_floor;
        const double p64 = static_cast<double>(g.noise.rate(k.core, 10) + g.noise.edge(k.core));
        CHECK(fabs(p64 / 18446744073709551616.0 / rate - 1) < 1e-3);
    }
    // A core that's cut out makes no edge errors: it runs nothing.
    g.cores[0].working = false;
    g.noise.core_edges(850);
    CHECK(g.noise.edge(0) == 0);
    g.cores[0].working = true;
    // The clock adds to it: at 1.5 times stock and the edge, 32 + 31 floors.
    g.noise.core_edges(850);
    set_level(g.cores[0].clock, 15);
    const u64 n = 200000; // 12,600 expected
    const u64 wrong = g.results(0, n);
    CHECK(near_count(static_cast<double>(wrong), 63 * s.p_floor * n));
    set_level(g.cores[0].clock, 10);
    // No band, no edge errors.
    NoiseSettings off = s;
    off.edge_band = 0;
    Rig z(off);
    z.noise.core_edges(850);
    CHECK(z.noise.edge(0) == 0 && z.noise.edge_rate(0, 850, 850, 1300) == 0);
    printf("  fade: a P core's error rate measured %.2f, %.2f and %.2f times its floor at 880, 860 and 850 mV (4, 16 "
           "and 32 set), %.2f at 900 mV and %.2f 10 mV from its maximum (16); an E core's %.2f at 760 mV (16)\n",
           measured[2], measured[3], measured[5], measured[1], measured[7], measured[8]);
}

// Memory's edge errors: bits it fails to hold, at the cosmic floor × 31 at
// its threshold, each reading back random, so about half change.
void test_memory_edge() {
    NoiseSettings s;
    s.p_floor = s.e_floor = 0;
    s.cosmic = 1e-6;
    Rig g(s);
    memset(g.m.bytes(), 0, g.m.size());
    const u64 ticks = 2500;
    u64 changed = 0;
    for (u64 t = 0; t < ticks; ++t) {
        // Device 0 at its threshold (700 mV); device 1 100 mV above its own.
        g.noise.memory_errors(g.states, 700);
        ++g.env.tick;
    }
    for (u32 i = 0; i < g.m.device(0).size; ++i)
        changed += static_cast<u64>(__popcnt(g.m.bytes()[g.m.device(0).base + i]));
    const double want = 31 * s.cosmic * 16384 * 8 * ticks; // 10,158
    CHECK(near_count(static_cast<double>(g.noise.memory_edge), want));
    // A bit that fails twice may land back where it was, so a little under half.
    CHECK(changed > 0.45 * static_cast<double>(g.noise.memory_edge) &&
          changed < 0.5 * static_cast<double>(g.noise.memory_edge) + 4 * sqrt(want));
    u32 other = 0; // device 1, 100 mV above its threshold, untouched
    for (u32 i = 0; i < g.m.device(1).size; ++i)
        other += g.m.bytes()[g.m.device(1).base + i] != 0 ? 1 : 0;
    CHECK(other == 0);
    // Cut out, it makes none (it fades instead).
    const u64 before = g.noise.memory_edge;
    g.states[0].on = false;
    for (u32 t = 0; t < 100; ++t) {
        g.noise.memory_errors(g.states, 700);
        ++g.env.tick;
    }
    CHECK(g.noise.memory_edge == before);
    printf("  fade: memory at its threshold failed to hold %llu bits in %llu ticks (%.0f expected), %llu of them "
           "left changed\n",
           static_cast<unsigned long long>(g.noise.memory_edge), static_cast<unsigned long long>(ticks), want,
           static_cast<unsigned long long>(changed));
}

// Done when (2): a dip of known depth and length loses the expected share
// of a device. At section 7's numbers, cells hold 1 to 1,000 ticks at 0 V,
// 1,000 times longer at the threshold: a tick at V adds 1000^(−V ÷ 700) ticks
// of 0 V to the dose, and log10(dose) ÷ 3 of the cells have failed.
void test_fade_share() {
    NoiseSettings s;
    s.p_floor = s.e_floor = s.cosmic = 0;
    Rig g(s);
    g.states[0].on = false; // device 0 (700 mV) cut out
    const double cells = 16384.0 * 8;
    struct Dip {
        i32 mv;
        u64 ticks;
    };
    const Dip dips[] = {{0, 1}, {0, 10}, {0, 100}, {0, 2000}, {350, 1000}, {600, 300}, {699, 1000}, {699, 990}, {700, 5000}};
    double shares[9];
    for (u32 i = 0; i < 9; ++i) {
        g.states[0].on = true; // back on: refreshed
        CHECK(!g.noise.fade_check(g.states, 0) && g.noise.faded(0) == 0 && g.noise.dose(0) == 0);
        g.states[0].on = false;
        g.hold(dips[i].mv, dips[i].ticks);
        const double dose =
            static_cast<double>(dips[i].ticks) * (dips[i].mv < 700 ? pow(1000.0, -dips[i].mv / 700.0) : 0);
        double want = dose >= 1 ? log10(dose) / 3 : 0;
        want = want > 1 ? 1 : want;
        shares[i] = static_cast<double>(g.noise.faded(0)) / cells;
        CHECK(fabs(shares[i] - want) < 2e-5);
    }
    CHECK(g.noise.faded(1) == 0); // device 1 stayed on
    CHECK(shares[7] == 0);        // just below the threshold, nothing for 990 ticks
    printf("  fade: at 0 V a device lost %.4f of its cells in 10 ticks, %.4f in 100 and all in 2,000; at half its "
           "threshold %.4f in 1,000 ticks; a millivolt below it, %.4f in 1,000 and nothing in 990\n",
           shares[1], shares[2], shares[4], shares[6]);
}

// Done when (3): fading visits only the failing cells. The leak ranks are a
// bijection onto the device's cells, the same cells fail first each time,
// and only they change; and the reset region fades with its device.
void test_fade_cells() {
    NoiseSettings s;
    s.p_floor = s.e_floor = s.cosmic = 0;
    Rig g(s);
    // Device 1's 65,536 cells: every rank to a different cell.
    const u64 n1 = 8192 * 8;
    u8* seen = static_cast<u8*>(calloc(n1 / 8, 1));
    u64 distinct = 0;
    for (u64 r = 0; r < n1; ++r) {
        const u64 c = g.noise.cell_of(1, r);
        if (c < n1 && !(seen[c >> 3] & (1u << (c & 7)))) {
            seen[c >> 3] = static_cast<u8>(seen[c >> 3] | (1u << (c & 7)));
            ++distinct;
        }
    }
    CHECK(distinct == n1);
    free(seen);
    // Device 0 zeroed, then 10 ticks at 0 V.
    memset(g.m.bytes(), 0, g.m.size());
    g.states[0].on = false;
    g.hold(0, 10);
    const u64 n = g.noise.faded(0);
    CHECK(n > 0 && g.noise.cells_faded == n);
    const u32 base = g.m.device(0).base, size = g.m.device(0).size;
    u8* failing = static_cast<u8*>(calloc(size, 1));
    for (u64 r = 0; r < n; ++r) {
        const u64 c = g.noise.cell_of(0, r);
        failing[c >> 3] = static_cast<u8>(failing[c >> 3] | (1u << (c & 7)));
    }
    u64 set = 0, outside = 0;
    for (u32 i = 0; i < size; ++i) {
        const u8 b = g.m.bytes()[base + i];
        set += static_cast<u64>(__popcnt(b));
        outside += static_cast<u64>(__popcnt(static_cast<u8>(b & ~failing[i])));
    }
    CHECK(outside == 0);                                   // nothing outside the failing cells changed
    CHECK(fabs(static_cast<double>(set) - n / 2.0) < 4 * sqrt(n / 4.0)); // each reads back random
    // Refreshed and faded again: the same cells, as the leakiest always fail first.
    memset(g.m.bytes() + base, 0, size);
    g.states[0].on = true;
    g.hold(1000, 1);
    g.states[0].on = false;
    g.hold(0, 10);
    CHECK(g.noise.faded(0) == n);
    u64 outside2 = 0;
    for (u32 i = 0; i < size; ++i)
        outside2 += static_cast<u64>(__popcnt(static_cast<u8>(g.m.bytes()[base + i] & ~failing[i])));
    CHECK(outside2 == 0);
    free(failing);
    // The reset region, on device 1, fades with it: its bytes change, and a
    // halted core would see the region written.
    const u32 writes = g.m.region_writes();
    memset(g.m.bytes() + g.m.region_base(), 0, g.m.region_size());
    g.states[1].on = false;
    g.hold(0, 10);
    u64 region_set = 0;
    for (u32 i = 0; i < g.m.region_size(); ++i)
        region_set += static_cast<u64>(__popcnt(g.m.bytes()[g.m.region_base() + i]));
    CHECK(region_set > 0 && g.m.region_writes() > writes);
    printf("  fade: 10 ticks at 0 V failed %llu cells of 131,072, %llu of them read back 1 and nothing else "
           "changed; again after a refresh, the same cells; the reset region faded with its device\n",
           static_cast<unsigned long long>(n), static_cast<unsigned long long>(set));
}

// ---------------------------------------------------------------------------
// In a place
// ---------------------------------------------------------------------------

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "supply.sag = 50\n";

// Sleeps 120 ticks at a time, for ever.
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
// Sets a timer handler with a period of a tick (B = its start + 17, C = 1,
// A = 2), waits for an interrupt, and in the handler sleeps 120 ticks at a
// time for ever: so it sleeps with its handler's frame on its stack.
const u8 kHandlerSleeper[23] = {encode(kMov, kB),   encode(kAdd, kImm), 17,     encode(kSto, kB),
                                encode(kMov, kImm), 1,                  encode(kSto, kC),
                                encode(kMov, kImm), kOnTimer,           encode(kSys, kImm),
                                kSetHandler,        encode(kMov, kImm), 0,      encode(kSys, kImm),
                                kSleep,             encode(kJmp, kImm), static_cast<u8>(-4),
                                encode(kMov, kImm), 120,                encode(kSys, kImm),
                                kSleep,             encode(kJmp, kImm), static_cast<u8>(-4)};

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

// A hand-placed program running `code` (R7): its thread's record index.
u32 place(Place& p, const u8* code, u32 len, u32 size = 64) {
    const u32 at = p.mem.alloc(1, size, 2, 0);
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

// The first drop-out to start after tick 300, and at least 50 ticks after
// the one before it ended.
Dropout settled_dropout(Place& p) {
    Dropout first;
    first.length = 0;
    u64 last_end = 0;
    for (u64 j = 0; j < 100000 && first.length == 0; ++j) {
        Dropout d;
        if (!p.supply.dropout_in(j, &d))
            continue;
        if (d.start > 300 && d.start >= last_end + 50)
            first = d;
        last_end = d.start + d.length;
    }
    CHECK(first.length != 0);
    return first;
}

void run_to(Place& p, u64 tick) {
    while (p.env.tick < tick) {
        p.os.clear_facts();
        p.tick();
    }
}

// Done when (4): a sleeping thread whose saved state fades ends, cause
// "memory". A 2-tick blackout, with cells holding 1.9 ticks to a billion at
// 0 V, fades about a quarter of a percent of memory: some sleepers' saved
// states lose a bit and some don't. Each that lost one, in its saved
// registers or its handler's frame, ended; each that didn't lives.
void test_saved_state() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "supply.dropouts = 1000\nsupply.dropout.min = 2\nsupply.dropout.max = 2\nsupply.blackout_share = 1\n"
                "memory.fade.fastest = 1.9\nmemory.fade.slowest = 1e9\n",
                &why));
    Place& p = w.place(0);
    const u32 kEach = 24;
    u32 t[2 * kEach];
    for (u32 i = 0; i < kEach; ++i) {
        t[i] = place(p, kSleeper, 6);
        t[kEach + i] = place(p, kHandlerSleeper, 23);
    }
    const Dropout d = settled_dropout(p);
    run_to(p, d.start);
    // Just before: their saved states, and which threads are on a core (the
    // cut-out ends those, so they're left out).
    struct Seen {
        u32 holder;
        u64 uid;
        bool on_core;
        u32 regs, frame; // their starts (frame 0: not in a handler)
    } seen[2 * kEach];
    for (u32 i = 0; i < 2 * kEach; ++i) {
        const Thread& th = p.os.thread(t[i]);
        CHECK(th.state != ThreadState::free);
        seen[i] = Seen{th.program, p.os.program(th.program).uid, th.core != 0, th.stack, th.in_handler ? th.frame : 0};
        if (i >= kEach)
            CHECK(th.in_handler); // the handler sleepers are in their handlers
    }
    // The blackout's two ticks, keeping their deaths.
    p.os.clear_facts();
    while (p.env.tick < d.start + d.length)
        p.tick();
    CHECK(!p.memory_state[0].on); // still cut out: nothing refreshed yet
    // The cells failed, by address: the first faded(d) ranks of each device.
    const u32 bits = p.mem.size() * 8;
    u8* failed = static_cast<u8*>(calloc(bits / 8, 1));
    u64 cells = 0, faded = 0;
    for (u32 k = 0; k < p.mem.device_count(); ++k) {
        const u64 n = p.noise.faded(k);
        faded += n;
        cells += static_cast<u64>(p.mem.device(k).size) * 8;
        for (u64 r = 0; r < n; ++r) {
            const u64 b = static_cast<u64>(p.mem.device(k).base) * 8 + p.noise.cell_of(k, r);
            failed[b >> 3] = static_cast<u8>(failed[b >> 3] | (1u << (b & 7)));
        }
        // The share each lost: log(2 ÷ 1.9) ÷ log(1e9 ÷ 1.9).
        const double want = log(2 / 1.9) / log(1e9 / 1.9);
        CHECK(fabs(static_cast<double>(n) / (p.mem.device(k).size * 8.0) - want) < 1e-4);
    }
    u32 died = 0, lived = 0, by_frame = 0, on_core = 0;
    for (u32 i = 0; i < 2 * kEach; ++i) {
        if (seen[i].on_core) {
            ++on_core;
            continue;
        }
        bool regs_hit = false, frame_hit = false;
        for (u32 b = seen[i].regs * 8; b < (seen[i].regs + kSavedRegsBytes) * 8; ++b)
            regs_hit = regs_hit || (failed[b >> 3] >> (b & 7) & 1) != 0;
        if (seen[i].frame)
            for (u32 b = seen[i].frame * 8; b < (seen[i].frame + kHandlerFrameBytes) * 8; ++b)
                frame_hit = frame_hit || (failed[b >> 3] >> (b & 7) & 1) != 0;
        const Program& pr = p.os.program(seen[i].holder);
        const bool same = pr.alive && pr.uid == seen[i].uid;
        bool by_memory = false;
        for (u32 k = 0; k < p.os.deaths.size(); ++k)
            by_memory = by_memory || (p.os.deaths[k].uid == seen[i].uid && p.os.deaths[k].cause == DeathCause::memory_lost);
        if (regs_hit || frame_hit) {
            CHECK(!same && by_memory);
            ++died;
            by_frame += frame_hit && !regs_hit ? 1 : 0;
        } else {
            CHECK(same);
            ++lived;
        }
    }
    free(failed);
    CHECK(died > 0 && lived > 0 && by_frame > 0 && p.faded_threads == died);
    printf("  fade: a 2-tick blackout faded %.2f%% of memory (as set); of %u sleepers, the %u whose saved state lost "
           "a bit ended, cause memory (%u through a handler's frame alone), and the %u whose didn't lived\n",
           100.0 * faded / cells, 2 * kEach - on_core, died, by_frame, lived);
}

// The new state: a busy world under a fast supply with drop-outs, its cores
// near their edges at night and its memory fading, is the same on 1 and 3
// threads; and resumed from a snapshot taken while a place fades, the same
// as never stopped.
void test_fade_state() {
    const char* busy = "world.places = 6\nhw.cores.max = 4\nsupply.night = 0.2\nsupply.day = 200\nsupply.swing = 0.25\n"
                       "supply.swing_period = 500\nsupply.dropouts = 1000\nsupply.dropout.min = 20\n"
                       "supply.dropout.max = 200\nnoise.p_floor = 1e-4\nnoise.e_floor = 1e-4\nnoise.cosmic = 1e-6\n";
    const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                         encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                         encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
    u64 h[2] = {};
    u64 band = 0, faded = 0, edge = 0, fade_deaths = 0, snap_at = 0;
    ByteWriter out;
    TextBuf text;
    for (u32 run = 0; run < 2; ++run) {
        World w;
        Config cfg;
        TextBuf why;
        const bool built = build(w, cfg, busy, &why);
        if (!built)
            printf("  fade: busy world: %s %s\n", why.c_str(), cfg.value_error());
        CHECK(built);
        for (u32 i = 0; i < w.place_count(); ++i)
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
        w.start_threads(run ? 3 : 1);
        for (u32 t = 0; t < 2000; ++t) {
            w.tick();
            if (run == 0 && !snap_at)
                for (u32 i = 0; i < w.place_count() && !snap_at; ++i)
                    if (w.place(i).noise.dose(0) > 0 && w.place(i).noise.faded(0) > 0 && t > 50) {
                        snap_at = w.tick_count(); // mid-fade
                        cfg.write(text);
                        snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
                        w.save(out);
                        snapshot_finish(out);
                    }
        }
        h[run] = w.hash();
        if (run == 0)
            for (u32 i = 0; i < w.place_count(); ++i) {
                band += w.place(i).noise.band_errors;
                edge += w.place(i).noise.memory_edge;
                faded += w.place(i).noise.cells_faded;
                fade_deaths += w.place(i).faded_threads;
            }
    }
    CHECK_EQ_U64(h[0], h[1]);
    CHECK(band > 0 && faded > 0 && snap_at > 0);
    // From the snapshot, on 2 threads, to the same end.
    SnapshotHeader sh;
    TextBuf err;
    CHECK(snapshot_parse(out.data(), out.size(), &sh, &err));
    Config cfg2;
    Config::ParseError pe;
    CHECK(cfg2.parse(sh.config_text, &pe));
    World b;
    TextBuf why;
    CHECK(b.init(cfg2, &why));
    ByteReader in(out.data() + sh.world_offset, sh.world_size);
    CHECK(b.load(in));
    b.start_threads(2);
    while (b.tick_count() < 2000)
        b.tick();
    CHECK_EQ_U64(b.hash(), h[0]);
    printf("  fade: a busy world made %llu errors near an edge, failed %llu bits near one and faded %llu cells "
           "(%llu threads ended by it): the same on 1 and 3 threads, and resumed from mid-fade at tick %llu\n",
           static_cast<unsigned long long>(band), static_cast<unsigned long long>(edge),
           static_cast<unsigned long long>(faded), static_cast<unsigned long long>(fade_deaths),
           static_cast<unsigned long long>(snap_at));
}

// A fade's chunks for test_fade_spread, one task each: the noise, the saved
// states and the blocks they reach, and each chunk's job, ranks, hits and
// whether it changed a byte of the reset region.
struct Chunks {
    const Noise* noise;
    const SavedState* saved;
    u32 saved_count;
    const u64* blocks;
    Array<u32> job;
    Array<u64> k0, k1;
    Array<u8> region;
    Array<u32>* hits;
};

void fail_chunk(void* ctx, u32 c) {
    Chunks* k = static_cast<Chunks*>(ctx);
    const FadeJob& f = k->noise->fade_jobs()[k->job[c]];
    k->region[c] =
        k->noise->fade_cells(f, k->k0[c], k->k1[c], k->saved, k->saved_count, k->blocks, &k->hits[c], true) ? 1 : 0;
}

// Each place's memory, faded threads, cells faded and reset-region writes
// the same in two worlds.
bool same_places(World& a, World& b) {
    for (u32 i = 0; i < a.place_count(); ++i) {
        Place& p = a.place(i);
        Place& q = b.place(i);
        if (memcmp(p.mem.bytes(), q.mem.bytes(), p.mem.size()) != 0 || p.faded_threads != q.faded_threads ||
            p.noise.cells_faded != q.noise.cells_faded || p.mem.region_writes() != q.mem.region_writes())
            return false;
    }
    return true;
}

// Done when (optimization-plan.md pass 10): a big fade cut into chunks, run
// at once on many host threads and in reverse, comes out as the fade run
// straight through, to the bit: the same memory, the same hits in the same
// order and the same counts. First a device's fade as Noise does it, in
// chunks of 64 cells; then a busy world's, where a place's fade of 2,048
// cells or more goes out in chunks and a smaller one stays in its tick.
void test_fade_spread() {
    NoiseSettings s;
    s.p_floor = s.e_floor = s.cosmic = 0;
    Rig a(s), b(s);
    // Saved states over both devices, every fifth with a frame inside it,
    // sorted by where they start.
    SavedState saved[360];
    u32 n = 0;
    for (u32 i = 0; i < 300; ++i) {
        saved[n++] = SavedState{i * 80, i * 80 + kSavedRegsBytes, i};
        if (i % 5 == 0)
            saved[n++] = SavedState{i * 80 + 12, i * 80 + 12 + kHandlerFrameBytes, 1000 + i};
    }
    ThreadPool pool;
    pool.start(8);
    pool.set_dispatch_reversed(true);
    Chunks k;
    k.noise = &b.noise;
    k.saved = saved;
    k.saved_count = n;
    Array<u64> blocks;
    saved_blocks(saved, n, b.m.size(), &blocks);
    k.blocks = blocks.data();
    k.hits = new Array<u32>[4096];
    Array<u32> hits_a, hits_b;
    bool same = true;
    u64 chunks = 0, most = 0, hits = 0;
    a.states[0].on = a.states[1].on = b.states[0].on = b.states[1].on = false;
    for (u32 t = 0; t < 12; ++t) {
        hits_a.resize(0);
        hits_b.resize(0);
        if (a.noise.fade_check(a.states, 0))
            a.noise.fade(a.states, 0, saved, n, &hits_a);
        if (b.noise.fade_check(b.states, 0)) {
            b.noise.fade_plan(b.states, 0);
            const Array<FadeJob>& jobs = b.noise.fade_jobs();
            k.job.resize(0);
            k.k0.resize(0);
            k.k1.resize(0);
            for (u32 j = 0; j < jobs.size(); ++j)
                for (u64 r = jobs[j].from; r < jobs[j].upto; r += 64) {
                    k.job.push(j);
                    k.k0.push(r);
                    k.k1.push(jobs[j].upto - r > 64 ? r + 64 : jobs[j].upto);
                }
            const u32 count = static_cast<u32>(k.job.size());
            CHECK(count <= 4096);
            k.region.resize(count);
            for (u32 c = 0; c < count; ++c)
                k.hits[c].resize(0);
            pool.run(count, fail_chunk, &k);
            bool region = false;
            for (u32 c = 0; c < count; ++c) {
                region = region || k.region[c];
                for (u32 i = 0; i < k.hits[c].size(); ++i)
                    hits_b.push(k.hits[c][i]);
            }
            if (region)
                b.m.note_region_change();
            chunks += count;
            most = count > most ? count : most;
        }
        same = same && memcmp(a.m.bytes(), b.m.bytes(), a.m.size()) == 0 && hits_a.size() == hits_b.size() &&
               (hits_a.empty() || memcmp(hits_a.data(), hits_b.data(), hits_a.size() * sizeof(u32)) == 0) &&
               a.noise.cells_faded == b.noise.cells_faded && a.noise.faded(0) == b.noise.faded(0) &&
               a.noise.faded(1) == b.noise.faded(1) && a.m.region_writes() == b.m.region_writes();
        hits += hits_a.size();
        ++a.env.tick;
        ++b.env.tick;
    }
    delete[] k.hits;
    CHECK(same);
    CHECK(most > 200 && hits > 1000 && a.m.region_writes() > 0);
    const u64 cells = a.noise.cells_faded;

    // A busy world (test_fade_state's), on 1 host thread with every fade in
    // its tick and on 8 with the big ones shared out and every run reversed.
    const char* busy = "world.places = 6\nhw.cores.max = 4\nsupply.night = 0.2\nsupply.day = 200\nsupply.swing = 0.25\n"
                       "supply.swing_period = 500\nsupply.dropouts = 1000\nsupply.dropout.min = 20\n"
                       "supply.dropout.max = 200\nnoise.p_floor = 1e-4\nnoise.e_floor = 1e-4\nnoise.cosmic = 1e-6\n";
    const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                         encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                         encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
    World w[2];
    Config cfg[2];
    for (u32 r = 0; r < 2; ++r) {
        TextBuf why;
        CHECK(build(w[r], cfg[r], busy, &why));
        for (u32 i = 0; i < w[r].place_count(); ++i)
            memcpy(w[r].place(i).mem.bytes() + w[r].place(i).mem.region_base(), kLax, 14);
    }
    w[0].start_threads(1);
    w[0].set_fade_spread(64, ~0ull);
    w[1].start_threads(8);
    w[1].set_fade_spread(64, 2048);
    w[1].pool().set_dispatch_reversed(true);
    bool world_same = true;
    for (u32 t = 0; t < 2000 && world_same; ++t) {
        w[0].tick();
        w[1].tick();
        world_same = same_places(w[0], w[1]);
    }
    CHECK(world_same);
    CHECK_EQ_U64(w[0].hash(), w[1].hash());
    u64 faded = 0, deaths = 0;
    for (u32 i = 0; i < w[0].place_count(); ++i) {
        faded += w[0].place(i).noise.cells_faded;
        deaths += w[0].place(i).faded_threads;
    }
    CHECK(w[0].spread_fades == 0 && w[1].spread_fades > 0 && w[1].spread_chunks > 10 * w[1].spread_fades);
    CHECK(w[1].spread_cells < faded && deaths > 0); // some fades stayed in their ticks; some threads were hit
    printf("  fade: %llu cells faded in 12 ticks at 0 V, in chunks of 64 on 8 host threads in reverse (%llu chunks, "
           "%llu at most in a tick), the same as straight through, with the same %llu hits; a busy world's %llu "
           "fades of 2,048 cells or more shared out in %llu chunks (%llu of its %llu cells, %llu threads ended by "
           "fading) the same as in their ticks, place by place every tick\n",
           static_cast<unsigned long long>(cells), static_cast<unsigned long long>(chunks),
           static_cast<unsigned long long>(most), static_cast<unsigned long long>(hits),
           static_cast<unsigned long long>(w[1].spread_fades), static_cast<unsigned long long>(w[1].spread_chunks),
           static_cast<unsigned long long>(w[1].spread_cells), static_cast<unsigned long long>(faded),
           static_cast<unsigned long long>(deaths));
}

// Done when (optimization-plan.md pass 11): a fade looks for a failed
// cell's saved states only in the blocks they reach, and finds the same
// hits. The saved states sit at the blocks' edges: one ending at an edge,
// one starting at it, one across it with a frame inside ending at it, and
// one starting a byte before it. Faded looking only in their blocks, the
// hits are those of the fade looking for every cell, in the same order,
// with the states' first and last bytes faded among them; and the block
// just past a state ending at an edge, which no state reaches, is clear.
void test_saved_blocks() {
    NoiseSettings s;
    s.p_floor = s.e_floor = s.cosmic = 0;
    Rig a(s), b(s);
    const u32 block = 1u << kSavedBlockShift;
    const u32 edges = a.m.size() / block;
    SavedState saved[200];
    u32 n = 0;
    for (u32 k = 1; k < edges; ++k) {
        const u32 e = k * block;
        if (k % 4 == 0) {
            saved[n++] = SavedState{e - kSavedRegsBytes, e, k}; // ends at the edge
        } else if (k % 4 == 1) {
            saved[n++] = SavedState{e, e + kSavedRegsBytes, k}; // starts at it
        } else if (k % 4 == 2) {
            saved[n++] = SavedState{e - kHandlerFrameBytes, e, 1000 + k}; // a frame ending at it
            saved[n++] = SavedState{e - 12, e + 12, k};                    // and across it
        } else {
            saved[n++] = SavedState{e - 1, e - 1 + kHandlerFrameBytes, k}; // a byte before it
        }
    }
    Array<u64> blocks;
    saved_blocks(saved, n, b.m.size(), &blocks);
    bool marked = true;
    for (u32 i = 0; i < n; ++i)
        for (u32 k = saved[i].lo / block; k <= (saved[i].hi - 1) / block; ++k)
            marked = marked && ((blocks[k >> 6] >> (k & 63)) & 1) != 0;
    u32 clear = 0, past = 0;
    for (u32 k = 4; k < edges; k += 4, ++past)
        clear += ((blocks[k >> 6] >> (k & 63)) & 1) == 0 ? 1 : 0;
    CHECK(marked && past > 0 && clear == past);

    Array<u32> hits_a, hits_b;
    bool same = true;
    u64 hits = 0;
    a.states[0].on = a.states[1].on = b.states[0].on = b.states[1].on = false;
    for (u32 t = 0; t < 12; ++t) {
        hits_a.resize(0);
        hits_b.resize(0);
        if (a.noise.fade_check(a.states, 0))
            a.noise.fade(a.states, 0, saved, n, &hits_a);
        if (b.noise.fade_check(b.states, 0)) {
            b.noise.fade_plan(b.states, 0);
            const Array<FadeJob>& jobs = b.noise.fade_jobs();
            bool region = false;
            for (u32 j = 0; j < jobs.size(); ++j)
                if (b.noise.fade_cells(jobs[j], jobs[j].from, jobs[j].upto, saved, n, blocks.data(), &hits_b, false))
                    region = true;
            if (region)
                b.m.note_region_change();
        }
        same = same && memcmp(a.m.bytes(), b.m.bytes(), a.m.size()) == 0 && hits_a.size() == hits_b.size() &&
               (hits_a.empty() || memcmp(hits_a.data(), hits_b.data(), hits_a.size() * sizeof(u32)) == 0) &&
               a.noise.cells_faded == b.noise.cells_faded && a.m.region_writes() == b.m.region_writes();
        hits += hits_a.size();
        ++a.env.tick;
        ++b.env.tick;
    }
    // The states' first and last bytes that lost a bit.
    u8* faded = static_cast<u8*>(calloc(a.m.size(), 1));
    for (u32 d = 0; d < a.m.device_count(); ++d)
        for (u64 r = 0; r < a.noise.faded(d); ++r)
            faded[a.m.device(d).base + static_cast<u32>(a.noise.cell_of(d, r) >> 3)] = 1;
    u32 ends = 0;
    for (u32 i = 0; i < n; ++i)
        ends += faded[saved[i].lo] + faded[saved[i].hi - 1];
    free(faded);
    CHECK(same);
    CHECK(hits > 100 && ends > n / 2);
    printf("  fade: %u saved states at the edges of %u-byte blocks; looking only in the blocks they reach, the "
           "same %llu hits as looking for every cell, %u of their first and last bytes faded among them; the %u "
           "blocks past a state ending at an edge left clear\n",
           n, block, static_cast<unsigned long long>(hits), ends, clear);
}

// Done when (optimization-plan.md pass 11): a fade's cells are cell_of's,
// though a batch's walks to them go a step at a time together. Device 0's
// 131,072 cells are half a Feistel domain of 262,144 values, so about half
// the walks take more than one step. Faded from a pattern, each tick's
// memory is the pattern with bit (k - from) % 64 of draw (k - from) / 64
// of the tick's stream written at cell_of(k), for every rank k of its jobs.
void test_fade_walks() {
    NoiseSettings s;
    s.p_floor = s.e_floor = s.cosmic = 0;
    Rig g(s);
    for (u32 i = 0; i < g.m.size(); ++i)
        g.m.bytes()[i] = static_cast<u8>(i * 37 + 11);
    u8* want = static_cast<u8*>(malloc(g.m.size()));
    Array<u32> hits;
    bool same = true;
    u64 ranks = 0, walked = 0;
    g.states[0].on = g.states[1].on = false;
    for (u32 t = 0; t < 12; ++t) {
        if (g.noise.fade_check(g.states, 0)) {
            g.noise.fade_plan(g.states, 0);
            memcpy(want, g.m.bytes(), g.m.size());
            const Array<FadeJob>& jobs = g.noise.fade_jobs();
            for (u32 j = 0; j < jobs.size(); ++j) {
                const FadeJob& job = jobs[j];
                const Rng r = Rng::from_base(job.base);
                const u32 at = g.m.device(job.device).base;
                for (u64 k = job.from; k < job.upto; ++k) {
                    const u64 c = g.noise.cell_of(job.device, k);
                    const u8 m = static_cast<u8>(1u << (c & 7));
                    u8& b = want[at + static_cast<u32>(c >> 3)];
                    b = (r.at((k - job.from) / 64) >> ((k - job.from) % 64)) & 1 ? static_cast<u8>(b | m)
                                                                                 : static_cast<u8>(b & ~m);
                }
                ranks += job.upto - job.from;
                if (job.device == 0)
                    walked += job.upto - job.from;
                g.noise.fade_cells(job, job.from, job.upto, nullptr, 0, nullptr, &hits, false);
            }
            same = same && memcmp(want, g.m.bytes(), g.m.size()) == 0;
        }
        ++g.env.tick;
    }
    free(want);
    CHECK(same);
    CHECK(walked > 10000 && ranks > walked);
    printf("  fade: %llu ranks faded a batch at a time, %llu of them walking a Feistel domain twice their "
           "device's cells, each written at cell_of's cell with its draw's bit\n",
           static_cast<unsigned long long>(ranks), static_cast<unsigned long long>(walked));
}

// Setup refuses an edge band with too many doublings in it, and retention
// slowest below fastest.
void test_fade_checks() {
    World a, b, c;
    Config ca, cb, cc;
    TextBuf why;
    CHECK(!build(a, ca, "noise.edge_band = 1000\nnoise.edge_doubling = 10\n", &why) && !ca.ok());
    CHECK(!build(b, cb, "memory.fade.fastest = 100\nmemory.fade.slowest = 10\n", &why) && !cb.ok());
    CHECK(build(c, cc, "noise.edge_band = 0\n", &why) && cc.ok());
}

} // namespace

void suite_fade() {
    test_edge_rates();
    test_memory_edge();
    test_fade_share();
    test_fade_cells();
    test_saved_state();
    test_fade_state();
    test_fade_spread();
    test_saved_blocks();
    test_fade_walks();
    test_fade_checks();
}
