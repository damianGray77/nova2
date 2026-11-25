// The widened saved-state rule (os.saved_state_fails, on by default since
// gate 2 passed with it; build-plan-gate2.md 11 step 10): a thread ends,
// cause "memory", when any bit of its saved state fails by the physics, a
// cosmic-ray flip or an edge error as well as a fade. An overwrite still
// only corrupts it.
//
// - Memory's two paths report every byte they fail: a flip always changes
//   its bit, an edge error's bit reads back random.
// - The rule, address by address: a sleeper's saved registers and a
//   handler's frame end their threads; the bytes just outside them don't;
//   and a thread on its core keeps its registers there, so their stale copy
//   in its stack doesn't count.
// - Real flips: each sleeper whose saved state took a flip in a tick ended
//   in it, and every other lived. With the rule off the same world loses no
//   one, as before.
// - A thread alone on its core lives through flips in its saved registers.
// - An overwrite of a sleeper's saved registers leaves it alive, and it
//   reads them back when it next runs.
// - The state through threads and snapshots, with the rule on.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/noise.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

// ---------------------------------------------------------------------------
// Memory's paths, without a place (as test_fade.cpp's rig)
// ---------------------------------------------------------------------------

struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Noise noise;
    DeviceState states[2];

    explicit Rig(NoiseSettings s) {
        // Fast memory at 700 mV and slow at 600.
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
    }
};

bool listed(const Array<u32>& a, u32 addr) {
    for (usize i = 0; i < a.size(); ++i)
        if (a[i] == addr)
            return true;
    return false;
}

// The cosmic-ray floor lists the byte of every flip, and every byte it
// changed is listed; memory's edge errors list every bit they fail, and
// every byte they changed is listed, about half of them unchanged.
void test_fails_listed() {
    NoiseSettings s;
    s.p_floor = s.e_floor = 0;
    s.cosmic = 1e-4;
    Rig g(s);
    u8* before = new u8[g.m.size()];
    Array<u32> failed;
    u64 flips = 0, unlisted = 0, unchanged = 0;
    for (u32 t = 0; t < 50; ++t) {
        memcpy(before, g.m.bytes(), g.m.size());
        failed.resize(0);
        const u64 c0 = g.noise.cosmic;
        g.noise.cosmic_flips(&failed);
        CHECK(failed.size() == g.noise.cosmic - c0);
        for (usize i = 0; i < failed.size(); ++i)
            unchanged += before[failed[i]] == g.m.bytes()[failed[i]] ? 1 : 0;
        for (u32 a = 0; a < g.m.size(); ++a)
            if (before[a] != g.m.bytes()[a] && !listed(failed, a))
                ++unlisted;
        flips += failed.size();
        ++g.env.tick;
    }
    CHECK(flips > 500 && unlisted == 0 && unchanged == 0);
    // At device 0's threshold its bits fail to hold at the floor × 31.
    u64 failures = 0, edge_unlisted = 0, edge_unchanged = 0;
    for (u32 t = 0; t < 50; ++t) {
        memcpy(before, g.m.bytes(), g.m.size());
        failed.resize(0);
        const u64 e0 = g.noise.memory_edge;
        g.noise.memory_errors(g.states, 700, &failed);
        CHECK(failed.size() == g.noise.memory_edge - e0);
        for (usize i = 0; i < failed.size(); ++i) {
            CHECK(failed[i] >= g.m.device(0).base && failed[i] < g.m.device(0).base + g.m.device(0).size);
            edge_unchanged += before[failed[i]] == g.m.bytes()[failed[i]] ? 1 : 0;
        }
        for (u32 a = 0; a < g.m.size(); ++a)
            if (before[a] != g.m.bytes()[a] && !listed(failed, a))
                ++edge_unlisted;
        failures += failed.size();
        ++g.env.tick;
    }
    delete[] before;
    CHECK(failures > 1000 && edge_unlisted == 0);
    CHECK(edge_unchanged > failures / 3 && edge_unchanged < failures * 2 / 3);
    // Without a list, nothing is listed (and nothing breaks).
    g.noise.cosmic_flips();
    g.noise.memory_errors(g.states, 700);
    printf("  fails: the floor listed all %llu of its flips' bytes, each changed; edge errors listed all %llu of "
           "theirs, %llu left unchanged; no changed byte went unlisted\n",
           static_cast<unsigned long long>(flips), static_cast<unsigned long long>(failures),
           static_cast<unsigned long long>(edge_unchanged));
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
// Sets a timer handler with a period of a tick, waits for an interrupt, and
// in the handler sleeps 120 ticks at a time for ever (test_fade.cpp's).
const u8 kHandlerSleeper[23] = {encode(kMov, kB),   encode(kAdd, kImm), 17,     encode(kSto, kB),
                                encode(kMov, kImm), 1,                  encode(kSto, kC),
                                encode(kMov, kImm), kOnTimer,           encode(kSys, kImm),
                                kSetHandler,        encode(kMov, kImm), 0,      encode(kSys, kImm),
                                kSleep,             encode(kJmp, kImm), static_cast<u8>(-4),
                                encode(kMov, kImm), 120,                encode(kSys, kImm),
                                kSleep,             encode(kJmp, kImm), static_cast<u8>(-4)};
// Counts in a loop, never leaving its core while it's alone.
const u8 kSpinner[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};

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
u32 place(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 64, 2, 0);
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

void run_to(Place& p, u64 tick) {
    while (p.env.tick < tick) {
        p.os.clear_facts();
        p.tick();
    }
}

bool alive(const Place& p, u32 t, u64 uid) {
    const Thread& th = p.os.thread(t);
    return th.state != ThreadState::free && p.os.program(th.program).alive && p.os.program(th.program).uid == uid;
}

bool died_of_memory(const Place& p, u64 uid) {
    for (usize k = 0; k < p.os.deaths.size(); ++k)
        if (p.os.deaths[k].uid == uid && p.os.deaths[k].cause == DeathCause::memory_lost)
            return true;
    return false;
}

// The rule address by address, through the place's own call: a sleeper's
// saved registers (24 bytes at its stack's base) and a handler's frame (20
// bytes where it went) end their threads; the bytes just outside them, and
// a running thread's registers, don't.
void test_fails_exact() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "os.saved_state_fails = true\n", &why));
    Place& p = w.place(0);
    const u32 s = place(p, kSleeper, sizeof kSleeper);
    const u32 h1 = place(p, kHandlerSleeper, sizeof kHandlerSleeper);
    const u32 h2 = place(p, kHandlerSleeper, sizeof kHandlerSleeper);
    const u32 x = place(p, kSpinner, sizeof kSpinner);
    run_to(p, 20);
    const Thread& ts = p.os.thread(s);
    const Thread& t1 = p.os.thread(h1);
    const Thread& t2 = p.os.thread(h2);
    const Thread& tx = p.os.thread(x);
    CHECK(ts.state == ThreadState::sleeping && !ts.core && !ts.in_handler);
    CHECK(t1.in_handler && !t1.core && t2.in_handler && !t2.core);
    CHECK(tx.core != 0 && !tx.in_handler); // the spinner, alone on its core
    const u64 us = p.os.program(ts.program).uid, u1 = p.os.program(t1.program).uid;
    const u64 u2 = p.os.program(t2.program).uid, ux = p.os.program(tx.program).uid;
    CHECK(t1.frame >= t1.stack + kSavedRegsBytes); // the frame sits apart from the saved registers
    // Outside every saved state: just past the sleeper's registers and just
    // before them, between a handler sleeper's registers and its frame, and
    // the spinner's registers, which are on its core.
    const u32 outside[] = {ts.stack + kSavedRegsBytes, ts.stack - 1, t1.stack + kSavedRegsBytes, t1.frame - 1,
                           tx.stack, tx.stack + kSavedRegsBytes - 1};
    p.os.clear_facts();
    p.end_failed_states(outside, sizeof outside / sizeof outside[0]);
    CHECK(p.os.deaths.size() == 0 && p.failed_threads == 0);
    CHECK(alive(p, s, us) && alive(p, h1, u1) && alive(p, h2, u2) && alive(p, x, ux));
    // Inside: the sleeper's last register byte, h1's frame's last byte, and
    // h2's first register byte.
    const u32 inside[] = {ts.stack + kSavedRegsBytes - 1, t1.frame + kHandlerFrameBytes - 1, t2.stack};
    p.end_failed_states(inside, 3);
    CHECK(p.failed_threads == 3 && p.os.deaths.size() == 3);
    CHECK(!alive(p, s, us) && !alive(p, h1, u1) && !alive(p, h2, u2) && alive(p, x, ux));
    CHECK(died_of_memory(p, us) && died_of_memory(p, u1) && died_of_memory(p, u2));
    printf("  fails: a sleeper's saved registers and a handler's frame ended their threads, cause memory; the "
           "bytes just outside them and a running thread's registers didn't\n");
}

// What test_fails_blocks's place came to (fails_twin).
struct FailsTwin {
    u32 bytes = 0, ended = 0, states = 0;
    u32 frames_on = 0; // handlers' frames starting a block or more past their stack's base
};
FailsTwin fails_twin(const char* settings, bool frames_only, u32 lists_pay = 0);

// Done when (optimization-plan.md pass 12): with many threads and more than
// a few failed bytes, a thread's states are held against the bytes only
// when one starts in a block their marks reach (place.h), and the same
// threads end.
// - The marks, for states at the edges of the blocks: ending at one,
//   starting at one, across one, a frame inside a state across one, a
//   frame ending at one, and starting a byte before one. Each is held
//   against a failed byte at every address of the memory in turn: one
//   holding the byte always starts in a marked block, and one a block or
//   more from it never does. Then against the first and last bytes of
//   every block at once. With every byte failed, kNowhere is still in no
//   marked block, and unmarking leaves every word clear.
// - In a place (fails_twin), with 64-byte stacks and, so that a handler's
//   frame can start blocks past its stack's base (pass 13, the OS's lists
//   by block), 100- and 200-byte ones. Through the lists always (a place's
//   lists_pay 0), and with 200-byte stacks through the walk of every
//   thread too, which so few threads for the bytes take by default.
void test_fails_blocks() {
    const u32 bytes = 4096, block = 1u << kFailedBlockShift;
    struct State {
        u32 lo, len;
    };
    State st[400];
    u32 n = 0;
    for (u32 e = block; e < bytes; e += block) {
        st[n++] = State{e - kSavedRegsBytes, kSavedRegsBytes};       // ends at the edge
        st[n++] = State{e, kSavedRegsBytes};                         // starts at it
        st[n++] = State{e - 12, kSavedRegsBytes};                    // across it
        st[n++] = State{e - 8, kHandlerFrameBytes};                  // a frame inside that, across it too
        st[n++] = State{e - kHandlerFrameBytes, kHandlerFrameBytes}; // a frame ending at it
        st[n++] = State{e - 1, kSavedRegsBytes};                     // a byte before it
    }
    Array<u64> bits;
    bits.resize(failed_mark_words(bytes));
    const u32 past = failed_block_past(bytes);
    u64 held = 0, missed = 0, far = 0, far_marked = 0;
    for (u32 a = 0; a < bytes; ++a) {
        failed_marks(&a, 1, bits.data());
        for (u32 i = 0; i < n; ++i) {
            const bool marked = failed_marked(bits.data(), past, st[i].lo);
            if (a - st[i].lo < st[i].len) {
                ++held;
                missed += marked ? 0 : 1;
            }
            if (st[i].lo >= a + block || st[i].lo + block + kSavedRegsBytes - 1 <= a) {
                ++far;
                far_marked += marked ? 1 : 0;
            }
        }
        failed_unmark(&a, 1, bits.data());
    }
    Array<u32> edges, every;
    for (u32 e = 0; e < bytes; e += block) {
        edges.push(e);
        edges.push(e + block - 1);
    }
    for (u32 a = 0; a < bytes; ++a)
        every.push(a);
    failed_marks(edges.data(), static_cast<u32>(edges.size()), bits.data());
    u32 at_edges = 0, edge_missed = 0;
    for (u32 i = 0; i < n; ++i) {
        bool holds = false;
        for (u32 k = 0; k < edges.size(); ++k)
            holds = holds || edges[k] - st[i].lo < st[i].len;
        at_edges += holds ? 1 : 0;
        edge_missed += holds && !failed_marked(bits.data(), past, st[i].lo) ? 1 : 0;
    }
    failed_unmark(edges.data(), static_cast<u32>(edges.size()), bits.data());
    failed_marks(every.data(), static_cast<u32>(every.size()), bits.data());
    const bool nowhere = failed_marked(bits.data(), past, kNowhere);
    failed_unmark(every.data(), static_cast<u32>(every.size()), bits.data());
    bool clear = true;
    for (u32 k = 0; k < bits.size(); ++k)
        clear = clear && bits[k] == 0;
    CHECK(n == 378 && held == 63ull * (4 * kSavedRegsBytes + 2 * kHandlerFrameBytes));
    CHECK(missed == 0 && far_marked == 0 && far > held);
    CHECK(at_edges == n && edge_missed == 0 && !nowhere && clear);
    const FailsTwin s64 = fails_twin("", false), s100 = fails_twin("os.stack = 100\n", false),
                    s200 = fails_twin("os.stack = 200\n", false), f100 = fails_twin("os.stack = 100\n", true),
                    f200 = fails_twin("os.stack = 200\n", true),
                    walked = fails_twin("os.stack = 200\n", false, 12);
    CHECK(s64.frames_on == 0 && s100.frames_on > 40 && s200.frames_on > 40);
    CHECK(walked.ended == s200.ended && walked.states == s200.states && walked.bytes == s200.bytes);
    printf("  fails: %u states at the edges of %u-byte blocks, each against a failed byte at all %u addresses, "
           "and every block's first and last bytes: each holding a failed byte starts in a marked block, none a "
           "block away; in a place, %u failed bytes ended the same %u of 96 threads (%u states) in the same "
           "order as looking at every thread; with 100- and 200-byte stacks, %u and %u (%u and %u frames a "
           "block or more past their stack's base), and with bytes failed by the frames alone %u and %u, and through "
           "the walk %u\n",
           n, block, bytes, s64.bytes, s64.ended, s64.states, s100.ended, s200.ended, s100.frames_on,
           s200.frames_on, f100.ended, f200.ended, walked.ended);
}

// test_fails_blocks's place, with these settings beside the rule's: 48
// sleepers and 48 sleeping in a handler, the bytes just outside their
// states failed, and for some the first or last byte of a state or of its
// block. Through end_failed_marked, the same threads end, in the same
// order, as through end_failed_among over every thread, and the worlds'
// hashes match. Then a byte past memory's end, which the marks leave to the
// plain way.
FailsTwin fails_twin(const char* settings, bool frames_only, u32 lists_pay) {
    const u32 block = 1u << kFailedBlockShift;
    char extra[200];
    snprintf(extra, sizeof extra, "os.saved_state_fails = true\nmemory.size = 262144\n%s", settings);
    FailsTwin out;
    World w[2];
    Config cfg[2];
    TextBuf why;
    const u32 kEach = 48;
    u32 t[2 * kEach];
    for (u32 k = 0; k < 2; ++k) {
        CHECK(build(w[k], cfg[k], extra, &why));
        Place& p = w[k].place(0);
        for (u32 i = 0; i < kEach; ++i) {
            t[i] = place(p, kSleeper, sizeof kSleeper);
            t[kEach + i] = place(p, kHandlerSleeper, sizeof kHandlerSleeper);
        }
        run_to(p, 20);
    }
    Place& p0 = w[0].place(0);
    Place& p1 = w[1].place(0);
    Array<u32> addrs;
    u32 states = 0;
    for (u32 i = 0; i < 2 * kEach; ++i) {
        const Thread& th = p0.os.thread(t[i]);
        CHECK(th.state == ThreadState::sleeping && !th.core && (i < kEach) != th.in_handler);
        const u32 s = th.stack, f = th.frame;
        states += th.in_handler ? 2 : 1;
        if (!frames_only) {
            addrs.push(s - 1);
            addrs.push(s + kSavedRegsBytes);
            if (i % 5 == 0)
                addrs.push(s);
            if (i % 5 == 1)
                addrs.push(s + kSavedRegsBytes - 1);
            if (i % 7 == 3)
                addrs.push(s & ~(block - 1));
            if (i % 7 == 5)
                addrs.push(s | (block - 1));
        }
        if (!th.in_handler)
            continue;
        if (frames_only && i % 3 == 0)
            addrs.push(f);
        out.frames_on += f >> kFailedBlockShift != s >> kFailedBlockShift;
        addrs.push(f - 1);
        addrs.push(f + kHandlerFrameBytes);
        if (i % 5 == 2)
            addrs.push(f + kHandlerFrameBytes - 1);
        if (i % 7 == 6)
            addrs.push((f + kHandlerFrameBytes - 1) | (block - 1));
    }
    p0.lists_pay = lists_pay;
    p0.os.clear_facts();
    p1.os.clear_facts();
    const bool marked = p0.end_failed_marked(addrs.data(), static_cast<u32>(addrs.size()));
    p1.end_failed_among(addrs.data(), static_cast<u32>(addrs.size()), 0, p1.os.thread_top());
    bool same = marked && p0.failed_threads == p1.failed_threads && p0.os.deaths.size() == p1.os.deaths.size();
    for (u32 k = 0; same && k < p0.os.deaths.size(); ++k)
        same = p0.os.deaths[k].uid == p1.os.deaths[k].uid && p0.os.deaths[k].cause == p1.os.deaths[k].cause;
    const u64 ended = p0.failed_threads;
    // A byte past memory's end among them: the plain way, as ever.
    addrs.push(p0.mem.size());
    same = same && !p0.end_failed_marked(addrs.data(), static_cast<u32>(addrs.size()));
    p0.end_failed_states(addrs.data(), static_cast<u32>(addrs.size()));
    p1.end_failed_among(addrs.data(), static_cast<u32>(addrs.size()), 0, p1.os.thread_top());
    same = same && p0.failed_threads == ended && p1.failed_threads == ended;
    CHECK(same && addrs.size() > (frames_only ? 100u : 300u) && ended > (frames_only ? 10u : 20u) &&
          ended + 20 < 2 * kEach);
    CHECK_EQ_U64(w[0].hash(), w[1].hash());
    TextBuf broken;
    CHECK(p0.check(&broken) && p1.check(&broken));
    out.bytes = static_cast<u32>(addrs.size() - 1);
    out.ended = static_cast<u32>(ended);
    out.states = states;
    return out;
}

// Real flips, one tick at a time, with the rule on and off. 24 sleepers and
// 24 that sleep in a handler, all asleep between ticks 8 and 110, so only
// the floor touches their stacks (steady supply: no edge errors, no fading).
// With the rule on, each whose saved state changed in a tick ended in it,
// and each whose didn't lived; with it off, none ended.
struct FlipSeen {
    u32 hit = 0, died = 0, by_frame = 0, survived = 0, wrong = 0, left_out = 0;
};

FlipSeen flip_run(bool fails) {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, fails ? "noise.cosmic = 5e-5\n" : "noise.cosmic = 5e-5\nos.saved_state_fails = false\n", &why));
    Place& p = w.place(0);
    const u32 kEach = 24;
    u32 t[2 * kEach];
    u64 uid[2 * kEach];
    bool gone[2 * kEach] = {};
    for (u32 i = 0; i < kEach; ++i) {
        t[i] = place(p, kSleeper, sizeof kSleeper);
        t[kEach + i] = place(p, kHandlerSleeper, sizeof kHandlerSleeper);
    }
    for (u32 i = 0; i < 2 * kEach; ++i)
        uid[i] = p.os.program(p.os.thread(t[i]).program).uid;
    run_to(p, 8);
    FlipSeen seen;
    // The floor runs from the first tick, so by tick 8 a few have ended (or
    // run code a flip changed, and not gone to sleep as they should): those
    // are left out.
    for (u32 i = 0; i < 2 * kEach; ++i) {
        const Thread& th = p.os.thread(t[i]);
        if (!alive(p, t[i], uid[i]) || th.state != ThreadState::sleeping || th.core || (i >= kEach && !th.in_handler)) {
            gone[i] = true;
            ++seen.left_out;
        }
    }
    const u64 failed_then = p.failed_threads;
    u8* before = new u8[p.mem.size()];
    for (u32 tick = 8; tick < 110; ++tick) {
        memcpy(before, p.mem.bytes(), p.mem.size());
        u64 wake[2 * kEach];
        u32 regs[2 * kEach], frame[2 * kEach];
        for (u32 i = 0; i < 2 * kEach; ++i) {
            if (gone[i])
                continue;
            const Thread& th = p.os.thread(t[i]);
            CHECK(th.state == ThreadState::sleeping && !th.core);
            CHECK(i < kEach || th.in_handler);
            wake[i] = th.wake_tick;
            regs[i] = th.stack;
            frame[i] = th.in_handler ? th.frame : 0;
        }
        p.os.clear_facts();
        p.tick();
        for (u32 i = 0; i < 2 * kEach; ++i) {
            if (gone[i])
                continue;
            bool in_regs = false, in_frame = false;
            for (u32 b = 0; b < kSavedRegsBytes; ++b)
                in_regs = in_regs || before[regs[i] + b] != p.mem.bytes()[regs[i] + b];
            for (u32 b = 0; frame[i] && b < kHandlerFrameBytes; ++b)
                in_frame = in_frame || before[frame[i] + b] != p.mem.bytes()[frame[i] + b];
            const bool lives = alive(p, t[i], uid[i]);
            if (lives)
                CHECK(p.os.thread(t[i]).wake_tick == wake[i]); // it didn't run
            if (in_regs || in_frame)
                ++seen.hit;
            if (!lives) {
                gone[i] = true;
                ++seen.died;
                seen.by_frame += in_frame && !in_regs ? 1 : 0;
                if (!died_of_memory(p, uid[i]))
                    ++seen.wrong;
            }
            // The rule on: it ended exactly when its saved state took a flip.
            // Off: nothing ends it.
            if (fails ? (in_regs || in_frame) != !lives : !lives)
                ++seen.wrong;
        }
    }
    delete[] before;
    seen.survived = 2 * kEach - seen.left_out - seen.died;
    CHECK_EQ_U64(p.failed_threads - failed_then, fails ? seen.died : 0);
    return seen;
}

void test_fails_flips() {
    const FlipSeen on = flip_run(true);
    CHECK(on.wrong == 0 && on.died > 0 && on.survived > 0 && on.by_frame > 0 && on.hit == on.died);
    CHECK(on.left_out < 12);
    const FlipSeen off = flip_run(false);
    CHECK(off.wrong == 0 && off.died == 0 && off.hit > 0);
    printf("  fails: in 102 ticks of flips, each of %u sleepers whose saved state took one ended in that tick, cause "
           "memory (%u through a handler's frame alone), and the %u whose didn't lived (%u left out, ended or "
           "changed before); with the rule off, %u flips in saved states ended no one\n",
           on.died, on.by_frame, on.survived, on.left_out, off.hit);
}

// A thread alone on its core keeps its registers there, so flips in their
// stale copy at its stack's base don't end it.
void test_fails_alone() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "noise.cosmic = 5e-4\nos.saved_state_fails = true\n", &why));
    Place& p = w.place(0);
    const u32 x = place(p, kSpinner, sizeof kSpinner);
    const u64 ux = p.os.program(p.os.thread(x).program).uid;
    run_to(p, 2);
    u32 hits = 0, ticks = 0;
    while (ticks < 2000 && alive(p, x, ux)) {
        const u32 stack = p.os.thread(x).stack;
        CHECK(p.os.thread(x).core != 0); // alone: never taken off
        p.os.clear_facts();
        p.tick();
        ++ticks;
        bool in_regs = false;
        for (usize i = 0; i < p.failed_bytes().size(); ++i)
            in_regs = in_regs || p.failed_bytes()[i] - stack < kSavedRegsBytes;
        if (!alive(p, x, ux)) {
            CHECK(!died_of_memory(p, ux)); // a flip in its code, say, but never its stale registers
            break;
        }
        hits += in_regs ? 1 : 0;
    }
    CHECK(hits > 0);
    printf("  fails: a thread alone on its core lived through %u ticks with a flip in its saved registers' stale "
           "copy (%u ticks in all)\n",
           hits, ticks);
}

// An overwrite only corrupts: a sleeper whose saved B is overwritten lives,
// and after it wakes and sleeps again its saved B is still the new value,
// read back when it ran.
void test_fails_overwrite() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "os.saved_state_fails = true\n", &why));
    Place& p = w.place(0);
    const u32 s = place(p, kSleeper, sizeof kSleeper);
    const u64 us = p.os.program(p.os.thread(s).program).uid;
    run_to(p, 20);
    const Thread& th = p.os.thread(s);
    CHECK(th.state == ThreadState::sleeping && !th.core);
    const u64 woke = th.wake_tick;
    const u32 kMark = 0x5a5aa5a5u;
    u8* b = p.mem.bytes() + th.stack + 4; // the saved B (A.3: A, B, C, D, IP, SP)
    for (u32 k = 0; k < 4; ++k)
        b[k] = static_cast<u8>(kMark >> (8 * k));
    run_to(p, woke + 5); // past its wake: it ran, and slept again
    CHECK(alive(p, s, us) && p.failed_threads == 0);
    CHECK(p.os.thread(s).wake_tick > woke && !p.os.thread(s).core);
    const u8* nb = p.mem.bytes() + p.os.thread(s).stack + 4;
    const u32 got = static_cast<u32>(nb[0]) | static_cast<u32>(nb[1]) << 8 | static_cast<u32>(nb[2]) << 16 |
                    static_cast<u32>(nb[3]) << 24;
    CHECK(got == kMark);
    printf("  fails: an overwritten sleeper lived, and ran with what was written (its saved B 0x%08x after it "
           "woke and slept again)\n",
           got);
}

// The state, with the rule on: a busy world under a fast supply with
// drop-outs is the same on 1 and 3 threads, and resumed from a snapshot
// mid-run the same as never stopped; and the rule changes its history.
void test_fails_state() {
    const char* busy = "world.places = 6\nhw.cores.max = 4\nsupply.night = 0.2\nsupply.day = 200\nsupply.swing = 0.25\n"
                       "supply.swing_period = 500\nsupply.dropouts = 1000\nsupply.dropout.min = 20\n"
                       "supply.dropout.max = 200\nnoise.p_floor = 1e-4\nnoise.e_floor = 1e-4\nnoise.cosmic = 1e-5\n";
    const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                         encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                         encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
    TextBuf off;
    off.append(busy);
    off.append("os.saved_state_fails = false\n");
    u64 h[3] = {};
    u64 failed = 0, faded = 0, edge = 0;
    const u64 kSnapAt = 1200;
    ByteWriter out;
    for (u32 run = 0; run < 3; ++run) {
        World w;
        Config cfg;
        TextBuf why;
        const bool built = build(w, cfg, run < 2 ? busy : off.c_str(), &why);
        if (!built)
            printf("  fails: busy world: %s %s\n", why.c_str(), cfg.value_error());
        CHECK(built);
        for (u32 i = 0; i < w.place_count(); ++i)
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
        w.start_threads(run == 1 ? 3 : 1);
        for (u32 t = 0; t < 2000; ++t) {
            w.tick();
            if (run == 0 && w.tick_count() == kSnapAt) {
                TextBuf text;
                cfg.write(text);
                snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
                w.save(out);
                snapshot_finish(out);
            }
        }
        h[run] = w.hash();
        if (run == 0)
            for (u32 i = 0; i < w.place_count(); ++i) {
                failed += w.place(i).failed_threads;
                faded += w.place(i).faded_threads;
                edge += w.place(i).noise.memory_edge;
            }
    }
    CHECK_EQ_U64(h[0], h[1]);
    CHECK(h[2] != h[0]); // the rule off: another history
    CHECK(failed > 0 && edge > 0);
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
    CHECK(b.place(0).os.settings().saved_state_fails);
    ByteReader in(out.data() + sh.world_offset, sh.world_size);
    CHECK(b.load(in));
    b.start_threads(2);
    while (b.tick_count() < 2000)
        b.tick();
    CHECK_EQ_U64(b.hash(), h[0]);
    printf("  fails: a busy world with the rule on (%llu threads ended by a flip or an edge error, %llu by fading; "
           "%llu bits failed near an edge) is the same on 1 and 3 threads, and resumed from tick %llu; off, it "
           "goes another way\n",
           static_cast<unsigned long long>(failed), static_cast<unsigned long long>(faded),
           static_cast<unsigned long long>(edge), static_cast<unsigned long long>(kSnapAt));
}

// The switch is on by default, and the key reads.
void test_fails_key() {
    World a, b;
    Config ca, cb;
    TextBuf why;
    CHECK(build(a, ca, "", &why) && a.place(0).os.settings().saved_state_fails);
    CHECK(build(b, cb, "os.saved_state_fails = false\n", &why) && !b.place(0).os.settings().saved_state_fails);
}

} // namespace

void suite_fails() {
    test_fails_listed();
    test_fails_key();
    test_fails_exact();
    test_fails_blocks();
    test_fails_flips();
    test_fails_alone();
    test_fails_overwrite();
    test_fails_state();
}
