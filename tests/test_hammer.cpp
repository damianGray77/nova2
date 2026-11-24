// Rowhammer (nova2/noise.h, memory.h; design.md 9.8; build-plan-gate5.md
// step 8): the law to the bit, with counts set by hand: nothing at or under
// a device's threshold, a bit either side for each activation past it at a
// strength of 1 and the strength's share otherwise, no flips across a
// device's edge, into a device that is off or has failed, or for
// activations already counted; the counts starting again at a window's end
// and for a replaced device; a hand-placed thread alternating between two
// pages flipping bits in the pages beside them, and one reading a single
// page flipping nothing; a strength of 0 off; a flip in a sleeper's saved
// state ending its thread; a tick of half the length leaving the flips in a
// span of simulated time the same; and worlds on one host thread and three
// and through a snapshot taken part way through a window.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h> // __popcnt

#include "check.h"
#include "nova2/noise.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

using ull = unsigned long long;

// ---------------------------------------------------------------------------
// The law, without a place (as test_fails.cpp's rig)
// ---------------------------------------------------------------------------

constexpr u32 kPage = 256;

struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Noise noise;
    DeviceState states[2];
    Array<u32> failed;
    u8 before[24576];

    // Two devices: pages 0 to 63 and 64 to 95.
    Rig(double strength, u64 window, u32 fast, u32 slow) {
        static const MemDeviceSpec devs[] = {{16384, 64, 700}, {8192, 64, 600}};
        MemorySpec ms;
        ms.page_size = kPage;
        ms.region_size = 1024;
        ms.devices = devs;
        ms.device_count = 2;
        CHECK(m.init(ms, nullptr));
        m.enable_counters(true);
        cores[0].kind = DeviceKind::p_core;
        cores[1].kind = DeviceKind::e_core;
        env.place_id = 3;
        NoiseSettings s;
        s.p_floor = s.e_floor = 0;
        s.cosmic = 0;
        CHECK(noise.init(&m, cores, 2, &env, 7, s, PowerSettings{}, nullptr));
        HammerSettings hs;
        hs.strength = strength;
        hs.window = window;
        const u32 at[2] = {fast, slow};
        noise.init_hammer(hs, at);
    }

    // A tick's end. The bytes as they were are kept, to count what changed.
    void step() {
        memcpy(before, m.bytes(), sizeof before);
        failed.resize(0);
        noise.hammer_flips(states, &failed);
        ++env.tick;
    }
    u32 listed(u32 page) const {
        u32 n = 0;
        for (usize i = 0; i < failed.size(); ++i)
            n += failed[i] / kPage == page ? 1 : 0;
        return n;
    }
    u32 changed(u32 page) const {
        u32 n = 0;
        for (u32 i = 0; i < kPage; ++i)
            n += __popcnt(static_cast<u32>(before[page * kPage + i] ^ m.bytes()[page * kPage + i]));
        return n;
    }
    u32 changed_all() const {
        u32 n = 0;
        for (u32 i = 0; i < sizeof before; ++i)
            n += __popcnt(static_cast<u32>(before[i] ^ m.bytes()[i]));
        return n;
    }
};

void test_law() {
    Rig r(1, 100, 1000, 5000);
    CHECK(r.noise.hammer_on() && r.m.hot_at() == 1000 && r.noise.hammer_threshold(1) == 5000);
    // Under the threshold, and at it: nothing.
    r.m.set_activations(20, 999);
    r.step();
    CHECK(r.failed.size() == 0 && r.m.hot().size() == 0);
    r.m.set_activations(20, 1000);
    r.step();
    CHECK(r.failed.size() == 0 && r.m.hot().size() == 1 && r.changed_all() == 0);
    // One past it, at a strength of 1: one bit in the page before and one
    // in the page after, and nowhere else.
    r.m.set_activations(20, 1001);
    r.step();
    CHECK(r.failed.size() == 2 && r.listed(19) == 1 && r.listed(21) == 1);
    CHECK(r.changed(19) == 1 && r.changed(21) == 1 && r.changed_all() == 2);
    CHECK(r.noise.hammered == 1 && r.noise.hammer == 2);
    // 500 more: 500 a side. A bit flipped twice is back as it was, so the
    // bits changed are fewer than the flips, and of their parity.
    r.m.set_activations(20, 1501);
    r.step();
    CHECK(r.listed(19) == 500 && r.listed(21) == 500 && r.failed.size() == 1000);
    CHECK(r.changed(19) > 300 && r.changed(19) <= 500 && r.changed(19) % 2 == 0);
    CHECK(r.changed(19) + r.changed(21) == r.changed_all());
    CHECK(r.noise.hammered == 501 && r.noise.hammer == 1002);
    // The same count the tick after: those activations have had their
    // chance.
    r.step();
    CHECK(r.failed.size() == 0 && r.changed_all() == 0);
    // A device's first and last pages have one neighbour in it: nothing
    // crosses to another device.
    r.m.set_activations(0, 1100);
    r.m.set_activations(63, 1100);
    r.step();
    CHECK(r.listed(1) == 100 && r.listed(62) == 100 && r.failed.size() == 200 && r.changed(64) == 0);
    // The second device's threshold is its own: 5,000.
    r.m.set_activations(70, 4999);
    r.m.set_activations(64, 5100);
    r.m.set_activations(95, 5100);
    r.step();
    CHECK(r.listed(65) == 100 && r.listed(94) == 100 && r.failed.size() == 200 && r.changed(63) == 0);
    CHECK(r.listed(69) == 0 && r.listed(71) == 0);
    // A device that is off, or has failed, takes no flips.
    r.states[0].on = false;
    r.m.set_activations(30, 2000);
    r.step();
    CHECK(r.failed.size() == 0 && r.changed_all() == 0);
    r.states[0].on = true;
    r.noise.set_worn(0, 0, 0, true);
    r.m.set_activations(30, 3000);
    r.step();
    CHECK(r.failed.size() == 0 && r.changed_all() == 0);
    r.noise.set_worn(0, 0, 0, false);
    r.m.set_activations(30, 3010);
    r.step();
    CHECK(r.listed(29) == 10 && r.listed(31) == 10 && r.failed.size() == 20);
    // A replaced device's counts begin again, and its pages leave the list.
    const u32 listed_before = static_cast<u32>(r.m.hot().size());
    r.noise.renew_memory(0, 1);
    CHECK(r.m.activations_of(20) == 0 && r.m.activations_of(30) == 0 && r.m.activations_of(64) == 5100);
    r.step();
    CHECK(r.failed.size() == 0 && r.m.hot().size() == 3 && listed_before == 7);
    r.m.set_activations(20, 1200);
    r.step();
    CHECK(r.listed(19) == 200 && r.listed(21) == 200);
    // The window's end: every count starts again, and a page must pass
    // its threshold afresh.
    CHECK(r.env.tick < 99);
    r.env.tick = 98;
    r.step();
    CHECK(r.m.activations_of(20) == 1200 && r.m.hot().size() == 4);
    r.step(); // tick 99, the window's last
    CHECK(r.m.activations_of(20) == 0 && r.m.activations_of(64) == 0 && r.m.hot().size() == 0);
    r.m.set_activations(20, 900);
    r.step();
    CHECK(r.failed.size() == 0);
    r.m.set_activations(20, 1500);
    r.step();
    CHECK(r.listed(19) == 500 && r.listed(21) == 500);
    // The order pages are listed in changes nothing: every draw is the
    // page's own.
    Rig a(1, 100, 1000, 1000), b(1, 100, 1000, 1000);
    a.m.set_activations(10, 1300);
    a.m.set_activations(50, 1700);
    a.m.set_activations(80, 1100);
    b.m.set_activations(80, 1100);
    b.m.set_activations(50, 1700);
    b.m.set_activations(10, 1300);
    a.step();
    b.step();
    CHECK(a.failed.size() == 2 * (300 + 700 + 100) && memcmp(a.m.bytes(), b.m.bytes(), sizeof a.before) == 0);
    // The strength as the chance: of 2,000,000 activations past the
    // threshold at 0.01, about 20,000 flips a side; and half at 0.5.
    Rig c(0.01, 1000, 1000, 1000);
    c.m.set_activations(40, 1000 + 2000000);
    c.step();
    const u32 low = c.listed(39), high = c.listed(41);
    CHECK(fabs(low - 20000.0) < 5 * sqrt(19800.0) && fabs(high - 20000.0) < 5 * sqrt(19800.0));
    CHECK(c.noise.hammered == 2000000 && c.noise.hammer == low + high);
    Rig d(0.5, 1000, 1000, 1000);
    d.m.set_activations(40, 1000 + 100000);
    d.step();
    CHECK(fabs(d.listed(39) - 50000.0) < 5 * sqrt(25000.0) && fabs(d.listed(41) - 50000.0) < 5 * sqrt(25000.0));
    // Off: nothing listed, drawn or flipped, whatever the counts.
    Rig off(0, 100, 1000, 5000);
    CHECK(!off.noise.hammer_on() && off.m.hot_at() == 0);
    off.m.set_activations(20, 4000000000u);
    off.step();
    CHECK(off.failed.size() == 0 && off.changed_all() == 0 && off.m.hot().size() == 0 && off.noise.hammer == 0);
    printf("  hammer: the law to the bit at a strength of 1: one bit either side for each activation past the "
           "threshold, none at or under it, none across a device's edge, none into a device off or failed, none "
           "twice; counts begin again at a window's end and for a replaced device; at 0.01, %u and %u flips from "
           "2,000,000 activations; at 0.5, %u and %u from 100,000\n",
           low, high, d.listed(39), d.listed(41));
}

// ---------------------------------------------------------------------------
// In a place
// ---------------------------------------------------------------------------

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "os.page_sizes = 256\n"
                           "memory.devices.min = 1\n"
                           "memory.devices.max = 1\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "supply.sag = 50\n"
                           "hammer.strength = 0.01\n"
                           "hammer.window = 60\n"
                           "hammer.threshold.fast = 3000\n"
                           "hammer.threshold.slow = 3000\n";

// Reads the byte B points at, then the byte D points at, for ever: two row
// activations a turn when they lie in different pages.
const u8 kHammer[4] = {encode(kMov, kAtB), encode(kMov, kAtD), encode(kJmp, kImm), static_cast<u8>(-2)};
// Reads the byte B points at for ever: one page, activated once.
const u8 kReader[3] = {encode(kMov, kAtB), encode(kJmp, kImm), static_cast<u8>(-1)};
// Sleeps 120 ticks at a time, for ever.
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};

bool build(World& w, Config& cfg, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(kSmall, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  hammer: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
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

// A hand-placed thread with five whole pages of its own, all zero, its B
// pointing into the second and its D into the fourth (or the second again).
// The first of the five pages' number.
u32 place_hammer(Place& p, bool two) {
    const u32 t = place(p, two ? kHammer : kReader, two ? sizeof kHammer : sizeof kReader);
    const u32 size = p.mem.page_size();
    const u32 buf = p.mem.alloc(1, 5 * size, 0, 0);
    CHECK(buf != 0 && buf % size == 0);
    p.mem.give(p.mem.alloc_at(buf), p.os.thread(t).program);
    memset(p.mem.bytes() + buf, 0, 5 * size);
    u8* saved = p.mem.bytes() + p.os.thread(t).stack;
    const u32 regs[3] = {buf + size + 8, 0, two ? buf + 3 * size + 8 : buf + size + 8}; // B, C, D
    for (u32 r = 0; r < 3; ++r)
        for (u32 k = 0; k < 4; ++k)
            saved[4 + r * 4 + k] = static_cast<u8>(regs[r] >> (8 * k));
    return buf / size;
}

u32 set_bits(const Place& p, u32 page) {
    u32 n = 0;
    const u32 size = p.mem.page_size();
    for (u32 i = 0; i < size; ++i)
        n += __popcnt(static_cast<u32>(p.mem.bytes()[page * size + i]));
    return n;
}

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

bool has_tag(const ByteWriter& b, u32 tag) {
    for (usize i = 0; i + 4 <= b.size(); ++i)
        if (memcmp(b.data() + i, &tag, 4) == 0)
            return true;
    return false;
}

// What a hammering run came to.
struct Hammered {
    u64 past = 0, flips = 0; // the place's counters
    u64 by_count = 0;        // the activations past the threshold, from the pages' counts at each window's end
    u32 most = 0;            // the most activations a page took in a tick
    u32 bits[5] = {};        // set in each of its five pages at the end
    u64 hash = 0;
};

// A world with one thread alternating between two pages, for `ticks`.
Hammered hammer_world(const char* extra, u64 ticks, u32 threshold, u64 window, ByteWriter* saved = nullptr,
                      u64 save_at = 0) {
    Hammered h;
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, extra, &why));
    Place& p = w.place(0);
    const u32 first = place_hammer(p, true);
    u32 last[2] = {};
    for (u64 t = 0; t < ticks; ++t) {
        if (saved && t == save_at)
            snapshot(w, cfg, *saved);
        w.tick();
        const u32 now[2] = {p.mem.activations_of(first + 1), p.mem.activations_of(first + 3)};
        const bool ended = (t + 1) % window == 0;
        for (u32 k = 0; k < 2; ++k) {
            if (ended) {
                CHECK(now[k] == 0); // the window's end: the counts start again
                // What the window's last tick added isn't seen here: about
                // what the tick before it did.
                h.by_count += last[k] > threshold ? last[k] - threshold : 0;
            } else {
                h.most = now[k] - last[k] > h.most ? now[k] - last[k] : h.most;
            }
            last[k] = now[k];
        }
    }
    h.past = p.noise.hammered;
    h.flips = p.noise.hammer;
    for (u32 k = 0; k < 5; ++k)
        h.bits[k] = set_bits(p, first + k);
    h.hash = w.hash();
    return h;
}

void test_thread() {
    // A thread alternating between its second and fourth pages, for 5
    // windows of 60 ticks, the threshold 3,000.
    const u64 ticks = 300;
    ByteWriter saved;
    const Hammered h = hammer_world("", ticks, 3000, 60, &saved, 45);
    CHECK(h.most > 100 && h.past > 20000);
    // The activations past the threshold are what the pages' counts say
    // (short of each window's last tick, which the counts here don't show).
    CHECK(h.past >= h.by_count && h.past <= h.by_count + 5 * 2 * h.most);
    // Each has the strength's chance of a bit either side.
    const double want = 0.01 * 2 * static_cast<double>(h.past);
    CHECK(fabs(h.flips - want) < 5 * sqrt(want));
    // Every flip landed in the page before or after a hammered one: the
    // first, the third (from both sides) and the fifth, and none in the
    // two hammered. The pages began all zero and the thread only reads.
    CHECK(h.bits[1] == 0 && h.bits[3] == 0);
    const u32 bits = h.bits[0] + h.bits[2] + h.bits[4];
    CHECK(bits <= h.flips && bits % 2 == h.flips % 2);
    // A quarter of the flips in the first page, half in the third and a
    // quarter in the fifth. A bit flipped twice is back at 0, so of a
    // page's 2,048 bits n flips leave about 1,024 x (1 - e^(-n / 1,024)) set.
    const double quarter = 1024 * (1 - exp(-static_cast<double>(h.flips) / 4 / 1024));
    const double middle = 1024 * (1 - exp(-static_cast<double>(h.flips) / 2 / 1024));
    CHECK(fabs(h.bits[0] - quarter) < 115 && fabs(h.bits[4] - quarter) < 115 && fabs(h.bits[2] - middle) < 115);
    // The same again from the same seed, and from a snapshot taken part
    // way through the first window with both pages past the threshold.
    const Hammered again = hammer_world("", ticks, 3000, 60);
    CHECK_EQ_U64(again.hash, h.hash);
    {
        CHECK(has_tag(saved, 0x48414D31)); // "HAM1"
        SnapshotHeader sh;
        TextBuf err, why;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &sh, &err) && sh.tick == 45);
        World w;
        Config cfg;
        CHECK(snapshot_config(sh, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + sh.world_offset, sh.world_size);
        CHECK(w.load(in, sh.version));
        CHECK(w.place(0).mem.hot().size() == 2); // both pages listed again, from their counts
        for (u64 t = 45; t < ticks; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), h.hash);
    }
    // A thread reading one page over and over: an activation or so a tick,
    // and nothing flips.
    u32 reader_most = 0;
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "", &why));
        Place& p = w.place(0);
        const u32 first = place_hammer(p, false);
        for (u64 t = 0; t < ticks; ++t) {
            const u32 was = p.mem.activations_of(first + 1);
            w.tick();
            const u32 now = p.mem.activations_of(first + 1);
            reader_most = now > was && now - was > reader_most ? now - was : reader_most;
        }
        CHECK(reader_most >= 1 && reader_most <= 40);
        CHECK(p.noise.hammered == 0 && p.noise.hammer == 0 && p.mem.hot().size() == 0);
        for (u32 k = 0; k < 5; ++k)
            CHECK(set_bits(p, first + k) == 0);
        CHECK(p.sched.instructions > 100000); // and it did run
    }
    // A strength of 0: off. The same thread flips nothing, nothing of it
    // is saved, and the world is another.
    {
        ByteWriter plain;
        const Hammered off = hammer_world("hammer.strength = 0\n", ticks, ~0u, ~0ull, &plain, 45);
        CHECK(off.past == 0 && off.flips == 0 && off.hash != h.hash && !has_tag(plain, 0x48414D31));
        for (u32 k = 0; k < 5; ++k)
            CHECK(off.bits[k] == 0);
    }
    // A tick of half the length (R9): the cores run half the cycles a
    // tick in half the quanta, and the window is the same simulated time,
    // so twice the ticks.
    // Over the same simulated time: the same activations past the
    // threshold, within a part in fifty, and the same flips, within their
    // scatter.
    // (Both with a source that doesn't sag, so the cores' speeds are in
    // proportion whatever the place draws.)
    const Hammered whole = hammer_world("supply.sag = 0\n", ticks, 3000, 60);
    const Hammered half = hammer_world("supply.sag = 0\nhw.p.slope = 83333\nhw.e.slope = 36000\nsched.quanta = 8\n"
                                       "hammer.window = 120\n",
                                       2 * ticks, 3000, 120);
    CHECK(half.most * 2 > whole.most * 9 / 10 && half.most * 2 < whole.most * 11 / 10);
    CHECK(fabs(static_cast<double>(half.past) - static_cast<double>(whole.past)) < 0.02 * whole.past);
    CHECK(fabs(static_cast<double>(half.flips) - static_cast<double>(whole.flips)) < 5 * sqrt(2 * want));
    printf("  hammer: a thread alternating between two pages, 300 ticks in windows of 60, threshold 3,000: up to "
           "%u activations of a page a tick, %llu past the threshold, %llu flips (%.0f expected at 0.01 a side): "
           "%u, %u and %u bits set in the pages before, between and after, none in the two hammered\n",
           h.most, static_cast<ull>(h.past), static_cast<ull>(h.flips), want, h.bits[0], h.bits[2], h.bits[4]);
    printf("  hammer: the same from a snapshot of tick 45; a thread reading one page: at most %u activations a "
           "tick and no flips; a strength of 0: none; a tick of half the length, over the same simulated time: "
           "%llu past the threshold and %llu flips, against %llu and %llu\n",
           reader_most, static_cast<ull>(half.past), static_cast<ull>(half.flips), static_cast<ull>(whole.past),
           static_cast<ull>(whole.flips));
}

// A flip in a sleeper's saved state ends its thread (9.4).
void test_saved_state() {
    u32 ended = 0, lived = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "hammer.strength = 1\n", &why));
        Place& p = w.place(0);
        const u32 t = place(p, kSleeper, sizeof kSleeper);
        const u64 uid = p.os.program(p.os.thread(t).program).uid;
        for (u32 k = 0; k < 4; ++k) {
            p.os.clear_facts();
            p.tick();
        }
        const Thread& th = p.os.thread(t);
        CHECK(th.state == ThreadState::sleeping && !th.core);
        const u32 page = p.mem.page_of(th.stack);
        const bool up = page + 8 < p.mem.page_count();
        CHECK(up || page > 8);
        // The page after its stack's, hammered 3,000 times past the
        // threshold: 3,000 bits flip in its stack's page, of 2,048. Or, the
        // second time, a page well away.
        p.mem.set_activations(pass == 0 ? (up ? page + 1 : page - 1) : (up ? page + 6 : page - 6), 6000);
        const u64 failed = p.failed_threads;
        p.os.clear_facts();
        p.tick();
        bool of_memory = false;
        for (usize k = 0; k < p.os.deaths.size(); ++k)
            of_memory = of_memory || (p.os.deaths[k].uid == uid && p.os.deaths[k].cause == DeathCause::memory_lost);
        CHECK(p.noise.hammer == 6000);
        if (pass == 0) {
            CHECK(of_memory && p.failed_threads == failed + 1);
            ended += of_memory ? 1 : 0;
        } else {
            CHECK(!of_memory && p.failed_threads == failed && th.state == ThreadState::sleeping);
            lived += of_memory ? 0 : 1;
        }
    }
    CHECK(ended == 1 && lived == 1);
    printf("  hammer: a sleeping thread whose stack's page took 3,000 flips from the page after it ended, cause "
           "memory; the same flips six pages away left it asleep\n");
}

// Worlds of several places, each with a hammering thread and replicators:
// the same on one host thread and three, and from a snapshot.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

void test_worlds() {
    const char* const kMany = "world.places = 6\nhammer.threshold.fast = 1500\nhammer.threshold.slow = 1500\n"
                              "hammer.strength = 0.05\nnoise.cosmic = 1e-7\n";
    const u32 first = 50, rest = 130;
    u64 hash[2] = {}, mid = 0, flips = 0, births = 0, deaths = 0;
    ByteWriter saved;
    u32 listed = 0;
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, kMany, &why));
        for (u32 i = 0; i < w.place_count(); ++i) {
            Place& p = w.place(i);
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
            place_hammer(p, true);
        }
        w.start_threads(pass == 0 ? 1 : 3);
        for (u32 t = 0; t < first; ++t)
            w.tick();
        if (pass == 0) {
            mid = w.hash();
            snapshot(w, cfg, saved);
            for (u32 i = 0; i < w.place_count(); ++i)
                listed += static_cast<u32>(w.place(i).mem.hot().size());
        } else {
            CHECK_EQ_U64(w.hash(), mid);
        }
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        hash[pass] = w.hash();
        if (pass == 0)
            for (u32 i = 0; i < w.place_count(); ++i) {
                flips += w.place(i).noise.hammer;
                births += w.place(i).births;
                deaths += w.place(i).deaths;
            }
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(flips > 100 && listed >= 6 && births > 100);
    {
        SnapshotHeader sh;
        TextBuf err, why;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &sh, &err) && sh.version == 6 && sh.tick == first);
        World w;
        Config cfg;
        CHECK(snapshot_config(sh, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + sh.world_offset, sh.world_size);
        CHECK(w.load(in, sh.version));
        CHECK_EQ_U64(w.hash(), mid);
        w.start_threads(3);
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
    }
    printf("  hammer: 6 places, each with a hammering thread among replicators, 180 ticks: %llu flips, %llu births "
           "and %llu deaths; the same on 1 host thread and 3, and from a snapshot of tick %u with %u pages past "
           "the lowest threshold\n",
           static_cast<ull>(flips), static_cast<ull>(births), static_cast<ull>(deaths), first, listed);
}

// Each memory device's threshold by its latency, and the settings refused.
void test_settings() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "world.places = 40\nmemory.devices.min = 2\nmemory.devices.max = 4\nmemory.size = 65536\n"
                "hammer.threshold.fast = 10000\nhammer.threshold.slow = 140000\n",
                &why));
    u32 lowest = ~0u, highest = 0, devices = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        u32 least = ~0u;
        for (u32 d = 0; d < p.mem.device_count(); ++d) {
            // Latencies run from 4 to 12 cycles: 10,000 x 14^((latency - 4) / 8).
            const double lat = p.mem.device(d).latency16 / 16.0;
            const double want = 10000 * pow(14.0, (lat - 4) / 8);
            const u32 at = p.noise.hammer_threshold(d);
            CHECK(fabs(at - want) < 1);
            lowest = at < lowest ? at : lowest;
            highest = at > highest ? at : highest;
            least = at < least ? at : least;
            ++devices;
        }
        CHECK(p.mem.hot_at() == least && p.noise.hammer_window() == 60);
    }
    CHECK(lowest == 10000 && highest == 140000 && devices > 80);
    World bad;
    Config c2;
    CHECK(!build(bad, c2, "hammer.threshold.fast = 5000\nhammer.threshold.slow = 4000\n", &why));
    printf("  hammer: %u memory devices of 40 places: thresholds from %u to %u by latency; a slow threshold under "
           "the fast one refused\n",
           devices, lowest, highest);
}

} // namespace

void suite_hammer() {
    test_law();
    test_thread();
    test_saved_state();
    test_worlds();
    test_settings();
}
