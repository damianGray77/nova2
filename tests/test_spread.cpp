// The spread measure (design.md 13.3, 14.1; build-plan-gate3.md step 1), on
// hand-made arrivals, before anything moves between places by itself: a
// replicator's bytes put by hand in another place's port 0 queue, a program
// at the first place their sender. They run there as a prompt run, and the
// program it makes is a colonist: cause "arrival", the sender its parent,
// in the sender's lineage, founding a colony. The colony's own offspring
// settle the place on stage 4's terms, counting only the copies born there,
// while the lineage's home keeps its own count. An arrival with no sender
// (an idle run's, 10.4) founds a lineage of its own, not a colony. And a
// colony that dies out is founded again by the next arrival. A static run
// in an arrival's landed bytes, all as they landed, makes the sender's
// colonists too (13.2, asked after build-plan-gate3.md step 7).
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kWorld = "world.places = 3\n"
                           "memory.size = 32768\n"
                           "os.strict_share = 0\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n";

// Sleeps 120 ticks at a time, for ever.
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
// A.5's 14-byte replicator on a lax place (test_os.cpp traces it). It copies
// itself as an idle run too, so a prompt run of it makes programs.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build(World& w, Config& cfg, TextBuf* why) {
    Config::ParseError pe;
    return cfg.parse(kWorld, &pe) && w.init(cfg, why);
}

// A hand-placed program running `code` (R7): its uid.
u64 place(Place& p, const u8* code, u32 len, u32 size = 128) {
    const u32 at = p.mem.alloc(1, size, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, len);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return p.os.program(p.os.thread(t).program).uid;
    CHECK(false);
    return 0;
}

// Fills a place's reset region with PUSHes, which end an idle run at once
// (it may not push), so nothing is born of static there.
void quiet(Place& p) { memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size()); }

StageCriteria short_hold() {
    StageCriteria c;
    c.span = 500;
    c.members = 20;
    c.share = 0.1;
    c.copies = 0.5;
    return c;
}

// The colonist, its colony, and the colony settling: done when (1).
void test_colony_settles() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, &why));
    for (u32 i = 0; i < w.place_count(); ++i)
        quiet(w.place(i));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(2);
    const u64 sender = place(w.place(0), kSleeper, 6);
    w.tick(); // the observer has the sender
    CHECK(obs.program(sender) && obs.lineage(sender));
    // The replicator's bytes at place 1's port 0, sent by the sleeper; and
    // at place 2's, sent by an idle run (no sender).
    CHECK(w.place(1).region.arrive(kLax, 14, BirthCause::arrival, sender, false, false));
    CHECK(w.place(2).region.arrive(kLax, 14, BirthCause::arrival, 0, false, false));
    const StageCriteria c = short_hold();
    LineageClock clock;
    u64 settled_at = ~0ull;
    bool seen = false;
    while (w.tick_count() < 5000 && settled_at == ~0ull) {
        w.tick();
        const ColonyRec* C = obs.colony(sender, 1);
        if (C && !seen) {
            // The colonist, the tick it's born: from the arrival, the sleeper
            // its parent, in its lineage, a generation down.
            seen = true;
            const ProgramRec* colonist = obs.program(C->colonist);
            CHECK(colonist != nullptr);
            if (colonist)
                CHECK(colonist->arrived && colonist->parent == sender && colonist->origin == sender &&
                      colonist->depth == 1 && colonist->place == 1 && colonist->born == C->first);
            // Its children, born in the same tick, are copies made there; it
            // isn't one, being an arrival's.
            CHECK(C->alive >= 1 && C->alive_copies == C->alive - 1);
        }
        if (C && colony_settles(*C, w.tick_count(), w.place(1).os.live_programs(), c, &clock))
            settled_at = w.tick_count();
    }
    CHECK(seen);
    CHECK(settled_at != ~0ull);
    const ColonyRec* C = obs.colony(sender, 1);
    CHECK(C != nullptr);
    if (!C)
        return;
    CHECK(C->colonist != 0 && C->origin == sender && C->place == 1 && C->from == 0 && C->foundings == 1);
    CHECK(C->ended == ~0ull && C->alive >= c.members && C->alive_copies * 2 >= C->alive);
    CHECK(w.place(1).region.prompt_runs == 1 && w.place(1).region.boot_runs == 0);
    // The settlement is the hold's: span ticks from the check its hold began.
    CHECK(clock.established == settled_at && clock.held_since != ~0ull && settled_at - clock.held_since >= c.span);
    // The lineage: one member at home (the sleeper), the rest in the colony;
    // the home doesn't hold, so stage 4 never comes there.
    const LineageRec* L = obs.lineage(sender);
    CHECK(L && L->place == 0 && L->home_alive == 1 && L->colonies == 1 && L->alive == 1 + C->alive);
    CHECK(L && L->home_copies == 0 && !lineage_holds(L->home_alive, L->home_copies, w.place(0).os.live_programs(), c));
    // The births from the arrival came from the prompt run, and none counts
    // as a copy made at place 1: the colony's copies were born of its members.
    const u64 from_arrival = obs.births_by_cause[static_cast<u8>(BirthCause::arrival)];
    CHECK(from_arrival >= 2); // one prompt run at each of places 1 and 2
    // Place 2's arrival had no sender: a lineage of its own, begun there, not
    // a colony; it may hold place 2, which is stage 4's business.
    u32 begun_at_2 = 0;
    for (u32 i = 0; i < obs.lineage_count(); ++i) {
        const LineageRec& M = obs.lineage_at(i);
        if (M.place == 2 && M.cause == BirthCause::arrival)
            ++begun_at_2;
        CHECK(M.home_alive <= M.alive && M.home_copies <= M.alive_copies);
    }
    CHECK(begun_at_2 == 1 && obs.colony_count() == 1);
    printf("  spread: a replicator's 14 bytes sent by hand from place 0 ran as a prompt run at place 1, making a "
           "colonist (cause arrival, the sender its parent) at tick %llu; its colony settled the place at tick %llu, "
           "%u of the place's %u programs, %u born there as copies; %llu births from arrivals\n",
           static_cast<unsigned long long>(C->first), static_cast<unsigned long long>(settled_at), C->alive,
           w.place(1).os.live_programs(), C->alive_copies, static_cast<unsigned long long>(from_arrival));
}

// A colony that dies out is founded again by the next arrival: its record
// keeps its first colonist and counts both foundings; and a colony of
// arrivals alone, with no copies made there, never settles.
void test_colony_refounded() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, &why));
    for (u32 i = 0; i < w.place_count(); ++i)
        quiet(w.place(i));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(2);
    const u64 sender = place(w.place(0), kSleeper, 6);
    w.tick();
    // The sleeper's own bytes as the arrival: SLEEP does nothing in an idle
    // run, so the prompt run spins to its cap, and nothing is born of it: no
    // colony.
    CHECK(w.place(1).region.arrive(kSleeper, 6, BirthCause::arrival, sender, false, false));
    for (u32 t = 0; t < 5; ++t)
        w.tick();
    CHECK(w.place(1).region.prompt_runs == 1 && obs.colony_count() == 0);
    // The replicator, sent by the sleeper: a colony.
    CHECK(w.place(1).region.arrive(kLax, 14, BirthCause::arrival, sender, false, false));
    for (u32 t = 0; t < 5; ++t)
        w.tick();
    const ColonyRec* C = obs.colony(sender, 1);
    CHECK(C && C->foundings == 1 && C->alive > 0);
    if (!C)
        return;
    const u64 first = C->colonist, first_tick = C->first;
    // A cold start ends every program at place 1: the colony dies out.
    w.place(1).cold_start();
    w.tick();
    C = obs.colony(sender, 1);
    CHECK(C && C->alive == 0 && C->alive_copies == 0 && C->ended != ~0ull);
    // The next arrival founds it again; the record keeps its first colonist.
    CHECK(w.place(1).region.arrive(kLax, 14, BirthCause::arrival, sender, false, false));
    for (u32 t = 0; t < 5; ++t)
        w.tick();
    C = obs.colony(sender, 1);
    CHECK(C && C->foundings == 2 && C->alive > 0 && C->ended == ~0ull && C->colonist == first &&
          C->first == first_tick);
    // A colony with no copies made there doesn't hold, however many members.
    ColonyRec A{};
    A.colonist = 9;
    A.alive = 100;
    A.alive_copies = 0;
    LineageClock k;
    const StageCriteria c = short_hold();
    CHECK(!colony_settles(A, 0, 100, c, &k) && !colony_settles(A, 1000, 100, c, &k) && k.held_since == ~0ull);
}

// A static run in an arrival's landed bytes (13.2). The arrival's prompt
// run ends at once (a PUSH), and the core's next start, region.step bytes
// on, is the replicator: a static run of it makes programs. With the landed
// bytes as they landed, they're the sender's colonists, as the prompt
// run's would be; with a byte of them changed first (one the run never
// reaches), they begin a lineage of their own. Halted cores (a long retry)
// start again only when the test says the region was written.
void test_static_in_landed() {
    for (u32 changed = 0; changed < 2; ++changed) {
        World w;
        Config cfg;
        TextBuf why;
        Config::ParseError pe;
        CHECK(cfg.parse(kWorld, &pe));
        cfg.set("region.retry", "1000000");
        CHECK(w.init(cfg, &why));
        for (u32 i = 0; i < w.place_count(); ++i)
            quiet(w.place(i));
        Observer obs;
        obs.init(ObserverSettings{});
        w.watch(&obs);
        w.start_threads(2);
        const u64 sender = place(w.place(0), kSleeper, 6);
        Place& p = w.place(1);
        for (u32 t = 0; t < 3; ++t)
            w.tick(); // place 1's cores halt on its quiet region
        const u32 step = p.region.settings().step;
        u8 bytes[64];
        memset(bytes, encode(kPush, kA), sizeof bytes);
        CHECK(step + 14 <= sizeof bytes);
        memcpy(bytes + step, kLax, 14);
        const u32 len = step + 14;
        CHECK(p.region.arrive(bytes, len, BirthCause::arrival, sender, false, false));
        w.tick();
        CHECK(p.region.prompt_runs == 1 && p.region.arrival_made == 0 && p.os.live_programs() == 0);
        CHECK(p.region.landing_count() == 1 && p.region.landed_runs == 0);
        if (p.region.landing_count() != 1)
            continue;
        const Landing l = p.region.landing(0);
        CHECK(l.sender == sender && l.len == len);
        if (changed)
            p.mem.bytes()[p.mem.region_base() + l.at] = encode(kPush, kB);
        p.mem.note_region_change(); // the halted cores start again
        for (u32 t = 0; t < 20 && p.os.live_programs() == 0; ++t)
            w.tick();
        CHECK(p.os.live_programs() > 0 && p.region.arrival_made == 0);
        const ColonyRec* C = obs.colony(sender, 1);
        if (!changed) {
            CHECK(p.region.landed_runs >= 1 && p.region.landed_made >= 1);
            CHECK(C && C->colonist != 0 && C->foundings == 1);
            const ProgramRec* colonist = C ? obs.program(C->colonist) : nullptr;
            CHECK(colonist && colonist->arrived && colonist->parent == sender && colonist->origin == sender &&
                  colonist->place == 1);
            CHECK(obs.births_by_cause[static_cast<u8>(BirthCause::arrival)] == 0);
        } else {
            // Changed: the landing went when the run found it so.
            CHECK(p.region.landed_runs == 0 && p.region.landed_made == 0 && p.region.landing_count() == 0);
            CHECK(C == nullptr && obs.colony_count() == 0);
            u32 begun = 0;
            for (u32 i = 0; i < obs.lineage_count(); ++i)
                begun += obs.lineage_at(i).place == 1 && obs.lineage_at(i).cause == BirthCause::static_bytes ? 1 : 0;
            CHECK(begun >= 1);
        }
        CHECK(p.check(&why));
    }
    printf("  spread: a static run in an arrival's landed bytes made the sender's colonist; with a byte changed, "
           "a lineage of its own\n");
}

} // namespace

void suite_spread() {
    test_colony_settles();
    test_colony_refounded();
    test_static_in_landed();
}
