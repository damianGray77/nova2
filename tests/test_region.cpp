// The reset region's idle runs and the raw disk (nova2/region.h, nova2/os.h;
// design.md 9.3, 10.2, 10.3, appendix B; build-plan-gate1.md step 7): start
// spots that step through every byte, the registers at a start, the cap,
// halting and waking, a thread taking the core, a replicator found in the
// region becoming a program from static, and DREAD and DWRITE at the barrier.
#include <cstring>

#include "check.h"
#include "nova2/region.h"

using namespace nova2;

namespace {

constexpr u32 RUN = 1;
const u8 kFault = encode(kPush, kA); // an idle run has no stack: this faults

struct Place {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Os os;
    Scheduler sched;
    Region region;
    CostTable p = default_p_costs(), e = default_e_costs();

    explicit Place(u32 ncores = 1, RegionSettings rs = RegionSettings{}, u32 disk = 0) {
        static const MemDeviceSpec dev[] = {{32768, 4 * 16, 900}};
        MemorySpec ms;
        ms.page_size = 256;
        ms.region_size = 1024;
        ms.devices = dev;
        ms.device_count = 1;
        CHECK(m.init(ms, nullptr));
        cores[0].kind = DeviceKind::p_core;
        cores[1].kind = DeviceKind::e_core;
        cores[1].stock_speed = cores[1].speed = 18000;
        env.tick = 100;
        env.disk_size = disk;
        CHECK(os.init(&m, cores, ncores, &env, OsSettings{}, nullptr));
        CHECK(sched.init(&os, &m, cores, ncores, &env, SchedSettings{}, &p, &e, nullptr));
        TextBuf why;
        CHECK(region.init(&os, &m, cores, ncores, &env, rs, &p, &e, &why));
        sched.idle = region.hooks();
    }

    u8* reg() { return m.bytes() + m.region_base(); }

    void ticks(u32 n) {
        for (u32 i = 0; i < n; ++i) {
            sched.tick();
            os.barrier();
            ++env.tick;
        }
    }
};

// Start spots step by 13 through a 1 KB region, so 1,024 starts visit every
// byte once; the registers point at the spot.
void test_start_spots() {
    Place pl;
    memset(pl.reg(), kFault, 1024);
    u8 seen[1024] = {};
    for (u32 i = 0; i < 1024; ++i) {
        pl.region.run(0, 1);
        const IdleRun& r = pl.region.state(0);
        seen[r.started_at] += 1;
        CHECK(r.phase == RunPhase::halted);
        pl.m.note_region_change(); // something writes to the region: it may start again
    }
    u32 once = 0;
    for (u32 i = 0; i < 1024; ++i)
        once += seen[i] == 1 ? 1 : 0;
    CHECK(once == 1024 && pl.region.started == 1024 && pl.region.faulted == 1024);
    // The registers at a start: A to D and IP all at the spot.
    memset(pl.reg(), encode(kMov, kA), 1024);
    pl.m.note_region_change();
    pl.region.run(0, 1);
    const Regs& r = pl.cores[0].x.r;
    const u32 at = pl.m.region_base() + pl.region.state(0).started_at;
    CHECK(r.a == at && r.b == at && r.c == at && r.d == at && r.ip == at + 1);
    CHECK(pl.cores[0].idle_run && pl.cores[0].x.stack_hi == 0);
    // Two cores start half a region apart.
    Place two(2);
    CHECK(two.region.state(0).spot == 0 && two.region.state(1).spot == 512);
    // A step sharing a factor with the region's size is refused.
    Place bad;
    RegionSettings rs;
    rs.step = 16;
    TextBuf why;
    CHECK(!bad.region.init(&bad.os, &bad.m, bad.cores, 1, &bad.env, rs, &bad.p, &bad.e, &why));
}

// A run that never faults hits the cap and halts; the slow retry starts it again.
void test_cap_and_retry() {
    RegionSettings rs;
    rs.retry_ticks = 10;
    Place pl(1, rs);
    for (u32 i = 0; i < 1024; i += 2) { // JMP # 0 everywhere: the endless two-byte loop
        pl.reg()[i] = encode(kJmp, kImm);
        pl.reg()[i + 1] = 0;
    }
    pl.ticks(3); // 75,000 cycles: under the cap
    CHECK(pl.region.state(0).phase == RunPhase::running && pl.region.capped == 0);
    pl.ticks(2); // past 100,000
    CHECK(pl.region.state(0).phase == RunPhase::halted && pl.region.capped == 1);
    const u64 at = pl.region.state(0).halted_at;
    while (pl.env.tick < at + 10) // the last tick processed: halted_at + 9
        pl.ticks(1);
    CHECK(pl.region.state(0).phase == RunPhase::halted && pl.region.started == 1);
    pl.ticks(1);
    CHECK(pl.region.started == 2); // the retry
}

// A write to the region by another core's run wakes a halted core.
void test_wake_on_write() {
    Place pl(2);
    memset(pl.reg(), kFault, 1024);
    pl.reg()[512] = encode(kSto, kAtB); // core 1's first spot: writes into the region, then faults
    pl.sched.begin_tick();
    pl.sched.run_round(); // core 0 faults and halts; core 1 writes, faults and halts
    CHECK(pl.region.started == 2 && pl.region.state(0).phase == RunPhase::halted);
    pl.sched.run_round(); // core 1 first now, and stays halted (its own write); core 0 wakes
    CHECK(pl.region.started == 3 && pl.region.state(0).started_at == 13);
    pl.sched.run_round(); // nobody wrote since: both stay halted
    CHECK(pl.region.started == 3);
}

// A ready thread takes the core from an idle run, and the run's holdings go.
void test_thread_takes_the_core() {
    Place pl;
    memset(pl.reg(), kFault, 1024);
    pl.reg()[0] = encode(kSys, kImm); // ALLOC(A = B = C = the spot): a holding
    pl.reg()[1] = kAlloc;
    pl.reg()[2] = encode(kJmp, kImm);
    pl.reg()[3] = 0;
    pl.sched.begin_tick();
    pl.sched.run_round();
    CHECK(pl.cores[0].idle_run && pl.m.first_of(RUN) != 0);
    const u32 code = pl.m.alloc(2, 64, 2, 0);
    pl.m.bytes()[code] = encode(kJmp, kImm);
    pl.m.bytes()[code + 1] = 0;
    pl.os.place_by_hand(2, code, code, 0);
    pl.sched.run_round();
    CHECK(!pl.cores[0].idle_run && pl.cores[0].thread != 0 && pl.region.preempted == 1);
    CHECK(pl.m.first_of(RUN) == 0); // freed when the run ended
}

// A.5's replicator sitting in the region is found by an idle run: its copy
// becomes a program from static, and its line goes on by itself.
void test_found_replicator() {
    Place pl(2);
    memset(pl.reg(), kFault, 1024);
    static const u8 lax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                               encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                               encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
    memcpy(pl.reg(), lax, 14); // at core 0's first spot (by hand: a test, R7)
    pl.ticks(30);
    CHECK(pl.os.births.size() > 10);
    if (pl.os.births.size() > 1) {
        CHECK(pl.os.births[0].cause == BirthCause::static_bytes && pl.os.births[0].parent_uid == 0);
        CHECK(pl.os.births[0].genome_len == 14);
        CHECK(pl.os.births.back().cause == BirthCause::thread);
    }
    TextBuf why;
    CHECK(pl.m.check(&why));
    printf("  region: %u births in 30 ticks from one copy in the region\n", static_cast<u32>(pl.os.births.size()));
}

// Bytes that don't fault (MOV A) run off the end of the region, and fault there.
void test_runs_off_the_end() {
    Place pl;
    memset(pl.reg(), encode(kMov, kA), 1024);
    pl.ticks(1);
    CHECK(pl.region.faulted == 1 && pl.region.state(0).phase == RunPhase::halted);
    CHECK(pl.region.instructions == 1025); // 1,024 MOV A, then the fetch past the end
}

// DREAD and DWRITE complete at the barrier, in core order then request
// order, rechecking access; idle runs may DREAD but not DWRITE.
void test_disk() {
    Place pl(2, RegionSettings{}, 4096);
    // A thread's DWRITE: 8 bytes at disk offset 10, then it waits.
    const u8 code[] = {encode(kSys, kImm), kDwrite, encode(kJmp, kImm), 0};
    const u32 at = pl.m.alloc(2, 64, 2, 0);
    memcpy(pl.m.bytes() + at, code, sizeof code);
    memcpy(pl.m.bytes() + at + 32, "diskdata", 8);
    const u32 id = pl.os.place_by_hand(2, at, at, 0);
    u32 t = 0;
    while (pl.os.thread(t).id != id)
        ++t;
    u8* saved = pl.m.bytes() + pl.os.thread(t).stack;
    const u32 regs[3] = {4096 + 10, at + 32, 8}; // A (mod the disk's size), B, C
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k)
            saved[r * 4 + k] = static_cast<u8>(regs[r] >> (8 * k));
    memset(pl.reg(), kFault, 1024);
    pl.sched.begin_tick();
    pl.sched.run_round();
    CHECK(pl.os.thread(t).state == ThreadState::waiting_disk && pl.os.pending_disk() == 1);
    CHECK(memcmp(pl.os.disk() + 10, "diskdata", 8) != 0); // not before the barrier
    pl.os.barrier();
    CHECK(memcmp(pl.os.disk() + 10, "diskdata", 8) == 0);
    CHECK(pl.os.thread(t).state == ThreadState::ready && pl.os.thread(t).debt16 > 0);
    CHECK(saved[0] == 8); // A: the count done

    // Rechecked at the barrier: a read into a page it can't write stops there.
    Place q(1, RegionSettings{}, 4096);
    const u32 qa = q.m.alloc(2, 256, 2, 0); // a whole page
    CHECK(q.m.alloc(3, 256, 0, 0) == qa + 256); // the next page is someone else's
    q.m.bytes()[qa] = encode(kSys, kImm);
    q.m.bytes()[qa + 1] = kDread;
    const u32 qid = q.os.place_by_hand(2, qa, qa, 0);
    u32 qt = 0;
    while (q.os.thread(qt).id != qid)
        ++qt;
    u8* qs = q.m.bytes() + q.os.thread(qt).stack;
    const u32 qregs[3] = {0, qa + 252, 8}; // the last 4 bytes of its page, and 4 past it
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k)
            qs[r * 4 + k] = static_cast<u8>(qregs[r] >> (8 * k));
    memset(q.os.disk(), 0xAB, 16);
    memset(q.reg(), kFault, 1024);
    q.sched.begin_tick();
    q.sched.run_round();
    q.os.barrier();
    CHECK(qs[0] == 4 && q.m.bytes()[qa + 255] == 0xAB);

    // An idle run's DWRITE fails at once; its DREAD waits, then fills the region.
    Place r(1, RegionSettings{}, 4096);
    memset(r.reg(), kFault, 1024);
    r.reg()[0] = encode(kSys, kImm);
    r.reg()[1] = kDwrite;
    r.reg()[2] = encode(kSys, kImm);
    r.reg()[3] = kDread; // A = B = C = the spot: offset (mod 4096), the region, length
    memset(r.os.disk(), encode(kMov, kA), 4096);
    r.sched.begin_tick();
    r.sched.run_round();
    CHECK(r.cores[0].disk_wait && r.os.pending_disk() == 1);
    const u32 writes = r.m.region_writes();
    r.os.barrier();
    CHECK(!r.cores[0].disk_wait && r.m.region_writes() == writes + 1);
    CHECK(r.reg()[0] == encode(kMov, kA) && r.cores[0].x.r.a > 0);

    // Two DWRITEs to one offset from two cores: core order decides, not who
    // asked first. A first round with nothing to run puts core 1 first in
    // the next, so core 1 asks first ('A') and core 0 second ('B'); core 1's
    // write still lands last.
    Place w(2, RegionSettings{}, 4096);
    memset(w.reg(), kFault, 1024);
    w.sched.begin_tick();
    w.sched.run_round();
    u32 ids[2], ts[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        const u32 a = w.m.alloc(2, 64, 2, 0);
        w.m.bytes()[a] = encode(kSys, kImm);
        w.m.bytes()[a + 1] = kDwrite;
        w.m.bytes()[a + 32] = static_cast<u8>('A' + i);
        ids[i] = w.os.place_by_hand(2, a, a, 0);
        while (w.os.thread(ts[i]).id != ids[i])
            ++ts[i];
        u8* s = w.m.bytes() + w.os.thread(ts[i]).stack;
        const u32 rr[3] = {0, a + 32, 1};
        for (int x = 0; x < 3; ++x)
            for (int k = 0; k < 4; ++k)
                s[x * 4 + k] = static_cast<u8>(rr[x] >> (8 * k));
    }
    w.sched.run_round(); // core 1 takes the first thread ('A'), then core 0 the second ('B')
    w.os.barrier();
    CHECK(w.os.disk()[0] == 'A'); // core 0's 'B' first, then core 1's 'A'
}

} // namespace

void suite_region() {
    test_start_spots();
    test_cap_and_retry();
    test_wake_on_write();
    test_thread_takes_the_core();
    test_found_replicator();
    test_runs_off_the_end();
    test_disk();
}
