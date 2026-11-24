// The cold start and the boot block (design.md 10.4, 10.5, 13.1, 14.1;
// build-plan-gate2.md step 6): a program that DWRITEs itself to offset 0
// living through a blackout by booting, the program born of the boot having
// it for parent; a boot block nobody wrote booting as static; a program
// bigger than the block booting through a second stage that DREADs the
// rest; a cold start on its own ending every program and thread and freeing
// their memory, with the boot block at the head of port 0's queue, which
// drops what it hasn't room for; the turnover measure marking the windows a
// cold start fell in, and the observer carrying a dead writer's lineage on
// through its boot; and the new state through threads and snapshots.
#include <cstdio>
#include <cstdlib> // calloc, free
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 1\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "supply.sag = 50\n";

// A booter, 34 bytes. As a thread it DWRITEs itself to offset 0 and waits
// for ever (A = 34 from the write sends it to the wait). Booted, in an idle
// run, the DWRITE fails with 0, so it copies itself from where it landed
// into an allocation, SPAWNs the copy, and ends the run with a PUSH (an
// idle run may not). The copy, a thread, writes itself back to offset 0.
//   0 MOV C; STO B; MOV #34; STO C        B = itself, C = 34
//   5 MOV #0; SYS #DWRITE; JNZ #19       to offset 0; a thread's goes to 28
//  11 MOV #2; STO B; MOV #34; SYS #ALLOC  34 bytes, into A and D
//  18 MOV IP; SUB #18; STO B; CPY #34     B = where it landed
//  24 MOV D; SYS #SPAWN; PUSH A           the copy, then the run ends
//  28 MOV #0; SYS #SLEEP; JMP #-4         waits for ever
const u8 kBooter[34] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                        kDwrite,            encode(kJnz, kImm), 19,                 encode(kMov, kImm),
                        2,                  encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSys, kImm), kAlloc,             encode(kMov, kIp),  encode(kSub, kImm),
                        18,                 encode(kSto, kB),   encode(kCpy, kImm), 34,
                        encode(kMov, kD),   encode(kSys, kImm), kSpawn,             encode(kPush, kA),
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4)};

// A program of 96 bytes, booting from a block of 64 through a second stage
// (10.5). As a thread it DWRITEs all of itself to offset 0 and waits for
// ever. Booted, only its first 64 bytes land: its first stage DREADs the
// other 32 from offset 64 into the region just past them, and runs on into
// them, the second stage, which copies all 96 bytes and SPAWNs the copy.
//   0 MOV C; STO B; MOV #96; STO C; MOV #0; SYS #DWRITE; JNZ #72   a thread's goes to 81
//  11 MOV IP; ADD #53; STO B; MOV #32; STO C; MOV #64; SYS #DREAD  to where it landed + 64
//  22 JMP #42                                                       to the second stage
//  64 MOV #2; STO B; MOV #96; SYS #ALLOC; MOV IP; SUB #71; STO B; CPY #96; MOV D; SYS #SPAWN; PUSH A
//  81 MOV #0; SYS #SLEEP; JMP #-4
u8 kStaged[96] = {};

void make_staged() {
    const u8 first[24] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 96,
                          encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                          kDwrite,            encode(kJnz, kImm), 72,                 encode(kMov, kIp),
                          encode(kAdd, kImm), 53,                 encode(kSto, kB),   encode(kMov, kImm),
                          32,                 encode(kSto, kC),   encode(kMov, kImm), 64,
                          encode(kSys, kImm), kDread,             encode(kJmp, kImm), 42};
    const u8 second[23] = {encode(kMov, kImm), 2,                  encode(kSto, kB),   encode(kMov, kImm),
                           96,                 encode(kSys, kImm), kAlloc,             encode(kMov, kIp),
                           encode(kSub, kImm), 71,                 encode(kSto, kB),   encode(kCpy, kImm),
                           96,                 encode(kMov, kD),   encode(kSys, kImm), kSpawn,
                           encode(kPush, kA),  encode(kMov, kImm), 0,                  encode(kSys, kImm),
                           kSleep,             encode(kJmp, kImm), static_cast<u8>(-4)};
    memset(kStaged, 0, sizeof kStaged);
    memcpy(kStaged, first, sizeof first);
    memcpy(kStaged + 64, second, sizeof second);
}

// Sleeps 120 ticks at a time, for ever.
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

// Parses the small world with `extra` on top and builds it.
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
u32 place(Place& p, const u8* code, u32 len, u32 size = 128) {
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

u64 uid_of(Place& p, u32 t) { return p.os.program(p.os.thread(t).program).uid; }

// A place's facts, kept over many ticks (the world drops them each tick).
struct Kept {
    Array<BirthFact> births;
    Array<u8> bytes; // their genomes, by genome_at
    Array<DeathFact> deaths;
    Array<ColdStartFact> colds;

    // The first birth with this cause, or null.
    const BirthFact* birth(BirthCause cause) const {
        for (usize i = 0; i < births.size(); ++i)
            if (births[i].cause == cause)
                return &births[i];
        return nullptr;
    }
    const DeathFact* death(u64 uid) const {
        for (usize i = 0; i < deaths.size(); ++i)
            if (deaths[i].uid == uid)
                return &deaths[i];
        return nullptr;
    }
    bool genome_is(const BirthFact& f, const u8* code, u32 len) const {
        return f.genome_len == len && memcmp(bytes.data() + f.genome_at, code, len) == 0;
    }
};

// One tick of a place on its own, as the world runs it: the facts go to
// the observer (if any) and are kept, then dropped.
void step(Place& p, Kept* k, Observer* o) {
    p.tick();
    if (o)
        o->ingest(p);
    if (k) {
        for (usize i = 0; i < p.os.births.size(); ++i) {
            BirthFact f = p.os.births[i];
            f.genome_at = static_cast<u32>(k->bytes.size());
            k->bytes.append(p.os.fact_bytes.data() + p.os.births[i].genome_at, f.genome_len);
            k->births.push(f);
        }
        for (usize i = 0; i < p.os.deaths.size(); ++i)
            k->deaths.push(p.os.deaths[i]);
        for (usize i = 0; i < p.os.cold_starts.size(); ++i)
            k->colds.push(p.os.cold_starts[i]);
    }
    p.os.clear_facts();
}

void run_to(Place& p, u64 tick, Kept* k, Observer* o) {
    while (p.env.tick < tick)
        step(p, k, o);
}

// The first drop-out of the place's schedule.
Dropout first_dropout(Place& p) {
    Dropout first;
    first.length = 0;
    first.start = ~0ull;
    for (u64 j = 0; j < 1000; ++j) {
        Dropout d;
        if (p.supply.dropout_in(j, &d) && d.start < first.start)
            first = d;
    }
    CHECK(first.length != 0);
    return first;
}

ObserverSettings small_windows() {
    ObserverSettings s;
    s.turnover.window = 100;
    s.turnover.members = 1;
    return s;
}

const char* cause_name(DeathCause c) {
    static const char* const kName[kDeathCauses] = {"exit",   "fault",      "cut-out", "device failed", "memory",
                                                    "cold start", "heat", "out of memory", "watchdog"};
    return kName[static_cast<u8>(c)];
}

// Done when (1): a program that DWRITEs itself to offset 0 lives through a
// deep drop-out by booting. A 20-tick blackout cuts out every device, so the
// region's memory restarts and the place cold-starts. The booter ends in it,
// by fading or at the cold start, and the boot block, its 34 bytes and the
// disk's static after them, runs as a prompt run: a birth with the cause
// "boot" whose parent is the booter and whose genome is its 34 bytes. The
// copy writes itself back, and the observer has it in the booter's lineage.
// The region is filled with PUSHes, which end an idle run at once (it may
// not push), so nothing born of static writes the block before the booter's
// boot: anything may, and the last writer wins.
void test_blackout_boot() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "supply.dropouts = 1000\nsupply.dropout.min = 20\nsupply.dropout.max = 20\n"
                "supply.blackout_share = 1\n",
                &why));
    Place& p = w.place(0);
    memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size());
    Observer obs;
    obs.init(small_windows());
    const u32 t = place(p, kBooter, 34);
    const u64 booter = uid_of(p, t);
    const Dropout d = first_dropout(p);
    CHECK(d.start > 10); // the booter has written itself by then
    Kept k;
    run_to(p, d.start, &k, &obs);
    // Just before it: the booter on disk at offset 0, the block's last writer.
    CHECK(memcmp(p.os.disk(), kBooter, 34) == 0 && p.os.boot_writer() == booter);
    CHECK(p.cold_starts == 0 && k.colds.size() == 0 && k.death(booter) == nullptr && k.births.size() == 1);
    // Through it, to the boot.
    const u64 back = d.start + d.length;
    while (!k.birth(BirthCause::boot) && p.env.tick < back + 10)
        step(p, &k, &obs);
    CHECK(p.cold_starts == 1 && p.cold_written == 1 && k.colds.size() == 1);
    const ColdStartFact cs = k.colds.size() ? k.colds[0] : ColdStartFact{};
    CHECK(cs.writer == booter && cs.place == p.id());
    CHECK(cs.tick >= back && cs.tick <= back + 1); // power back: the first tick after
    const DeathFact* died = k.death(booter);
    CHECK(died != nullptr);
    const DeathCause how = died ? died->cause : DeathCause::exit;
    CHECK(how == DeathCause::memory_lost || how == DeathCause::cold_start);
    if (how == DeathCause::cold_start)
        CHECK(died->tick == cs.tick);
    const BirthFact* found = k.birth(BirthCause::boot);
    CHECK(found != nullptr);
    if (!found)
        return;
    const BirthFact b = *found; // k's arrays grow as it runs on
    CHECK(b.parent_uid == booter && b.tick >= cs.tick && b.tick <= cs.tick + 2);
    CHECK(k.genome_is(b, kBooter, 34) && b.size == 34);
    // The copy writes itself back: the block's last writer next.
    const u64 born = p.env.tick;
    while (p.os.boot_writer() == booter && p.env.tick < born + 10)
        step(p, &k, &obs);
    CHECK(p.os.boot_writer() == b.uid && memcmp(p.os.disk(), kBooter, 34) == 0);
    // On to the end of the window the cold start fell in, the next drop-out
    // still to come.
    const u64 window_end = cs.tick / 100 * 100 + 100;
    for (u64 j = 0; j < 1000; ++j) {
        Dropout e;
        if (p.supply.dropout_in(j, &e) && e.start != d.start)
            CHECK(e.start >= window_end);
    }
    run_to(p, window_end, &k, &obs);
    // The boot was one prompt run, the whole block landed or as much as fits.
    CHECK(p.region.prompt_runs == 1 && p.region.boot_runs == 1 && p.region.queued() == 0);
    CHECK(p.region.landed >= 34 && p.region.landed <= 512);
    // The observer: the booter's lineage carried on, a generation down.
    const ProgramRec* child = obs.program(b.uid);
    const LineageRec* L = obs.lineage(booter);
    CHECK(child && child->origin == booter && child->depth == 1 && child->parent == booter);
    CHECK(L && L->alive == 1 && L->extinct_at == ~0ull && L->births == 2);
    CHECK(obs.cold_starts == 1 && obs.births_by_cause[static_cast<u8>(BirthCause::boot)] == 1);
    // The window the cold start fell in is marked, and turned over (the
    // booter was its one starter, and died; the boot was born), and no other.
    u32 marked = 0, others = 0;
    bool turned = false;
    for (usize i = 0; i < obs.windows.size(); ++i) {
        const TurnoverWindow& win = obs.windows[i];
        if (cs.tick >= win.start && cs.tick < win.start + 100) {
            marked += win.cold ? 1 : 0;
            turned = win.turned;
        } else {
            others += win.cold ? 1 : 0;
        }
    }
    const PlaceTurnover& T = obs.turnover[0];
    CHECK(marked == 1 && others == 0 && turned);
    CHECK(T.cold_windows == 1 && T.cold_turned == 1 && T.crowded_cold_turned == 0);
    printf("  boot: a booter lived through a 20-tick blackout from tick %llu: it ended (%s) and the cold start at "
           "tick %llu booted its 34 bytes (%u landed) into a copy born at tick %llu, cause boot, in its lineage, "
           "which wrote itself back; the window marked cold turned over\n",
           static_cast<unsigned long long>(d.start), cause_name(how), static_cast<unsigned long long>(cs.tick),
           p.region.landed, static_cast<unsigned long long>(b.tick));
}

// Done when (2): a block nobody wrote boots static. The cold start's fact
// has no writer, and the boot block waits with the cause "static" and no
// parent. On a one-core place, the core's next run lands it at its spot,
// the disk's static byte for byte, and runs it.
void test_static_boot() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "hw.cores.min = 1\nhw.cores.max = 1\n", &why));
    Place& p = w.place(0);
    Kept k;
    run_to(p, 20, &k, nullptr);
    CHECK(p.os.boot_writer() == 0);
    p.cold_start();
    CHECK(p.os.cold_starts.size() == 1 && p.os.cold_starts[0].writer == 0 && p.cold_written == 0);
    CHECK(p.region.queued() == 1 && p.region.queued_bytes() == 512);
    const Arrival a = p.region.waiting(0);
    CHECK(a.boot && a.len == 512 && a.cause == BirthCause::static_bytes && a.sender == 0);
    const u32 spot = p.region.state(0).spot;
    const u64 started = p.region.started;
    const u32 writes = p.mem.region_writes();
    step(p, &k, nullptr);
    CHECK(p.region.prompt_runs == 1 && p.region.boot_runs == 1 && p.region.queued() == 0);
    const u32 room = p.mem.region_size() - spot;
    CHECK(p.region.landed == (room < 512 ? room : 512));
    CHECK(p.region.started == started + 1 && p.region.state(0).started_at == spot);
    // Nothing but the landing wrote to the region this tick, so it holds the
    // disk's static where it landed, byte for byte.
    CHECK(p.mem.region_writes() == writes + 1);
    CHECK(memcmp(p.mem.bytes() + p.mem.region_base() + spot, p.os.disk(), p.region.landed) == 0);
    CHECK(p.cores[0].run_cause == BirthCause::static_bytes && p.cores[0].run_parent == 0);
    // Anything the run gave birth to is static's, with no parent.
    for (usize i = 0; i < k.births.size(); ++i)
        CHECK(k.births[i].cause != BirthCause::boot);
    printf("  boot: a boot block nobody wrote booted static: %u bytes of the disk's static landed at spot %u and "
           "ran, with no parent\n",
           p.region.landed, spot);
}

// Done when (3): a program bigger than the boot block boots through a
// second stage (10.5). With a 64-byte block, only the first 64 of its 96
// bytes land; its first stage DREADs the other 32 from the disk into the
// region after them and runs on into them. The copy it makes is all 96.
void test_second_stage() {
    make_staged();
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "disk.boot_block = 64\nregion.size = 4096\nmemory.size = 65536\n", &why));
    Place& p = w.place(0);
    const u32 t = place(p, kStaged, 96);
    const u64 writer = uid_of(p, t);
    Kept k;
    run_to(p, 10, &k, nullptr);
    CHECK(memcmp(p.os.disk(), kStaged, 96) == 0 && p.os.boot_writer() == writer);
    p.cold_start();
    p.os.clear_facts();
    CHECK(p.region.queued() == 1 && p.region.waiting(0).len == 64 && p.region.waiting(0).sender == writer);
    const u64 at = p.env.tick;
    while (!k.birth(BirthCause::boot) && p.env.tick < at + 20)
        step(p, &k, nullptr);
    CHECK(p.region.boot_runs == 1 && p.region.landed == 64);
    const BirthFact* found = k.birth(BirthCause::boot);
    CHECK(found != nullptr);
    if (!found)
        return;
    const BirthFact b = *found; // k's arrays grow as it runs on
    CHECK(b.parent_uid == writer && b.size == 96 && k.genome_is(b, kStaged, 96));
    // The copy runs, and writes all of itself back: the block's last writer
    // next. (Later, an idle run starting inside the program, still in the
    // region where it landed, may copy it again: a birth of static, which
    // writes itself back in turn.)
    const u64 born = p.env.tick;
    while (p.os.boot_writer() == writer && p.env.tick < born + 10)
        step(p, &k, nullptr);
    CHECK(p.os.boot_writer() == b.uid && memcmp(p.os.disk(), kStaged, 96) == 0);
    printf("  boot: a 96-byte program booted from a 64-byte block: its first stage DREAD the other 32 bytes and "
           "ran on into them; the copy born at tick %llu (after the cold start at %llu) is all 96 bytes\n",
           static_cast<unsigned long long>(b.tick), static_cast<unsigned long long>(at));
}

// A cold start on its own (Place::cold_start): every program and thread
// ends with the cause "cold start" and its memory is freed, its bytes left
// as they were; the disk requests waiting, the ready queues and every idle
// run go; the boot block waits at the head of port 0's queue. The queue,
// bounded in bytes, drops what it hasn't room for, and the next cold start
// empties it again.
void test_cold_start() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    const u32 free0 = p.mem.free_bytes();
    place(p, kSleeper, 6);
    const u32 tb = place(p, kBooter, 34);
    const u64 booter = uid_of(p, tb);
    place(p, kLax, 14);
    Kept k;
    run_to(p, 12, &k, nullptr);
    const u32 programs = p.os.live_programs();
    CHECK(programs > 3 && p.os.live_threads() >= programs && p.mem.free_bytes() < free0);
    CHECK(p.os.boot_writer() == booter);
    u8* before = static_cast<u8*>(calloc(p.mem.size(), 1));
    memcpy(before, p.mem.bytes(), p.mem.size());
    p.cold_start();
    CHECK(p.os.live_programs() == 0 && p.os.live_threads() == 0);
    CHECK(p.os.deaths.size() == programs);
    bool all_cold = true;
    for (usize i = 0; i < p.os.deaths.size(); ++i)
        all_cold = all_cold && p.os.deaths[i].cause == DeathCause::cold_start && p.os.deaths[i].tick == p.env.tick;
    CHECK(all_cold);
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        CHECK(p.os.thread(t).state == ThreadState::free);
    // Freed, every byte as it was.
    CHECK(p.mem.free_bytes() == free0);
    CHECK(memcmp(before, p.mem.bytes(), p.mem.size()) == 0);
    free(before);
    CHECK(p.os.pending_disk() == 0 && !p.os.any_ready(DeviceKind::p_core) && !p.os.any_ready(DeviceKind::e_core));
    for (u32 c = 0; c < p.core_count(); ++c)
        CHECK(p.region.state(c).phase == RunPhase::none && !p.cores[c].idle_run && p.cores[c].thread == 0);
    // The fact, and the boot block at the queue's head, its writer the parent to be.
    CHECK(p.os.cold_starts.size() == 1 && p.os.cold_starts[0].writer == booter &&
          p.os.cold_starts[0].tick == p.env.tick && p.os.cold_starts[0].place == p.id());
    CHECK(p.region.queued() == 1 && p.region.queued_bytes() == 512);
    CHECK(p.region.waiting(0).boot && p.region.waiting(0).cause == BirthCause::boot &&
          p.region.waiting(0).sender == booter && p.region.waiting(0).len == 512);
    // Port 0's queue holds 4,096 bytes: an arrival of 3,584 fits behind the
    // block; then nothing does, not a byte, and not a boot block at the head.
    u8 junk[3584];
    for (u32 i = 0; i < sizeof junk; ++i)
        junk[i] = static_cast<u8>(i * 7);
    // The arrivals' own counts (build-plan-gate3.md step 8) leave the boot block out.
    const u64 joined0 = p.region.arrival_joined, dropped0 = p.region.arrival_dropped;
    const u64 cleared0 = p.region.arrival_cleared;
    CHECK(p.region.arrive(junk, 3584, BirthCause::arrival, 7, false, false));
    CHECK(!p.region.arrive(junk, 1, BirthCause::arrival, 8, false, false) && p.region.dropped == 1);
    CHECK(!p.region.arrive(p.os.disk(), 512, BirthCause::boot, booter, true, true) && p.region.dropped == 2);
    CHECK(!p.region.arrive(junk, 0, BirthCause::arrival, 8, false, false) && p.region.dropped == 3);
    CHECK(p.region.queued() == 2 && p.region.queued_bytes() == 4096 && p.region.waiting(0).boot &&
          p.region.waiting(1).sender == 7 && p.region.waiting(1).at == 512 && p.region.waiting(1).len == 3584);
    CHECK(p.region.arrival_joined == joined0 + 1 && p.region.arrival_dropped == dropped0 + 2 &&
          p.region.arrivals_waiting() == 1);
    // A second cold start empties the queue (the old OS's) and queues the block alone.
    p.os.clear_facts();
    p.cold_start();
    CHECK(p.region.queued() == 1 && p.region.queued_bytes() == 512 && p.cold_starts == 2 && p.cold_written == 2);
    CHECK(p.region.arrival_cleared == cleared0 + 1 && p.region.arrivals_waiting() == 0);
    CHECK(p.os.deaths.size() == 0 && p.os.cold_starts.size() == 1);
    // An arrival behind it: the next two runs to start take them in turn.
    CHECK(p.region.arrive(junk, 100, BirthCause::arrival, 7, false, false));
    p.os.clear_facts();
    step(p, &k, nullptr);
    CHECK(p.core_count() < 2 || (p.region.prompt_runs == 2 && p.region.boot_runs == 1 && p.region.queued() == 0));
    printf("  boot: a cold start ended all %u programs, cause cold start, and freed their memory, every byte left "
           "as it was; the boot block waited at port 0's head, and a full queue dropped what came after\n",
           programs);
}

// The turnover measure marks each window with a cold start in it, and
// counts the counted ones that turned over, crowded or not.
void test_turnover_marks() {
    TurnoverSettings s;
    s.window = 100;
    s.members = 2;
    PlaceTurnover T;
    TurnoverWindow got;
    // [0, 100): 4 born.
    for (u32 i = 0; i < 4; ++i)
        T.birth();
    CHECK(T.end_tick(100, 4, false, s, 0, &got) && !got.cold);
    // [100, 200): a cold start ends all 4, and 2 are born of the boot; the
    // place is full as it ends.
    T.cold_start();
    for (u32 i = 0; i < 4; ++i)
        T.death(0, DeathCause::cold_start);
    T.birth();
    T.birth();
    CHECK(T.end_tick(200, 2, true, s, 0, &got) && got.cold && got.counted && got.turned && !got.crowded);
    CHECK(T.cold_windows == 1 && T.cold_turned == 1 && T.crowded_cold_turned == 0);
    CHECK(T.deaths_by_cause[static_cast<u8>(DeathCause::cold_start)] == 4);
    // [200, 300): crowded; another cold start ends both, and 1 is born.
    T.cold_start();
    T.death(150, DeathCause::cold_start);
    T.death(150, DeathCause::cold_start);
    T.birth();
    CHECK(T.end_tick(300, 1, false, s, 0, &got) && got.cold && got.crowded && got.turned);
    CHECK(T.cold_windows == 2 && T.cold_turned == 2 && T.crowded_cold_turned == 1);
    // [300, 400): one starter, too few to count: marked, but not counted.
    T.cold_start();
    CHECK(T.end_tick(400, 1, false, s, 0, &got) && got.cold && !got.counted && T.cold_windows == 2);
    // [400, 500): the mark doesn't carry over.
    T.birth();
    CHECK(T.end_tick(500, 2, false, s, 0, &got) && !got.cold);
}

// A boot's parent may be dead before the cold start: the place's observer
// keeps the record of a writer that died while it was the block's last, so
// the boot carries its lineage on, and brings it back from extinction.
void test_dead_writer() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    Observer obs;
    obs.init(small_windows());
    const u32 t = place(p, kBooter, 34);
    const u64 booter = uid_of(p, t);
    Kept k;
    run_to(p, 10, &k, &obs);
    CHECK(p.os.boot_writer() == booter && obs.program(booter) != nullptr);
    p.os.end_thread(t, DeathCause::exit);
    step(p, &k, &obs);
    const LineageRec* L = obs.lineage(booter);
    CHECK(obs.program(booter) == nullptr && L && L->alive == 0 && L->extinct_at != ~0ull &&
          L->remembered == 1); // the boot block names it: not let go
    p.cold_start();
    CHECK(p.os.deaths.size() == 0 && p.os.cold_starts.size() == 1 && p.os.cold_starts[0].writer == booter);
    const u64 at = p.env.tick;
    while (!k.birth(BirthCause::boot) && p.env.tick < at + 10)
        step(p, &k, &obs);
    const BirthFact* b = k.birth(BirthCause::boot);
    CHECK(b != nullptr);
    if (!b)
        return;
    CHECK(b->parent_uid == booter);
    const ProgramRec* child = obs.program(b->uid);
    L = obs.lineage(booter);
    CHECK(child && child->origin == booter && child->depth == 1);
    CHECK(L && L->alive == 1 && L->extinct_at == ~0ull && L->births == 2);
    CHECK(obs.cold_starts == 1 && obs.births_by_cause[static_cast<u8>(BirthCause::boot)] == 1);
    printf("  boot: a booter ended by hand before the cold start was its boot's parent all the same: its lineage, "
           "extinct, came back a generation down\n");
}

// The new state: a busy world with blackouts, a booter in every place, is
// the same on 1 and 3 threads; and resumed from a snapshot taken with two
// arrivals waiting in a place's port 0 (a cold start called between ticks,
// and one more by hand), the same as never stopped. And setup refuses a
// queue too small for the boot block.
void test_boot_state() {
    const char* busy = "world.places = 6\nhw.cores.max = 4\nsupply.dropouts = 1000\nsupply.dropout.min = 5\n"
                       "supply.dropout.max = 60\nsupply.blackout_share = 0.7\n";
    const u64 kTicks = 4000, kAt = 1500;
    u64 h[2] = {};
    u64 colds = 0, written = 0, boots = 0;
    ByteWriter out;
    TextBuf text;
    for (u32 run = 0; run < 2; ++run) {
        World w;
        Config cfg;
        TextBuf why;
        const bool built = build(w, cfg, busy, &why);
        CHECK(built);
        if (!built)
            return;
        for (u32 i = 0; i < w.place_count(); ++i) {
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
            place(w.place(i), kBooter, 34);
        }
        w.start_threads(run ? 3 : 1);
        while (w.tick_count() < kTicks) {
            if (w.tick_count() == kAt) {
                Place& q = w.place(1);
                q.cold_start();
                CHECK(q.region.arrive(kBooter, 34, BirthCause::arrival, 0, false, false));
                CHECK(q.region.queued() == 2);
                if (run == 0) {
                    cfg.write(text);
                    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
                    w.save(out);
                    snapshot_finish(out);
                }
            }
            w.tick();
        }
        h[run] = w.hash();
        if (run == 0)
            for (u32 i = 0; i < w.place_count(); ++i) {
                colds += w.place(i).cold_starts;
                written += w.place(i).cold_written;
                boots += w.place(i).region.boot_runs;
            }
    }
    CHECK_EQ_U64(h[0], h[1]);
    CHECK(colds > 6 && written > 1 && boots > 1);
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
    CHECK(b.tick_count() == kAt && b.place(1).region.queued() == 2 && b.place(1).region.queued_bytes() == 512 + 34);
    CHECK(b.place(1).region.waiting(0).boot && b.place(1).region.waiting(1).cause == BirthCause::arrival);
    b.start_threads(2);
    while (b.tick_count() < kTicks)
        b.tick();
    CHECK_EQ_U64(b.hash(), h[0]);
    // Setup refuses a queue too small for the boot block, or the whole disk if that's smaller.
    World x, y, z;
    Config cx, cy, cz;
    CHECK(!build(x, cx, "region.queue = 511\n", &why) && !cx.ok());
    CHECK(build(y, cy, "region.queue = 512\n", &why) && cy.ok());
    CHECK(build(z, cz, "disk.size = 256\nregion.queue = 256\n", &why) && cz.ok() && z.place(0).os.boot_size() == 256);
    printf("  boot: a busy world made %llu cold starts, %llu of them with a written block, and %llu boots: the same "
           "on 1 and 3 threads, and resumed with two arrivals waiting in port 0 at tick %llu\n",
           static_cast<unsigned long long>(colds), static_cast<unsigned long long>(written),
           static_cast<unsigned long long>(boots), static_cast<unsigned long long>(kAt));
}

} // namespace

void suite_boot() {
    test_blackout_boot();
    test_static_boot();
    test_second_stage();
    test_cold_start();
    test_turnover_marks();
    test_dead_writer();
    test_boot_state();
}
