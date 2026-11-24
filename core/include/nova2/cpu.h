#pragma once

// The interpreter (design.md 8, appendix A; build-plan-gate1.md step 4): the
// 22 instructions on one thread's (or idle run's) registers, against a place's
// memory, with their costs in sixteenths of a cycle.
//
// It executes one instruction at a time and reports what happened: done, a
// fault, or a SYS for the OS to carry out (step 5). Scheduling, handlers and
// the calls themselves live elsewhere.
#include "nova2/memory.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

// Instruction codes (A.1: a structural constant). A byte is code * 8 + operand.
enum Op : u8 {
    kMov = 0, kSto, kSwp, kPush, kPop,
    kAdd, kSub, kMul, kDiv,
    kAnd, kOr, kXor, kNot,
    kShl, kShr,
    kJmp, kJez, kJnz, kJgz, kJlz,
    kCpy,
    kSys,
    kFirstSpare // 22..31 do nothing
};

// Operand codes (A.1).
enum Operand : u8 { kA = 0, kB, kC, kD, kIp, kAtB, kAtD, kImm };

inline constexpr u8 encode(u8 op, u8 operand) { return static_cast<u8>(op * 8 + operand); }

// The saved state of a thread: A-D, IP and SP (A.3: 24 bytes at the base of
// its stack while it's off its core).
struct Regs {
    u32 a = 0, b = 0, c = 0, d = 0, ip = 0, sp = 0;
};

inline constexpr u32 kSavedRegsBytes = 24;
// A handler's frame on its thread's stack: IP, D, C, B and A as they were (A.3).
inline constexpr u32 kHandlerFrameBytes = 20;

// Costs in sixteenths of a cycle (8.4), one table per core kind (7.2). The
// crossing and the call's own work belong to the OS.
struct CostTable {
    u16 work[32];     // per instruction code
    u16 cpy_setup;
    u16 cpy_per_byte;
};

CostTable default_p_costs(); // placeholders from 8.4
CostTable default_e_costs(); // about 15% slower on simple work, 65% on divides (7.2)

// The error floor's countdown (7.5, appendix C), kept per core. Each result
// counts one; when `left` reaches 0 the result is wrong: `next` draws the next
// gap into `left` and returns random bits that pick the bit to flip. A `left`
// of ~0 means never (a rate of 0). With no
// `next`, nothing is ever wrong.
struct ErrorClock {
    u64 left = ~0ull;
    u32 (*next)(ErrorClock* self) = nullptr;
    void* owner = nullptr;
    u8 level = 10; // the clock level (tenths of stock) the rate is drawn for (7.5)
    // Draws `left` afresh for a new level. A countdown at a fixed rate has no
    // memory, so redrawing when the rate changes keeps it exact.
    void (*redraw)(ErrorClock* self) = nullptr;
};

// Moves a core's error countdown to a clock level (a thread switch, or a
// new SETCLOCK taking effect).
inline void set_level(ErrorClock& c, u8 level) {
    if (c.level == level)
        return;
    c.level = level;
    if (c.redraw)
        c.redraw(&c);
}

// One remembered page check (run, read or write): valid while the page's
// version is unchanged, since every change to who holds what, or to the
// permission, bumps it.
struct PageCache {
    u32 page = ~0u;
    u32 version = 0;
    u32 latency16 = 0;
};

// What executes: a thread or an idle run.
struct ExecContext {
    Regs r;
    u32 holder = 0;   // whose access applies (memory.h's holder numbers)
    u32 stack_lo = 0; // the usable stack, [stack_lo, stack_hi); both 0: no stack (an idle run)
    u32 stack_hi = 0;
    ErrorClock* clock = nullptr; // the core's
    PageCache run, read, write;

    // Field by field: copied whole, the host reads back what it has only just
    // written in smaller pieces, and waits for it.
    void forget_pages() {
        run.page = read.page = write.page = ~0u;
        run.version = read.version = write.version = 0;
        run.latency16 = read.latency16 = write.latency16 = 0;
    }

    // The per-access counters (9.8: rowhammer counts row activations; 4.5:
    // memory draws per access), always on from gate 2 (a place turns them
    // on). A data access counts an activation of its page when it touches a
    // different page from this context's last; CPY counts once per page it
    // touches. Fetches don't count: real cores fetch code from their caches.
    // act_total counts them all, for memory's draw. The scheduler forgets
    // last_row at each tick's start, so no count depends on an earlier tick.
    u32* acts = nullptr;
    u64* act_total = nullptr;
    u32 last_row = ~0u;
};

enum class StepKind : u8 { done, fault_memory, fault_stack, sys };

struct Step {
    StepKind kind = StepKind::done;
    u8 length = 1;     // the instruction's bytes (a fault's saved IP goes past them, A.3)
    i32 sys_value = 0; // SYS: its operand's value, for the OS to map to a call (9.2)
    u32 cost16 = 0;    // what it cost, in sixteenths of a cycle
    u32 results = 0;   // results it produced (the error floor's unit)
};

// Executes the instruction at x.r.ip. On done or sys, IP has moved on (or
// jumped); on a fault nothing has changed but the caches, and IP still names
// the faulting instruction.
//
// step_into writes what happened into s, which must be fresh (Step{}): the
// loops that run instructions read it field by field where it lies, with no
// copy of the whole (optimization-plan.md 4.2). step returns it instead.
void step_into(Memory& mem, ExecContext& x, const CostTable& costs, Step& s);
inline Step step(Memory& mem, ExecContext& x, const CostTable& costs) {
    Step s;
    step_into(mem, x, costs, s);
    return s;
}

// What a stretch of instructions came to (run_steps).
struct Stretch {
    Step last;            // its last instruction, as step_into leaves it
    u32 ip = 0;           // where that instruction was
    bool cpy = false;     // it was a CPY, and the stretch was following a path
    u64 instructions = 0; // how many ran, the last among them
    i64 left16 = 0;       // the budget less what they cost (it can fall below 0)
};

// Runs instructions one after another, each as step_into would, while each
// is done and the budget holds: the loop every instruction runs in, with its
// counts kept in registers and the step inlined (optimization-plan.md 4.2).
// It ends after an instruction that faulted or was a SYS, or once budget16
// is spent (one instruction always runs, so call it with a budget above 0).
//
// path, when given, is an observer's running hash of the bytes executed (the
// idle runs' paths, region.h): each instruction's first two bytes are mixed
// in before it runs. A stretch with a path also ends after a CPY, so that
// its caller can count the copy.
void run_steps(Memory& mem, ExecContext& x, const CostTable& costs, i64 budget16, u64* path, Stretch& out);

// The instructions in a run of code bytes, as text ("MOV IP; STO D; MOV # 2;
// ..."), for the observers and the genome browser (13.2). Calls show their
// number as written; the numbering table names them (9.2).
void disassemble(const u8* code, u32 len, TextBuf* out);

} // namespace nova2
