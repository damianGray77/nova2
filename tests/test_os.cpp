// The OS (nova2/os.h; design.md 9.1-9.5, appendix A.3-A.5;
// build-plan-gate1.md step 5): the traced replicators of A.5, register by
// register and child included, on a lax and a strict place; the
// out-of-memory death; SPAWN's thread-or-gift rule; every call's argument
// edges (A.4); fault handlers; and what idle runs may and may not do.
//
// Hand-placed programs are allowed here (R7): these check mechanics, never
// origins.
#include <cstring>
#include <intrin.h> // _umul128

#include "check.h"
#include "nova2/archive.h"
#include "nova2/io.h"
#include "nova2/os.h"

using namespace nova2;

namespace {

constexpr u32 RUN = 1; // the idle run of core 0, as a holder

struct Rig {
    Memory m;
    Core cores[2];
    PlaceEnv env;
    Os os;
    OsSettings s;
    CostTable costs = default_p_costs();

    explicit Rig(u32 page = 256, bool strict = false, u32 mem = 16384, DeviceKind second = DeviceKind::e_core,
                 const OsSettings* with = nullptr) {
        if (with)
            s = *with;
        static const MemDeviceSpec dev16[] = {{16384, 4 * 16, 900}};
        static const MemDeviceSpec dev4[] = {{4096, 4 * 16, 900}};
        MemorySpec ms;
        ms.page_size = page;
        ms.strict = strict;
        ms.region_size = page;
        ms.devices = mem == 16384 ? dev16 : dev4;
        ms.device_count = 1;
        CHECK(m.init(ms, nullptr));
        cores[0].kind = DeviceKind::p_core;
        cores[1].kind = second;
        env.place_id = 7;
        env.tick = 100;
        SenseDevice d;
        d.kind = DeviceKind::p_core;
        d.speed = 25000;
        d.threshold_mv = 800;
        d.memory_index = 0;
        d.size = 0;
        env.devices.push(d);
        d.kind = second;
        d.speed = 18000;
        d.threshold_mv = 650;
        env.devices.push(d);
        d.kind = DeviceKind::memory;
        d.speed = 4;
        d.threshold_mv = 600;
        env.devices.push(d);
        d.kind = DeviceKind::disk;
        d.speed = 900;
        d.threshold_mv = 700;
        d.size = 1 << 20;
        env.devices.push(d);
        TextBuf why;
        CHECK(os.init(&m, cores, 2, &env, s, &why));
    }

    // The record index of the thread with this id.
    u32 index_of(u32 id) const {
        for (u32 t = 0; t < os.thread_capacity(); ++t)
            if (os.thread(t).state != ThreadState::free && os.thread(t).id == id)
                return t;
        return ~0u;
    }

    // Places code by hand as a new program and puts its thread on a core.
    u32 place(const u8* code, u32 n, u32 core, u32 perm = 2, u32 size = 0) {
        const u32 at = m.alloc(RUN, size ? size : n, perm, 0);
        CHECK(at != 0);
        memcpy(m.bytes() + at, code, n);
        const u32 id = os.place_by_hand(RUN, at, at, 0);
        CHECK(id != 0);
        os.put_on(index_of(id), core);
        return at;
    }

    // One instruction on a core, with SYS and faults handled as the scheduler will.
    Step run(u32 core) {
        Core& c = cores[core];
        const u32 ip = c.x.r.ip;
        const Step st = step(m, c.x, costs);
        if (st.kind == StepKind::sys)
            os.call(core, st.sys_value, ip);
        else if (st.kind != StepKind::done)
            os.fault(core, st.kind, st.length);
        return st;
    }

    // Puts an idle run on a core, starting at a spot in the reset region.
    void idle_run(u32 core, u32 spot) {
        Core& c = cores[core];
        c.idle_run = true;
        c.thread = 0;
        c.x = ExecContext{};
        c.x.holder = core + 1;
        c.x.r.a = c.x.r.b = c.x.r.c = c.x.r.d = c.x.r.ip = spot;
    }

    // Executes one call directly for whatever is on the core.
    u32 sys(u32 core, u8 call, u32 a = 0, u32 b = 0, u32 c = 0, u32 d = 0) {
        Regs& r = cores[core].x.r;
        r.a = a;
        r.b = b;
        r.c = c;
        r.d = d;
        os.call(core, call, r.ip);
        return r.a;
    }
};

// A.5's replicators, under the conventions of the 2026-11-13 review: a new
// thread starts with B, C and D at its start (A.3), and ALLOC's result lands
// in D as well as A (A.4).
//
// Lax, 14 bytes: MOV # 14; STO B (B = 14, which is 2 mod 3: "both"); SYS #
// ALLOC (A = D = N); MOV C; STO B (B = S, its start); CPY # 14; MOV D; SYS #
// SPAWN; JMP # -12.
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

// Strict with 64-byte pages, 20 bytes: a 63-byte allocation takes a page's
// only slot, and 63 is 0 mod 3, data. Copied, made executable with PROTECT,
// spawned.
const u8 kStrictCode[20] = {encode(kMov, kImm), 63, encode(kSto, kB), encode(kSys, kImm), kAlloc,
                            encode(kMov, kC), encode(kSto, kB), encode(kCpy, kImm), 20, encode(kMov, kImm), 1,
                            encode(kSto, kB), encode(kMov, kD), encode(kSys, kImm), kProtect, encode(kMov, kD),
                            encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-18)};

// A.5's 14-byte replicator on a lax place, register by register.
void test_lax_replicator() {
    Rig g;
    const u32 S = g.place(kLax, 14, 0);
    const Program& parent = g.os.program(g.cores[0].x.holder);
    Regs& r = g.cores[0].x.r;
    CHECK(r.a == 0 && r.b == S && r.c == S && r.d == S && r.ip == S); // A.3's new thread
    g.os.clear_facts();

    g.run(0);
    CHECK(r.a == 14); // MOV # 14
    g.run(0);
    CHECK(r.b == 14); // STO B: 2 mod 3, "both"
    g.run(0);         // SYS # ALLOC: size 14, both, device C (clamped to one that exists)
    const u32 N = r.a;
    CHECK(N != 0 && N != S && r.d == N && g.m.record(g.m.alloc_at(N)).holder == g.cores[0].x.holder);
    g.run(0);
    CHECK(r.a == S); // MOV C
    g.run(0);
    CHECK(r.b == S); // STO B
    g.run(0);        // CPY # 14, from B = S to D = N
    CHECK(memcmp(g.m.bytes() + N, kLax, 14) == 0);
    g.run(0);
    CHECK(r.a == N); // MOV D
    g.run(0);        // SYS # SPAWN: N is outside the allocation holding IP, so it's a gift
    CHECK(r.a != 0);
    const u32 child_t = g.index_of(r.a);
    CHECK(child_t != ~0u);
    const u32 child = g.os.thread(child_t).program;
    CHECK(child != g.cores[0].x.holder && g.m.record(g.m.alloc_at(N)).holder == child);
    CHECK(g.os.live_programs() == 2);
    // The birth fact: parent, cause, and the 14 bytes the parent wrote as the genome.
    CHECK(g.os.births.size() == 1);
    if (g.os.births.size() == 1) {
        const BirthFact& f = g.os.births[0];
        CHECK(f.parent_uid == parent.uid && f.cause == BirthCause::thread && f.base == N && f.size == 14);
        CHECK(f.wlo == 0 && f.whi == 14 && f.genome_len == 14);
        CHECK(memcmp(g.os.fact_bytes.data() + f.genome_at, kLax, 14) == 0);
    }
    g.run(0);
    CHECK(r.ip == S); // JMP # -12: back to offset 0

    // The child: A = SPAWN's starting value (the parent's B, S); B, C, D and
    // IP = N; SP at the top of its own stack.
    g.os.put_on(child_t, 1);
    Regs& cr = g.cores[1].x.r;
    const u32 stack = g.os.thread(child_t).stack;
    CHECK(cr.a == S && cr.b == N && cr.c == N && cr.d == N && cr.ip == N && cr.sp == stack + 64);
    CHECK(g.m.record(g.m.alloc_at(stack)).holder == child && g.m.device_of(stack) == g.m.device_of(N));
    for (int i = 0; i < 8; ++i) // the same sequence at its own address, on to its own SPAWN
        g.run(1);
    CHECK(g.os.live_programs() == 3 && g.os.births.size() == 2);
    if (g.os.births.size() == 2)
        CHECK(g.os.births[1].genome_hash == g.os.births[0].genome_hash); // a faithful copy
    TextBuf why;
    CHECK(g.m.check(&why));
}

// A.5's 20-byte replicator on a strict place with 64-byte pages.
void test_strict_replicator() {
    Rig g(64, true);
    // By hand: a whole page of data, written, then made executable.
    const u32 S = g.m.alloc(RUN, 64, 0, 0);
    memcpy(g.m.bytes() + S, kStrictCode, 20);
    CHECK(g.m.protect(RUN, S, 1));
    const u32 id = g.os.place_by_hand(RUN, S, S, 0);
    CHECK(id != 0);
    g.os.put_on(g.index_of(id), 0);
    Regs& r = g.cores[0].x.r;
    g.os.clear_facts();
    g.run(0);
    g.run(0); // MOV # 63, STO B
    CHECK(r.a == 63 && r.b == 63 && r.c == S && r.d == S);
    g.run(0); // ALLOC: a page's only slot, data
    const u32 N = r.a;
    CHECK(N != 0 && N % 64 == 0 && r.d == N && g.m.page(g.m.page_of(N)).perm == Perm::data);
    for (int i = 0; i < 6; ++i)
        g.run(0); // MOV C, STO B, CPY, MOV # 1, STO B, MOV D
    CHECK(memcmp(g.m.bytes() + N, kStrictCode, 20) == 0 && r.a == N && r.b == 1);
    g.run(0); // PROTECT: the program holds the whole page, so it works
    CHECK(r.a == 1 && g.m.page(g.m.page_of(N)).perm == Perm::exec);
    g.run(0);
    g.run(0); // MOV D, SPAWN
    CHECK(r.a != 0 && g.os.live_programs() == 2 && g.os.births.size() == 1);
    if (g.os.births.size() == 1)
        CHECK(g.os.births[0].genome_len == 20);
    g.run(0);
    CHECK(r.ip == S);
    // On a strict place "both" fails, so the lax replicator can't copy itself here.
    CHECK(g.m.alloc(RUN, 14, 2, 0) == 0);
}

// When memory runs out: ALLOC gives 0 in A and D, CPY writes to address 0, the
// only thread faults, and the program dies (A.5).
void test_out_of_memory_death() {
    Rig g(256, false, 4096);
    const u32 S = g.place(kLax, 14, 0);
    while (g.m.alloc(2, 14, 2, 0)) // another idle run takes everything left
        ;
    const u32 me = g.cores[0].x.holder;
    g.os.clear_facts();
    for (int i = 0; i < 5; ++i) // MOV # 14, STO B, ALLOC (giving 0), MOV C, STO B
        g.run(0);
    CHECK(g.cores[0].x.r.d == 0);
    const Step s = g.run(0); // CPY into address 0
    CHECK(s.kind == StepKind::fault_memory);
    CHECK(g.os.live_programs() == 0 && g.cores[0].thread == 0);
    CHECK(g.os.deaths.size() == 1 && g.os.deaths[0].cause == DeathCause::fault);
    // Its allocations are freed, its bytes kept: a corpse.
    CHECK(g.m.alloc_at(S) == 0 && memcmp(g.m.bytes() + S, kLax, 14) == 0);
    CHECK(g.m.first_of(me) == 0);
}

// SPAWN: a thread inside its own code (or any allocation one of its threads
// runs in), a gift anywhere else it holds; never someone else's, never data.
void test_spawn_rules() {
    Rig g;
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    g.os.clear_facts();
    // Into its own code: another thread of the same program, no birth.
    const u32 t2 = g.sys(0, kSpawn, code, 5);
    CHECK(t2 != 0 && g.os.program(me).threads == 2 && g.os.births.size() == 0);
    CHECK(g.os.thread(g.index_of(t2)).program == me);
    // Into an allocation another of its threads runs in: a thread too.
    const u32 other = g.sys(0, kAlloc, 64, 2, 0);
    g.os.thread(g.index_of(t2)); // t2 is off-core; its saved IP says where it runs
    u8* saved = g.m.bytes() + g.os.thread(g.index_of(t2)).stack + 16;
    saved[0] = static_cast<u8>(other);
    saved[1] = static_cast<u8>(other >> 8);
    saved[2] = saved[3] = 0;
    CHECK(g.sys(0, kSpawn, other + 4, 0) != 0 && g.os.births.size() == 0 && g.os.program(me).threads == 3);
    // Others' allocations can't be given. (A data page can't start a thread,
    // but on a lax place data is "both", 9.3: see the strict place below.)
    const u32 theirs = g.m.alloc(2, 64, 2, 0);
    CHECK(g.sys(0, kSpawn, theirs, 0) == 0);
    CHECK(g.sys(0, kSpawn, 0, 0) == 0);
    // Anything else it holds is a gift.
    const u32 gift = g.sys(0, kAlloc, 30, 1, 0);
    CHECK(g.sys(0, kSpawn, gift + 3, 9) != 0 && g.os.births.size() == 1);
    CHECK(g.m.record(g.m.alloc_at(gift)).holder != me);
    // If the new thread's stack can't be had, nothing is given.
    Rig f(256, false, 4096);
    const u32 c2 = f.place(loop, 2, 0, 2, 64);
    (void)c2;
    const u32 g2 = f.sys(0, kAlloc, 30, 1, 0);
    while (f.m.alloc(2, 64, 0, 0)) // no room left for a 64-byte stack
        ;
    CHECK(f.sys(0, kSpawn, g2, 0) == 0 && f.m.record(f.m.alloc_at(g2)).holder == f.cores[0].x.holder);
    // A thread that ends while its program lives on gives its stack back.
    const u32 t2i = g.index_of(t2);
    const u32 st = g.os.thread(t2i).stack;
    g.os.end_thread(t2i, DeathCause::exit);
    CHECK(g.m.alloc_at(st) == 0 && g.os.program(me).alive);
    TextBuf why;
    CHECK(g.m.check(&why));
    // On a strict place a data page can't start a thread, or hold a handler.
    Rig sr(64, true);
    sr.place(loop, 2, 0, 1, 64);
    const u32 sdata = sr.sys(0, kAlloc, 64, 0, 0);
    CHECK(sdata != 0 && sr.m.page(sr.m.page_of(sdata)).perm == Perm::data);
    CHECK(sr.sys(0, kSpawn, sdata, 0) == 0 && sr.sys(0, kSetHandler, 0, sdata) == 0);
}

// A thread run past the end of its allocation, on the rest of a lax page it
// may use by holding that allocation, still runs on it: SPAWN there starts a
// thread, never a gift (R15; the hand-overs, build-plan-gate1.md 11). Running
// inside another allocation of its own, it doesn't.
void test_spawn_past_its_end() {
    Rig g(1024, false);
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = g.place(loop, 2, 0, 2, 16);
    const u32 me = g.cores[0].x.holder;
    g.os.clear_facts();
    u32 gap = 0;
    for (u32 a = code + 16; g.m.page_of(a) == g.m.page_of(code); ++a)
        if (!g.m.alloc_at(a)) {
            gap = a;
            break;
        }
    CHECK(gap != 0);
    Regs& r = g.cores[0].x.r;
    // In a gap of the page: a thread, no birth, and it keeps its code.
    r.ip = gap;
    const u32 t2 = g.sys(0, kSpawn, code, 0);
    CHECK(t2 != 0 && g.os.births.size() == 0 && g.os.program(me).threads == 2);
    CHECK(g.m.record(g.m.alloc_at(code)).holder == me);
    g.os.end_thread(g.index_of(t2), DeathCause::exit); // its saved IP is in the code
    // In someone else's allocation in the page: the same.
    const u32 theirs = g.m.alloc(2, 16, 2, 0);
    CHECK(theirs != 0 && g.m.page_of(theirs) == g.m.page_of(code));
    r.ip = theirs + 1;
    const u32 t3 = g.sys(0, kSpawn, code, 0);
    CHECK(t3 != 0 && g.os.births.size() == 0);
    g.os.end_thread(g.index_of(t3), DeathCause::exit);
    // In another allocation of its own: the code isn't in use, so it's a gift.
    const u32 mine = g.sys(0, kAlloc, 16, 2, 0);
    CHECK(mine != 0 && g.m.page_of(mine) == g.m.page_of(code));
    r.ip = mine + 1;
    CHECK(g.sys(0, kSpawn, code, 0) != 0 && g.os.births.size() == 1);
    CHECK(g.m.record(g.m.alloc_at(code)).holder != me);
    TextBuf why;
    CHECK(g.m.check(&why));
}

void test_call_edges() {
    Rig g;
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    // The numbering: 18 slots, the last two a second ALLOC and SPAWN; s is read unsigned.
    CHECK(g.os.call_of(16) == kAlloc && g.os.call_of(17) == kSpawn && g.os.call_of(18) == kAlloc);
    CHECK(g.os.call_of(-1) == kSpawn); // 4294967295 mod 18 = 3
    CHECK(g.os.call_of(static_cast<i32>(0x80000000u)) == 2147483648u % 18);
    // ALLOC: 0 and too big fail; the permission is B mod 3; the device clamps.
    CHECK(g.sys(0, kAlloc, 0, 0, 0) == 0 && g.sys(0, kAlloc, 4097, 0, 0) == 0);
    const u32 p5 = g.sys(0, kAlloc, 20, 5, 0); // 5 mod 3 = 2: both
    CHECK(p5 && g.m.page(g.m.page_of(p5)).perm == Perm::both);
    // mod 3 = 0: data, which a lax place makes "both" (9.3); the device clamps.
    const u32 pm = g.sys(0, kAlloc, 20, 0xFFFFFFFFu, 0x80000000u);
    CHECK(pm && g.m.page(g.m.page_of(pm)).perm == Perm::both);
    // FREE: only an allocation's start.
    CHECK(g.sys(0, kFree, pm + 1) == 0 && g.sys(0, kFree, pm) == 1 && g.sys(0, kFree, pm) == 0);
    // PROTECT: B mod 3.
    CHECK(g.sys(0, kProtect, p5, 4) == 1 && g.m.page(g.m.page_of(p5)).perm == Perm::exec);
    // SETCLOCK clamps to its range and says what it applied.
    CHECK(g.sys(0, kSetClock, 12) == 12 && g.sys(0, kSetClock, 100) == 15);
    CHECK(g.sys(0, kSetClock, static_cast<u32>(-5)) == 5 && g.os.thread(g.cores[0].thread - 1).clock == 5);
    // SETCORE: A mod 3; it fails where no working core of that kind exists.
    CHECK(g.sys(0, kSetCore, 4) == 1 && g.os.thread(g.cores[0].thread - 1).pin == kPinP);
    CHECK(g.sys(0, kSetCore, 2) == 1);
    Rig pp(256, false, 16384, DeviceKind::p_core);
    pp.place(loop, 2, 0, 2, 64);
    CHECK(pp.sys(0, kSetCore, 2) == 0 && pp.sys(0, kSetCore, 0) == 1);
    // SETHANDLER: kind A mod 4; the handler must be runnable and the program's
    // (a strict place's data page: test_spawn_rules); port 0 fails; a timer
    // period of 0 clears.
    const u32 h = g.sys(0, kAlloc, 16, 1, 0);
    const u32 data = g.sys(0, kAlloc, 16, 0, 0); // a buffer, for the calls below
    const u32 others = g.m.alloc(2, 256, 1, 0); // a whole page, so none of it is the program's
    Thread& th = g.os.thread(g.cores[0].thread - 1);
    CHECK(g.sys(0, kSetHandler, 4, h) == 1 && th.handler[kOnFault] == h);
    CHECK(g.sys(0, kSetHandler, 0, others) == 0);
    CHECK(g.sys(0, kSetHandler, 1, h, 16) == 0);                            // port 16 mod 16 = 0
    CHECK(g.sys(0, kSetHandler, 1, h, 3) == 1 && th.data_port == 3);
    CHECK(g.sys(0, kSetHandler, 2, h, 10) == 1 && th.timer_next == g.env.tick + 10);
    CHECK(g.sys(0, kSetHandler, 2, h, 0) == 1 && th.handler[kOnTimer] == 0 && th.timer_next == kNever);
    CHECK(g.sys(0, kSetHandler, 3, h) == 1 && th.handler[kOnLowVoltage] == h);
    CHECK(g.sys(0, kSetHandler, 0, 0) == 1 && th.handler[kOnFault] == 0); // 0 clears
    // HRET outside a handler does nothing.
    const Regs before = g.cores[0].x.r;
    g.os.call(0, kHret, before.ip);
    CHECK(memcmp(&before, &g.cores[0].x.r, sizeof before) == 0);
    // SENSE: every reading, A mod 21, indices mod the count, empty lists read 0.
    CHECK(g.sys(0, kSense, kTemperature) == 250 && g.sys(0, kSense, kVoltage) == 1000);
    CHECK(g.sys(0, kSense, kTick) == 100 && g.sys(0, kSense, kPlaceId) == 7);
    CHECK(g.sys(0, kSense, kReadingCount + kPlaceId) == 7); // A mod 21
    CHECK(g.sys(0, kSense, kIdleCores) == 1 && g.sys(0, kSense, kFreeMemory) == g.m.free_bytes());
    CHECK(g.sys(0, kSense, kPageSize) == 256 && g.sys(0, kSense, kStrict) == 0);
    CHECK(g.sys(0, kSense, kDeviceCount) == 4 && g.sys(0, kSense, kNeighbourCount) == 0);
    CHECK(g.sys(0, kSense, kDeviceKind, 1) == static_cast<u32>(DeviceKind::e_core));
    CHECK(g.sys(0, kSense, kDeviceKind, 5) == static_cast<u32>(DeviceKind::e_core)); // 5 mod 4
    CHECK(g.sys(0, kSense, kDeviceSpeed, 0) == 25000 && g.sys(0, kSense, kDeviceThreshold, 2) == 600);
    CHECK(g.sys(0, kSense, kDeviceFree, 2) == g.m.device_free(0) && g.sys(0, kSense, kDeviceFree, 3) == (1u << 20));
    CHECK(g.sys(0, kSense, kNeighbourId, 3) == 0 && g.sys(0, kSense, kPortBytes, 4) == 0);
    // SEND and PING before the network; DREAD and DWRITE before the disk.
    CHECK(g.sys(0, kSend, 1, 2, h, 4) == 0 && g.sys(0, kPing, 3, 5) == static_cast<u32>(-1));
    CHECK(g.sys(0, kDread, 0, data, 8) == 0 && g.sys(0, kDwrite, 0, data, 8) == 0);
    // RECV: port 0 and a zero length fail; a poll finds nothing.
    CHECK(g.sys(0, kRecv, 0, data, 8, 0) == 0 && g.cores[0].x.r.b == static_cast<u32>(-1));
    CHECK(g.sys(0, kRecv, 3, data, 0, 0) == 0);
    CHECK(g.sys(0, kRecv, 3, data, 8, 0) == 0 && g.cores[0].x.r.b == static_cast<u32>(-1));
    // Costs: the call's whole cost, slower on an E core.
    g.cores[0].x.r.a = 4;
    CHECK(g.os.call(0, kSense, 0).cost16 == 150 * 16);
    (void)me;
}

void test_waits() {
    Rig g;
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    g.place(loop, 2, 0, 2, 64);
    u32 t = g.cores[0].thread - 1;
    g.sys(0, kSleep, 5);
    CHECK(g.os.thread(t).state == ThreadState::sleeping && g.os.thread(t).wake_tick == 105 && g.cores[0].thread == 0);
    g.os.put_on(t, 0);
    g.sys(0, kSleep, 0);
    CHECK(g.os.thread(t).state == ThreadState::waiting_interrupt && g.os.thread(t).wake_tick == kNever);
    g.os.put_on(t, 0);
    g.sys(0, kSleep, static_cast<u32>(-1)); // a yield
    CHECK(g.os.thread(t).state == ThreadState::ready && g.cores[0].thread == 0);
    g.os.put_on(t, 0);
    g.sys(0, kRecv, 3, 0, 8, 5);
    CHECK(g.os.thread(t).state == ThreadState::waiting_recv && g.os.thread(t).wake_tick == 105);
    CHECK(g.os.thread(t).pend && g.os.thread(t).pend_b == static_cast<u32>(-1));
    g.os.put_on(t, 0);
    g.sys(0, kRecv, 3, 0, 8, static_cast<u32>(-1));
    CHECK(g.os.thread(t).wake_tick == kNever);
    // Saved registers survive a trip off the core, in the stack's first 24 bytes.
    g.os.put_on(t, 0);
    g.cores[0].x.r.c = 0xC0FFEE;
    g.os.take_off(0, ThreadState::ready);
    g.os.put_on(t, 0);
    CHECK(g.cores[0].x.r.c == 0xC0FFEE);
    // EXIT: the last thread ends its program.
    g.sys(0, kExit);
    CHECK(g.os.live_programs() == 0 && g.os.deaths.size() == 1 && g.os.deaths[0].cause == DeathCause::exit);
    (void)t;
}

void test_fault_handlers() {
    Rig g;
    // A program whose code faults at offset 2 (MOV [B] with B = 0) after
    // registering a handler at offset 10; the handler HRETs.
    u8 code[16] = {};
    code[0] = encode(kMov, kA);
    code[1] = encode(kMov, kA);
    code[2] = encode(kMov, kAtB); // faults: B = 0
    code[3] = encode(kMov, kImm);
    code[4] = 42;
    code[10] = encode(kSys, kImm);
    code[11] = kHret;
    const u32 at = g.place(code, 16, 0, 2, 16);
    Regs& r = g.cores[0].x.r;
    g.sys(0, kSetHandler, 0, at + 10);
    r.ip = at + 2;
    r.a = 7;
    r.b = 0; // so MOV [B] faults
    r.c = 9;
    const u32 sp = r.sp;
    g.run(0); // the fault: into the handler, A = the fault's code
    CHECK(r.ip == at + 10 && r.a == kFaultMemory && r.sp == sp - 20);
    CHECK(g.os.thread(g.cores[0].thread - 1).in_handler);
    g.run(0); // HRET: registers back, on after the faulting instruction
    CHECK(r.ip == at + 3 && r.a == 7 && r.c == 9 && r.sp == sp);
    g.run(0);
    CHECK(r.a == 42);
    // A fault inside a handler ends the thread.
    r.ip = at + 2;
    g.run(0);
    r.ip = at + 2; // the handler faults the same way
    g.run(0);
    CHECK(g.os.live_programs() == 0 && g.os.deaths.size() >= 1 && g.os.deaths.back().cause == DeathCause::fault);
    // A stack fault's code.
    Rig h;
    u8 pop[16] = {};
    pop[0] = encode(kPop, kA); // the stack is empty
    pop[10] = encode(kSys, kImm);
    pop[11] = kHret;
    const u32 at2 = h.place(pop, 16, 0, 2, 16);
    h.sys(0, kSetHandler, 0, at2 + 10);
    h.cores[0].x.r.ip = at2;
    h.run(0);
    CHECK(h.cores[0].x.r.a == kFaultStack && h.cores[0].x.r.ip == at2 + 10);
}

// A new thread's registers (A.3): B, C and D point at its start, as an idle
// run's do; its A is the starting value.
void test_new_thread_registers() {
    Rig g;
    const u32 at = g.m.alloc(RUN, 32, 2, 0);
    const u32 id = g.os.place_by_hand(RUN, at, at + 4, 77);
    CHECK(id != 0);
    g.os.put_on(g.index_of(id), 0);
    const Regs& r = g.cores[0].x.r;
    CHECK(r.a == 77 && r.b == at + 4 && r.c == at + 4 && r.d == at + 4 && r.ip == at + 4);
}

// ALLOC's result, the address or 0, lands in D as well as A (A.4), so this
// 11-byte copier needs no register moves: MOV # 11; SYS # ALLOC; CPY # 11; MOV
// D; SYS # SPAWN; JMP # -9. Its ALLOC's permission is B mod 3, with B at its
// own start. A lax place has no no-execute bit (9.3), so it works where its
// address is 0 mod 3 (data, which is "both" here) or 2 ("both"), and fails at
// 1, where its new block is executable only and its CPY can't write it.
const u8 kShort[11] = {encode(kMov, kImm), 11, encode(kSys, kImm), kAlloc, encode(kCpy, kImm), 11,
                       encode(kMov, kD), encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-9)};

void test_alloc_result_in_d() {
    for (u32 want = 0; want < 3; ++want) {
        Rig g;
        u32 S = 0;
        while ((S = g.m.alloc(RUN, 11, 2, 0)) % 3 != want) // the run keeps the others
            CHECK(S != 0);
        memcpy(g.m.bytes() + S, kShort, 11);
        const u32 id = g.os.place_by_hand(RUN, S, S, 0);
        CHECK(id != 0);
        g.os.put_on(g.index_of(id), 0);
        Regs& r = g.cores[0].x.r;
        g.run(0); // MOV # 11
        g.run(0); // SYS # ALLOC: A = D = N
        const u32 N = r.a;
        CHECK(N != 0 && r.d == N);
        CHECK(g.m.page(g.m.page_of(N)).perm == (want == 1 ? Perm::exec : Perm::both));
        const Step st = g.run(0); // CPY # 11, from B = S to D = N
        if (want == 1) { // it can't write its executable block, and dies
            CHECK(st.kind == StepKind::fault_memory && g.os.live_programs() == 0);
            continue;
        }
        CHECK(memcmp(g.m.bytes() + N, kShort, 11) == 0);
        g.run(0); // MOV D
        g.run(0); // SYS # SPAWN: a gift
        CHECK(r.a != 0 && g.os.live_programs() == 2);
        g.run(0); // JMP # -9
        CHECK(r.ip == S);
    }
    // A failed ALLOC puts 0 in D too.
    Rig h;
    h.place(kShort, 11, 0);
    CHECK(h.sys(0, kAlloc, 0, 2, 0, 1234) == 0 && h.cores[0].x.r.d == 0);
}

void test_idle_runs() {
    Rig g;
    const u32 spot = g.m.region_base() + 5;
    g.idle_run(0, spot);
    g.cores[0].run_cause = BirthCause::static_bytes;
    // What it may not do fails soft, and nothing happens.
    CHECK(g.sys(0, kSetClock, 12) == 0 && g.sys(0, kSetCore, 1) == 0 && g.sys(0, kSetHandler, 0, spot) == 0);
    g.sys(0, kExit);
    g.sys(0, kSleep, 5);
    CHECK(g.cores[0].idle_run && g.sys(0, kDwrite, 0, spot, 4) == 0);
    CHECK(g.sys(0, kRecv, 3, spot, 4, 5) == 0 && g.cores[0].x.r.b == static_cast<u32>(-1));
    // What it may: SENSE, ALLOC (held by the run), and SPAWN into what it holds: a new program.
    CHECK(g.sys(0, kSense, kPlaceId) == 7);
    const u32 a = g.sys(0, kAlloc, 18, 2, 0);
    CHECK(a != 0 && g.m.record(g.m.alloc_at(a)).holder == RUN);
    g.os.clear_facts();
    CHECK(g.sys(0, kSpawn, a, 1) != 0 && g.os.births.size() == 1);
    if (g.os.births.size() == 1)
        CHECK(g.os.births[0].cause == BirthCause::static_bytes && g.os.births[0].parent_uid == 0);
    // The reset region itself isn't an allocation: it can't be given.
    CHECK(g.sys(0, kSpawn, spot, 0) == 0);
}

} // namespace

// A live thread's stack is in use (R15, os.stack_held; Damian, 2026-11-19).
// SPAWN into a sibling's stack or the caller's own starts a thread, never a
// gift, and FREE of either fails, so a thread's saved registers always lie
// in memory its program holds. An ended thread's stack goes back as ever.
// With the rule off both go through, as they did before.
void test_stack_held() {
    Rig g; // lax, where a data page can start a thread (9.3)
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    g.os.clear_facts();
    const u32 t2 = g.sys(0, kSpawn, code, 5);
    CHECK(t2 != 0);
    const u32 mine = g.os.thread(g.cores[0].thread - 1).stack;
    const u32 sibling = g.os.thread(g.index_of(t2)).stack;
    CHECK(mine != sibling && g.m.record(g.m.alloc_at(sibling)).size == g.s.stack_size);
    // SPAWN there: another thread of the same program, and nothing given.
    CHECK(g.sys(0, kSpawn, sibling + 30, 0) != 0 && g.os.births.size() == 0 && g.os.program(me).threads == 3);
    CHECK(g.sys(0, kSpawn, mine + 30, 0) != 0 && g.os.births.size() == 0 && g.os.program(me).threads == 4);
    CHECK(g.m.record(g.m.alloc_at(sibling)).holder == me && g.m.record(g.m.alloc_at(mine)).holder == me);
    // FREE there fails, and the stacks stay.
    CHECK(g.sys(0, kFree, sibling) == 0 && g.sys(0, kFree, mine) == 0);
    CHECK(g.m.alloc_at(sibling) != 0 && g.m.alloc_at(mine) != 0);
    // An allocation of a stack's size that is no thread's stack is freed, or
    // given, as any other.
    const u32 plain = g.sys(0, kAlloc, g.s.stack_size, 2, 0);
    CHECK(plain != 0 && g.sys(0, kFree, plain) == 1 && g.m.alloc_at(plain) == 0);
    const u32 gift = g.sys(0, kAlloc, g.s.stack_size, 2, 0);
    CHECK(gift != 0 && g.sys(0, kSpawn, gift, 0) != 0 && g.os.births.size() == 1);
    CHECK(g.m.record(g.m.alloc_at(gift)).holder != me);
    // Every live thread's stack is its program's.
    u32 strays = 0;
    for (u32 t = 0; t < g.os.thread_top(); ++t) {
        const Thread& th = g.os.thread(t);
        if (th.state == ThreadState::free)
            continue;
        const u32 id = g.m.alloc_at(th.stack);
        strays += !id || g.m.record(id).base != th.stack || g.m.record(id).holder != th.program;
    }
    CHECK(strays == 0);
    // The thread ended, its stack goes back.
    g.os.end_thread(g.index_of(t2), DeathCause::exit);
    CHECK(g.m.alloc_at(sibling) == 0 && g.os.program(me).alive);
    TextBuf why;
    CHECK(g.m.check(&why) && g.os.check(&why));

    // Off, as before 2026-11-19: the stack is given, or freed, under a live thread.
    OsSettings off;
    off.stack_held = false;
    Rig o(256, false, 16384, DeviceKind::e_core, &off);
    const u32 code2 = o.place(loop, 2, 0, 2, 64);
    const u32 me2 = o.cores[0].x.holder;
    o.os.clear_facts();
    const u32 a = o.sys(0, kSpawn, code2, 0), b = o.sys(0, kSpawn, code2, 0);
    CHECK(a != 0 && b != 0);
    const u32 sa = o.os.thread(o.index_of(a)).stack, sb = o.os.thread(o.index_of(b)).stack;
    CHECK(o.sys(0, kSpawn, sa + 30, 0) != 0 && o.os.births.size() == 1);
    CHECK(o.m.record(o.m.alloc_at(sa)).holder != me2 && o.os.thread(o.index_of(a)).program == me2);
    CHECK(o.sys(0, kFree, sb) == 1 && o.m.alloc_at(sb) == 0 && o.os.thread(o.index_of(b)).stack == sb);
}

namespace {

// What stack_of and SPAWN's choice of a thread or a gift answered before
// pass 13, written out again: a walk of the program's threads for each.
u32 old_ip(const Rig& g, u32 t) {
    const Thread& th = g.os.thread(t);
    if (th.core)
        return g.cores[th.core - 1].x.r.ip;
    const u8* p = g.m.bytes() + th.stack + 16;
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

bool old_runs_on(const Rig& g, u32 holder, u32 ip, u32 id) {
    const u32 at = g.m.alloc_at(ip);
    if (at == id)
        return true;
    if (at && g.m.record(at).holder == holder)
        return false;
    const Alloc& a = g.m.record(id);
    const u32 page = g.m.page_of(ip);
    return a.size && page >= g.m.page_of(a.base) && page <= g.m.page_of(a.base + a.size - 1);
}

bool old_stack_of(const Rig& g, u32 holder, u32 id) {
    if (!id || holder < kFirstProgramHolder)
        return false;
    const Alloc& a = g.m.record(id);
    if (a.holder != holder || a.size != g.s.stack_size)
        return false;
    for (u32 t = g.os.program(holder).first_thread; t; t = g.os.thread(t - 1).next)
        if (g.os.thread(t - 1).stack == a.base)
            return true;
    return false;
}

// 1: a thread, 2: a gift, as spawn_asked_ (0 where SPAWN refuses first).
// *by_page: a thread only by a page an IP shares with the allocation.
u32 old_asked(const Rig& g, u32 holder, u32 addr, u32 sys_ip, bool* by_page) {
    const u32 id = g.m.alloc_at(addr);
    *by_page = false;
    if (!id || g.m.record(id).holder != holder || g.m.page(g.m.page_of(addr)).perm == Perm::data)
        return 0;
    bool own = old_runs_on(g, holder, sys_ip, id), inside = g.m.alloc_at(sys_ip) == id;
    for (u32 t = g.os.program(holder).first_thread; t; t = g.os.thread(t - 1).next) {
        own = own || old_runs_on(g, holder, old_ip(g, t - 1), id);
        inside = inside || g.m.alloc_at(old_ip(g, t - 1)) == id;
    }
    const bool stack = g.s.stack_held && old_stack_of(g, holder, id);
    *by_page = own && !inside && !stack;
    return own || stack ? 1 : 2;
}

u32 draw(u64* x) {
    *x = *x * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<u32>(*x >> 33);
}

// An address of one of the kinds an IP or a target takes: in one of the
// program's allocations, in one of their pages (a gap, or someone else's
// allocation), anywhere in memory, or past it.
u32 some_address(Rig& g, u32 holder, u64* x) {
    u32 n = 0, pick = 0;
    for (u32 id = g.m.first_of(holder); id; id = g.m.record(id).next)
        ++n;
    if (n) {
        pick = draw(x) % n;
        for (u32 id = g.m.first_of(holder); id; id = g.m.record(id).next)
            if (pick-- == 0) {
                pick = id;
                break;
            }
    }
    const Alloc& a = g.m.record(pick);
    switch (draw(x) % 5) {
    case 0:
    case 1:
        return n ? a.base + draw(x) % a.extent : draw(x) % g.m.size();
    case 2:
        return n ? g.m.page_of(a.base + draw(x) % a.extent) * g.m.page_size() + draw(x) % g.m.page_size() : 0;
    case 3:
        return draw(x) % g.m.size();
    default:
        return g.m.size() + draw(x) % 4096;
    }
}

void set_ip(Rig& g, u32 t, u32 ip) {
    u8* p = g.m.bytes() + g.os.thread(t).stack + 16;
    p[0] = static_cast<u8>(ip);
    p[1] = static_cast<u8>(ip >> 8);
    p[2] = static_cast<u8>(ip >> 16);
    p[3] = static_cast<u8>(ip >> 24);
}

// A program's live threads, by record.
u32 nth_thread(const Rig& g, u32 holder, u32 k) {
    u32 t = g.os.program(holder).first_thread;
    while (k-- && t)
        t = g.os.thread(t - 1).next;
    return t ? t - 1 : ~0u;
}

// The ready queues without their stale entries, as the scheduler's picks
// leave them: each ready thread taken off and queued again. Here nothing
// picks, so a queue would fill with the ended threads' entries.
void requeue(Rig& g) {
    Array<u32> ready;
    for (u32 t = g.os.pick(DeviceKind::p_core); t != ~0u; t = g.os.pick(DeviceKind::p_core))
        ready.push(t);
    for (u32 i = 0; i < ready.size(); ++i)
        g.os.make_ready(ready[i]);
}

struct LookupCount {
    u32 asked[3] = {}; // by old_asked
    u32 by_page = 0, frees = 0, freed = 0, shared = 0, checks = 0;
};

// Random calls of one program, its threads' IPs set anywhere between them,
// each SPAWN and FREE held against the walks above, and the OS's own check
// (stacks_ against the records) every 50 steps.
void stack_lookup_run(const OsSettings* with, LookupCount* n) {
    Rig g(256, false, 16384, DeviceKind::e_core, with);
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    for (u32 i = 0; i < 24; ++i)
        CHECK(g.sys(0, kSpawn, code + i % 2, i) != 0);
    u64 x = with ? 0x5EED0002ull : 0x5EED0001ull;
    const u32 sizes[7] = {g.s.stack_size, g.s.stack_size, 8, 30, 120, 300, 600};
    TextBuf why;
    for (u32 step = 0; step < 12000; ++step) {
        Regs& r = g.cores[0].x.r;
        const u32 threads = g.os.program(me).threads;
        requeue(g);
        switch (draw(&x) % 9) {
        case 0:
            g.sys(0, kAlloc, sizes[draw(&x) % 7], 2, 0);
            break;
        case 1:
        case 2: { // a thread off its core, its saved IP somewhere
            const u32 t = nth_thread(g, me, draw(&x) % threads);
            if (t != ~0u && !g.os.thread(t).core)
                set_ip(g, t, some_address(g, me, &x));
            break;
        }
        case 3:
            r.ip = draw(&x) % 4 ? code : some_address(g, me, &x);
            break;
        case 4:
        case 5: {
            const u32 at = some_address(g, me, &x);
            bool by_page = false;
            const u32 want = old_asked(g, me, at, r.ip, &by_page);
            const u64 t0 = g.os.spawns_thread, p0 = g.os.spawns_program;
            g.sys(0, kSpawn, at, 0);
            const u32 got = g.os.spawns_thread > t0 ? 1 : g.os.spawns_program > p0 ? 2 : 0;
            CHECK(got == want);
            ++n->asked[want];
            n->by_page += by_page;
            break;
        }
        case 6: { // FREE of an allocation of a stack's size, a thread's stack or not
            const u32 at = some_address(g, me, &x);
            const u32 id = g.m.alloc_at(at);
            if (!id || g.m.record(id).size != g.s.stack_size)
                break;
            const u32 base = g.m.record(id).base;
            const bool held = g.s.stack_held && old_stack_of(g, me, id);
            const bool mine = g.m.record(id).holder == me;
            CHECK(g.sys(0, kFree, base) == (!held && mine ? 1u : 0u));
            ++n->frees;
            n->freed += !held && mine;
            break;
        }
        case 7: { // a thread of any program ends, but the one on the core
            const u32 t = draw(&x) % (g.os.thread_top() ? g.os.thread_top() : 1);
            if (g.os.thread(t).state != ThreadState::free && !g.os.thread(t).core)
                g.os.end_thread(t, DeathCause::exit);
            break;
        }
        default:
            if (threads < 40)
                g.sys(0, kSpawn, code, 0);
            break;
        }
        if (step % 50 == 0 && why.size() == 0) { // the first break is enough to say
            CHECK(g.os.check(&why) && g.m.check(&why));
            ++n->checks;
        }
    }
    // Two live threads with their stack at one base (with os.stack_held off).
    for (u32 t = 0; t < g.os.thread_top(); ++t)
        for (u32 u = t + 1; u < g.os.thread_top(); ++u)
            n->shared += g.os.thread(t).state != ThreadState::free && g.os.thread(u).state != ThreadState::free &&
                         g.os.thread(t).stack == g.os.thread(u).stack;
    CHECK(g.os.check(&why));
    if (why.size())
        fprintf(stderr, "  %s\n", why.c_str());
}

struct DeliverCount {
    u32 taken = 0, joined = 0, dropped = 0, raised = 0, past = 0, checks = 0;
};

// Random RECVs and data handlers of one program's threads on a few ports,
// threads ended and started, and messages delivered to every port and past
// them: each delivery held against deliver's old walk of every record (the
// longest waiting RECV on the port takes it; else every thread with a data
// handler there gets the interrupt), and the OS's own check every 50 steps.
void deliver_keys_run(DeliverCount* n) {
    OsSettings s;
    s.ports = 5;
    Rig g(256, false, 16384, DeviceKind::e_core, &s);
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    const u32 buf = g.m.alloc(me, 64, 0, 0);
    CHECK(buf != 0);
    for (u32 i = 0; i < 30; ++i)
        CHECK(g.sys(0, kSpawn, code, i) != 0);
    u64 x = 0x5EED0003ull;
    const u8 msg[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TextBuf why;
    Array<u8> pending, states;
    for (u32 step = 0; step < 8000; ++step) {
        requeue(g);
        if (!g.cores[0].thread) {
            const u32 t = g.os.pick(DeviceKind::p_core);
            if (t != ~0u)
                g.os.put_on(t, 0);
        }
        const u32 kind = draw(&x) % 10;
        if (kind < 3 && g.cores[0].thread) {
            g.sys(0, kRecv, draw(&x) % 6, buf, 8, draw(&x) % 3 ? 0xFFFFFFFFu : 50);
        } else if (kind < 5 && g.cores[0].thread) {
            g.sys(0, kSetHandler, kOnData, draw(&x) % 4 ? code : 0, draw(&x) % 6);
        } else if (kind < 6) {
            const u32 t = draw(&x) % (g.os.thread_top() ? g.os.thread_top() : 1);
            if (g.os.thread(t).state != ThreadState::free && !g.os.thread(t).core)
                g.os.end_thread(t, DeathCause::exit);
        } else if (kind < 7) {
            if (g.cores[0].thread && g.os.program(me).threads < 60)
                g.sys(0, kSpawn, code, 0);
        } else {
            const u32 port = 1 + draw(&x) % 7; // not 0 (the reset region's), and past os.ports
            u32 best = ~0u;
            pending.resize(0);
            states.resize(0);
            for (u32 t = 0; t < g.os.thread_top(); ++t) {
                const Thread& th = g.os.thread(t);
                if (th.state == ThreadState::waiting_recv && th.recv_port == port &&
                    (best == ~0u || th.wait_seq < g.os.thread(best).wait_seq))
                    best = t;
                pending.push(th.pending);
                states.push(static_cast<u8>(th.state));
            }
            const u64 taken = g.os.recv_direct, joined = g.os.inbox_joined;
            g.os.deliver(port, msg, 1 + draw(&x) % 8, 3, 0);
            n->past += port >= s.ports;
            if (best != ~0u) {
                CHECK(g.os.recv_direct == taken + 1 && g.os.thread(best).state != ThreadState::waiting_recv);
                ++n->taken;
            } else if (g.os.inbox_joined == joined + 1) {
                ++n->joined;
                for (u32 t = 0; t < g.os.thread_top(); ++t) {
                    const Thread& th = g.os.thread(t);
                    const bool has = th.state != ThreadState::free && th.handler[kOnData] && th.data_port == port;
                    CHECK(th.pending == (has ? (pending[t] | (1u << kOnData)) : pending[t]));
                    n->raised += has;
                }
            } else {
                ++n->dropped;
            }
            for (u32 t = 0; t < states.size(); ++t) // no one else woke, or ended
                CHECK(t == best || static_cast<u8>(g.os.thread(t).state) == states[t]);
        }
        if (step % 50 == 0 && why.size() == 0) {
            CHECK(g.os.check(&why) && g.m.check(&why));
            ++n->checks;
        }
    }
    if (why.size())
        fprintf(stderr, "  %s\n", why.c_str());
}

// The killer's draw as it was (os.cpp before pass 13): every program record
// walked twice, each live one's allocations summed both times.
u32 old_pick_by_bytes(const Rig& g, u64 draw) {
    u64 total = 0;
    for (u32 p = 0; p < g.os.program_capacity(); ++p)
        if (g.os.program(kFirstProgramHolder + p).alive)
            total += g.os.bytes_held(kFirstProgramHolder + p);
    if (total == 0)
        return 0;
    u64 hi = 0;
    _umul128(draw, total, &hi);
    for (u32 p = 0; p < g.os.program_capacity(); ++p) {
        if (!g.os.program(kFirstProgramHolder + p).alive)
            continue;
        const u64 n = g.os.bytes_held(kFirstProgramHolder + p);
        if (hi < n)
            return kFirstProgramHolder + p;
        hi -= n;
    }
    return 0;
}

struct DrawCount {
    u32 picks = 0, none = 0, started = 0, ended = 0, top = 0, checks = 0;
};

// Programs placed by hand and ended at random, each holding what it was
// given, and the killer's draw held against the old one at every step.
void killer_draw_run(DrawCount* n) {
    Rig g(64, false, 16384);
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    u64 x = 0x5EED0004ull;
    TextBuf why;
    Array<u32> live;
    for (u32 step = 0; step < 4000; ++step) {
        requeue(g);
        const u32 kind = draw(&x) % 10;
        if (kind < 3) {
            const u32 at = g.m.alloc(RUN, 2, 2, 0);
            if (at) {
                memcpy(g.m.bytes() + at, loop, 2);
                const u32 id = g.os.place_by_hand(RUN, at, at, 0);
                if (id) {
                    live.push(g.os.thread(g.index_of(id)).program);
                    ++n->started;
                } else {
                    g.m.free(RUN, at);
                }
            }
        } else if (kind < 6 && live.size()) {
            g.m.alloc(live[draw(&x) % live.size()], 1 + draw(&x) % 200, 2, 0);
        } else if (kind < 8 && live.size()) {
            const u32 i = draw(&x) % live.size();
            g.os.end_program(live[i], DeathCause::exit);
            live[i] = live.back();
            live.pop();
            ++n->ended;
        }
        const u64 d = static_cast<u64>(draw(&x)) << 32 | draw(&x);
        const u32 want = old_pick_by_bytes(g, d);
        CHECK(g.os.pick_by_bytes(d) == want);
        n->picks += want != 0;
        n->none += want == 0;
        u32 top = 0;
        for (u32 p = 0; p < g.os.program_capacity(); ++p)
            top = g.os.program(kFirstProgramHolder + p).alive ? p + 1 : top;
        n->top = top > n->top ? top : n->top;
        if (step % 50 == 0 && why.size() == 0) {
            CHECK(g.os.check(&why) && g.m.check(&why));
            ++n->checks;
        }
    }
    if (why.size())
        fprintf(stderr, "  %s\n", why.c_str());
}

} // namespace

// stack_of without the walk of the program's threads, and SPAWN passing
// over the threads whose IPs lie in none of the target's pages (pass 13):
// each answer held against the walks they replaced, with os.stack_held on
// and off. Off, a stack can be freed under its live thread and handed out
// again, so two threads can have their stack at one base.
void test_stack_lookup() {
    LookupCount on, off;
    stack_lookup_run(nullptr, &on);
    OsSettings s;
    s.stack_held = false;
    stack_lookup_run(&s, &off);
    printf("  stack lookup: held, %u threads, %u gifts (%u by a page), %u of %u FREEs; off, %u, %u (%u), "
           "%u of %u, %u stacks shared\n",
           on.asked[1], on.asked[2], on.by_page, on.freed, on.frees, off.asked[1], off.asked[2], off.by_page,
           off.freed, off.frees, off.shared);
    CHECK(on.asked[1] > 200 && on.asked[2] > 200 && on.by_page > 30 && on.freed > 20 && on.freed < on.frees);
    CHECK(off.asked[1] > 200 && off.asked[2] > 200 && off.by_page > 30 && off.shared > 0);
}

// deliver without the walks of every record (pass 13): the ports' keys and
// counts kept by note, each delivery held against the walks they replaced.
void test_deliver_keys() {
    DeliverCount n;
    deliver_keys_run(&n);
    printf("  deliver: %u taken by a RECV, %u joined the inbox (%u interrupts raised), %u dropped, %u past the "
           "ports; %u checks\n",
           n.taken, n.joined, n.raised, n.dropped, n.past, n.checks);
    CHECK(n.taken > 200 && n.joined > 200 && n.raised > 200 && n.dropped > 20 && n.past > 100 && n.checks == 160);
}

// The killer's draw up to the highest live program, each one's bytes summed
// once (pass 13), held against the draw it replaced.
void test_killer_draw() {
    DrawCount n;
    killer_draw_run(&n);
    printf("  killer's draw: %u picks and %u with nothing held, as before; %u programs started, %u ended, at most "
           "%u records up; %u checks\n",
           n.picks, n.none, n.started, n.ended, n.top, n.checks);
    CHECK(n.picks > 3000 && n.none > 0 && n.started > 500 && n.ended > 400 && n.top > 20 && n.checks == 80);
}

// The limit on the threads one program may have (os.thread_limits, a
// place's OsSettings::thread_limit; 9.6, design-thread-limit-2026-11-25.md).
// At the limit a SPAWN that would start a thread of the caller's program
// fails as any SPAWN fails, at the same cost, and nothing else changes; a
// birth at the limit works; 0 is no limit; a limit of 1 leaves a program
// its first thread only; and a program over the limit (a world saved
// without it, loaded under it) keeps its threads but can't add one until
// it is under.
void test_thread_limit() {
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    OsSettings three;
    three.thread_limit = 3;
    Rig g(256, false, 16384, DeviceKind::e_core, &three);
    const u32 code = g.place(loop, 2, 0, 2, 64);
    const u32 me = g.cores[0].x.holder;
    g.os.clear_facts();
    CHECK(g.sys(0, kSpawn, code, 1) != 0 && g.sys(0, kSpawn, code, 2) != 0 && g.os.program(me).threads == 3);
    CHECK(g.os.spawns_thread == 2 && g.os.spawns_thread_ok == 2 && g.os.spawns_limited == 0);
    // A SPAWN that fails today, at an address it doesn't hold: its cost.
    Regs& r = g.cores[0].x.r;
    r.a = 0;
    const CallOutcome missed = g.os.call(0, kSpawn, r.ip);
    // At the limit, into its own code: refused.
    Array<u8> bytes;
    bytes.assign(g.m.bytes(), g.m.size());
    const u32 free_before = g.m.free_bytes(), threads_before = g.os.live_threads();
    const u32 thread_top = g.os.thread_top();
    r.a = code;
    r.b = 7;
    r.c = 11;
    r.d = 13;
    const CallOutcome refused = g.os.call(0, kSpawn, r.ip);
    CHECK(r.a == 0 && r.b == 7 && r.c == 11 && r.d == 13);
    CHECK(refused.cost16 == missed.cost16 && refused.stays == missed.stays && refused.stays);
    CHECK(memcmp(bytes.data(), g.m.bytes(), bytes.size()) == 0 && g.m.free_bytes() == free_before);
    CHECK(g.os.live_threads() == threads_before && g.os.thread_top() == thread_top && g.os.program(me).threads == 3);
    CHECK(g.os.births.size() == 0 && g.os.live_programs() == 1);
    CHECK(g.os.spawns_thread == 3 && g.os.spawns_thread_ok == 2 && g.os.spawns_limited == 1);
    // Into a live thread's stack (os.stack_held) too.
    const u32 mine = g.os.thread(g.cores[0].thread - 1).stack;
    CHECK(g.sys(0, kSpawn, mine + 30, 0) == 0 && g.os.spawns_limited == 2 && g.os.program(me).threads == 3);
    // A birth at the limit works, and the child may start threads to the limit.
    const u32 gift = g.sys(0, kAlloc, 30, 1, 0);
    const u32 child_id = g.sys(0, kSpawn, gift + 3, 9);
    CHECK(gift != 0 && child_id != 0 && g.os.births.size() == 1 && g.os.live_programs() == 2);
    CHECK(g.os.program(me).threads == 3 && g.os.spawns_program_ok == 1 && g.os.spawns_limited == 2);
    const u32 child = g.os.thread(g.index_of(child_id)).program;
    CHECK(child != me && g.os.program(child).threads == 1);
    g.os.put_on(g.index_of(child_id), 1);
    CHECK(g.sys(1, kSpawn, gift, 0) != 0 && g.sys(1, kSpawn, gift, 0) != 0 && g.os.program(child).threads == 3);
    CHECK(g.sys(1, kSpawn, gift, 0) == 0 && g.os.program(child).threads == 3 && g.os.spawns_limited == 3);
    // One of its threads ends: under the limit, it may start one again.
    u32 other = ~0u;
    for (u32 t = 0; t < g.os.thread_top(); ++t)
        if (g.os.thread(t).state == ThreadState::ready && g.os.thread(t).program == me)
            other = t;
    CHECK(other != ~0u);
    g.os.end_thread(other, DeathCause::exit);
    CHECK(g.os.program(me).threads == 2 && g.sys(0, kSpawn, code, 0) != 0 && g.os.program(me).threads == 3);
    TextBuf why;
    CHECK(g.m.check(&why) && g.os.check(&why));

    // 0 is no limit: 40 threads of one program, none refused.
    Rig none;
    const u32 code0 = none.place(loop, 2, 0, 2, 64);
    u32 started = 0;
    for (u32 k = 0; k < 40; ++k)
        started += none.sys(0, kSpawn, code0, k) != 0 ? 1 : 0;
    CHECK(started == 40 && none.os.program(none.cores[0].x.holder).threads == 41 && none.os.spawns_limited == 0);

    // A limit of 1: a program has its first thread only, and births still work.
    OsSettings one;
    one.thread_limit = 1;
    Rig o(256, false, 16384, DeviceKind::e_core, &one);
    const u32 code1 = o.place(loop, 2, 0, 2, 64);
    const u32 me1 = o.cores[0].x.holder;
    o.os.clear_facts();
    const u32 stack1 = o.os.thread(o.cores[0].thread - 1).stack;
    CHECK(o.sys(0, kSpawn, code1, 0) == 0 && o.sys(0, kSpawn, stack1 + 30, 0) == 0);
    CHECK(o.os.program(me1).threads == 1 && o.os.spawns_limited == 2 && o.os.spawns_thread_ok == 0);
    const u32 gift1 = o.sys(0, kAlloc, 30, 1, 0);
    const u32 born = o.sys(0, kSpawn, gift1 + 3, 0);
    CHECK(born != 0 && o.os.births.size() == 1 && o.os.live_programs() == 2);
    o.os.put_on(o.index_of(born), 1);
    CHECK(o.sys(1, kSpawn, gift1, 0) == 0 && o.os.spawns_limited == 3);

    // Over the limit: five threads saved with no limit, loaded under a limit
    // of 2. It keeps all five, and can't add one until it is under 2.
    Rig before;
    const u32 code5 = before.place(loop, 2, 0, 2, 64);
    const u32 me5 = before.cores[0].x.holder;
    for (u32 k = 0; k < 4; ++k)
        CHECK(before.sys(0, kSpawn, code5, k) != 0);
    CHECK(before.os.program(me5).threads == 5);
    before.os.take_off(0, ThreadState::ready);
    ByteWriter saved;
    {
        Archive ar(&saved);
        before.m.serialize(ar);
        before.os.serialize(ar);
        CHECK(ar.ok());
    }
    OsSettings two;
    two.thread_limit = 2;
    Rig after(256, false, 16384, DeviceKind::e_core, &two);
    {
        ByteReader in(saved.data(), saved.size());
        Archive ar(&in);
        after.m.serialize(ar);
        after.os.serialize(ar);
        CHECK(ar.ok() && in.remaining() == 0);
    }
    CHECK(after.os.program(me5).threads == 5 && after.os.check(&why));
    u32 ready[5];
    u32 n = 0;
    for (u32 t = 0; t < after.os.thread_top() && n < 5; ++t)
        if (after.os.thread(t).state == ThreadState::ready && after.os.thread(t).program == me5)
            ready[n++] = t;
    CHECK(n == 5);
    after.os.put_on(ready[0], 0);
    CHECK(after.sys(0, kSpawn, code5, 0) == 0 && after.os.program(me5).threads == 5);
    for (u32 k = 1; k < 4; ++k)
        after.os.end_thread(ready[k], DeathCause::exit);
    CHECK(after.os.program(me5).threads == 2 && after.sys(0, kSpawn, code5, 0) == 0); // at it: still refused
    after.os.end_thread(ready[4], DeathCause::exit);
    CHECK(after.os.program(me5).threads == 1 && after.sys(0, kSpawn, code5, 0) != 0); // under it
    CHECK(after.os.program(me5).threads == 2 && after.os.spawns_limited == 2);
    CHECK(after.m.check(&why) && after.os.check(&why));
}

// The room for the barrier's old bytes (2026-11-25): a backlog of DWRITEs
// left while the disk was cut out is served in one barrier, whose room is
// made once for exactly the writes' old bytes; the next barrier, needing far
// less, gives it back; and a place writing a disk's worth or less keeps its
// room from one barrier to the next.
void test_disk_room() {
    static Memory m;
    static Core cores[2];
    static PlaceEnv env;
    static Os os;
    constexpr u32 kDisk = 1024, kWriters = 8;
    static const MemDeviceSpec dev[] = {{16384, 4 * 16, 900}};
    MemorySpec ms;
    ms.page_size = 256;
    ms.region_size = 256;
    ms.devices = dev;
    ms.device_count = 1;
    CHECK(m.init(ms, nullptr));
    cores[0].kind = DeviceKind::p_core;
    cores[1].kind = DeviceKind::e_core;
    env.tick = 100;
    env.disk_size = kDisk;
    SenseDevice d;
    d.kind = DeviceKind::p_core;
    d.speed = 25000;
    d.threshold_mv = 800;
    env.devices.push(d);
    d.kind = DeviceKind::e_core;
    env.devices.push(d);
    d.kind = DeviceKind::memory;
    d.speed = 4;
    d.threshold_mv = 600;
    env.devices.push(d);
    d.kind = DeviceKind::disk;
    d.speed = 900;
    d.threshold_mv = 700;
    d.size = kDisk;
    env.devices.push(d);
    TextBuf why;
    CHECK(os.init(&m, cores, 2, &env, OsSettings(), &why));
    const u8 loop[2] = {encode(kJmp, kImm), 0};
    const u32 code = m.alloc(RUN, 64, 2, 0);
    CHECK(code != 0);
    memcpy(m.bytes() + code, loop, sizeof loop);
    const u32 id = os.place_by_hand(RUN, code, code, 0);
    u32 first = ~0u;
    for (u32 t = 0; t < os.thread_capacity(); ++t)
        if (os.thread(t).state != ThreadState::free && os.thread(t).id == id)
            first = t;
    os.put_on(first, 0);
    Regs& r = cores[0].x.r;
    r.a = kDisk;
    r.b = r.c = 0;
    os.call(0, kAlloc, r.ip);
    const u32 buf = r.a;
    CHECK(buf != 0);
    for (u32 i = 0; i < kDisk; ++i)
        m.bytes()[buf + i] = static_cast<u8>(i * 7 + 1);
    for (u32 k = 1; k < kWriters; ++k) {
        r.a = code;
        r.b = k;
        os.call(0, kSpawn, r.ip);
        CHECK(r.a != 0);
    }
    // Every thread asks to write the whole disk while it's cut out.
    os.set_disk_on(false);
    u32 asked = 0;
    for (u32 k = 0; k <= os.thread_top(); ++k) {
        const u32 t = k == 0 ? first : k - 1; // the one on the core first
        if (k > 0 && (t == first || os.thread(t).state != ThreadState::ready))
            continue;
        if (k > 0)
            os.put_on(t, 0);
        r.a = 0;
        r.b = buf;
        r.c = kDisk;
        os.call(0, kDwrite, r.ip);
        asked += os.thread(t).state == ThreadState::waiting_disk ? 1 : 0;
    }
    CHECK(asked == kWriters);
    os.barrier();
    CHECK(os.pending_disk() == kWriters && os.flight_room() == 0);
    // Back on: one barrier serves them all, its room just their old bytes.
    os.set_disk_on(true);
    os.barrier();
    CHECK(os.calls_ok[kDwrite] == kWriters && os.in_flight_bytes() == kWriters * kDisk);
    CHECK(os.flight_room() == kWriters * kDisk);
    CHECK(memcmp(os.disk(), m.bytes() + buf, kDisk) == 0);
    // The next barrier needs none of it, and gives it back.
    os.settle_disk();
    os.barrier();
    CHECK(os.flight_room() == 0);
    // One write of the whole disk, then barriers with nothing to serve: the
    // place keeps the room for it.
    u32 t = 0;
    while (os.thread(t).state != ThreadState::ready)
        ++t;
    os.put_on(t, 0);
    r.a = 0;
    r.b = buf;
    r.c = kDisk;
    os.call(0, kDwrite, r.ip);
    os.barrier();
    CHECK(os.in_flight_bytes() == kDisk && os.flight_room() == kDisk);
    os.settle_disk();
    os.barrier();
    os.barrier();
    CHECK(os.flight_room() == kDisk);
}

void suite_os() {
    test_thread_limit();
    test_lax_replicator();
    test_strict_replicator();
    test_out_of_memory_death();
    test_spawn_rules();
    test_spawn_past_its_end();
    test_stack_held();
    test_stack_lookup();
    test_deliver_keys();
    test_killer_draw();
    test_call_edges();
    test_waits();
    test_fault_handlers();
    test_idle_runs();
    test_new_thread_registers();
    test_alloc_result_in_d();
    test_disk_room();
}
