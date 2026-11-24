// 9.4's memory-taker (build-plan-gate2.md step 9; app/taker_world.h): one
// program, placed by hand (R7), that takes all of its place's free memory and
// fills it with threads asleep until an interrupt that never comes.
//
// - The fill built by hand at setup is the state the taker's own code
//   reaches when it runs on a steady supply: the same threads with the same
//   saved registers, the same allocations, every byte of memory the same; on
//   a lax place and a strict one.
// - Filled, a place is frozen: on a steady supply, static runs on in the
//   reset region but nothing is born, and the taker keeps every thread and
//   byte.
// - A blackout ends it: its threads fade or end at the cold start, and its
//   memory is freed.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/world.h"
#include "taker_world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n";

// The small world with `extra` on top.
bool build(World& w, const char* extra) {
    Config cfg;
    Config::ParseError pe;
    if (!cfg.parse(kSmall, &pe))
        return false;
    Config over;
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  taker: %s %s\n", why.c_str(), cfg.value_error());
        return false;
    }
    return true;
}

// Fills the reset region with PUSHes, which end an idle run at once (it may
// not push), so static does nothing.
void quiet_region(Place& p) { memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size()); }

bool alive(const Place& p, const TakerPlaced& t) {
    const Program& pr = p.os.program(t.holder);
    return pr.alive && pr.uid == t.uid;
}

// Whether every one of the taker's threads sleeps until an interrupt.
bool all_asleep(const Place& p, const TakerPlaced& t) {
    const Program& pr = p.os.program(t.holder);
    for (u32 k = pr.first_thread; k; k = p.os.thread(k - 1).next)
        if (p.os.thread(k - 1).state != ThreadState::waiting_interrupt)
            return false;
    return pr.first_thread != 0;
}

// One tick of a place on its own; the death of the taker, if it's among this
// tick's facts, is noted. The facts are dropped.
void step(Place& p, const TakerPlaced& t, DeathFact* end) {
    p.tick();
    for (usize i = 0; i < p.os.deaths.size(); ++i)
        if (p.os.deaths[i].uid == t.uid && end->tick == ~0ull)
            *end = p.os.deaths[i];
    p.os.clear_facts();
}

// The fill by hand against the code's own, on a place `strict` or not.
void fill_matches(bool strict) {
    const char* type = strict ? "os.strict_share = 1\n" : "os.strict_share = 0\n";
    World wa, wb;
    CHECK(build(wa, type) && build(wb, type));
    Place& a = wa.place(0);
    Place& b = wb.place(0);
    CHECK(a.mem.strict() == strict && b.mem.strict() == strict);
    quiet_region(a);
    quiet_region(b);
    TakerPlaced ta, tb;
    CHECK(place_taker(a, &ta));
    CHECK(place_taker_code(b, &tb));
    CHECK(ta.code == tb.code && ta.holder == tb.holder && ta.uid == tb.uid);
    TextBuf why;
    CHECK(a.check(&why));
    // The code fills b as it runs, on the steady supply.
    DeathFact end{};
    end.tick = ~0ull;
    u64 ticks = 0;
    while (!(all_asleep(b, tb) && b.os.program(tb.holder).threads > 1) && ticks < 100000) {
        step(b, tb, &end);
        ++ticks;
    }
    CHECK(end.tick == ~0ull && alive(b, tb));
    const u32 threads = b.os.program(tb.holder).threads;
    CHECK(threads == ta.threads && threads > 100);
    CHECK(held_by(b.mem, tb.holder) == ta.held);
    CHECK(b.mem.free_bytes() == ta.free);
    // Every thread record the same: its id, state, stack and saved registers.
    u32 same = 0;
    for (u32 t = 0; t < a.os.thread_capacity(); ++t) {
        const Thread& x = a.os.thread(t);
        const Thread& y = b.os.thread(t);
        CHECK(x.state == y.state);
        if (x.state == ThreadState::free)
            continue;
        ExecContext cx, cy;
        a.os.load(t, cx);
        b.os.load(t, cy);
        const bool ok = x.id == y.id && x.program == y.program && x.stack == y.stack && cx.r.a == cy.r.a &&
                        cx.r.b == cy.r.b && cx.r.c == cy.r.c && cx.r.d == cy.r.d && cx.r.ip == cy.r.ip &&
                        cx.r.sp == cy.r.sp;
        CHECK(ok);
        same += ok ? 1 : 0;
    }
    CHECK(same == threads);
    // And every byte of memory.
    CHECK(a.mem.size() == b.mem.size() && memcmp(a.mem.bytes(), b.mem.bytes(), a.mem.size()) == 0);
    CHECK(b.check(&why));
    printf("  taker: on a %s place the code filled it in %llu ticks: %u threads, %u of %u bytes held, %u left "
           "free; the fill by hand is the same, every thread and byte\n",
           strict ? "strict" : "lax", static_cast<unsigned long long>(ticks), threads, ta.held, a.mem.size(),
           ta.free);
}

void test_fill_matches_code() {
    fill_matches(false);
    fill_matches(true);
}

// Filled, a place is frozen: static runs on in the region, but there's no
// room for a stack, so nothing is born, and the taker keeps everything.
void test_frozen() {
    for (u32 strict = 0; strict < 2; ++strict) {
        World w;
        CHECK(build(w, strict ? "os.strict_share = 1\n" : "os.strict_share = 0\n"));
        Place& p = w.place(0);
        TakerPlaced t;
        CHECK(place_taker(p, &t));
        const u64 births = p.births;
        DeathFact end{};
        end.tick = ~0ull;
        for (u32 k = 0; k < 4000; ++k)
            step(p, t, &end);
        CHECK(end.tick == ~0ull && alive(p, t) && all_asleep(p, t));
        CHECK(p.os.program(t.holder).threads == t.threads && held_by(p.mem, t.holder) == t.held);
        CHECK(p.births == births && p.os.live_programs() == 1);
        CHECK(p.region.started > 1000);
        TextBuf why;
        CHECK(p.check(&why));
        printf("  taker: a %s place, filled, stayed frozen for 4,000 ticks: %llu idle runs, no birth, the taker "
               "whole\n",
               strict ? "strict" : "lax", static_cast<unsigned long long>(p.region.started));
    }
}

// A blackout ends it: a 20-tick blackout fades its threads' saved registers
// or reaches the cold start, and its memory is freed with it.
void test_blackout() {
    World w;
    CHECK(build(w, "os.strict_share = 0\nsupply.sag = 50\nsupply.dropouts = 1000\nsupply.dropout.min = 20\n"
                   "supply.dropout.max = 20\nsupply.blackout_share = 1\n"));
    Place& p = w.place(0);
    TakerPlaced t;
    CHECK(place_taker(p, &t));
    Dropout first;
    first.length = 0;
    first.start = ~0ull;
    for (u64 j = 0; j < 1000; ++j) {
        Dropout d;
        if (p.supply.dropout_in(j, &d) && d.start < first.start)
            first = d;
    }
    CHECK(first.length == 20 && first.keep == 0 && first.start > 100);
    DeathFact end{};
    end.tick = ~0ull;
    while (p.env.tick < first.start)
        step(p, t, &end);
    // Whole up to the blackout.
    CHECK(end.tick == ~0ull && alive(p, t) && p.os.program(t.holder).threads == t.threads);
    while (p.cold_starts == 0 && p.env.tick < first.start + first.length + 10)
        step(p, t, &end);
    CHECK(p.cold_starts == 1);
    CHECK(end.tick != ~0ull && end.tick >= first.start && end.tick <= p.env.tick);
    CHECK(end.cause == DeathCause::memory_lost || end.cause == DeathCause::cold_start);
    CHECK(!alive(p, t) && held_by(p.mem, t.holder) == 0);
    CHECK(p.mem.free_bytes() > t.held);
    TextBuf why;
    CHECK(p.check(&why));
    printf("  taker: a 20-tick blackout from tick %llu ended it at tick %llu (%s), and the cold start freed its "
           "%u bytes\n",
           static_cast<unsigned long long>(first.start), static_cast<unsigned long long>(end.tick),
           end.cause == DeathCause::memory_lost ? "its last thread faded" : "at the cold start", t.held);
}

} // namespace

void suite_taker() {
    test_fill_matches_code();
    test_frozen();
    test_blackout();
}
