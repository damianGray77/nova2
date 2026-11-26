#include "nova2/os.h"

#include <cstring>  // memcpy
#include <intrin.h> // _mm_prefetch, _BitScanForward64, _umul128

#include "nova2/hash.h"
#include "nova2/net.h"

namespace nova2 {

namespace {

u32 load32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

void store32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
}

void say(TextBuf* why, const char* msg) {
    if (why)
        why->append(msg);
}

// stacks_'s key for a stack at `base` of a thread of program `holder`.
u64 stack_key(u32 holder, u32 base) { return static_cast<u64>(holder) << 32 | base; }


} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool Os::init(Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const OsSettings& s, TextBuf* why) {
    if (s.slot_count < 1 || s.slot_count > kMaxSlots) {
        say(why, "os.slots: there must be 1 to 64 call slots");
        return false;
    }
    for (u32 i = 0; i < s.slot_count; ++i)
        if (s.slots[i] >= kCallCount) {
            say(why, "os.slots: every slot must name one of the 16 calls");
            return false;
        }
    if (s.stack_size < kSavedRegsBytes + 4 || s.stack_size > mem->max_alloc()) {
        say(why, "os.stack_size: a stack must hold the saved registers and a word, and fit one allocation");
        return false;
    }
    if (s.clock_min < 1 || s.clock_min > s.clock_max || s.clock_max > 255) {
        say(why, "os.clock: the clock range must run from at least 1 to at most 255 tenths, in order");
        return false;
    }
    if (s.ports < 1) {
        say(why, "os.ports: a place needs at least port 0");
        return false;
    }
    if (core_count < 1) {
        say(why, "os: a place needs at least one core");
        return false;
    }
    if (s.boot_block < 1) {
        say(why, "disk.boot_block: the boot block must be at least a byte");
        return false;
    }
    if (s.inbox_bytes < 1) {
        say(why, "net.inbox: an inbox must hold at least a byte");
        return false;
    }
    mem_ = mem;
    cores_ = cores;
    core_count_ = core_count;
    env_ = env;
    s_ = s;
    // A thread needs a stack, so stacks bound threads, and threads bound programs.
    const u32 cap = mem->size() / s.stack_size + 1;
    programs_.resize(0);
    programs_.resize(cap);
    threads_.resize(0);
    threads_.resize(cap);
    if (kFirstProgramHolder + cap > mem->holder_capacity())
        NOVA2_FATAL("os: more programs than memory has holders (cannot happen)");
    next_thread_id_ = 1;
    next_uid_ = 1;
    static_known_ = false; // until note_static
    live_programs_ = live_threads_ = 0;
    thread_top_ = thread_free_ = program_free_ = waiters_ = timers_ = 0;
    program_top_ = 0;
    stacks_.clear();
    block_first_.resize(0);
    block_first_.resize((mem->size() >> kStackBlockShift) + 1);
    block_next_.resize(0);
    block_next_.resize(cap);
    attention_.resize(0);
    attention_.resize(cap);
    saved_at_.resize(0);
    saved_at_.resize(cap);
    saved_at_.fill(kNowhere);
    frame_at_.resize(0);
    frame_at_.resize(cap);
    frame_at_.fill(kNowhere);
    due_.resize(0);
    due_.resize(cap);
    due_.fill(kNever);
    due_min_.resize(0);
    due_min_.resize((cap + kDueGroup - 1) >> kDueGroupShift);
    due_min_.fill(kNever);
    recv_key_.resize(0);
    recv_key_.resize(cap);
    data_key_.resize(0);
    data_key_.resize(cap);
    recv_count_.resize(0);
    recv_count_.resize(s.ports);
    data_count_.resize(0);
    data_count_.resize(s.ports);
    free_bits_.resize(0);
    free_bits_.resize((static_cast<usize>(cap) + 63) / 64);
    for (u32 t = 0; t < cap; ++t)
        free_bits_[t >> 6] |= 1ull << (t & 63); // every record starts free
    births.clear();
    deaths.clear();
    cold_starts.clear();
    fact_bytes.clear();
    births.reserve(1024);
    deaths.reserve(1024);
    fact_bytes.reserve(64 * 1024);
    for (u32 q = 0; q < 3; ++q) {
        rq_[q].e.resize(0);
        rq_[q].e.resize(2 * static_cast<usize>(cap) + 1);
        rq_[q].head = rq_[q].size = 0;
    }
    rq_seq_ = 0;
    disk_.resize(0);
    disk_.resize(env->disk_size);
    ops_.clear();
    ops_.reserve(cap + core_count);
    op_seq_ = 0;
    disk_on_ = true;
    boot_writer_ = 0; // the disk starts as static (10.5)
    flight_.clear();
    flight_old_.clear();
    torn_writes = failed_reads = 0;
    outbox_.clear();
    outbox_bytes_.clear();
    inbox_.clear();
    inbox_store_.clear();
    inbox_used_.resize(0);
    inbox_used_.resize(s.ports);
    asks_.clear();
    wait_seq_ = 0;
    sends.clear();
    drifts.clear();
    recvs.clear();
    overruns.clear();
    recv_direct = inbox_joined = inbox_dropped = inbox_taken = 0;
    memset(calls, 0, sizeof calls);
    memset(calls_ok, 0, sizeof calls_ok);
    sends_other = sends_other_ok = 0;
    spawns_thread = spawns_thread_ok = spawns_program = spawns_program_ok = 0;
    spawns_limited = 0;
    crowded_for_ = 0;
    kills = killed_bytes = killed_threads = 0;
    watchdog_faults = watchdog_handled = 0;
    note_working();
    return true;
}

void Os::clear_facts() {
    births.clear();
    deaths.clear();
    cold_starts.clear();
    sends.clear();
    drifts.clear();
    recvs.clear();
    overruns.clear();
    fact_bytes.clear();
}

u64 Os::holder_of(u32 addr, u32 len) const {
    constexpr u32 kMost = 64;
    u64 who[kMost];
    u32 held[kMost];
    u32 n = 0;
    const u32 end = addr + len;
    for (u32 a = addr; a < end;) {
        u32 stop = (a / kGranule + 1) * kGranule;
        const u32 id = mem_->alloc_at(a);
        if (id) {
            const Alloc& r = mem_->record(id);
            stop = r.base + r.extent;
            stop = stop < end ? stop : end;
            if (r.holder >= kFirstProgramHolder && programs_[r.holder - kFirstProgramHolder].alive) {
                const u64 uid = programs_[r.holder - kFirstProgramHolder].uid;
                u32 k = 0;
                while (k < n && who[k] != uid)
                    ++k;
                if (k == n && n < kMost) {
                    who[n] = uid;
                    held[n++] = 0;
                }
                if (k < n)
                    held[k] += stop - a;
            }
        }
        a = stop;
    }
    u32 best = 0;
    for (u32 k = 1; k < n; ++k)
        if (held[k] > held[best])
            best = k;
    return n ? who[best] : 0;
}

u32 Os::cost16(const Core& c, u32 cycles) const {
    return cycles * s_.call_factor16[c.kind == DeviceKind::e_core ? 1 : 0];
}

u32 Os::holder_on(u32 core) const {
    const Core& c = cores_[core];
    if (c.idle_run)
        return core + 1;
    return c.thread ? threads_[c.thread - 1].program : 0;
}

// ---------------------------------------------------------------------------
// Programs and threads
// ---------------------------------------------------------------------------

u32 Os::new_program(BirthCause cause, u64 parent_uid) {
    for (u32 p = program_free_; p < programs_.size(); ++p) { // the first free record
        Program& pr = programs_[p];
        if (pr.alive)
            continue;
        program_free_ = p + 1;
        pr = Program{};
        pr.alive = true;
        program_top_ = p + 1 > program_top_ ? p + 1 : program_top_;
        pr.cause = cause;
        pr.uid = (static_cast<u64>(env_->place_id) + 1) << 40 | next_uid_++;
        pr.parent_uid = parent_uid;
        pr.born_tick = env_->tick;
        ++live_programs_;
        return p;
    }
    return ~0u;
}

u32 Os::new_thread(u32 holder, u32 start, u32 value, u32 device) {
    // The first free record: none lies below thread_free_, and from there
    // the free records' bits are read a word at a time, not the records.
    u32 t = static_cast<u32>(threads_.size());
    for (usize w = thread_free_ >> 6; w < free_bits_.size(); ++w) {
        u64 bits = free_bits_[w];
        if (w == thread_free_ >> 6)
            bits &= ~0ull << (thread_free_ & 63);
        unsigned long i;
        if (_BitScanForward64(&i, bits)) {
            t = static_cast<u32>(w * 64 + i);
            break;
        }
    }
    thread_free_ = t;
    if (t == threads_.size())
        return ~0u;
    const u32 stack = mem_->alloc_on(holder, s_.stack_size, Perm::data, device);
    if (!stack)
        return ~0u;
    Thread& th = threads_[t];
    th = Thread{};
    th.state = ThreadState::ready; // queued once it is set up, below
    thread_free_ = t + 1;
    free_bits_[t >> 6] &= ~(1ull << (t & 63));
    if (thread_top_ < t + 1)
        thread_top_ = t + 1;
    th.id = next_thread_id_++;
    if (next_thread_id_ == 0)
        next_thread_id_ = 1;
    th.program = holder;
    th.stack = stack;
    add_stack(t);
    note(t);
    // A new thread's registers (A.3): A = the starting value; IP, B, C and D =
    // the start, as an idle run's are (the 2026-11-13 review); SP = the top of
    // its stack. They wait at the stack's base.
    Regs r;
    r.a = value;
    r.ip = r.b = r.c = r.d = start;
    r.sp = stack + s_.stack_size;
    ExecContext x;
    x.r = r;
    save(t, x);
    Program& pr = programs_[holder - kFirstProgramHolder];
    th.next = pr.first_thread;
    if (th.next)
        threads_[th.next - 1].prev = t + 1;
    pr.first_thread = t + 1;
    ++pr.threads;
    ++live_threads_;
    ++threads_started;
    make_ready(t);
    return t;
}

void Os::load(u32 t, ExecContext& x) const {
    const Thread& th = threads_[t];
    const u8* p = mem_->bytes() + th.stack;
    x.r.a = load32(p);
    x.r.b = load32(p + 4);
    x.r.c = load32(p + 8);
    x.r.d = load32(p + 12);
    x.r.ip = load32(p + 16);
    x.r.sp = load32(p + 20);
    x.holder = th.program;
    x.stack_lo = th.stack + kSavedRegsBytes;
    x.stack_hi = th.stack + s_.stack_size;
    x.forget_pages();
}

void Os::save(u32 t, const ExecContext& x) {
    u8* p = mem_->bytes() + threads_[t].stack;
    store32(p, x.r.a);
    store32(p + 4, x.r.b);
    store32(p + 8, x.r.c);
    store32(p + 12, x.r.d);
    store32(p + 16, x.r.ip);
    store32(p + 20, x.r.sp);
}

void Os::put_on(u32 t, u32 core) {
    Core& c = cores_[core];
    Thread& th = threads_[t];
    c.thread = t + 1;
    c.idle_run = false;
    load(t, c.x);
    c.x.clock = &c.clock;
    c.x.acts = mem_->activations(); // the per-access counters (cpu.h)
    c.x.act_total = mem_->activation_total();
    c.x.last_row = ~0u;
    set_state(th, ThreadState::running);
    th.core = core + 1;
    note(t);
}

void Os::take_off(u32 core, ThreadState now) {
    Core& c = cores_[core];
    if (!c.thread)
        return;
    const u32 t = c.thread - 1;
    save(t, c.x);
    threads_[t].core = 0;
    c.thread = 0;
    if (now == ThreadState::ready)
        make_ready(t);
    else
        set_state(threads_[t], now);
}

namespace {

// The states a tick's start looks at (the scheduler's begin_tick).
bool waits(ThreadState s) {
    return s == ThreadState::sleeping || s == ThreadState::waiting_interrupt || s == ThreadState::waiting_recv ||
           s == ThreadState::waiting_ping;
}

// The first tick a tick's start has anything to do for a thread (os.h,
// due()): exactly when begin_tick's tests of its record would first pass.
u64 due_of(const Thread& th) {
    if (th.state == ThreadState::free)
        return kNever;
    u64 due = th.handler[kOnTimer] ? th.timer_next : kNever; // the timer fires
    if (th.state == ThreadState::waiting_interrupt)
        return (th.pending & kPendingKinds) ? 0 : due; // woken at once, or when the timer raises one
    if (waits(th.state) && th.wake_tick < due)
        due = th.wake_tick; // its sleep or wait runs out
    return due;
}

// A thread's key for deliver() (1 + a port, 0: none) becomes k, and the
// ports' counts follow.
void rekey(u32& key, u32 k, u32* count) {
    if (key)
        --count[key - 1];
    if (k)
        ++count[k - 1];
    key = k;
}

} // namespace

// Every change of a thread's state comes through here, so the count of
// waiting threads is always right.
void Os::set_state(Thread& th, ThreadState now) {
    waiters_ += (waits(now) ? 1 : 0) - (waits(th.state) ? 1 : 0);
    th.state = now;
    note(static_cast<u32>(&th - threads_.data()));
}

void Os::note(u32 t) {
    const Thread& th = threads_[t];
    const bool live = th.state != ThreadState::free;
    const u8 was = attention_[t];
    const u8 now = static_cast<u8>((waits(th.state) ? 1 : 0) | (live && th.handler[kOnTimer] ? 2 : 0));
    attention_[t] = now;
    saved_at_[t] = live && !th.core ? th.stack : kNowhere;
    frame_at_[t] = live && th.in_handler ? th.frame : kNowhere;
    const u32 rk = th.state == ThreadState::waiting_recv && th.recv_port < s_.ports ? th.recv_port + 1 : 0;
    const u32 dk = live && th.handler[kOnData] && th.data_port < s_.ports ? th.data_port + 1 : 0;
    if (rk != recv_key_[t])
        rekey(recv_key_[t], rk, recv_count_.data());
    if (dk != data_key_[t])
        rekey(data_key_[t], dk, data_count_.data());
    // A thread with no attention bit has nothing due, ever: its entry is
    // kNever already unless it had a bit until now. So a thread that only
    // goes on and off its core, as most do in a busy world, costs no write
    // here.
    if (was | now) {
        const u64 d = due_of(th), old = due_[t];
        due_[t] = d;
        if (d != old)
            note_due(t, old, d);
    }
}

// Thread t's due went from `old` to `d`: its group's least follows. Only
// when the least itself went up are the group's entries read again.
void Os::note_due(u32 t, u64 old, u64 d) {
    u64& least = due_min_[t >> kDueGroupShift];
    if (d < least) {
        least = d;
    } else if (old == least) {
        const u32 from = t >> kDueGroupShift << kDueGroupShift;
        const u32 to = from + kDueGroup < due_.size() ? from + kDueGroup : static_cast<u32>(due_.size());
        u64 m = kNever;
        for (u32 u = from; u < to; ++u)
            m = due_[u] < m ? due_[u] : m;
        least = m;
    }
}

// What's kept beside the records, worked out from them (after a load).
void Os::reindex() {
    thread_top_ = waiters_ = timers_ = 0;
    thread_free_ = static_cast<u32>(threads_.size());
    stacks_.clear();
    block_first_.resize((mem_->size() >> kStackBlockShift) + 1);
    block_first_.fill(0);
    block_next_.resize(threads_.size());
    due_.resize(threads_.size());
    due_.fill(kNever); // note() leaves an entry alone where no attention bit was or is set
    due_min_.resize((threads_.size() + kDueGroup - 1) >> kDueGroupShift);
    due_min_.fill(kNever);
    recv_key_.resize(threads_.size());
    recv_key_.fill(0);
    data_key_.resize(threads_.size());
    data_key_.fill(0);
    recv_count_.resize(s_.ports);
    recv_count_.fill(0);
    data_count_.resize(s_.ports);
    data_count_.fill(0);
    attention_.fill(0);
    free_bits_.resize(0);
    free_bits_.resize((threads_.size() + 63) / 64);
    for (u32 t = 0; t < threads_.size(); ++t) {
        const Thread& th = threads_[t];
        note(t);
        if (th.state == ThreadState::free) {
            free_bits_[t >> 6] |= 1ull << (t & 63);
            if (t < thread_free_)
                thread_free_ = t;
            continue;
        }
        thread_top_ = t + 1;
        waiters_ += waits(th.state) ? 1 : 0;
        timers_ += th.handler[kOnTimer] ? 1 : 0;
        add_stack(t);
    }
    program_free_ = 0;
    while (program_free_ < programs_.size() && programs_[program_free_].alive)
        ++program_free_;
    program_top_ = static_cast<u32>(programs_.size());
    lower_program_top();
}

// ---------------------------------------------------------------------------
// The ready queues
// ---------------------------------------------------------------------------

namespace {

// A queue's slot for head + i: the head is under the capacity and so is
// i, so the sum is under twice it, and one subtraction gives what % does
// without its divide (optimization-plan.md, pass 9).
u32 slot(u32 x, u32 cap) { return x >= cap ? x - cap : x; }

} // namespace

void Os::make_ready(u32 t) {
    Thread& th = threads_[t];
    set_state(th, ThreadState::ready);
    // A pin to a kind with no working core falls back to any (9.5).
    const u32 q = working_[th.pin] ? th.pin : kPinAny;
    Ring& r = rq_[q];
    const u32 cap = static_cast<u32>(r.e.size());
    if (r.size == cap)
        NOVA2_FATAL("os: a ready queue overflowed (cannot happen: each thread is queued once)");
    ReadyEntry& e = r.e[slot(r.head + r.size, cap)];
    e.t = t;
    e.id = th.id;
    e.seq = rq_seq_++;
    ++r.size;
}

bool Os::front(u32 q, ReadyEntry* out) {
    Ring& r = rq_[q];
    const u32 cap = static_cast<u32>(r.e.size());
    while (r.size) {
        const ReadyEntry& e = r.e[r.head];
        const Thread& th = threads_[e.t];
        if (th.state == ThreadState::ready && th.id == e.id) {
            *out = e;
            return true;
        }
        r.head = slot(r.head + 1, cap); // stale: the thread ran, ended or was queued again
        --r.size;
    }
    return false;
}

void Os::ask_thread(u32 t) const {
    const char* const rec = reinterpret_cast<const char*>(threads_.data() + t);
    for (usize b = 0; b < sizeof(Thread); b += 64)
        _mm_prefetch(rec + b, _MM_HINT_T0);
    const u32 s = saved_at_[t];
    if (s >= mem_->size() || mem_->size() - s < kSavedRegsBytes)
        return; // on a core, or a stale entry
    _mm_prefetch(reinterpret_cast<const char*>(mem_->bytes() + s), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(mem_->bytes() + s + kSavedRegsBytes - 1), _MM_HINT_T0);
}

void Os::ask_code(u32 t) const {
    const u32 s = saved_at_[t];
    if (s >= mem_->size() || mem_->size() - s < kSavedRegsBytes)
        return;
    mem_->ask_for(load32(mem_->bytes() + s + 16)); // ip
    mem_->ask_for(load32(mem_->bytes() + s + 20)); // sp
}

void Os::ask_records(u32 t) const {
    const u32 s = saved_at_[t];
    if (s >= mem_->size() || mem_->size() - s < kSavedRegsBytes)
        return;
    mem_->ask_for_record(load32(mem_->bytes() + s + 16));
    mem_->ask_for_record(load32(mem_->bytes() + s + 20));
}

void Os::ask_for_ready() const {
    for (u32 step = 0; step < 3; ++step) {
        for (u32 q = 0; q < 3; ++q) {
            const Ring& r = rq_[q];
            const u32 cap = static_cast<u32>(r.e.size());
            const u32 ahead = r.size < kAskAhead ? r.size : kAskAhead;
            for (u32 i = 0; i < ahead; ++i) {
                const u32 t = r.e[slot(r.head + i, cap)].t;
                if (step == 0)
                    ask_thread(t);
                else if (step == 1)
                    ask_code(t);
                else
                    ask_records(t);
            }
        }
    }
}

void Os::ask_for_next() const {
    for (u32 q = 0; q < 3; ++q) {
        const Ring& r = rq_[q];
        const u32 cap = static_cast<u32>(r.e.size());
        if (r.size >= kAskAhead)
            ask_thread(r.e[slot(r.head + kAskAhead - 1, cap)].t);
        if (r.size >= kAskAhead / 2)
            ask_code(r.e[slot(r.head + kAskAhead / 2 - 1, cap)].t);
        if (r.size >= kAskAhead / 4)
            ask_records(r.e[slot(r.head + kAskAhead / 4 - 1, cap)].t);
    }
}

u32 Os::pick(DeviceKind kind) {
    const u32 own = kind == DeviceKind::e_core ? kPinE : kPinP;
    const u32 other = own == kPinP ? kPinE : kPinP;
    ReadyEntry best{}, e{};
    u32 from = ~0u;
    const u32 qs[3] = {kPinAny, own, other};
    for (u32 i = 0; i < 3; ++i) {
        const u32 q = qs[i];
        if (q == other && working_[other])
            continue; // pinned elsewhere, and that kind can run it
        if (front(q, &e) && (from == ~0u || e.seq < best.seq)) {
            best = e;
            from = q;
        }
    }
    if (from == ~0u)
        return ~0u;
    Ring& r = rq_[from];
    r.head = slot(r.head + 1, static_cast<u32>(r.e.size()));
    --r.size;
    return best.t;
}

bool Os::any_ready(DeviceKind kind) {
    const u32 own = kind == DeviceKind::e_core ? kPinE : kPinP;
    const u32 other = own == kPinP ? kPinE : kPinP;
    ReadyEntry e;
    return front(kPinAny, &e) || front(own, &e) || (!working_[other] && front(other, &e));
}

void Os::wake(u32 t) {
    Thread& th = threads_[t];
    if (th.pend) {
        u8* p = mem_->bytes() + th.stack;
        store32(p, th.pend_a); // the saved A and B (A.3)
        store32(p + 4, th.pend_b);
        th.pend = false;
    }
    th.wake_tick = kNever;
    make_ready(t);
}

// ---------------------------------------------------------------------------
// The raw disk (9.3, R14)
// ---------------------------------------------------------------------------

// DREAD or DWRITE: queued for the barrier, the caller waiting there. A
// (the result) is set when it completes.
u32 Os::disk_call(u32 core, bool write, CallOutcome* out) {
    Core& c = cores_[core];
    const Regs& r = c.x.r;
    const bool run = c.idle_run;
    const u32 size = disk_size();
    if ((write && run) || size == 0 || disk_failed_)
        return 0; // idle runs may not DWRITE; a place with no disk, or a failed one, has nothing to move
    const u32 off = r.a % size;
    const u32 room = size - off;
    const u32 len = r.c < room ? r.c : room; // clamped at the end of the disk space
    if (len == 0)
        return 0;
    DiskOp op;
    op.core = core;
    op.thread = run ? 0 : c.thread;
    op.thread_id = run ? 0 : threads_[c.thread - 1].id;
    op.holder = holder_on(core);
    op.write = write;
    op.live = true;
    op.offset = off;
    op.addr = r.b;
    op.len = len;
    op.seq = op_seq_++;
    ops_.push(op);
    out->stays = false;
    if (run) {
        c.disk_wait = true;
        out->disk = true;
    } else {
        take_off(core, ThreadState::waiting_disk);
    }
    return 0;
}

void Os::cancel_disk(u32 core) {
    for (u32 i = 0; i < ops_.size(); ++i)
        if (ops_[i].core == core && ops_[i].thread == 0)
            ops_[i].live = false;
    cores_[core].disk_wait = false;
}

// The room for a barrier's old bytes (flight_old_): what this one's writes
// need, made at once (no doubling, no copy of a block it leaves); and given
// back after a barrier that needed far more than this one does, so that a
// burst (a backlog served as the disk comes back, 7.6) doesn't hold its room
// for the rest of the run. Room isn't state: nothing saves or draws from it.
void Os::fit_flight_room(u64 need) {
    const u64 room = flight_old_.capacity();
    const u64 keep = 2 * (need > disk_size() ? need : disk_size()); // two whole-disk writes, at least
    if (room >= need && room <= keep)
        return;
    flight_old_ = Array<u8>();
    if (need)
        flight_old_.reserve(static_cast<usize>(need));
}

void Os::barrier() {
    flight_.clear();
    flight_old_.clear();
    if (!disk_on_) {
        fit_flight_room(0);
        return; // the disk is cut out: DREAD and DWRITE wait for it (7.6)
    }
    // Core order, then request order (9.3). Few requests: insertion sort.
    for (u32 i = 1; i < ops_.size(); ++i) {
        const DiskOp v = ops_[i];
        u32 j = i;
        while (j > 0 && (ops_[j - 1].core > v.core || (ops_[j - 1].core == v.core && ops_[j - 1].seq > v.seq))) {
            ops_[j] = ops_[j - 1];
            --j;
        }
        ops_[j] = v;
    }
    // Access is checked again now: each call stops at the first page it can't
    // use. Every count is found before any request is served, nothing a
    // request does changing another's access (it moves bytes, not pages or
    // holders), so the room for the writes' old bytes is made once.
    op_n_.resize(ops_.size());
    u64 need = 0;
    for (u32 i = 0; i < ops_.size(); ++i) {
        const DiskOp& op = ops_[i];
        op_n_[i] = 0;
        if (!op.live || (op.thread && !still_waits(op)))
            continue;
        op_n_[i] = mem_->usable_prefix(op.addr, op.len, op.holder, op.write ? kRead : kWrite);
        need += op.write ? op_n_[i] : 0;
    }
    fit_flight_room(need);
    u8* mem = mem_->bytes();
    for (u32 i = 0; i < ops_.size(); ++i) {
        const DiskOp& op = ops_[i];
        // Asked again: a thread served above is ready now, should it have
        // asked twice.
        if (!op.live || (op.thread && !still_waits(op)))
            continue;
        const u32 n = op_n_[i];
        if (n) { // in flight until the next tick's device states (7.6)
            Flight f{op.core, op.thread, op.thread_id, op.write, op.offset, n, 0, false, 0};
            if (op.write) {
                f.old_at = static_cast<u32>(flight_old_.size());
                flight_old_.append(disk_.data() + op.offset, n);
                // Landing in the boot block (its first byte, at least) makes the
                // writer its last (10.5). Only a thread writes, so it's a program.
                if (op.offset < boot_size()) {
                    f.boot = true;
                    f.writer_before = boot_writer_;
                    boot_writer_ = programs_[op.holder - kFirstProgramHolder].uid;
                }
            }
            flight_.push(f);
        }
        if (op.write) {
            memcpy(disk_.data() + op.offset, mem + op.addr, n);
            wear_blocks(op.offset, n); // each block's write, and a worn block's flips (7.4)
        } else {
            memcpy(mem + op.addr, disk_.data() + op.offset, n);
            mem_->note_write(op.addr, n, op.holder);
        }
        const i64 cost16 = (static_cast<i64>(s_.disk_latency_cycles) + static_cast<i64>(s_.disk_cycles_per_byte) * n) * 16;
        if (op.thread) {
            Thread& th = threads_[op.thread - 1];
            store32(mem + th.stack, n); // the saved A: the count done
            calls_ok[op.write ? kDwrite : kDread] += n ? 1 : 0;
            th.debt16 += cost16;       // the disk's latency, paid from its next quantum
            make_ready(op.thread - 1);
        } else {
            Core& c = cores_[op.core];
            if (!c.disk_wait)
                continue;
            c.x.r.a = n;
            c.disk_wait = false;
            c.disk_debt16 = cost16;
        }
    }
    ops_.clear();
}

// A thread that ended while it waited (its saved state faded while the disk
// was out) asked for nothing any more: its program's holder may be another's
// by now.
bool Os::still_waits(const DiskOp& op) const {
    const Thread& th = threads_[op.thread - 1];
    return th.state == ThreadState::waiting_disk && th.id == op.thread_id;
}

u64 Os::in_flight_bytes() const {
    u64 n = 0;
    for (usize i = 0; i < flight_.size(); ++i)
        n += flight_[i].n;
    return n;
}

void Os::tear(u64 at) {
    u8* mem = mem_->bytes();
    u64 end = in_flight_bytes();
    // Each write's old bytes, walked back from flight_old_'s end as `end`
    // walks back (2026-11-25): a u32 old_at wraps once one barrier's old
    // bytes pass 4 GiB, and this doesn't.
    u64 old_end = flight_old_.size();
    // From the last back: a later write over an earlier one is put back first,
    // so what's left is what the disk had done by the cut.
    for (usize i = flight_.size(); i-- > 0;) {
        const Flight& f = flight_[i];
        const u64 start = end - f.n;
        end = start;
        if (start + f.n <= at)
            break; // done before the cut, and so was everything before it
        const u32 done = at > start ? static_cast<u32>(at - start) : 0;
        if (f.write) {
            old_end -= f.n;
            memcpy(disk_.data() + f.offset + done, flight_old_.data() + old_end + done, f.n - done);
            ++torn_writes;
            // Put back whole, it never landed in the boot block: the writer
            // before is the last again. (Its first byte is in the block, so any
            // part done landed there.)
            if (f.boot && done == 0)
                boot_writer_ = f.writer_before;
            continue;
        }
        ++failed_reads;
        if (f.thread) {
            const Thread& th = threads_[f.thread - 1];
            if (th.state != ThreadState::free && th.id == f.thread_id)
                store32(mem + th.stack, 0); // the saved A: nothing read
        } else if (cores_[f.core].idle_run) {
            cores_[f.core].x.r.a = 0;
        }
    }
}

void Os::note_working() {
    working_[kPinP] = working_[kPinE] = false;
    for (u32 i = 0; i < core_count_; ++i)
        if (cores_[i].working)
            working_[cores_[i].kind == DeviceKind::e_core ? kPinE : kPinP] = true;
    working_[kPinAny] = true;
}

void Os::end_thread(u32 t, DeathCause cause) {
    Thread& th = threads_[t];
    if (th.state == ThreadState::free)
        return;
    const u32 holder = th.program;
    Program& pr = programs_[holder - kFirstProgramHolder];
    if (th.core)
        cores_[th.core - 1].thread = 0;
    if (th.prev)
        threads_[th.prev - 1].next = th.next;
    else
        pr.first_thread = th.next;
    if (th.next)
        threads_[th.next - 1].prev = th.prev;
    const u32 stack = th.stack;
    drop_stack(t);
    set_state(th, ThreadState::free);
    timers_ -= th.handler[kOnTimer] ? 1 : 0;
    th = Thread{};
    note(t);
    free_bits_[t >> 6] |= 1ull << (t & 63);
    if (t < thread_free_)
        thread_free_ = t;
    while (thread_top_ && threads_[thread_top_ - 1].state == ThreadState::free)
        --thread_top_;
    --live_threads_;
    if (--pr.threads > 0) {
        // The program lives on: the thread's stack goes back (if the program
        // still holds it there).
        const u32 id = mem_->alloc_at(stack);
        if (id && mem_->record(id).base == stack && mem_->record(id).holder == holder)
            mem_->free(holder, stack);
        return;
    }
    // Its last thread: the program ends, and everything it holds is freed,
    // bytes kept (9.4).
    mem_->free_all(holder);
    DeathFact f;
    f.tick = env_->tick;
    f.uid = pr.uid;
    f.place = env_->place_id;
    f.cause = cause;
    deaths.push(f);
    pr.alive = false;
    lower_program_top();
    if (holder - kFirstProgramHolder < program_free_)
        program_free_ = holder - kFirstProgramHolder;
    --live_programs_;
}

void Os::cold_start() {
    ColdStartFact f;
    f.tick = env_->tick;
    f.place = env_->place_id;
    f.writer = boot_writer_;
    cold_starts.push(f);
    end_everything(DeathCause::cold_start);
}

void Os::end_everything(DeathCause cause) {
    // In record order, so the deaths come in an order no host affects.
    for (u32 t = 0, top = thread_top_; t < top; ++t)
        if (threads_[t].state != ThreadState::free)
            end_thread(t, cause);
    ops_.clear(); // the requests waiting for the disk were the old OS's
    for (u32 q = 0; q < 3; ++q)
        rq_[q].head = rq_[q].size = 0; // nothing is ready: every entry left was stale
    // The inboxes are lost (11.5, gap 6), and so are the network's waits.
    inbox_.clear();
    inbox_store_.clear();
    inbox_used_.fill(0);
    asks_.clear();
    clear_outbox();
}

// Code runs on the allocation holding it. On a lax place a program may also
// run code in a page where it holds no allocation of those bytes (a gap, or
// someone else's), by holding another allocation in the page: then it runs
// on every allocation it holds in that page. Without this, a thread could
// run past the end of its allocation and give that allocation away while
// running on it (build-plan-gate1.md 11, the hand-overs).
bool Os::runs_on(u32 holder, u32 ip, u32 id) const {
    const u32 at = mem_->alloc_at(ip);
    if (at == id)
        return true;
    if (at && mem_->record(at).holder == holder)
        return false; // inside another allocation of its own
    const Alloc& a = mem_->record(id);
    const u32 page = mem_->page_of(ip);
    return a.size && page >= mem_->page_of(a.base) && page <= mem_->page_of(a.base + a.size - 1);
}

// A thread's saved registers wait in the first bytes of its stack, so while
// the thread lives its stack is in use, as the allocation it runs on is
// (R15). Every stack is an allocation of os.stack_size bytes, so nothing
// else is looked up. A live thread of the program with its stack at the
// allocation's base is one in stacks_ (until pass 13, a walk of the
// program's threads).
bool Os::stack_of(u32 holder, u32 id) const {
    if (!id || holder < kFirstProgramHolder)
        return false;
    const Alloc& a = mem_->record(id);
    if (a.holder != holder || a.size != s_.stack_size)
        return false;
    return stacks_.contains(stack_key(holder, a.base));
}

void Os::add_stack(u32 t) {
    const Thread& th = threads_[t];
    ++stacks_.get_or_insert(stack_key(th.program, th.stack));
    u32& first = block_first_[th.stack >> kStackBlockShift];
    block_next_[t] = first;
    first = t + 1;
}

void Os::drop_stack(u32 t) {
    const Thread& th = threads_[t];
    u32* n = stacks_.find(stack_key(th.program, th.stack));
    if (n && --*n == 0)
        stacks_.erase(stack_key(th.program, th.stack));
    // A block's list is short: the stacks starting in a block's bytes.
    u32* at = &block_first_[th.stack >> kStackBlockShift];
    while (*at && *at != t + 1)
        at = &block_next_[*at - 1];
    if (*at)
        *at = block_next_[t];
}

u32 Os::caller_ip(u32 core, u32 t) const {
    (void)core;
    const Thread& th = threads_[t];
    if (th.core)
        return cores_[th.core - 1].x.r.ip;
    return load32(mem_->bytes() + th.stack + 16);
}

void Os::record_birth(u32 holder, u32 alloc_id) {
    const Program& pr = programs_[holder - kFirstProgramHolder];
    const Alloc& a = mem_->record(alloc_id);
    BirthFact f;
    f.tick = env_->tick;
    f.uid = pr.uid;
    f.parent_uid = pr.parent_uid;
    f.cause = pr.cause;
    f.place = env_->place_id;
    f.base = a.base;
    f.size = a.size;
    f.wlo = a.wlo < a.whi ? a.wlo : 0;
    f.whi = a.wlo < a.whi ? a.whi : 0;
    const u8* bytes = mem_->bytes();
    f.genome_at = static_cast<u32>(fact_bytes.size());
    f.genome_len = f.whi - f.wlo;
    fact_bytes.append(bytes + a.base + f.wlo, f.genome_len);
    f.genome_hash = hash_bytes(bytes + a.base + f.wlo, f.genome_len, kGenomeDomain);
    // The image is hashed as a genome would be, so a birth into a corpse's
    // unchanged code matches that corpse's genome (the Scavenger detector, 13.3).
    f.image_hash = hash_bytes(bytes + a.base, a.size, kGenomeDomain);
    births.push(f);
}

// ---------------------------------------------------------------------------
// SPAWN (9.4, A.4)
// ---------------------------------------------------------------------------

u32 Os::spawn(u32 core, u32 addr, u32 value, u32 sys_ip) {
    const Core& c = cores_[core];
    const u32 holder = holder_on(core);
    const u32 id = mem_->alloc_at(addr);
    spawn_miss_ = SpawnMiss::not_held;
    spawn_asked_ = 0;
    if (!holder || !id || mem_->record(id).holder != holder)
        return 0;
    spawn_miss_ = SpawnMiss::data_page;
    if (mem_->page(mem_->page_of(addr)).perm == Perm::data)
        return 0; // a start must be runnable: an executable or "both" page
    spawn_miss_ = SpawnMiss::no_record;
    const u32 device = mem_->device_of(addr);

    // Inside the allocation holding the caller's IP, or any where one of its
    // program's threads is running (page sharing included: runs_on), SPAWN
    // starts a thread: an allocation in use is never given away (R15). A
    // live thread's stack is in use too (os.stack_held). An idle run has no
    // such allocation. Each test only reads, so the cheap ones go first; and
    // code at an IP runs on the allocation only from a page the allocation
    // covers (runs_on: its own bytes, or a page it shares), so a thread with
    // its IP in none of them is passed over unasked (pass 13).
    bool own = false;
    if (!c.idle_run) {
        own = runs_on(holder, sys_ip, id) || (s_.stack_held && stack_of(holder, id));
        if (!own) {
            const Alloc& a = mem_->record(id);
            const u32 span = a.size > a.extent ? a.size : a.extent;
            const u32 lo = mem_->page_of(a.base);
            const u32 pages = mem_->page_of(a.base + (span ? span : 1) - 1) - lo + 1;
            for (u32 t = programs_[holder - kFirstProgramHolder].first_thread; t && !own; t = threads_[t - 1].next) {
                const u32 ip = caller_ip(core, t - 1);
                if (mem_->page_of(ip) - lo < pages)
                    own = runs_on(holder, ip, id);
            }
        }
    }
    spawn_asked_ = own ? 1 : 2;
    if (own) {
        // The place's limit on a program's threads (9.6, 2026-11-25): at it,
        // or over it, the SPAWN fails as any SPAWN does, at the same cost.
        // A program over it keeps its threads until they end.
        if (s_.thread_limit && programs_[holder - kFirstProgramHolder].threads >= s_.thread_limit) {
            spawn_miss_ = SpawnMiss::limit;
            ++spawns_limited;
            return 0;
        }
        const u32 t = new_thread(holder, addr, value, device);
        return t == ~0u ? 0 : threads_[t].id;
    }

    // Anything else held is a gift to a new program (R5). An idle run's has
    // its run's cause, and a boot's names the boot block's last writer (13.1).
    const BirthCause cause = c.idle_run ? c.run_cause : BirthCause::thread;
    const u64 parent = c.idle_run ? c.run_parent : programs_[holder - kFirstProgramHolder].uid;
    const u32 p = new_program(cause, parent);
    if (p == ~0u)
        return 0;
    const u32 child = kFirstProgramHolder + p;
    const u32 t = new_thread(child, addr, value, device);
    if (t == ~0u) {
        programs_[p].alive = false;
        lower_program_top();
        if (p < program_free_)
            program_free_ = p;
        --live_programs_;
        return 0;
    }
    mem_->give(id, child);
    record_birth(child, id);
    return threads_[t].id;
}

u32 Os::place_by_hand(u32 run, u32 addr, u32 start, u32 value) {
    const u32 id = mem_->alloc_at(addr);
    if (!is_run_holder(run) || !id || mem_->record(id).holder != run)
        return 0;
    const u32 p = new_program(BirthCause::hand, 0);
    if (p == ~0u)
        return 0;
    const u32 child = kFirstProgramHolder + p;
    const u32 t = new_thread(child, start, value, mem_->device_of(start));
    if (t == ~0u) {
        programs_[p].alive = false;
        lower_program_top();
        if (p < program_free_)
            program_free_ = p;
        --live_programs_;
        return 0;
    }
    mem_->give(id, child);
    record_birth(child, id);
    return threads_[t].id;
}

// ---------------------------------------------------------------------------
// Handlers (A.3)
// ---------------------------------------------------------------------------

bool Os::enter_handler(u32 core, u32 handler, u32 a_value) {
    Core& c = cores_[core];
    if (!c.thread)
        return false;
    Thread& th = threads_[c.thread - 1];
    Regs& r = c.x.r;
    if (th.in_handler || r.sp < c.x.stack_lo + kHandlerFrameBytes || r.sp > c.x.stack_hi)
        return false;
    const u32 at = r.sp - kHandlerFrameBytes;
    if (!mem_->allows_range(at, kHandlerFrameBytes, th.program, kWrite))
        return false;
    u8* p = mem_->bytes() + at;
    store32(p, r.ip); // IP on top, A deepest: the order they were pushed, A first
    store32(p + 4, r.d);
    store32(p + 8, r.c);
    store32(p + 12, r.b);
    store32(p + 16, r.a);
    r.sp = at;
    r.ip = handler;
    r.a = a_value;
    th.in_handler = true;
    th.frame = at;
    note(static_cast<u32>(&th - threads_.data()));
    return true;
}

bool Os::hret(Core& c, Thread& th) {
    Regs& r = c.x.r;
    if (r.sp < c.x.stack_lo || r.sp > c.x.stack_hi - kHandlerFrameBytes ||
        !mem_->allows_range(r.sp, kHandlerFrameBytes, th.program, kRead))
        return false;
    const u8* p = mem_->bytes() + r.sp;
    r.ip = load32(p);
    r.d = load32(p + 4);
    r.c = load32(p + 8);
    r.b = load32(p + 12);
    r.a = load32(p + 16);
    r.sp += kHandlerFrameBytes;
    th.in_handler = false;
    th.frame = 0;
    note(static_cast<u32>(&th - threads_.data()));
    return true;
}

CallOutcome Os::fault(u32 core, StepKind kind, u8 length) {
    return take_fault(core, kind == StepKind::fault_stack ? kFaultStack : kFaultMemory, length, DeathCause::fault);
}

CallOutcome Os::watchdog(u32 core) {
    Core& c = cores_[core];
    ++watchdog_faults;
    if (c.thread)
        threads_[c.thread - 1].quiet16 = 0;
    return take_fault(core, kFaultWatchdog, 0, DeathCause::watchdog);
}

CallOutcome Os::take_fault(u32 core, u32 code, u8 length, DeathCause cause) {
    Core& c = cores_[core];
    CallOutcome out;
    out.cost16 = cost16(c, s_.trap_cycles);
    if (!c.thread) {
        out.stays = false; // an idle run's fault ends the run: the scheduler's business
        return out;
    }
    const u32 t = c.thread - 1;
    Thread& th = threads_[t];
    const u32 handler = th.handler[kOnFault];
    if (handler && !th.in_handler) {
        const u32 back = c.x.r.ip;
        c.x.r.ip += length; // HRET goes on after the faulting instruction
        if (enter_handler(core, handler, code)) {
            watchdog_handled += code == kFaultWatchdog ? 1 : 0;
            return out;
        }
        c.x.r.ip = back;
    }
    end_thread(t, cause);
    out.stays = false;
    return out;
}

// ---------------------------------------------------------------------------
// The out-of-memory killer (9.1; build-plan-gate5.md step 10)
// ---------------------------------------------------------------------------

bool Os::crowded_tick(bool crowded) {
    if (!crowded) {
        crowded_for_ = 0;
        return false;
    }
    if (++crowded_for_ < s_.killer)
        return false;
    crowded_for_ = 0; // the spell is up: the clock starts again
    return true;
}

u64 Os::bytes_held(u32 holder) const {
    u64 n = 0;
    for (u32 id = mem_->first_of(holder); id; id = mem_->record(id).next)
        n += mem_->record(id).extent;
    return n;
}

void Os::lower_program_top() {
    while (program_top_ && !programs_[program_top_ - 1].alive)
        --program_top_;
}

u32 Os::pick_by_bytes(u64 draw) const {
    // Each live program's bytes, summed once (pass 13: they were summed
    // again for the pick), up to the highest live record.
    Array<u64> held;
    held.resize(program_top_);
    u64 total = 0;
    for (u32 p = 0; p < program_top_; ++p)
        if (programs_[p].alive) {
            held[p] = bytes_held(kFirstProgramHolder + p);
            total += held[p];
        }
    if (total == 0)
        return 0;
    // The draw's place among the bytes: the high half of draw × total.
    u64 hi = 0;
    _umul128(draw, total, &hi);
    for (u32 p = 0; p < program_top_; ++p) {
        if (!programs_[p].alive)
            continue;
        if (hi < held[p])
            return kFirstProgramHolder + p;
        hi -= held[p];
    }
    return 0; // cannot happen
}

void Os::end_program(u32 holder, DeathCause cause) {
    Program& pr = programs_[holder - kFirstProgramHolder];
    while (pr.alive && pr.first_thread)
        end_thread(pr.first_thread - 1, cause);
}

u32 Os::set_handler(Core& c, Thread& th, u32 holder) {
    const Regs& r = c.x.r;
    const u32 kind = r.a % 4;
    const u32 addr = r.b;
    if (addr != 0 && !mem_->allows(addr, holder, kRun))
        return 0; // a handler must lie in a runnable page the program can use
    if (kind == kOnData) {
        const u32 port = r.c % s_.ports;
        if (addr != 0 && port == 0)
            return 0; // port 0 is the reset region's
        th.data_port = addr ? port : 0;
    } else if (kind == kOnTimer) {
        const u32 period = addr ? r.c : 0;
        th.timer_period = period;
        th.timer_next = period ? env_->tick + period : kNever;
        timers_ -= th.handler[kind] ? 1 : 0; // counted again below if it's set
        if (!period) {
            th.handler[kind] = 0;
            note(static_cast<u32>(&th - threads_.data()));
            return 1;
        }
        timers_ += addr ? 1 : 0;
    }
    lv_handlers += kind == kOnLowVoltage && addr ? 1 : 0;
    th.handler[kind] = addr;
    note(static_cast<u32>(&th - threads_.data()));
    return 1;
}

// ---------------------------------------------------------------------------
// SENSE (A.4)
// ---------------------------------------------------------------------------

u32 Os::sense(u32 core, u32 which, u32 index) const {
    const Core& c = cores_[core];
    const PlaceEnv& e = *env_;
    switch (which) {
    case kTemperature:
        return static_cast<u32>(e.temperature_dc);
    case kVoltage:
        return static_cast<u32>(e.voltage_mv);
    case kTick:
        return static_cast<u32>(e.tick);
    case kPlaceId:
        return e.place_id;
    case kTurnLeft:
        return c.turn_left16 / 16;
    case kPortBytes: {
        const u32 port = index % s_.ports;
        return port == 0 ? 0 : inbox_used_[port]; // port 0 is the reset region's, not an inbox: RECV can't take from it
    }
    case kIdleCores: {
        u32 n = 0;
        for (u32 i = 0; i < core_count_; ++i)
            n += cores_[i].working && !cores_[i].thread ? 1 : 0;
        return n;
    }
    case kFreeMemory:
        return mem_->free_bytes();
    case kPageSize:
        return mem_->page_size();
    case kStrict:
        return mem_->strict() ? 1 : 0;
    case kDeviceCount:
        return static_cast<u32>(e.devices.size());
    case kNeighbourCount:
        return e.neighbours;
    case kDeviceKind:
    case kDeviceSpeed:
    case kDeviceThreshold:
    case kDeviceFree: {
        const u32 n = static_cast<u32>(e.devices.size());
        if (n == 0)
            return 0; // an index into an empty list reads 0
        const SenseDevice& d = e.devices[index % n];
        if (which == kDeviceKind)
            return static_cast<u32>(d.kind);
        if (d.failed)
            return 0; // a failed device reads 0 in every reading but its kind (7.6)
        if (which == kDeviceSpeed)
            return d.speed;
        if (which == kDeviceThreshold)
            return static_cast<u32>(d.threshold_mv);
        if (d.kind == DeviceKind::memory)
            return mem_->device_free(d.memory_index);
        return d.kind == DeviceKind::disk ? d.size : 0;
    }
    default:
        return 0; // neighbours: none before gate 3, and an empty list reads 0
    }
}

// ---------------------------------------------------------------------------
// The calls
// ---------------------------------------------------------------------------

// An arrival's first failed call, for the counts (Core::run_failed; asked
// after build-plan-gate3.md step 7). Nothing in the world reads them.
static bool counts_failure(const Core& c) {
    return c.idle_run && c.run_cause == BirthCause::arrival && !c.run_failed;
}

static void note_failure(Core& c, u32 call, u8 why) {
    c.run_failed = static_cast<u8>(call + 1);
    c.run_miss = why;
}

CallOutcome Os::call(u32 core, i32 sys_value, u32 sys_ip) {
    Core& c = cores_[core];
    const u32 which = call_of(sys_value);
    CallOutcome out;
    out.cost16 = cost16(c, s_.call_cycles[which]);
    const bool run = c.idle_run;
    const u32 holder = holder_on(core);
    Regs& r = c.x.r;
    Thread* th = run || !c.thread ? nullptr : &threads_[c.thread - 1];
    if (run)
        ++run_calls[which];
    if (!run && !th)
        return out; // nothing on the core (cannot happen)
    if (!run)
        ++calls[which];

    switch (which) {
    case kAlloc: {
        // The result, the address or 0, lands in D as well as A, where CPY
        // writes (A.4, the 2026-11-13 review). So after a failed ALLOC a copy
        // goes to address 0 and faults, as A.5's replicator does when memory
        // runs out.
        const u32 size = r.a, perm = r.b % 3;
        r.a = r.d = mem_->alloc(holder, size, perm, static_cast<i32>(r.c));
        calls_ok[kAlloc] += !run && r.a ? 1 : 0;
        if (run && r.a)
            ++run_alloc_ok;
        else if (counts_failure(c))
            note_failure(c, kAlloc, static_cast<u8>(mem_->alloc_miss(size, perm, static_cast<i32>(r.c))));
        break;
    }
    case kFree:
        // A live thread's stack is in use, and can't be freed (R15, 9.4).
        if (s_.stack_held && !run && stack_of(holder, mem_->alloc_at(r.a)))
            r.a = 0;
        else
            r.a = mem_->free(holder, r.a) ? 1 : 0;
        calls_ok[kFree] += !run && r.a ? 1 : 0;
        if (!r.a && counts_failure(c))
            note_failure(c, kFree, 0);
        break;
    case kProtect:
        r.a = mem_->protect(holder, r.a, r.b % 3) ? 1 : 0;
        calls_ok[kProtect] += !run && r.a ? 1 : 0;
        if (!r.a && counts_failure(c))
            note_failure(c, kProtect, 0);
        break;
    case kSpawn:
        r.a = spawn(core, r.a, r.b, sys_ip);
        calls_ok[kSpawn] += !run && r.a ? 1 : 0;
        if (!run && spawn_asked_ == 1) {
            ++spawns_thread;
            spawns_thread_ok += r.a ? 1 : 0;
        } else if (!run && spawn_asked_ == 2) {
            ++spawns_program;
            spawns_program_ok += r.a ? 1 : 0;
        }
        if (run && r.a) {
            ++run_spawn_ok;
            c.run_made = true;
        } else if (counts_failure(c)) {
            note_failure(c, kSpawn, static_cast<u8>(spawn_miss_));
        }
        break;
    case kExit:
        if (run)
            break; // idle runs may not: fails soft
        ++calls_ok[kExit];
        end_thread(c.thread - 1, DeathCause::exit);
        out.stays = false;
        break;
    case kSleep: {
        if (run)
            break;
        ++calls_ok[kSleep];
        const i32 n = static_cast<i32>(r.a);
        if (n < 0) {
            take_off(core, ThreadState::ready); // a yield: the rest of the turn goes
        } else if (n == 0) {
            th->wake_tick = kNever;
            take_off(core, ThreadState::waiting_interrupt);
        } else {
            th->wake_tick = env_->tick + static_cast<u32>(n);
            take_off(core, ThreadState::sleeping);
        }
        out.stays = false;
        break;
    }
    case kSetClock: {
        if (run) {
            r.a = 0;
            break;
        }
        const i32 v = static_cast<i32>(r.a);
        const i32 lo = static_cast<i32>(s_.clock_min), hi = static_cast<i32>(s_.clock_max);
        th->clock = static_cast<u8>(v < lo ? lo : (v > hi ? hi : v));
        r.a = th->clock;
        ++calls_ok[kSetClock];
        break;
    }
    case kSetCore: {
        if (run) {
            r.a = 0;
            break;
        }
        const u32 kind = r.a % 3;
        bool ok = false;
        for (u32 i = 0; i < core_count_ && !ok; ++i)
            ok = cores_[i].working && (kind == kPinAny || (kind == kPinP) == (cores_[i].kind == DeviceKind::p_core));
        if (ok)
            th->pin = static_cast<u8>(kind);
        r.a = ok ? 1 : 0;
        calls_ok[kSetCore] += ok ? 1 : 0;
        break;
    }
    case kSetHandler:
        r.a = run ? 0 : set_handler(c, *th, holder);
        calls_ok[kSetHandler] += !run && r.a ? 1 : 0;
        break;
    case kHret:
        if (run || !th->in_handler)
            break; // outside a handler it does nothing
        if (!hret(c, *th)) {
            end_thread(c.thread - 1, DeathCause::fault); // a fault inside a handler
            out.stays = false;
        } else {
            ++calls_ok[kHret];
        }
        break;
    case kSense: {
        const u32 reading = r.a % kReadingCount;
        calls_ok[kSense] += run ? 0 : 1;
        if (reading >= kNeighbourId && net_ && env_->neighbours)
            neighbour_sense(core, reading, r.b, &out); // a round trip (A.4)
        else
            r.a = sense(core, reading, r.b);
        break;
    }
    case kSend:
        send_call(core, &out);
        break;
    case kRecv:
        recv_call(core, &out);
        break;
    case kDread:
    case kDwrite:
        r.a = disk_call(core, which == kDwrite, &out);
        break;
    default: // kPing
        ping_call(core, &out);
        break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The network's calls (11.2, A.4; build-plan-gate3.md step 4)
// ---------------------------------------------------------------------------

u32 Os::kept_room(u32 end) const {
    u32 n = 0;
    for (usize i = 0; i < outbox_.size(); ++i)
        n += outbox_[i].end == end ? outbox_[i].size : 0;
    return n;
}

u32 Os::copy_in(u32 holder, u32 addr, u32 max, const u8* bytes, u32 len, u64 sender) {
    const u32 want = len < max ? len : max; // the rest of the message is discarded
    const u32 n = mem_->usable_prefix(addr, want, holder, kWrite);
    if (n) {
        note_recv(holder, addr, bytes, n, sender);
        ++calls_ok[kRecv];
        memcpy(mem_->bytes() + addr, bytes, n);
        mem_->note_write(addr, n, holder);
    }
    return n;
}

// The fact of a RECV that returned bytes, for the observers (13.1), taken
// as the bytes land: and of each stretch they reach past the end of the
// allocation the buffer began in (an overrun, 9.8), whose it was then and
// the bytes as written. Nothing in the world reads them.
void Os::note_recv(u32 holder, u32 addr, const u8* bytes, u32 n, u64 sender) {
    RecvFact f{env_->tick, programs_[holder - kFirstProgramHolder].uid, sender, env_->place_id, addr, n, 0};
    const u32 recv = static_cast<u32>(recvs.size());
    const u32 end = addr + n;
    const u32 first = mem_->alloc_at(addr);
    u32 a = first ? mem_->record(first).base + mem_->record(first).extent : end;
    f.past = a < end ? end - a : 0;
    while (a < end) {
        OverrunFact o{};
        u32 stop = a;
        const u32 id = mem_->alloc_at(a);
        if (id) {
            const Alloc& r = mem_->record(id);
            stop = r.base + r.extent;
            if (r.holder >= kFirstProgramHolder && programs_[r.holder - kFirstProgramHolder].alive) {
                o.owner = programs_[r.holder - kFirstProgramHolder].uid;
                o.stack = stack_of(r.holder, id);
            }
        } else {
            while (stop < end && !mem_->alloc_at(stop))
                stop = (stop / kGranule + 1) * kGranule;
        }
        stop = stop < end ? stop : end;
        o.tick = f.tick;
        o.uid = f.uid;
        o.sender = sender;
        o.recv = recv;
        o.place = f.place;
        o.addr = a;
        o.len = stop - a;
        o.bytes_at = static_cast<u32>(fact_bytes.size());
        fact_bytes.append(bytes + (a - addr), o.len);
        overruns.push(o);
        a = stop;
    }
    recvs.push(f);
}

// SEND (11.2): fire and forget. It fails only for local reasons: the
// device off, no route, or no room in its exit queue, counting what this
// tick's earlier SENDs keep there. Its bytes are copied now (appendix B),
// and the caller waits the device's latency plus the size over the first
// link's bandwidth (none, to its own place).
void Os::send_call(u32 core, CallOutcome* out) {
    Core& c = cores_[core];
    Regs& r = c.x.r;
    const bool run = c.idle_run;
    if (run)
        ++idle_sends; // code that would travel without becoming a program (R4)
    if (run && !s_.idle_send) {
        r.a = 0; // an idle run may not SEND (9.2): code travels only in a program's SEND
        return;
    }
    const u32 holder = holder_on(core);
    const bool other = !run && r.b % s_.ports != 0; // a program's, to a comms port
    sends_other += other ? 1 : 0;
    u32 e = Network::kNone;
    u32 len = 0;
    if (net_ && env_->network_on) {
        len = r.d < net_->message_max() ? r.d : net_->message_max();
        if (len && mem_->allows_range(r.c, len, holder, kRead)) {
            e = net_->exit_end(env_->place_id, r.a % net_->place_count());
            if (e != Network::kNone && e != Network::kLoop && kept_room(e) + len > net_->end_room(e))
                e = Network::kNone; // its exit queue is full
        }
    }
    if (e == Network::kNone) {
        r.a = 0; // and without a network (no map), as when the device is off
        return;
    }
    OutMsg o{};
    o.dst = r.a % net_->place_count();
    o.port = r.b % s_.ports;
    o.end = e;
    o.size = len;
    o.at = static_cast<u32>(outbox_bytes_.size());
    o.sender = run ? 0 : programs_[holder - kFirstProgramHolder].uid;
    o.sent = env_->tick;
    outbox_bytes_.append(mem_->bytes() + r.c, len);
    outbox_.push(o);
    SendFact f{env_->tick, o.sender, env_->place_id, o.dst, o.port, len};
    sends.push(f);
    out->cost16 += cost16(c, s_.send_byte_cycles * len);
    const u32 bw = e == Network::kLoop ? 0 : net_->end_bandwidth(e);
    const u64 wait = net_->latency() + (bw ? (static_cast<u64>(len) + bw - 1) / bw : 0);
    r.a = 1;
    calls_ok[kSend] += run ? 0 : 1;
    sends_other_ok += other ? 1 : 0;
    out->stays = false;
    if (run) {
        c.run_sent = true;
        c.net_until = env_->tick + wait;
        out->wait = true;
        return;
    }
    threads_[c.thread - 1].wake_tick = env_->tick + wait;
    take_off(core, ThreadState::sleeping);
}

// RECV (11.2): one whole message from a comms port's inbox, at most the
// max length copied. A poll answers at once; a wait ends with a message, or
// its timeout (0, and -1 for the sender).
void Os::recv_call(u32 core, CallOutcome* out) {
    Core& c = cores_[core];
    Regs& r = c.x.r;
    const u32 port = r.a % s_.ports;
    if (c.idle_run || port == 0 || r.c == 0) {
        r.a = 0;
        r.b = static_cast<u32>(-1);
        return;
    }
    Thread& th = threads_[c.thread - 1];
    for (usize i = 0; i < inbox_.size(); ++i) {
        const InMsg m = inbox_[i];
        if (m.port != port)
            continue;
        r.a = copy_in(th.program, r.b, r.c, inbox_store_.data() + m.at, m.len, m.sender);
        r.b = m.from;
        // It's gone from the inbox: its bytes and its record.
        u8* s = inbox_store_.data();
        memmove(s + m.at, s + m.at + m.len, inbox_store_.size() - m.at - m.len);
        inbox_store_.resize_unfilled(inbox_store_.size() - m.len);
        for (usize k = i + 1; k < inbox_.size(); ++k) {
            inbox_[k - 1] = inbox_[k];
            inbox_[k - 1].at -= inbox_[k].at > m.at ? m.len : 0;
        }
        inbox_.pop();
        inbox_used_[port] -= m.len;
        ++inbox_taken;
        return;
    }
    const i32 timeout = static_cast<i32>(r.d);
    if (timeout == 0) { // a poll: nothing waiting
        r.a = 0;
        r.b = static_cast<u32>(-1);
        return;
    }
    th.pend = true;
    th.pend_a = 0;
    th.pend_b = static_cast<u32>(-1);
    th.recv_port = port;
    th.recv_addr = r.b;
    th.recv_max = r.c;
    th.wait_seq = ++wait_seq_;
    th.wake_tick = timeout > 0 ? env_->tick + static_cast<u32>(timeout) : kNever;
    take_off(core, ThreadState::waiting_recv);
    out->stays = false;
}

// PING (11.2): a request to the place, answered by its OS if its device is
// on; the thread waits for the echo or the timeout. Fails at once (-1) for
// the same local reasons as SEND.
void Os::ping_call(u32 core, CallOutcome* out) {
    Core& c = cores_[core];
    Regs& r = c.x.r;
    if (!net_) {
        r.a = static_cast<u32>(-1); // without a network (no map), as when the device is off, for anyone
        return;
    }
    if (c.idle_run) {
        r.a = 0; // a call an idle run may not make (A.4)
        return;
    }
    u32 e = Network::kNone;
    if (env_->network_on) {
        e = net_->exit_end(env_->place_id, r.a % net_->place_count());
        if (e != Network::kNone && e != Network::kLoop && kept_room(e) + net_->ping_size() > net_->end_room(e))
            e = Network::kNone;
    }
    if (e == Network::kNone) {
        r.a = static_cast<u32>(-1);
        return;
    }
    const u32 t = c.thread - 1;
    Thread& th = threads_[t];
    OutMsg o{};
    o.dst = r.a % net_->place_count();
    o.end = e;
    o.size = net_->ping_size();
    o.at = static_cast<u32>(outbox_bytes_.size());
    o.ping = true;
    o.sender = programs_[th.program - kFirstProgramHolder].uid;
    o.sent = env_->tick;
    o.tag_t = t;
    o.tag_id = th.id;
    o.tag_seq = ++th.ping_seq;
    outbox_bytes_.resize(outbox_bytes_.size() + o.size); // zeros
    outbox_.push(o);
    const i32 timeout = static_cast<i32>(r.b) <= 0 ? static_cast<i32>(s_.ping_timeout) : static_cast<i32>(r.b);
    th.pend = true;
    th.pend_a = static_cast<u32>(-1);
    th.pend_b = r.b;
    // An echo at the barrier `timeout` ticks on is still in time.
    th.wake_tick = env_->tick + static_cast<u32>(timeout) + 1;
    take_off(core, ThreadState::waiting_ping);
    out->stays = false;
}

// A neighbour's reading (A.4, gap 4): a round trip on that link, outside
// the queues. The request lands after the link's delay, when the world
// reads the neighbour, and the caller has its answer after twice the delay.
void Os::neighbour_sense(u32 core, u32 which, u32 index, CallOutcome* out) {
    Core& c = cores_[core];
    Regs& r = c.x.r;
    const Map& m = *net_->map();
    const u32 k = m.link_at(env_->place_id, index % env_->neighbours);
    const u64 d = m.link(k).delay;
    Ask a{};
    a.core = core;
    a.thread = c.idle_run ? 0 : c.thread;
    a.thread_id = c.idle_run ? 0 : threads_[c.thread - 1].id;
    a.neighbour = m.other(k, env_->place_id);
    a.which = which;
    a.lands = env_->tick + d;
    asks_.push(a);
    out->stays = false;
    if (c.idle_run) {
        r.a = 0;
        c.net_until = env_->tick + 2 * d;
        out->wait = true;
        return;
    }
    Thread& th = threads_[c.thread - 1];
    th.pend = true;
    th.pend_a = 0;
    th.pend_b = r.b;
    th.wake_tick = env_->tick + 2 * d;
    take_off(core, ThreadState::sleeping);
}

void Os::answer(const Ask& a, u32 value) {
    if (a.thread) {
        Thread& th = threads_[a.thread - 1];
        if (th.state == ThreadState::sleeping && th.id == a.thread_id && th.pend)
            th.pend_a = value;
        return;
    }
    Core& c = cores_[a.core];
    if (c.idle_run && c.net_until)
        c.x.r.a = value;
}

void Os::drop_asks(u64 landed) {
    usize k = 0;
    for (usize i = 0; i < asks_.size(); ++i)
        if (asks_[i].lands > landed)
            asks_[k++] = asks_[i];
    asks_.resize_unfilled(k);
}

void Os::cancel_net(u32 core) {
    usize k = 0;
    for (usize i = 0; i < asks_.size(); ++i)
        if (asks_[i].thread != 0 || asks_[i].core != core)
            asks_[k++] = asks_[i];
    asks_.resize_unfilled(k);
    cores_[core].net_until = 0;
}

void Os::deliver(u32 port, const u8* bytes, u32 size, u32 from, u64 sender) {
    // The thread that has waited longest in RECV on the port takes it. The
    // keys (note) find the port's waiters without reading every record, up
    // to the last of them; a port past os.ports (sent from a place with
    // more) has none kept, so its threads are looked for record by record.
    const bool kept = port < s_.ports;
    u32 best = ~0u;
    for (u32 t = 0, left = kept ? recv_count_[port] : 0; left; ++t)
        if (recv_key_[t] == port + 1) {
            --left;
            if (best == ~0u || threads_[t].wait_seq < threads_[best].wait_seq)
                best = t;
        }
    for (u32 t = 0; !kept && t < thread_top_; ++t) {
        const Thread& th = threads_[t];
        if (th.state == ThreadState::waiting_recv && th.recv_port == port &&
            (best == ~0u || th.wait_seq < threads_[best].wait_seq))
            best = t;
    }
    if (best != ~0u) {
        Thread& th = threads_[best];
        th.pend_a = copy_in(th.program, th.recv_addr, th.recv_max, bytes, size, sender);
        th.pend_b = from;
        wake(best);
        ++recv_direct;
        return;
    }
    if (port >= inbox_used_.size() || inbox_used_[port] + static_cast<u64>(size) > s_.inbox_bytes) {
        ++inbox_dropped; // full: the newcomer is dropped (11.5)
        return;
    }
    InMsg m{port, from, size, static_cast<u32>(inbox_store_.size()), sender};
    inbox_store_.append(bytes, size);
    inbox_.push(m);
    inbox_used_[port] += size;
    ++inbox_joined;
    // The data interrupt, pending once for every thread with a handler here
    // (found as the waiters are; note leaves the keys as they are).
    for (u32 t = 0, left = kept ? data_count_[port] : 0; left; ++t)
        if (data_key_[t] == port + 1) {
            --left;
            threads_[t].pending = static_cast<u8>(threads_[t].pending | (1u << kOnData));
            note(t); // one waiting for an interrupt is due now
        }
    for (u32 t = 0; !kept && t < thread_top_; ++t) {
        Thread& th = threads_[t];
        if (th.state != ThreadState::free && th.handler[kOnData] && th.data_port == port) {
            th.pending = static_cast<u8>(th.pending | (1u << kOnData));
            note(t);
        }
    }
}

void Os::ping_reply(u32 t, u32 id, u32 seq, u32 rtt) {
    if (t >= threads_.size())
        return;
    Thread& th = threads_[t];
    if (th.state != ThreadState::waiting_ping || th.id != id || th.ping_seq != seq)
        return; // it timed out, or ended: nobody waits for this echo
    th.pend_a = rtt;
    ++calls_ok[kPing];
    wake(t);
}

} // namespace nova2

// ---------------------------------------------------------------------------
// The invariant check
// ---------------------------------------------------------------------------

namespace nova2 {

namespace {

// "<what> <n>" into why, for the first invariant found broken; false.
bool broken(TextBuf* why, const char* what, u64 n) {
    if (why) {
        why->append(what);
        why->append_char(' ');
        why->append_u64(n);
    }
    return false;
}

} // namespace

bool Os::check(TextBuf* why) const {
    const u32 nt = static_cast<u32>(threads_.size());
    // The programs, and each one's list of threads.
    u32 programs = 0;
    for (u32 h = 0; h < programs_.size(); ++h) {
        const Program& pr = programs_[h];
        const u32 holder = h + kFirstProgramHolder;
        if (!pr.alive) {
            if (pr.threads || pr.first_thread)
                return broken(why, "os check: a dead program has threads: holder", holder);
            continue;
        }
        ++programs;
        if (pr.threads == 0)
            return broken(why, "os check: a live program has no thread: holder", holder);
        u32 n = 0, prev = 0;
        for (u32 t = pr.first_thread; t; t = threads_[t - 1].next) {
            if (t > nt || ++n > pr.threads)
                return broken(why, "os check: a program's thread list runs on: holder", holder);
            const Thread& th = threads_[t - 1];
            if (th.state == ThreadState::free || th.program != holder || th.prev != prev)
                return broken(why, "os check: a program's thread list is broken: holder", holder);
            prev = t;
        }
        if (n != pr.threads)
            return broken(why, "os check: a program's thread count is wrong: holder", holder);
    }
    if (programs != live_programs_)
        return broken(why, "os check: the live program count is wrong:", live_programs_);
    // The ready queues: each ready thread has a live entry in one of them.
    Array<u8> queued;
    queued.resize(nt);
    for (u32 q = 0; q < 3; ++q) {
        const Ring& r = rq_[q];
        const u32 cap = static_cast<u32>(r.e.size());
        if (r.size > cap || (cap && r.head >= cap))
            return broken(why, "os check: a ready queue's bounds are wrong: queue", q);
        for (u32 i = 0; i < r.size; ++i) {
            const ReadyEntry& e = r.e[slot(r.head + i, cap)];
            if (e.t < nt && threads_[e.t].state == ThreadState::ready && threads_[e.t].id == e.id)
                queued[e.t] = 1;
        }
    }
    // The threads, against their programs, the cores and the disk.
    u32 threads = 0;
    for (u32 t = 0; t < nt; ++t) {
        const Thread& th = threads_[t];
        if (th.state == ThreadState::free)
            continue;
        ++threads;
        const u32 p = th.program - kFirstProgramHolder;
        if (th.program < kFirstProgramHolder || p >= programs_.size() || !programs_[p].alive)
            return broken(why, "os check: a thread's program isn't alive: thread", t);
        const bool on = th.core != 0;
        if (on != (th.state == ThreadState::running))
            return broken(why, "os check: a thread is running off a core, or on one and not running: thread", t);
        if (on && (th.core > core_count_ || cores_[th.core - 1].thread != t + 1 || cores_[th.core - 1].idle_run))
            return broken(why, "os check: a thread's core doesn't have it: thread", t);
        if (th.state == ThreadState::ready && !queued[t])
            return broken(why, "os check: a ready thread isn't queued: thread", t);
        if (th.state == ThreadState::waiting_disk) {
            bool asked = false;
            for (u32 i = 0; i < ops_.size() && !asked; ++i)
                asked = ops_[i].live && ops_[i].thread == t + 1 && ops_[i].thread_id == th.id;
            if (!asked)
                return broken(why, "os check: a thread waits on the disk without a request: thread", t);
        }
    }
    if (threads != live_threads_)
        return broken(why, "os check: the live thread count is wrong:", live_threads_);
    // What's kept beside the records agrees with them.
    u32 top = 0, waiting = 0, timed = 0;
    if (recv_key_.size() != nt || data_key_.size() != nt || recv_count_.size() != s_.ports ||
        data_count_.size() != s_.ports)
        return broken(why, "os check: the ports kept for deliver are the wrong size:", recv_count_.size());
    Array<u32> recv_n, data_n; // each port's threads, counted again
    recv_n.resize(s_.ports);
    data_n.resize(s_.ports);
    for (u32 t = 0; t < nt; ++t) {
        const Thread& th = threads_[t];
        const bool live = th.state != ThreadState::free;
        if (attention_[t] != ((waits(th.state) ? 1 : 0) | (live && th.handler[kOnTimer] ? 2 : 0)) ||
            saved_at_[t] != (live && !th.core ? th.stack : kNowhere) ||
            frame_at_[t] != (live && th.in_handler ? th.frame : kNowhere) || due_[t] != due_of(th) ||
            ((free_bits_[t >> 6] >> (t & 63)) & 1) != (live ? 0u : 1u))
            return broken(why, "os check: what's kept beside a thread's record doesn't follow from it: thread", t);
        const u32 rk = th.state == ThreadState::waiting_recv && th.recv_port < s_.ports ? th.recv_port + 1 : 0;
        const u32 dk = live && th.handler[kOnData] && th.data_port < s_.ports ? th.data_port + 1 : 0;
        if (recv_key_[t] != rk || data_key_[t] != dk)
            return broken(why, "os check: the ports kept for deliver don't follow from a thread's record: thread", t);
        recv_n[rk ? rk - 1 : 0] += rk ? 1 : 0;
        data_n[dk ? dk - 1 : 0] += dk ? 1 : 0;
        if (th.state == ThreadState::free) {
            if (t < thread_free_)
                return broken(why, "os check: a free thread record lies below the first free mark: thread", t);
            continue;
        }
        top = t + 1;
        waiting += waits(th.state) ? 1 : 0;
        timed += th.handler[kOnTimer] ? 1 : 0;
    }
    if (top != thread_top_)
        return broken(why, "os check: the highest thread record in use is wrong:", thread_top_);
    if (waiting != waiters_ || timed != timers_)
        return broken(why, "os check: the count of waiting or timed threads is wrong:", waiters_ + timers_);
    for (u32 p = 0; p < s_.ports; ++p)
        if (recv_n[p] != recv_count_[p] || data_n[p] != data_count_[p])
            return broken(why, "os check: a port's count of threads kept for deliver is wrong: port", p);
    HashMap<u32> stacks;
    for (u32 t = 0; t < nt; ++t)
        if (threads_[t].state != ThreadState::free)
            ++stacks.get_or_insert(stack_key(threads_[t].program, threads_[t].stack));
    if (stacks.size() != stacks_.size())
        return broken(why, "os check: the count of live threads' stacks kept is wrong:", stacks_.size());
    for (usize i = 0; i < stacks.slot_count(); ++i) {
        const u32* n = stacks.slot_used(i) ? stacks_.find(stacks.slot_key(i)) : nullptr;
        if (stacks.slot_used(i) && (!n || *n != stacks.slot_value(i)))
            return broken(why, "os check: a live thread's stack isn't kept as it lies: holder",
                          stacks.slot_key(i) >> 32);
    }
    for (u32 g = 0; g < due_min_.size(); ++g) {
        u64 m = kNever;
        for (u32 t = g << kDueGroupShift; t < nt && t < (g + 1) << kDueGroupShift; ++t)
            m = due_[t] < m ? due_[t] : m;
        if (due_min_[g] != m)
            return broken(why, "os check: a group's least due isn't its threads': group", g);
    }
    // Each live thread once in its stack's block's list, and nothing else;
    // a handler's frame inside its thread's stack, past the saved registers.
    u32 listed = 0;
    for (u32 b = 0; b < block_first_.size(); ++b)
        for (u32 t = block_first_[b]; t; t = block_next_[t - 1]) {
            if (t > nt || ++listed > live_threads_ || threads_[t - 1].state == ThreadState::free ||
                threads_[t - 1].stack >> kStackBlockShift != b)
                return broken(why, "os check: a block's list of threads is wrong: block", b);
        }
    if (listed != live_threads_)
        return broken(why, "os check: the threads listed by their stacks' blocks are too few:", listed);
    for (u32 t = 0; t < nt; ++t) {
        const Thread& th = threads_[t];
        if (th.state != ThreadState::free && th.in_handler &&
            (th.frame < th.stack + kSavedRegsBytes || th.frame > th.stack + s_.stack_size - kHandlerFrameBytes))
            return broken(why, "os check: a handler's frame lies outside its thread's stack: thread", t);
    }
    for (u32 h = 0; h < program_free_ && h < programs_.size(); ++h)
        if (!programs_[h].alive)
            return broken(why, "os check: a free program record lies below the first free mark: holder",
                          h + kFirstProgramHolder);
    u32 ptop = 0;
    for (u32 h = 0; h < programs_.size(); ++h)
        ptop = programs_[h].alive ? h + 1 : ptop;
    if (ptop != program_top_)
        return broken(why, "os check: the highest live program record is wrong:", program_top_);
    // The cores.
    for (u32 c = 0; c < core_count_; ++c) {
        const Core& k = cores_[c];
        if (k.thread && (k.thread > nt || threads_[k.thread - 1].core != c + 1))
            return broken(why, "os check: a core's thread isn't on it: core", c);
        if (k.thread && k.idle_run)
            return broken(why, "os check: a core has a thread and an idle run: core", c);
        if (!k.working && (k.thread || k.idle_run))
            return broken(why, "os check: a core that's cut out is running something: core", c);
        if (k.disk_wait) {
            bool asked = false;
            for (u32 i = 0; i < ops_.size() && !asked; ++i)
                asked = ops_[i].live && ops_[i].thread == 0 && ops_[i].core == c;
            if (!asked || !k.idle_run)
                return broken(why, "os check: an idle run waits on the disk without a request: core", c);
        }
    }
    // The disk requests: none left once a barrier with the disk on has run;
    // an idle run's live one has its run waiting; all inside the disk.
    if (disk_on_ && ops_.size())
        return broken(why, "os check: disk requests outlived the barrier:", ops_.size());
    for (u32 i = 0; i < ops_.size(); ++i) {
        const DiskOp& op = ops_[i];
        if (op.offset >= disk_size() || op.len > disk_size() - op.offset)
            return broken(why, "os check: a disk request runs off the disk: request", i);
        if (op.live && op.thread == 0 && (op.core >= core_count_ || !cores_[op.core].disk_wait))
            return broken(why, "os check: an idle run's disk request has nobody waiting: request", i);
    }
    for (u32 i = 0; i < flight_.size(); ++i)
        if (flight_[i].offset >= disk_size() || flight_[i].n > disk_size() - flight_[i].offset)
            return broken(why, "os check: I/O in flight runs off the disk: flight", i);
    // A failed disk has nothing waiting or in flight, and no boot block's writer.
    if (disk_failed_ && (disk_on_ || ops_.size() || flight_.size() || boot_writer_))
        return broken(why, "os check: a failed disk is on, or has requests, I/O in flight or a boot writer:", 1);
    // Levelling's map: every block held once, and the least-written block
    // the lowest-numbered of the least.
    if (block_count_ && s_.levelling) {
        for (u32 b = 0; b < block_count_; ++b)
            if (block_map_[b] >= block_count_ || block_holds_[block_map_[b]] != b)
                return broken(why, "os check: the levelling map and its inverse disagree: block", b);
        for (u32 p = 0; p < block_count_; ++p)
            if (block_writes_[p] < block_writes_[least_] || (block_writes_[p] == block_writes_[least_] && p < least_))
                return broken(why, "os check: a block is written less than the least-written: block", p);
    }
    // The inboxes: each port's bytes are its messages', end to end, within
    // the bound.
    u64 at = 0;
    Array<u32> used;
    used.resize(inbox_used_.size());
    for (u32 i = 0; i < inbox_.size(); ++i) {
        const InMsg& m = inbox_[i];
        if (m.port == 0 || m.port >= used.size() || m.at != at)
            return broken(why, "os check: an inbox message is out of place: message", i);
        at += m.len;
        used[m.port] += m.len;
    }
    if (at != inbox_store_.size())
        return broken(why, "os check: the inboxes' bytes don't add up:", at);
    for (u32 p = 0; p < used.size(); ++p)
        if (used[p] != inbox_used_[p] || used[p] > s_.inbox_bytes)
            return broken(why, "os check: an inbox's count is wrong, or over its bound: port", p);
    return true;
}

} // namespace nova2
