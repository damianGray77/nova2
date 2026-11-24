// The interpreter (nova2/cpu.h; design.md 8, appendix A; build-plan-gate1.md
// step 4): every row of A.2's table with its edge values, the operands, the
// stack, faults leaving state unchanged, the costs on both core kinds, the
// error floor's countdown, and the access cache noticing changes.
#include <cstring>

#include "check.h"
#include "nova2/cpu.h"

using namespace nova2;

namespace {

constexpr u32 P = kFirstProgramHolder;
constexpr u32 Q = kFirstProgramHolder + 1;
constexpr u32 kFast = 4 * 16, kSlow = 12 * 16; // device latencies, sixteenths

// Device 0 is fast; device 1 is slow and holds the reset region.
const MemDeviceSpec kDevs[] = {{8192, kFast, 1000}, {4096, kSlow, 900}};

struct Rig {
    Memory m;
    ExecContext x;
    CostTable p = default_p_costs();
    CostTable e = default_e_costs();
    u32 code = 0, data = 0, slow = 0, stack = 0;

    // A lax place unless strict, where the code is executable only and data
    // pages can't run (on a lax one they can, 9.3).
    explicit Rig(bool strict = false) {
        MemorySpec s;
        s.page_size = 256;
        s.strict = strict;
        s.devices = kDevs;
        s.device_count = 2;
        s.region_size = 256;
        CHECK(m.init(s, nullptr));
        code = m.alloc_on(P, 512, strict ? Perm::exec : Perm::both, 0); // two whole pages, fast
        data = m.alloc_on(P, 256, Perm::data, 0);
        slow = m.alloc_on(P, 256, Perm::data, 1);
        stack = m.alloc_on(P, 64, Perm::data, 0);
        x.holder = P;
        x.stack_lo = stack + kSavedRegsBytes;
        x.stack_hi = stack + 64;
        x.r.sp = x.stack_hi;
    }

    // Puts code at the start of the code allocation and runs one instruction.
    Step run(const u8* bytes, u32 n, const CostTable* t = nullptr) {
        memcpy(m.bytes() + code, bytes, n);
        x.r.ip = code;
        return step(m, x, t ? *t : p);
    }
    Step one(u8 op, u8 od, i8 imm = 0) {
        const u8 b[2] = {encode(op, od), static_cast<u8>(imm)};
        return run(b, 2);
    }
};

u32 u(i32 v) { return static_cast<u32>(v); }

void test_data_moves() {
    Rig g;
    Step s = g.one(kMov, kImm, -5);
    CHECK(s.kind == StepKind::done && g.x.r.a == u(-5) && g.x.r.ip == g.code + 2 && s.length == 2);
    CHECK(s.cost16 == kFast + 16 && s.results == 1);
    s = g.one(kMov, kIp);
    CHECK(g.x.r.a == g.code && g.x.r.ip == g.code + 1 && s.length == 1); // IP reads as this instruction
    g.m.bytes()[g.data] = 0xF0;
    g.x.r.b = g.data;
    s = g.one(kMov, kAtB);
    CHECK(g.x.r.a == u(-16)); // byte reads are sign-extended
    CHECK(s.cost16 == kFast + 16 + kFast);
    g.m.bytes()[g.slow] = 0x7F;
    g.x.r.d = g.slow;
    s = g.one(kMov, kAtD);
    CHECK(g.x.r.a == 127 && s.cost16 == kFast + 16 + kSlow); // the operand's device's latency
    s = g.one(kMov, kA);
    CHECK(s.results == 0); // MOV A is neutral

    g.x.r.a = 0x12345678;
    g.one(kSto, kB);
    CHECK(g.x.r.b == 0x12345678);
    g.x.r.d = g.data + 5;
    g.one(kSto, kAtD); // the low byte
    CHECK(g.m.bytes()[g.data + 5] == 0x78);
    CHECK(g.m.record(g.m.alloc_at(g.data)).wlo == 5 && g.m.record(g.m.alloc_at(g.data)).whi == 6);
    s = g.one(kSto, kImm, 9);
    CHECK(g.x.r.ip == g.code + 2 && s.results == 0); // a store to # does nothing
    g.x.r.a = g.code + 100;
    g.one(kSto, kIp); // storing to IP jumps
    CHECK(g.x.r.ip == g.code + 100);

    g.x.r.a = 1;
    g.x.r.c = 2;
    g.one(kSwp, kC);
    CHECK(g.x.r.a == 2 && g.x.r.c == 1);
    g.one(kSwp, kImm, -3); // with #: A <- #, nothing else
    CHECK(g.x.r.a == u(-3) && g.x.r.ip == g.code + 2);
    g.x.r.a = g.code + 40;
    g.one(kSwp, kIp); // A <- this address, IP <- old A
    CHECK(g.x.r.a == g.code && g.x.r.ip == g.code + 40);
    g.m.bytes()[g.data + 7] = 0x81;
    g.x.r.b = g.data + 7;
    g.x.r.a = 0x55;
    g.one(kSwp, kAtB);
    CHECK(g.x.r.a == u(-127) && g.m.bytes()[g.data + 7] == 0x55);
}

void test_stack() {
    Rig g;
    g.x.r.c = 0xA1B2C3D4;
    Step s = g.one(kPush, kC);
    CHECK(s.kind == StepKind::done && g.x.r.sp == g.x.stack_hi - 4);
    const u8* top = g.m.bytes() + g.x.stack_hi - 4;
    CHECK(top[0] == 0xD4 && top[1] == 0xC3 && top[2] == 0xB2 && top[3] == 0xA1); // little-endian
    CHECK(s.cost16 == kFast + 16 + kFast); // 1 + the stack's latency
    g.one(kPop, kD);
    CHECK(g.x.r.d == 0xA1B2C3D4 && g.x.r.sp == g.x.stack_hi);
    // POP IP returns; POP # discards.
    g.x.r.a = g.code + 50;
    g.one(kPush, kA);
    g.one(kPop, kIp);
    CHECK(g.x.r.ip == g.code + 50);
    g.one(kPush, kImm, 7);
    g.one(kPop, kImm);
    CHECK(g.x.r.sp == g.x.stack_hi && g.x.r.ip == g.code + 2);
    // Empty: POP faults. Full (40 usable bytes = 10 words): the 11th PUSH faults.
    s = g.one(kPop, kA);
    CHECK(s.kind == StepKind::fault_stack && g.x.r.ip == g.code);
    for (int i = 0; i < 10; ++i)
        CHECK(g.one(kPush, kImm, static_cast<i8>(i)).kind == StepKind::done);
    Regs before = g.x.r;
    before.ip = g.code; // where one() puts the instruction
    s = g.one(kPush, kImm, 99);
    CHECK(s.kind == StepKind::fault_stack && memcmp(&before, &g.x.r, sizeof before) == 0);
    // An idle run has no stack.
    g.x.stack_lo = g.x.stack_hi = 0;
    CHECK(g.one(kPush, kA).kind == StepKind::fault_stack && g.one(kPop, kA).kind == StepKind::fault_stack);
}

i32 alu(Rig& g, u8 op, i32 a, i32 s) {
    g.x.r.a = u(a);
    g.x.r.b = u(s);
    g.one(op, kB);
    return static_cast<i32>(g.x.r.a);
}

void test_arithmetic() {
    Rig g;
    const i32 kMin = static_cast<i32>(0x80000000u), kMax = 0x7FFFFFFF;
    CHECK(alu(g, kAdd, kMax, 1) == kMin); // wraps
    CHECK(alu(g, kSub, kMin, 1) == kMax);
    CHECK(alu(g, kMul, 0x10000, 0x10000) == 0); // the low 32 bits
    CHECK(alu(g, kMul, -3, 7) == -21);
    CHECK(alu(g, kDiv, 7, -2) == -3 && alu(g, kDiv, -7, 2) == -3); // truncates toward 0
    CHECK(alu(g, kDiv, 123, 0) == 0);                                // R18
    CHECK(alu(g, kDiv, kMin, -1) == kMin);                           // wraps to itself
    CHECK(alu(g, kAnd, 0xF0F0, 0xFF00) == 0xF000 && alu(g, kOr, 0xF0, 0x0F) == 0xFF && alu(g, kXor, 6, 3) == 5);
    CHECK(alu(g, kNot, 99, 0) == -1); // A <- s with every bit flipped
    CHECK(alu(g, kShl, 1, 33) == 2);  // by s mod 32
    CHECK(alu(g, kShl, 1, -1) == kMin); // -1 mod 32 is 31
    CHECK(alu(g, kShr, kMin, 31) == 1); // fills with zeros
    // The work costs: MUL 3, DIV 20 cycles on a P core; DIV 65% slower on an E core.
    g.x.r.b = 1;
    CHECK(g.one(kMul, kB).cost16 == kFast + 48 && g.one(kDiv, kB).cost16 == kFast + 320);
    const u8 div[1] = {encode(kDiv, kB)};
    CHECK(g.run(div, 1, &g.e).cost16 == kFast + 528);
    CHECK(g.one(kAdd, kB).cost16 == kFast + 16);
}

void test_jumps() {
    Rig g;
    g.one(kJmp, kImm, -2);
    CHECK(g.x.r.ip == g.code - 2);
    g.one(kJmp, kImm, 0); // the two-byte endless loop
    CHECK(g.x.r.ip == g.code);
    g.x.r.b = 77;
    g.one(kJmp, kB); // a computed jump, relative to this instruction
    CHECK(g.x.r.ip == g.code + 77);
    struct Case {
        u8 op;
        i32 a;
        bool taken;
    };
    const Case cases[] = {{kJez, 0, true},  {kJez, 1, false}, {kJnz, -1, true}, {kJnz, 0, false},
                          {kJgz, 1, true},  {kJgz, 0, false}, {kJgz, -1, false}, {kJlz, -1, true},
                          {kJlz, 0, false}, {kJlz, static_cast<i32>(0x80000000u), true}};
    for (const Case& c : cases) {
        g.x.r.a = u(c.a);
        g.one(c.op, kImm, 10);
        CHECK(g.x.r.ip == (c.taken ? g.code + 10 : g.code + 2));
    }
}

void test_copy() {
    Rig g;
    u8* mem = g.m.bytes();
    for (u32 i = 0; i < 18; ++i)
        mem[g.data + i] = static_cast<u8>(i + 1);
    g.x.r.b = g.data;
    g.x.r.d = g.data + 100;
    Step s = g.one(kCpy, kImm, 18);
    CHECK(s.kind == StepKind::done && memcmp(mem + g.data, mem + g.data + 100, 18) == 0);
    CHECK(g.x.r.b == g.data && g.x.r.d == g.data + 100); // registers unchanged
    CHECK(s.results == 18 && s.cost16 == kFast + 16 + 64 + 32 * 18 + kFast + kFast);
    // Overlap is safe: as if through a buffer.
    g.x.r.d = g.data + 1;
    g.one(kCpy, kImm, 18);
    CHECK(mem[g.data + 1] == 1 && mem[g.data + 18] == 18);
    // The count is s mod 65,536.
    g.x.r.c = 65536 + 3;
    g.x.r.d = g.data + 200;
    memset(mem + g.data + 200, 0, 8);
    g.one(kCpy, kC);
    CHECK(mem[g.data + 202] != 0 && mem[g.data + 203] == 0);
    // Across devices: both latencies.
    g.x.r.d = g.slow;
    s = g.one(kCpy, kImm, 4);
    CHECK(s.cost16 == kFast + 16 + 64 + 32 * 4 + kFast + kSlow);
    // Checked whole before any byte moves: a destination it can't write copies nothing.
    const u32 theirs = g.m.alloc_on(Q, 256, Perm::data, 0);
    memset(mem + theirs, 0xEE, 4);
    g.x.r.d = theirs;
    const Regs before = g.x.r;
    s = g.one(kCpy, kImm, 4);
    CHECK(s.kind == StepKind::fault_memory && mem[theirs] == 0xEE && g.x.r.ip == g.code);
    CHECK(g.x.r.b == before.b && g.x.r.d == before.d);
    // CPY 0 moves nothing and pays only the setup.
    s = g.one(kCpy, kImm, 0);
    CHECK(s.kind == StepKind::done && s.cost16 == kFast + 16 + 64);
    // The copy widens its holder's written extent.
    g.x.r.b = g.data;
    g.x.r.d = g.slow + 10;
    g.one(kCpy, kImm, 8);
    const Alloc& r = g.m.record(g.m.alloc_at(g.slow));
    CHECK(r.wlo <= 10 && r.whi >= 18);
}

void test_sys_and_spare() {
    Rig g;
    Step s = g.one(kSys, kImm, 3);
    CHECK(s.kind == StepKind::sys && s.sys_value == 3 && g.x.r.ip == g.code + 2);
    g.x.r.a = u(-1);
    s = g.one(kSys, kA);
    CHECK(s.sys_value == -1 && g.x.r.ip == g.code + 1);
    // The spare slots do nothing: no operand read, so no fault, even through a bad [B].
    g.x.r.b = 0;
    for (u8 op = kFirstSpare; op < 32; ++op) {
        const Regs before = g.x.r;
        s = g.one(op, kAtB);
        CHECK(s.kind == StepKind::done && s.results == 0 && g.x.r.ip == g.code + 1);
        g.x.r.ip = before.ip;
        CHECK(memcmp(&before, &g.x.r, sizeof before) == 0);
        s = g.one(op, kImm, 5);
        CHECK(g.x.r.ip == g.code + 2); // still two bytes with #
    }
}

void test_faults() {
    // On a lax place a data page runs (9.3).
    Rig lax;
    lax.x.r.ip = lax.data;
    lax.m.bytes()[lax.data] = encode(kMov, kA);
    CHECK(step(lax.m, lax.x, lax.p).kind == StepKind::done);

    Rig g(true); // strict, from here on
    // Running a data page faults; nothing changes.
    g.x.r.ip = g.data;
    g.m.bytes()[g.data] = encode(kMov, kImm);
    const Regs before = g.x.r;
    Step s = step(g.m, g.x, g.p);
    CHECK(s.kind == StepKind::fault_memory && memcmp(&before, &g.x.r, sizeof before) == 0);
    // Reading through [B] at address 0, or into someone else's page.
    g.x.r.b = 0;
    CHECK(g.one(kMov, kAtB).kind == StepKind::fault_memory && g.x.r.ip == g.code);
    g.x.r.b = g.m.alloc_on(Q, 256, Perm::data, 0);
    CHECK(g.one(kAdd, kAtB).kind == StepKind::fault_memory);
    // Writing to an executable page faults; reading it doesn't.
    const u32 exec = g.m.alloc_on(P, 256, Perm::exec, 0);
    g.x.r.d = exec;
    CHECK(g.one(kSto, kAtD).kind == StepKind::fault_memory && g.one(kMov, kAtD).kind == StepKind::done);
    // A two-byte instruction whose # byte lies past the runnable page.
    const u32 last = g.code + 511; // the code allocation's last byte; the next page is data
    g.m.bytes()[last] = encode(kMov, kImm);
    g.x.r.ip = last;
    s = step(g.m, g.x, g.p);
    CHECK(s.kind == StepKind::fault_memory && s.length == 2 && g.x.r.ip == last);
    // The access cache notices a FREE: the next fetch from freed code faults.
    g.x.r.ip = exec;
    g.m.bytes()[exec] = encode(kMov, kA);
    CHECK(step(g.m, g.x, g.p).kind == StepKind::done);
    CHECK(g.m.free(P, exec));
    g.x.r.ip = exec;
    CHECK(step(g.m, g.x, g.p).kind == StepKind::fault_memory);
    // An idle run runs the reset region, and nothing else of anyone's.
    ExecContext run;
    run.holder = 1;
    run.r.ip = g.m.region_base();
    g.m.bytes()[run.r.ip] = encode(kMov, kImm);
    g.m.bytes()[run.r.ip + 1] = 4;
    CHECK(step(g.m, run, g.p).kind == StepKind::done && run.r.a == 4);
    run.r.ip = g.code;
    CHECK(step(g.m, run, g.p).kind == StepKind::fault_memory);
}

// Every instruction's cost on both core kinds (8.4, 7.2): the fetch, its work,
// and the stack or copy extras where it has them. Register operands, so no
// operand latency.
void test_costs_every_instruction() {
    for (int kind = 0; kind < 2; ++kind) {
        Rig g;
        const CostTable& t = kind == 0 ? g.p : g.e;
        u32 bad = 0;
        for (u32 op = 0; op < 32; ++op) {
            g.x.r = Regs{};
            g.x.r.sp = g.x.stack_hi - 8; // room to PUSH and to POP
            g.x.r.b = g.data;
            g.x.r.d = g.data + 64;
            const u8 b[1] = {encode(static_cast<u8>(op), kA)}; // A = 0: CPY moves nothing
            const Step s = g.run(b, 1, &t);
            u32 want = kFast + t.work[op];
            if (op == kPush || op == kPop)
                want += kFast; // the stack's device
            if (op == kCpy)
                want += t.cpy_setup;
            if (s.cost16 != want) {
                fprintf(stderr, "  cost of op %u on %s: %u, want %u\n", op, kind ? "E" : "P", s.cost16, want);
                ++bad;
            }
        }
        CHECK(bad == 0);
    }
    // The E table is slower everywhere it should be.
    const CostTable p = default_p_costs(), e = default_e_costs();
    bool slower = e.cpy_per_byte > p.cpy_per_byte && e.cpy_setup > p.cpy_setup;
    for (u32 op = 0; op < 32; ++op)
        slower = slower && e.work[op] > p.work[op];
    CHECK(slower);
}

u32 g_flips = 0;
u32 next_gap(ErrorClock* c) {
    ++g_flips;
    c->left = 1000;
    return 5; // flip bit 5 of a register, bit 5 of a byte
}

void test_error_floor() {
    Rig g;
    ErrorClock clock;
    clock.left = 2;
    clock.next = next_gap;
    g.x.clock = &clock;
    g.one(kMov, kImm, 0);
    CHECK(g.x.r.a == 0 && clock.left == 1);
    g.one(kMov, kImm, 0); // the second result is wrong
    CHECK(g.x.r.a == 32 && g_flips == 1 && clock.left == 1000);
    // Inside a copy: the 3rd byte moved is wrong.
    clock.left = 3;
    u8* mem = g.m.bytes();
    memset(mem + g.data, 0, 10);
    g.x.r.b = g.data;
    g.x.r.d = g.data + 50;
    g.one(kCpy, kImm, 10);
    CHECK(mem[g.data + 52] == 32 && mem[g.data + 51] == 0 && mem[g.data + 53] == 0);
    CHECK(g_flips == 2 && clock.left == 1000 - 7);
    // A clock with no draw never makes an error.
    ErrorClock quiet;
    quiet.left = 1;
    g.x.clock = &quiet;
    g.one(kMov, kImm, 0);
    CHECK(g.x.r.a == 0 && quiet.left == ~0ull);
}

// The disassembler: each instruction by name, immediates with their byte, the
// spare codes as nop, and an immediate cut off at the end said so.
void test_disassemble() {
    const u8 code[] = {u8(kMov << 3 | kImm), u8(-5),  u8(kCpy << 3 | kAtB), u8(kJmp << 3 | kIp),
                       u8(22 << 3 | kA),     u8(kSys << 3 | kImm)};
    TextBuf t;
    disassemble(code, sizeof code, &t);
    CHECK(t.view().eq(Str("MOV # -5; CPY [B]; JMP IP; NOP A; SYS # (cut)")));
    TextBuf none;
    disassemble(code, 0, &none);
    CHECK(none.size() == 0);
}

} // namespace

void suite_cpu() {
    test_data_moves();
    test_stack();
    test_arithmetic();
    test_jumps();
    test_copy();
    test_sys_and_spare();
    test_faults();
    test_costs_every_instruction();
    test_error_floor();
    test_disassemble();
}
