// Cut-outs and the crash rule (design.md 4.4, 7.6, 9.5, appendix B;
// build-plan-gate2.md step 4): a device's states against appendix B's rule;
// a running replicator ending when its core cuts out while a sleeping
// program survives, and the pause switch keeping it; threads moving to a
// powered E core when the P cores go, the pinned ones too; each core's
// speed and what SENSE reads following the voltage tick by tick; the disk
// waiting while it's out, and its cut-out tearing a write in flight and
// failing a read; and the new state through threads and snapshots.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
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

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// Sleeps 120 ticks at a time, for ever.
const u8 kSleeper[6] = {encode(kMov, kImm), 120, encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};
// Counts in A as it runs: ADD # 1; JMP back.
const u8 kCounter[4] = {encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// Pins itself to the P cores (SETCORE 1), then counts.
const u8 kPinnedCounter[8] = {encode(kMov, kImm), 1, encode(kSys, kImm), kSetCore,
                              encode(kAdd, kImm), 1, encode(kJmp, kImm), static_cast<u8>(-2)};
// Reads SENSE over and over, device 0's speed into C and the idle cores into
// D, yielding (SLEEP -1) after each round. Its yields end turns part way
// through quanta, so the others' turns don't line up with the ticks: as with
// real programs, some thread is part way through a turn when a tick ends.
const u8 kSenser[23] = {encode(kMov, kImm), 0,      encode(kSto, kB),   encode(kMov, kImm), kDeviceSpeed,
                        encode(kSys, kImm), kSense, encode(kSto, kC),   encode(kMov, kImm), kIdleCores,
                        encode(kSys, kImm), kSense, encode(kSto, kD),   encode(kMov, kImm), kVoltage,
                        encode(kSys, kImm), kSense, encode(kMov, kImm), static_cast<u8>(-1),
                        encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-21)};
// DWRITE in a loop: its offset in D, buffer in B and length in C.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
// DREAD in a loop, each count it gets kept in D, the next read's offset: so
// D shows what the last read answered.
const u8 kReader[6] = {encode(kMov, kD), encode(kSys, kImm), kDread, encode(kSto, kD),
                       encode(kJmp, kImm), static_cast<u8>(-4)};

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

// A thread's registers, on its core or saved in its stack (A.3).
Regs regs(Place& p, u32 t) {
    const Thread& th = p.os.thread(t);
    if (th.core)
        return p.cores[th.core - 1].x.r;
    ExecContext x;
    p.os.load(t, x);
    return x.r;
}

void set_regs(Place& p, u32 t, const Regs& r) {
    ExecContext x;
    p.os.load(t, x);
    x.r.a = r.a;
    x.r.b = r.b;
    x.r.c = r.c;
    x.r.d = r.d;
    p.os.save(t, x);
}

// The first drop-out to start after tick 50, and at least 50 ticks after
// the one before it ended, so the place has settled first.
Dropout settled_dropout(Place& p) {
    Dropout first;
    first.length = 0;
    u64 last_end = 0;
    for (u64 j = 0; j < 100000 && first.length == 0; ++j) {
        Dropout d;
        if (!p.supply.dropout_in(j, &d))
            continue;
        if (d.start > 50 && d.start >= last_end + 50)
            first = d;
        last_end = d.start + d.length;
    }
    CHECK(first.length != 0);
    return first;
}

// Ticks a place on its own, so its facts stay for the test to read.
void run_to(Place& p, u64 tick) {
    while (p.env.tick < tick) {
        p.os.clear_facts();
        p.tick();
    }
}

// Appendix B's rule: on cuts out below the threshold; off stays off for the
// off-time and below the threshold plus the gap, and restarts at both.
void test_device_step() {
    PowerSettings s; // a 50 mV gap, 20 ticks off at least
    DeviceState d;
    CHECK(step_device(&d, 850, 850, s, 10) == PowerChange::none && d.on); // at the threshold: still on
    CHECK(step_device(&d, 849, 850, s, 11) == PowerChange::cut_out && !d.on && d.off_at == 11);
    CHECK(step_device(&d, 1000, 850, s, 30) == PowerChange::none && !d.on); // 19 ticks: too soon
    CHECK(step_device(&d, 899, 850, s, 31) == PowerChange::none && !d.on);  // 20 ticks, but under the gap
    CHECK(step_device(&d, 900, 850, s, 31) == PowerChange::restarted && d.on);
    CHECK(step_device(&d, 860, 850, s, 32) == PowerChange::none && d.on); // inside the gap, once on, it stays on
    // The off-time counts from the cut-out, not from the voltage's return.
    CHECK(step_device(&d, 0, 850, s, 40) == PowerChange::cut_out);
    CHECK(step_device(&d, 0, 850, s, 59) == PowerChange::none);
    CHECK(step_device(&d, 950, 850, s, 60) == PowerChange::restarted);
}

// Done when (1): a replicator running on the core when it cuts out ends,
// with the cause "cut-out", while a sleeping program survives the blackout
// and runs after it. With the pause switch on, the running one is paused
// instead, and survives too.
void crash_or_pause(bool pause) {
    World w;
    Config cfg;
    TextBuf why;
    char extra[512];
    // A drop-out to a fifth of capacity, as long as the off-time: the P core
    // cuts out at once and stays out to its end, while memory holds (a
    // blackout would cold-start the place, step 6, and end the sleeper too).
    snprintf(extra, sizeof extra,
             "hw.cores.min = 1\nhw.cores.max = 1\nhw.e_share = 0\nsupply.dropouts = 1000\n"
             "supply.dropout.min = 20\nsupply.dropout.max = 20\nsupply.blackout_share = 0\n"
             "supply.dropout.keep_min = 0.2\nsupply.dropout.keep_max = 0.2\nos.cutout_pause = %s\n",
             pause ? "true" : "false");
    CHECK(build(w, cfg, extra, &why));
    Place& p = w.place(0);
    place(p, kLax, 14, 64);
    const u32 sleeper = place(p, kSleeper, 6, 64);
    const u32 sleeper_holder = p.os.thread(sleeper).program;
    const u64 sleeper_uid = p.os.program(sleeper_holder).uid;
    const Dropout d = settled_dropout(p);
    run_to(p, d.start);
    // Just before: a replicator on the core, the sleeper off it (asleep, or
    // queued behind the replicators after waking).
    CHECK(p.cores[0].thread != 0 && p.cores[0].working);
    const u32 on = p.cores[0].thread - 1;
    const u32 on_id = p.os.thread(on).id;
    const u64 on_uid = p.os.program(p.os.thread(on).program).uid;
    CHECK(on != sleeper && p.os.thread(sleeper).core == 0);
    const ThreadState sleeper_was = p.os.thread(sleeper).state;
    const u32 threads = p.os.live_threads();
    p.os.clear_facts();
    p.tick(); // the drop-out's first tick
    CHECK(!p.cores[0].working && p.env.voltage_mv < 850);
    for (u32 k = 0; k < p.mem.device_count(); ++k)
        CHECK(p.memory_state[k].on); // memory holds
    const i32 dip_mv = p.env.voltage_mv;
    u32 cut = 0;
    for (usize i = 0; i < p.os.deaths.size(); ++i)
        cut += p.os.deaths[i].cause == DeathCause::cut_out && p.os.deaths[i].uid == on_uid ? 1 : 0;
    if (pause) {
        CHECK(p.os.deaths.size() == 0 && p.paused_threads == 1 && p.cut_threads == 0);
        CHECK(p.os.live_threads() == threads);
        CHECK(p.os.thread(on).state == ThreadState::ready && p.os.thread(on).id == on_id);
    } else {
        CHECK(p.os.deaths.size() == 1 && cut == 1 && p.cut_threads == 1);
        CHECK(p.os.live_threads() == threads - 1);
    }
    CHECK(p.os.program(sleeper_holder).alive && p.os.thread(sleeper).state == sleeper_was);
    // Through the drop-out: nothing runs, and nothing more dies.
    u32 deaths = 0;
    while (p.env.tick < d.start + d.length) {
        p.os.clear_facts();
        p.tick();
        deaths += static_cast<u32>(p.os.deaths.size());
        CHECK(!p.cores[0].working);
    }
    CHECK(deaths == 0 && p.cold_starts == 0);
    // Power back: the core restarts and runs again, and the sleeper, in its
    // time, runs and goes back to sleep.
    const u64 instr = p.sched.instructions;
    const u64 back = d.start + d.length;
    run_to(p, back + 1);
    CHECK(p.cores[0].working && p.sched.instructions > instr);
    while (p.env.tick < back + 2000 &&
           !(p.os.thread(sleeper).state == ThreadState::sleeping && p.os.thread(sleeper).wake_tick > back + 1))
        run_to(p, p.env.tick + 1);
    CHECK(p.os.program(sleeper_holder).alive && p.os.program(sleeper_holder).uid == sleeper_uid);
    CHECK(p.os.thread(sleeper).state == ThreadState::sleeping && p.os.thread(sleeper).wake_tick > back + 1);
    printf("  cutout: a drop-out to a fifth at tick %llu (%d mV) %s the replicator running on the core; the sleeper "
           "lived through it and was asleep again by tick %llu\n",
           static_cast<unsigned long long>(d.start), dip_mv, pause ? "paused" : "ended",
           static_cast<unsigned long long>(p.env.tick));
}

void test_crash_and_sleep() {
    crash_or_pause(false);
    crash_or_pause(true);
}

// Done when (2): when the P cores go, the queued threads move to the
// powered E core, and one pinned to the P cores falls back to it (9.5's rule
// for failed cores, applied to cut-outs). While the P core is out nothing
// runs on it, its speed is 0, and SENSE reads that live.
void test_move_to_e() {
    // A seed whose place has a P core then an E core.
    u64 seed = 0;
    for (u64 s = 1; s < 200 && !seed; ++s) {
        World w;
        Config cfg;
        TextBuf why;
        char extra[512];
        snprintf(extra, sizeof extra, "world.seed = %llu\nhw.cores.min = 2\nhw.cores.max = 2\nhw.e_share = 0.5\n",
                 static_cast<unsigned long long>(s));
        CHECK(build(w, cfg, extra, &why));
        if (w.place(0).cores[0].kind == DeviceKind::p_core && w.place(0).cores[1].kind == DeviceKind::e_core)
            seed = s;
    }
    CHECK(seed != 0);
    for (u32 pause = 0; pause < 2; ++pause) {
        World w;
        Config cfg;
        TextBuf why;
        char extra[512];
        snprintf(extra, sizeof extra,
                 "world.seed = %llu\nhw.cores.min = 2\nhw.cores.max = 2\nhw.e_share = 0.5\nsupply.dropouts = 1000\n"
                 "supply.dropout.min = 30\nsupply.dropout.max = 30\nsupply.blackout_share = 0\n"
                 "supply.dropout.keep_min = 0.2\nsupply.dropout.keep_max = 0.2\nos.cutout_pause = %s\n",
                 static_cast<unsigned long long>(seed), pause ? "true" : "false");
        CHECK(build(w, cfg, extra, &why));
        Place& p = w.place(0);
        u32 t[4];
        t[0] = place(p, kCounter, 4);
        t[1] = place(p, kCounter, 4);
        t[2] = place(p, kPinnedCounter, 8);
        t[3] = place(p, kSenser, 23);
        const Dropout d = settled_dropout(p);
        run_to(p, d.start);
        CHECK(p.os.thread(t[2]).pin == kPinP && p.cores[0].working && p.cores[1].working);
        CHECK(regs(p, t[3]).c == p.cores[0].speed && p.cores[0].speed == 16666); // 950 mV: 0.1 V over 850
        const u32 on_p = p.cores[0].thread; // record + 1, or 0
        CHECK(on_p != 0);                   // part way through its turn as the tick ended
        p.tick(); // the drop-out's first tick: the P core cuts out
        CHECK(!p.cores[0].working && p.cores[1].working && p.env.voltage_mv < 850);
        CHECK(p.cores[0].thread == 0 && p.cores[0].speed == 0 && p.env.devices[0].speed == 0);
        // Over the off-time, every thread that's left runs on the E core.
        u32 before[4], ran[4] = {};
        for (u32 k = 0; k < 4; ++k)
            before[k] = p.os.thread(t[k]).state == ThreadState::free ? 0 : regs(p, t[k]).a;
        bool p_idle = true;
        for (u32 k = 0; k < 18; ++k) {
            p.tick();
            p_idle = p_idle && !p.cores[0].working && p.cores[0].thread == 0;
        }
        u32 alive = 0;
        for (u32 k = 0; k < 4; ++k) {
            if (p.os.thread(t[k]).state == ThreadState::free)
                continue;
            ++alive;
            ran[k] = regs(p, t[k]).a != before[k] ? 1 : 0;
        }
        CHECK(p_idle);
        CHECK(alive == (pause ? 4u : 3u));
        CHECK(pause || p.os.thread(on_p - 1).state == ThreadState::free); // the one on the P core ended
        // The counters counted, the pinned one among them; the senser read the
        // P core's speed as 0, and no idle core (a core that's out isn't idle).
        for (u32 k = 0; k < 3; ++k)
            CHECK(p.os.thread(t[k]).state == ThreadState::free || ran[k] == 1);
        if (p.os.thread(t[3]).state != ThreadState::free) {
            const Regs r = regs(p, t[3]);
            CHECK(r.c == 0 && r.d == 0);
        }
        const bool pinned_left = p.os.thread(t[2]).state != ThreadState::free;
        CHECK(!pinned_left || p.os.thread(t[2]).pin == kPinP); // still pinned
        CHECK(pause || pinned_left); // here the one that ended was another
        printf("  cutout: the P core out from tick %llu, %s the thread on it; the %u left all ran on the E core, "
               "the one pinned to P among them\n",
               static_cast<unsigned long long>(d.start), pause ? "pausing" : "ending", alive);
    }
}

// Speeds by tick (7.5): a core runs speed x slice cycles in each quantum,
// at the speed its voltage gives this tick. A counter counts in proportion.
void test_speed_by_tick() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg,
                "hw.cores.min = 1\nhw.cores.max = 1\nhw.e_share = 0\nsupply.dropouts = 1000\n"
                "supply.dropout.min = 60\nsupply.dropout.max = 60\nsupply.blackout_share = 0\n"
                "supply.dropout.keep_min = 0.6\nsupply.dropout.keep_max = 0.6\n",
                &why));
    Place& p = w.place(0);
    const u32 t = place(p, kCounter, 4);
    const Dropout d = settled_dropout(p);
    run_to(p, d.start - 1);
    u32 a0 = regs(p, t).a;
    p.tick();
    const u32 full = regs(p, t).a - a0; // a tick at full capacity
    const i32 mv_full = p.env.voltage_mv;
    const u32 speed_full = p.cores[0].speed;
    run_to(p, d.start + 30); // settled in the drop-out
    a0 = regs(p, t).a;
    p.tick();
    const u32 low = regs(p, t).a - a0;
    const i32 mv_low = p.env.voltage_mv;
    const u32 speed_low = p.cores[0].speed;
    CHECK(speed_full == core_speed(p.power().p, mv_full) && speed_low == core_speed(p.power().p, mv_low));
    CHECK(mv_low < mv_full && speed_low < speed_full);
    CHECK(p.sched.quantum16(0) == static_cast<i64>(speed_low) * 16 / p.sched.settings().quanta_per_tick);
    CHECK(p.env.devices[0].speed == speed_low);
    const double want = static_cast<double>(speed_low) / speed_full, got = static_cast<double>(low) / full;
    CHECK(got > want - 0.01 && got < want + 0.01);
    printf("  cutout: at %d mV the P core runs %u cycles a tick and a counter counted %u; at %d mV, %u and %u "
           "(%.3f of it, against %.3f)\n",
           mv_full, speed_full, full, mv_low, speed_low, low, got, want);
}

// Done when (4), and the disk's wait (7.6): a place with an E core and a disk
// set to cut out above it (900 mV), so the disk goes while the core runs.
// - A write still in flight when the disk cuts out is torn: up to the point
//   drawn for the cut it's new, and from there it's what was there before.
// - A DWRITE made while the disk is out waits for it, and lands once it's
//   back.
// - A read in flight fails: its count is 0.
struct DiskWorld {
    World w;
    Config cfg;
    u32 t = 0, buf = 0;
    Dropout d;
    static const u32 kLen = 200, kOffset = 4096;

    bool init(const u8* code, u32 len, const char* more = "") {
        TextBuf why;
        char extra[640];
        snprintf(extra, sizeof extra,
                 "hw.cores.min = 1\nhw.cores.max = 1\nhw.e_share = 1\ndisk.latency = 1000\ndisk.threshold = 900\n"
                 "supply.dropouts = 1000\nsupply.dropout.min = 40\nsupply.dropout.max = 40\n"
                 "supply.blackout_share = 0\nsupply.dropout.keep_min = 0.2\nsupply.dropout.keep_max = 0.2\n%s",
                 more);
        if (!build(w, cfg, extra, &why))
            return false;
        Place& p = w.place(0);
        t = place(p, code, len, 512);
        buf = p.mem.record(p.mem.alloc_at(regs(p, t).ip)).base + 64; // in its own allocation, after its code
        Regs r;
        r.b = buf;
        r.c = kLen;
        r.d = kOffset;
        set_regs(p, t, r);
        d = settled_dropout(p);
        return true;
    }
    // The buffer filled with one byte value.
    void fill(u8 v) { memset(w.place(0).mem.bytes() + buf, v, kLen); }
};

void test_torn_write() {
    DiskWorld dw;
    CHECK(dw.init(kWriter, 5));
    Place& p = dw.w.place(0);
    // Each tick the writer writes the buffer, filled with the tick's number.
    while (p.env.tick + 1 < dw.d.start) {
        dw.fill(static_cast<u8>(p.env.tick));
        p.tick();
    }
    u8 old[DiskWorld::kLen], fresh[DiskWorld::kLen];
    memcpy(old, p.os.disk() + DiskWorld::kOffset, DiskWorld::kLen);
    dw.fill(static_cast<u8>(p.env.tick));
    p.tick(); // the last tick before the drop-out: its write lands at the barrier
    memcpy(fresh, p.os.disk() + DiskWorld::kOffset, DiskWorld::kLen);
    CHECK(p.os.in_flight_bytes() == DiskWorld::kLen && memcmp(old, fresh, DiskWorld::kLen) != 0);
    const u64 at = Rng(dw.w.seed(), 0, kPurposeTear, dw.d.start).below(DiskWorld::kLen);
    dw.fill(0xEE);
    p.tick(); // the drop-out: the disk cuts out, the E core runs on
    CHECK(!p.disk_state.on && p.cores[0].working && p.disk_cutouts == 1 && p.os.torn_writes == 1);
    const u8* disk = p.os.disk() + DiskWorld::kOffset;
    CHECK(memcmp(disk, fresh, at) == 0);                             // new up to the cut
    CHECK(memcmp(disk + at, old + at, DiskWorld::kLen - at) == 0); // old from it on
    // While the disk is out, the writer's next DWRITE waits for it.
    u32 waited = 0;
    u8 last = 0;
    while (!p.disk_state.on) {
        last = static_cast<u8>(p.env.tick);
        dw.fill(last);
        p.tick();
        if (!p.disk_state.on) {
            waited += p.os.thread(dw.t).state == ThreadState::waiting_disk && p.os.pending_disk() == 1 ? 1 : 0;
            CHECK(memcmp(disk + at, old + at, DiskWorld::kLen - at) == 0); // nothing landed
        }
    }
    // Back on: the waiting write lands at that tick's barrier, from the
    // buffer as it was then.
    CHECK(p.os.pending_disk() == 0 && disk[0] == last && disk[DiskWorld::kLen - 1] == last);
    CHECK(waited > 20);
    printf("  cutout: a 200-byte write in flight when the disk cut out at tick %llu was torn %llu bytes in; the "
           "next waited %u ticks for the disk, then landed\n",
           static_cast<unsigned long long>(dw.d.start), static_cast<unsigned long long>(at), waited);
}

void test_failed_read() {
    DiskWorld dw;
    CHECK(dw.init(kReader, 6));
    Place& p = dw.w.place(0);
    run_to(p, dw.d.start - 1);
    p.tick(); // its read lands at the barrier, the count in its saved A
    CHECK(p.os.in_flight_bytes() == DiskWorld::kLen);
    CHECK(p.os.thread(dw.t).state == ThreadState::ready && regs(p, dw.t).a == DiskWorld::kLen);
    CHECK(regs(p, dw.t).d == DiskWorld::kLen); // the read before answered in full too
    p.tick(); // the disk cuts out before the reader runs again
    CHECK(!p.disk_state.on && p.os.failed_reads == 1 && p.os.torn_writes == 0);
    // The reader ran in this tick, found 0 where its count was, kept it in D,
    // and read again: that read waits for the disk.
    CHECK(regs(p, dw.t).d == 0 && p.os.thread(dw.t).state == ThreadState::waiting_disk);
    printf("  cutout: a read in flight when the disk cut out failed: its reader found it answered 0\n");
}

// The new state (each device's state and off-time, disk requests waiting,
// I/O in flight) is in the hash and the snapshot: the disk world resumed
// from a snapshot taken with a write in flight, and from one taken while the
// writer waits on the disk, comes out the same as the run never stopped.
void test_cutout_state() {
    for (u32 when = 0; when < 2; ++when) {
        DiskWorld a;
        CHECK(a.init(kWriter, 5));
        const u64 stop = when == 0 ? a.d.start : a.d.start + 10; // in flight / waiting
        const u64 end = a.d.start + a.d.length + 40;
        a.w.start_threads(1);
        while (a.w.tick_count() < stop) {
            a.fill(static_cast<u8>(a.w.tick_count()));
            a.w.tick();
        }
        Place& pa = a.w.place(0);
        if (when == 0)
            CHECK(pa.os.in_flight_bytes() > 0);
        else
            CHECK(!pa.disk_state.on && pa.os.pending_disk() == 1);
        TextBuf text;
        a.cfg.write(text);
        ByteWriter out;
        snapshot_begin(a.w.seed(), a.w.tick_count(), text.view(), out);
        a.w.save(out);
        snapshot_finish(out);
        while (a.w.tick_count() < end) {
            a.fill(static_cast<u8>(a.w.tick_count()));
            a.w.tick();
        }
        SnapshotHeader h;
        TextBuf err;
        CHECK(snapshot_parse(out.data(), out.size(), &h, &err));
        Config cfg2;
        Config::ParseError pe;
        CHECK(cfg2.parse(h.config_text, &pe));
        World b;
        TextBuf why;
        CHECK(b.init(cfg2, &why));
        ByteReader in(out.data() + h.world_offset, h.world_size);
        CHECK(b.load(in));
        b.start_threads(2);
        const u32 buf = a.buf;
        while (b.tick_count() < end) {
            memset(b.place(0).mem.bytes() + buf, static_cast<u8>(b.tick_count()), DiskWorld::kLen);
            b.tick();
        }
        CHECK_EQ_U64(b.hash(), a.w.hash());
        CHECK(memcmp(b.place(0).os.disk(), pa.os.disk(), pa.os.disk_size()) == 0);
    }
    // And a busy world of replicators under a fast supply: the same on 1 and
    // 3 threads, cut-outs and all.
    u64 h[2] = {};
    u64 cuts = 0;
    for (u32 run = 0; run < 2; ++run) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg,
                    "world.places = 4\nhw.cores.max = 4\nsupply.night = 0.2\nsupply.day = 200\nsupply.swing = 0.25\n"
                    "supply.swing_period = 500\nsupply.dropouts = 1000\nsupply.dropout.min = 5\n"
                    "supply.dropout.max = 50\nnoise.p_floor = 1e-4\nnoise.e_floor = 1e-4\n",
                    &why));
        for (u32 i = 0; i < w.place_count(); ++i)
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
        w.start_threads(run ? 3 : 1);
        for (u32 t = 0; t < 600; ++t)
            w.tick();
        h[run] = w.hash();
        if (run == 0)
            for (u32 i = 0; i < w.place_count(); ++i)
                cuts += w.place(i).cutouts[0] + w.place(i).cutouts[1];
    }
    CHECK_EQ_U64(h[0], h[1]);
    CHECK(cuts > 0);
    printf("  cutout: resumed with a write in flight and with one waiting, the same as never stopped; %llu core "
           "cut-outs in a busy world, the same on 1 and 3 threads\n",
           static_cast<unsigned long long>(cuts));
}

// Setup refuses a disk range with no room for the restart gap, and builds
// the pause switch either way.
void test_cutout_checks() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(!build(w, cfg, "disk.threshold = 1280\n", &why) && !cfg.ok()); // 1,280 + 50 over 1,300
    World a;
    Config ca;
    CHECK(build(a, ca, "os.cutout_pause = true\n", &why) && a.place(0).os.settings().cutout_pause);
    World b;
    Config cb;
    CHECK(build(b, cb, "", &why) && !b.place(0).os.settings().cutout_pause);
}

} // namespace

void suite_cutout() {
    test_device_step();
    test_crash_and_sleep();
    test_move_to_e();
    test_speed_by_tick();
    test_torn_write();
    test_failed_read();
    test_cutout_state();
    test_cutout_checks();
}
