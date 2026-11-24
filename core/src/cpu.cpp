#include "nova2/cpu.h"

#include <cstring> // memcpy, memmove

#include "nova2/hash.h"

namespace nova2 {

CostTable default_p_costs() {
    CostTable t{};
    for (u32 i = 0; i < 32; ++i)
        t.work[i] = 16; // 1 cycle
    t.work[kMul] = 48;  // 3
    t.work[kDiv] = 320; // 20
    t.cpy_setup = 64;   // 4
    t.cpy_per_byte = 32; // 2 per byte
    return t;
}

CostTable default_e_costs() {
    CostTable t{};
    for (u32 i = 0; i < 32; ++i)
        t.work[i] = 18; // 15% slower on simple work
    t.work[kMul] = 55;
    t.work[kDiv] = 528; // 65% slower on divides
    t.cpy_setup = 74;
    t.cpy_per_byte = 37;
    return t;
}

namespace {

// May the context do `want` at addr? Remembers the page while its version
// holds. The remembered case is inline, so the loops that run instructions
// take it without a call.
bool access_slow(const Memory& m, PageCache& c, u32 addr, u32 holder, u8 want) {
    if (!m.allows(addr, holder, want))
        return false;
    const u32 p = m.page_of(addr);
    const Page& pg = m.page(p);
    c.page = p;
    c.version = pg.version;
    c.latency16 = m.device(pg.device).latency16;
    return true;
}

NOVA2_INLINE bool access(const Memory& m, PageCache& c, u32 addr, u32 holder, u8 want) {
    if (addr >= m.size())
        return false;
    const u32 p = m.page_of(addr);
    if (p == c.page && m.page(p).version == c.version)
        return true;
    return access_slow(m, c, addr, holder, want);
}

// Counts one result for the error floor, and flips one of its bits when the
// core's countdown runs out (7.5).
NOVA2_INLINE u32 result(ExecContext& x, u32& results, u32 v, u32 width) {
    ++results;
    ErrorClock* c = x.clock;
    if (!c || c->left == ~0ull || --c->left != 0) // ~0: never (a rate of 0)
        return v;
    if (!c->next) {
        c->left = ~0ull;
        return v;
    }
    return v ^ (1u << (c->next(c) % width));
}

NOVA2_INLINE u32 load32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

NOVA2_INLINE void store32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
}

NOVA2_INLINE u32 sext8(u8 b) { return static_cast<u32>(static_cast<i32>(static_cast<i8>(b))); }

NOVA2_INLINE u32 mem_addr(const ExecContext& x, u8 od) { return od == kAtB ? x.r.b : x.r.d; }

// Whether an operand code names a byte of memory (@B or @D), as 0 or 1.
NOVA2_INLINE u32 is_mem(u8 od) { return static_cast<u32>(od) - kAtB < 2u ? 1u : 0u; }

// The per-access counters (cpu.h), when on: a data access to a page other than
// the last one this context touched is a row activation.
NOVA2_INLINE void touch(ExecContext& x, const Memory& m, u32 addr) {
    if (!x.acts)
        return;
    const u32 row = m.page_of(addr);
    if (row != x.last_row) {
        if (++x.acts[row] == m.hot_at())
            m.note_hot(row); // it has reached the lowest rowhammer threshold (9.8)
        ++*x.act_total;
        x.last_row = row;
    }
}

// CPY counts once per page it touches (9.8).
void touch_range(ExecContext& x, const Memory& m, u32 addr, u32 n) {
    if (!x.acts || n == 0)
        return;
    const u32 first = m.page_of(addr), last = m.page_of(addr + n - 1);
    for (u32 p = first; p <= last; ++p)
        if (++x.acts[p] == m.hot_at())
            m.note_hot(p);
    if (last >= first)
        *x.act_total += last - first + 1;
    x.last_row = last;
}

// Reads a memory operand: the byte at addr. False on a memory fault.
NOVA2_INLINE bool read_mem(Memory& m, ExecContext& x, u32 addr, u32* v, u32* cost) {
    if (!access(m, x.read, addr, x.holder, kRead))
        return false;
    *v = sext8(m.bytes()[addr]);
    touch(x, m, addr);
    *cost += x.read.latency16;
    return true;
}

// Whether an operand can be written (only memory operands can refuse).
NOVA2_INLINE bool can_write(Memory& m, ExecContext& x, u8 od) {
    if (is_mem(od) == 0)
        return true;
    return access(m, x.write, mem_addr(x, od), x.holder, kWrite);
}

// Writes an operand, after can_write said yes. IP jumps; # takes nothing.
NOVA2_INLINE void write_dst(Memory& m, ExecContext& x, u32& results, u8 od, u32 v, u32* next_ip, u32* cost) {
    if (od < kIp) { // A, B, C or D, where kRegAt says it lies
        const u32 r = result(x, results, v, 32);
        memcpy(reinterpret_cast<u8*>(&x.r) + od * 4, &r, 4);
    } else if (od == kIp) {
        *next_ip = result(x, results, v, 32);
    } else if (od != kImm) { // @B or @D
        const u32 addr = mem_addr(x, od);
        m.bytes()[addr] = static_cast<u8>(result(x, results, v, 8));
        touch(x, m, addr);
        m.note_write(addr, 1, x.holder);
        *cost += x.write.latency16;
    }
}

NOVA2_INLINE void fault(StepKind& kind, u32& cost16, StepKind why, u32 cost) {
    kind = why;
    cost16 = cost;
}

// May the context run the byte at addr? [lo, lo + len) is the page its last
// such check passed for, when the caller knows nothing has changed who holds
// what since (run_steps: only a call to the OS does, and a call ends its
// stretch); an address there needs no second look. The context's cache of
// that page (x.run) is the same page, so its latency is still the one to
// charge.
NOVA2_INLINE bool fetch(const Memory& m, ExecContext& x, u32 addr, u32& lo, u32& len) {
    if (addr - lo < len)
        return true;
    if (!access(m, x.run, addr, x.holder, kRun))
        return false;
    len = m.page_size();
    lo = addr & ~(len - 1);
    return true;
}

// Where in Regs each operand code's value lies, a byte a code, the lowest
// first: A, B, C, D, IP, then B for @B and D for @D (# has no register).
constexpr u64 kRegAt = 0x000C04100C080400ull;

// The instructions that begin by reading their operand, whatever it is: MOV,
// SWP, the ten of arithmetic, the five jumps, CPY and SYS. STO and POP never
// read theirs, a spare slot has none, and PUSH reads it only once it knows
// it has a stack.
constexpr u32 kReadsFirst = (1u << kMov) | (1u << kSwp) | (1u << kAdd) | (1u << kSub) | (1u << kMul) |
                            (1u << kDiv) | (1u << kAnd) | (1u << kOr) | (1u << kXor) | (1u << kNot) |
                            (1u << kShl) | (1u << kShr) | (1u << kJmp) | (1u << kJez) | (1u << kJnz) |
                            (1u << kJgz) | (1u << kJlz) | (1u << kCpy) | (1u << kSys);

// One instruction: step_into's work, inlined into it and into run_steps'
// loop.
//
// A place starts as random bytes, so a branch on an instruction's bits is
// mispredicted about as often as it has ways to go. So the operand is read
// without one: its value is loaded from where its code says, and only a
// memory operand (two codes of the eight) takes a branch, for its access
// check. And each instruction has a case of its own, so the jump on the
// instruction's code is the only one (optimization-plan.md 4.2).
//
// ip is x.r.ip, handed in, and after is what the step leaves there: a loop
// carries it from one instruction to the next in a register. Read back from
// x.r.ip each time, it waits on the store just made, and that wait is on the
// path from every instruction to the next.
//
// What an instruction's byte says before anything runs, looked up by the
// byte: its length, where its operand's register lies (kRegAt's byte), and
// whether its operand is a byte of memory that is read first.
struct DecodeTable {
    u8 len[256];
    u8 reg_at[256];
    u8 reads[256];
};

constexpr DecodeTable make_decode() {
    DecodeTable d{};
    for (u32 b = 0; b < 256; ++b) {
        const u32 op = b >> 3, od = b & 7;
        d.len[b] = od == kImm ? 2 : 1;
        d.reg_at[b] = static_cast<u8>((kRegAt >> (od * 8)) & 0xFF);
        d.reads[b] = static_cast<u8>((kReadsFirst >> op) & (od - kAtB < 2u ? 1u : 0u));
    }
    return d;
}

constexpr DecodeTable kDecode = make_decode();

// What happened comes back in the last five, a Step's fields one by one
// (fresh, as a Step is): in a loop they are registers, where a Step would
// be memory. lo and len are fetch's.
NOVA2_INLINE void step_one(Memory& m, ExecContext& x, const CostTable& t, const u32 ip, u32& after, u32& lo,
                           u32& len_ok, StepKind& kind, u8& length, i32& sys_value, u32& cost16, u32& results) {
    after = ip; // a fault leaves it
    if (!fetch(m, x, ip, lo, len_ok))
        return fault(kind, cost16, StepKind::fault_memory, t.work[0]);
    u8* mem = m.bytes();
    const u8 byte = mem[ip];
    const u8 op = byte >> 3;
    const u8 od = byte & 7;
    const u8 len = kDecode.len[byte];
    length = len;
    u32 cost = x.run.latency16 + t.work[op];
    if (len == 2 && (ip + 1 == 0 || !fetch(m, x, ip + 1, lo, len_ok)))
        return fault(kind, cost16, StepKind::fault_memory, cost);
    const u32 imm = len == 2 ? sext8(mem[ip + 1]) : 0;
    u32 next_ip = ip + len;
    // The operand as a value, by its code: A, B, C, D, IP, @B, @D, #. For a
    // memory operand that is its address (B or D), and the byte there is read
    // next. Where a code's register lies is a byte of kRegAt, so the value is
    // one load with no branch.
    u32 v;
    memcpy(&v, reinterpret_cast<const u8*>(&x.r) + kDecode.reg_at[byte], 4);
    v = od == kImm ? imm : v;
    if (kDecode.reads[byte] != 0) {
        if (!read_mem(m, x, v, &v, &cost))
            return fault(kind, cost16, StepKind::fault_memory, cost);
    }

    switch (op) {
    case kMov:
        if (od != kA) // MOV A is neutral
            x.r.a = result(x, results, v, 32);
        break;
    case kSto:
        if (od == kA || od == kImm) // neutral
            break;
        if (!can_write(m, x, od))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        write_dst(m, x, results, od, x.r.a, &next_ip, &cost);
        break;
    case kSwp:
        if (od == kA)
            break;
        if (od == kImm) {
            x.r.a = result(x, results, imm, 32);
            break;
        }
        if (!can_write(m, x, od))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        {
            const u32 old = x.r.a;
            x.r.a = result(x, results, v, 32);
            write_dst(m, x, results, od, old, &next_ip, &cost);
        }
        break;
    case kPush: {
        if (x.stack_hi == 0)
            return fault(kind, cost16, StepKind::fault_stack, cost); // an idle run has no stack
        if (is_mem(od) != 0 && !read_mem(m, x, v, &v, &cost))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        const u32 sp = x.r.sp;
        if (sp < x.stack_lo + 4 || sp > x.stack_hi)
            return fault(kind, cost16, StepKind::fault_stack, cost);
        const u32 at = sp - 4;
        if (!access(m, x.write, at, x.holder, kWrite) || !access(m, x.write, at + 3, x.holder, kWrite))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        store32(mem + at, result(x, results, v, 32));
        touch(x, m, at);
        m.note_write(at, 4, x.holder);
        x.r.sp = at;
        cost += x.write.latency16;
        break;
    }
    case kPop: {
        if (x.stack_hi == 0)
            return fault(kind, cost16, StepKind::fault_stack, cost);
        const u32 sp = x.r.sp;
        if (sp < x.stack_lo || sp > x.stack_hi - 4)
            return fault(kind, cost16, StepKind::fault_stack, cost);
        if (!access(m, x.read, sp, x.holder, kRead) || !access(m, x.read, sp + 3, x.holder, kRead))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        cost += x.read.latency16;
        v = load32(mem + sp);
        touch(x, m, sp);
        if (!can_write(m, x, od))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        x.r.sp = sp + 4;
        write_dst(m, x, results, od, v, &next_ip, &cost); // POP # discards
        break;
    }
    case kAdd:
        x.r.a = result(x, results, x.r.a + v, 32);
        break;
    case kSub:
        x.r.a = result(x, results, x.r.a - v, 32);
        break;
    case kMul:
        x.r.a = result(x, results, x.r.a * v, 32);
        break;
    case kDiv: {
        const u32 a = x.r.a;
        const i32 sa = static_cast<i32>(a), sv = static_cast<i32>(v);
        u32 r;
        if (sv == 0)
            r = 0; // R18
        else if (sa == static_cast<i32>(0x80000000u) && sv == -1)
            r = a; // wraps to itself
        else
            r = static_cast<u32>(sa / sv);
        x.r.a = result(x, results, r, 32);
        break;
    }
    case kAnd:
        x.r.a = result(x, results, x.r.a & v, 32);
        break;
    case kOr:
        x.r.a = result(x, results, x.r.a | v, 32);
        break;
    case kXor:
        x.r.a = result(x, results, x.r.a ^ v, 32);
        break;
    case kNot:
        x.r.a = result(x, results, ~v, 32);
        break;
    case kShl:
        x.r.a = result(x, results, x.r.a << (v & 31), 32);
        break;
    case kShr:
        x.r.a = result(x, results, x.r.a >> (v & 31), 32);
        break;
    case kJmp:
    case kJez:
    case kJnz:
    case kJgz:
    case kJlz: {
        // Which of the five are taken at this A, a bit each, in their order.
        const i32 a = static_cast<i32>(x.r.a);
        const u32 taken = 1u | (a == 0 ? 2u : 0u) | (a != 0 ? 4u : 0u) | (a > 0 ? 8u : 0u) | (a < 0 ? 16u : 0u);
        if ((taken >> (op - kJmp)) & 1)
            next_ip = result(x, results, ip + v, 32);
        break;
    }
    case kCpy: {
        const u32 n = v & 0xFFFF; // s mod 65,536
        cost += t.cpy_setup;
        if (n == 0)
            break;
        const u32 from = x.r.b, to = x.r.d;
        if (!m.allows_range(from, n, x.holder, kRead) || !m.allows_range(to, n, x.holder, kWrite))
            return fault(kind, cost16, StepKind::fault_memory, cost);
        memmove(mem + to, mem + from, n);
        touch_range(x, m, from, n);
        touch_range(x, m, to, n);
        // Every byte moved is a result: the error floor may land inside the copy.
        results += n;
        ErrorClock* c = x.clock;
        if (c && c->left != ~0ull) {
            u32 pos = 0; // bytes already counted
            while (c->left <= n - pos) { // the next wrong result lands in this copy
                const u32 at = pos + static_cast<u32>(c->left) - 1;
                if (!c->next) {
                    c->left = ~0ull;
                    pos = n;
                    break;
                }
                mem[to + at] ^= static_cast<u8>(1u << (c->next(c) % 8)); // next() sets a fresh left
                pos = at + 1;
            }
            c->left -= n - pos;
        }
        m.note_write(to, n, x.holder);
        cost += t.cpy_per_byte * n + m.device(m.device_of(from)).latency16 + m.device(m.device_of(to)).latency16;
        break;
    }
    case kSys:
        kind = StepKind::sys;
        sys_value = static_cast<i32>(v);
        break;
    default: // the spare slots do nothing
        break;
    }
    x.r.ip = next_ip;
    after = next_ip;
    cost16 = cost;
}

} // namespace

void step_into(Memory& m, ExecContext& x, const CostTable& t, Step& s) {
    u32 after, lo = 0, len = 0; // no page known: every fetch is checked
    step_one(m, x, t, x.r.ip, after, lo, len, s.kind, s.length, s.sys_value, s.cost16, s.results);
}

void run_steps(Memory& m, ExecContext& x, const CostTable& t, i64 budget16, u64* path, Stretch& out) {
    const u32 size = m.size();
    const u8* const bytes = m.bytes();
    u64 acc = path ? *path : 0;
    u64 n = 0;
    u32 ip = x.r.ip;
    u32 lo = 0, len = 0; // the page fetches are known to be allowed in: none yet
    for (;;) {
        bool cpy = false;
        if (path && ip < size) { // the path: the bytes executed (an observer's, not state)
            const u8 first = bytes[ip];
            const u64 v = static_cast<u64>(first) | (ip + 1 < size ? static_cast<u64>(bytes[ip + 1]) << 8 : 0);
            acc = mix64(acc ^ v);
            cpy = (first >> 3) == kCpy;
        }
        StepKind kind = StepKind::done; // a fresh Step's fields
        u8 length = 1;
        i32 sys_value = 0;
        u32 cost16 = 0, results = 0, after;
        step_one(m, x, t, ip, after, lo, len, kind, length, sys_value, cost16, results);
        ++n;
        budget16 -= cost16;
        if (kind != StepKind::done || cpy || budget16 <= 0) {
            out.last.kind = kind;
            out.last.length = length;
            out.last.sys_value = sys_value;
            out.last.cost16 = cost16;
            out.last.results = results;
            out.ip = ip;
            out.cpy = cpy;
            break;
        }
        ip = after;
    }
    if (path)
        *path = acc;
    out.instructions = n;
    out.left16 = budget16;
}

} // namespace nova2
