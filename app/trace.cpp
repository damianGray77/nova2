// nova2 trace: code run instruction by instruction (build-plan-gate1.md 11).
// For reading what a copier the origin test found really does: the calls it
// makes and what each returns, the children it makes, and how each thread
// ends. Two ways in:
//
//   nova2 trace <hex> [config] [--page N] [--strict] [--zero] [--at N] [--copies N]
//               [--a N] [--steps N] [--turn N] [--quiet] [--set key=value]...
//   nova2 trace --snapshot <file> [--place P] [--list] [--program U] [--wake] [--steps N] [--turn N]
//               [--quiet]
//
// A genome in hex runs alone in a sandbox place. It's placed by hand, so this
// checks mechanics, never origins (R7):
// - The genome becomes a new program in place 0, built from the settings
//   with one page size (--page, default 1,024) and lax unless --strict. The
//   rest of memory is the place's static, or zeros with --zero.
// - A '/' in the hex ends the genome's allocation: the bytes after it are
//   written straight after it, as what follows it in the page.
// - --at N puts the genome in the first block of its size at or after
//   address N (the blocks before it are left as static).
// - --copies N places N - 1 more copies of the genome, allocated after it
//   and held by an idle run that never runs (a page of copies, as a copier's
//   own children make one).
// - Its thread starts with A = --a (default 0) and B, C and D at its start
//   (A.3).
//
// A place snapshot (origin.snapshots, snapshot.h) runs that place as it was,
// so a lineage can be traced where it lived, its neighbours and all. A world
// snapshot (nova2 run --save) works too, with --place P (default 0).
// - --list prints the place and its living programs, and stops.
// - --program U traces program U and the children it makes; the place's
//   other threads stand still. Without it, every thread runs.
// - --list also shows each thread's state, IP and registers.
// - --wake ends every sleep and wait first, as if their time had passed.
// - Threads come off their cores first; sleeping and waiting threads stay
//   so, since no tick passes (unless --wake).
//
// Either way, threads run one at a time on core 0, --turn instructions a
// turn (default 200), round robin, until --steps instructions in all
// (default 2,000). --quiet prints only calls, births, deaths and faults.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.h"
#include "nova2/keys.h"
#include "nova2/observe.h" // kDeathCauses
#include "nova2/place.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

namespace nova2 {

namespace {

const char* const kCallName[kCallCount] = {"ALLOC", "FREE", "PROTECT", "SPAWN", "EXIT", "SLEEP",
                                           "SETCLOCK", "SETCORE", "SETHANDLER", "HRET", "SENSE", "SEND",
                                           "RECV", "DREAD", "DWRITE", "PING"};
const char* const kDeathName[kDeathCauses] = {"exit",        "fault",      "cut-out", "device failed", "memory lost",
                                              "cold start", "heat",       "out of memory", "watchdog"};
const char* const kCauseName[] = {"a thread", "static", "an arrival", "a boot", "by hand"};
const char* const kStateName[] = {"free", "ready", "running", "sleeping", "waiting for an interrupt",
                                   "waiting for a message", "waiting for the disk", "waiting for an echo"};

int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// The genome's bytes, and those after a '/'. False if the hex is malformed.
bool parse_hex(const char* s, Array<u8>* genome, Array<u8>* after) {
    Array<u8>* into = genome;
    int hi = -1;
    for (; *s; ++s) {
        if (*s == ' ' || *s == ',' || *s == '\t')
            continue;
        if (*s == '/') {
            if (hi >= 0 || into == after)
                return false;
            into = after;
            continue;
        }
        const int d = hex_digit(*s);
        if (d < 0)
            return false;
        if (hi < 0) {
            hi = d;
        } else {
            into->push(static_cast<u8>(hi * 16 + d));
            hi = -1;
        }
    }
    return hi < 0 && genome->size() > 0;
}

// One program the trace has seen: its number in the trace, and its genome.
struct Seen {
    u64 uid;
    u32 base, size;
    u32 genome_at, genome_len; // in the trace's own byte pool
    i32 parent;                // its index here, or -1
    bool focus;                // traced (and run, when not everything is)
};

struct Trace {
    Place* p = nullptr;
    Array<Seen> seen;
    Array<u8> pool;
    const u8* hand_genome = nullptr; // the genome placed by hand
    u32 hand_len = 0;
    bool focus_all = true;           // every program traced, and run
    u32 births = 0, faithful = 0, deaths = 0;

    i32 index_of(u64 uid) const {
        for (u32 i = 0; i < seen.size(); ++i)
            if (seen[i].uid == uid)
                return static_cast<i32>(i);
        return -1;
    }

    // The program a holder is, by its trace number.
    i32 number_of_holder(u32 holder) const { return index_of(p->os.program(holder).uid); }
    bool shows(i32 who) const { return focus_all || (who >= 0 && seen[static_cast<u32>(who)].focus); }

    // A program already alive when the trace starts (a snapshot's): its
    // genome is what its first allocation holds, the written part if any.
    void add_living(const Program& pr, u32 holder, bool focus) {
        Memory& m = p->mem;
        Seen s;
        s.uid = pr.uid;
        s.base = s.size = 0;
        s.genome_at = static_cast<u32>(pool.size());
        s.genome_len = 0;
        s.parent = -1;
        s.focus = focus;
        const u32 id = m.first_of(holder);
        if (id) {
            const Alloc& a = m.record(id);
            s.base = a.base;
            s.size = a.size;
            const u32 lo = a.wlo < a.whi ? a.wlo : 0, hi = a.wlo < a.whi ? a.whi : a.size;
            s.genome_len = hi - lo;
            pool.append(m.bytes() + a.base + lo, s.genome_len);
        }
        seen.push(s);
    }

    // Births and deaths since the last call, printed and remembered.
    void drain(u64 step) {
        Os& os = p->os;
        for (u32 i = 0; i < os.births.size(); ++i) {
            const BirthFact& f = os.births[i];
            Seen s;
            s.uid = f.uid;
            s.base = f.base;
            s.size = f.size;
            s.genome_at = static_cast<u32>(pool.size());
            if (f.cause == BirthCause::hand) { // nothing wrote it: its genome is the one placed
                s.genome_len = hand_len;
                pool.append(hand_genome, hand_len);
            } else {
                s.genome_len = f.genome_len;
                pool.append(os.fact_bytes.data() + f.genome_at, f.genome_len);
            }
            s.parent = index_of(parent_of(f.parent_uid));
            s.focus = focus_all || (s.parent >= 0 && seen[static_cast<u32>(s.parent)].focus);
            seen.push(s);
            const u32 n = static_cast<u32>(seen.size() - 1);
            if (f.cause == BirthCause::hand) {
                printf("[%llu] p%u placed at %u, %u bytes\n", static_cast<unsigned long long>(step), n, f.base,
                       f.size);
                continue;
            }
            if (!s.focus)
                continue;
            ++births;
            const char* same = "no genome of its own";
            if (s.genome_len) {
                same = "differs from its parent's";
                if (s.parent >= 0) {
                    const Seen& par = seen[static_cast<u32>(s.parent)];
                    if (par.genome_len == s.genome_len &&
                        memcmp(pool.data() + par.genome_at, pool.data() + s.genome_at, s.genome_len) == 0) {
                        same = "its parent's";
                        if (s.genome_len >= 8)
                            ++faithful;
                    }
                }
            }
            printf("[%llu] BIRTH p%u from p%d: at %u, %u bytes; genome %u bytes, %s\n",
                   static_cast<unsigned long long>(step), n, s.parent, f.base, f.size, s.genome_len, same);
        }
        for (u32 i = 0; i < os.deaths.size(); ++i) {
            const DeathFact& d = os.deaths[i];
            const i32 who = index_of(d.uid);
            if (!shows(who))
                continue;
            ++deaths;
            printf("[%llu] DEATH p%d: %s\n", static_cast<unsigned long long>(step), who,
                   kDeathName[static_cast<u32>(d.cause)]);
        }
        os.clear_facts();
    }
};

// The next ready thread to run, round robin over the thread records from
// *cursor: any, or only a traced program's. ~0u if there is none.
u32 next_ready(const Trace& tr, u32* cursor) {
    const Os& os = tr.p->os;
    const u32 n = os.thread_capacity();
    for (u32 k = 0; k < n; ++k) {
        const u32 t = (*cursor + k) % n;
        const Thread& th = os.thread(t);
        if (th.state != ThreadState::ready || !tr.shows(tr.number_of_holder(th.program)))
            continue;
        *cursor = t + 1;
        return t;
    }
    return ~0u;
}

// Threads one at a time on core 0, --turn instructions a turn, until --steps
// in all or nothing is ready. by_record: pick round robin over the records
// (a snapshot's place), else by the ready queues (the sandbox's one program).
u64 run_threads(Trace& tr, const CostTable& costs, const TraceOptions& t, bool by_record) {
    Place& p = *tr.p;
    Memory& m = p.mem;
    Os& os = p.os;
    Core& c = p.cores[0];
    u64 done = 0;
    u32 cursor = 0;
    TextBuf dis;
    while (done < t.steps) {
        const u32 next = by_record ? next_ready(tr, &cursor) : os.pick(c.kind);
        if (next == ~0u)
            break;
        os.put_on(next, 0);
        const u32 tid = os.thread(next).id;
        const i32 who = tr.number_of_holder(os.thread(next).program);
        const bool shown = tr.shows(who);
        const u32 base = who >= 0 ? tr.seen[static_cast<u32>(who)].base : 0;
        if (!t.quiet && shown)
            printf("-- thread %u of p%d\n", tid, who);
        u32 n = 0;
        while (c.thread && n < t.turn && done < t.steps) {
            Regs& r = c.x.r;
            const u32 ip = r.ip;
            u8 code[2] = {0, 0};
            if (ip < m.size())
                code[0] = m.bytes()[ip];
            if (ip + 1 < m.size())
                code[1] = m.bytes()[ip + 1];
            const Step st = step(m, c.x, costs);
            ++n;
            ++done;
            dis.clear();
            disassemble(code, (code[0] & 7) == kImm ? 2 : 1, &dis);
            const i64 off = static_cast<i64>(ip) - static_cast<i64>(base);
            if (st.kind == StepKind::sys) {
                const u32 call = os.call_of(st.sys_value);
                const u32 a_in = r.a, b_in = r.b;
                const CallOutcome o = os.call(0, st.sys_value, ip);
                if (shown)
                    printf("t%u p%d %6u %+5lld %-16s SYS %d -> %s(A %u, B %u) = %u%s\n", tid, who, ip,
                           static_cast<long long>(off), dis.c_str(), st.sys_value, kCallName[call], a_in, b_in,
                           c.thread ? c.x.r.a : 0u, o.stays ? "" : ", leaves its core");
            } else if (st.kind != StepKind::done) {
                os.fault(0, st.kind, st.length);
                if (shown)
                    printf("t%u p%d %6u %+5lld %-16s FAULT (%s)%s\n", tid, who, ip, static_cast<long long>(off),
                           dis.c_str(), st.kind == StepKind::fault_stack ? "stack" : "memory",
                           c.thread ? ", into its handler" : ", the thread ends");
            } else if (!t.quiet && shown) {
                printf("t%u p%d %6u %+5lld %-16s A %u B %u C %u D %u\n", tid, who, ip, static_cast<long long>(off),
                       dis.c_str(), r.a, r.b, r.c, r.d);
            }
            tr.drain(done);
        }
        if (c.thread)
            os.take_off(0, ThreadState::ready);
    }
    return done;
}

void print_end(const Trace& tr, u64 done) {
    const Place& p = *tr.p;
    printf("\n%llu instructions; %u births (%u faithful), %u deaths; %u programs and %u threads alive; %u bytes "
           "free\n",
           static_cast<unsigned long long>(done), tr.births, tr.faithful, tr.deaths, p.os.live_programs(),
           p.os.live_threads(), p.mem.free_bytes());
}

void print_hex(const u8* b, u32 n) {
    for (u32 i = 0; i < n; ++i)
        printf("%02x", b[i]);
}

// The place in a place snapshot, as it was, with its threads off their cores.
int trace_snapshot(const TraceOptions& t) {
    Array<u8> bytes;
    if (!read_file(t.snapshot, &bytes)) {
        fprintf(stderr, "nova2: trace: cannot read %s\n", t.snapshot);
        return 2;
    }
    SnapshotHeader h;
    TextBuf err;
    if (!snapshot_parse(bytes.data(), bytes.size(), &h, &err)) {
        fprintf(stderr, "nova2: %s: %s\n", t.snapshot, err.c_str());
        return 2;
    }
    Config cfg;
    Config::ParseError pe;
    if (!snapshot_config(h, &cfg, &pe)) {
        fprintf(stderr, "nova2: the snapshot's settings: line %u: %s\n", pe.line, pe.msg);
        return 2;
    }
    // A place snapshot holds one place; a world snapshot (nova2 run --save)
    // holds them all, and --place picks one.
    const bool one = h.flags == kSnapshotPlace;
    const u32 place = one ? h.place : t.place;
    World* w = new World;
    TextBuf why;
    ByteReader in(bytes.data() + h.world_offset, h.world_size);
    if (!w->init(cfg, &why) || !(one ? w->load_place(place, in, h.version) : w->load(in, h.version))) {
        fprintf(stderr, "nova2: %s: the snapshot doesn't match its settings%s%s\n", t.snapshot,
                why.c_str()[0] ? ": " : "", why.c_str());
        delete w;
        return 2;
    }
    if (place >= w->place_count()) {
        fprintf(stderr, "nova2: trace: the world has places 0 to %u\n", w->place_count() - 1);
        delete w;
        return 2;
    }
    Place& p = w->place(place);
    Memory& m = p.mem;
    Os& os = p.os;
    for (u32 c = 0; c < p.core_count(); ++c) {
        if (p.cores[c].thread)
            os.take_off(c, ThreadState::ready);
        p.cores[c].idle_run = false;
    }
    printf("seed %llu, tick %llu, place %u: %s, %u-byte pages; memory %u bytes, %u free (%.1f%%); %u programs, %u "
           "threads\n",
           static_cast<unsigned long long>(h.seed), static_cast<unsigned long long>(h.tick), place,
           m.strict() ? "strict" : "lax", m.page_size(), m.size(), m.free_bytes(),
           100.0 * m.free_bytes() / (m.size() ? m.size() : 1), os.live_programs(), os.live_threads());

    Trace tr;
    tr.p = &p;
    tr.focus_all = t.program == 0;
    bool found = false;
    for (u32 i = 0; i < os.program_capacity(); ++i) {
        const u32 holder = kFirstProgramHolder + i;
        const Program& pr = os.program(holder);
        if (!pr.alive)
            continue;
        const bool focus = tr.focus_all || pr.uid == t.program;
        found = found || pr.uid == t.program;
        tr.add_living(pr, holder, focus);
        if (!t.list)
            continue;
        u32 allocs = 0, held = 0;
        for (u32 id = m.first_of(holder); id; id = m.record(id).next) {
            ++allocs;
            held += m.record(id).extent;
        }
        const Seen& s = tr.seen.back();
        printf("  program %llu: parent %llu, born at tick %llu from %s; %u allocations, %u bytes; first at %u, %u "
               "bytes: ",
               static_cast<unsigned long long>(pr.uid), static_cast<unsigned long long>(parent_of(pr.parent_uid)),
               static_cast<unsigned long long>(pr.born_tick), kCauseName[static_cast<u32>(pr.cause)], allocs, held,
               s.base, s.size);
        print_hex(tr.pool.data() + s.genome_at, s.genome_len < 48 ? s.genome_len : 48);
        printf(s.genome_len > 48 ? "...\n" : "\n");
        for (u32 th = pr.first_thread; th; th = os.thread(th - 1).next) {
            const Thread& x = os.thread(th - 1);
            ExecContext ctx;
            os.load(th - 1, ctx);
            printf("    thread %u: %s", x.id, kStateName[static_cast<u32>(x.state)]);
            if (x.wake_tick != kNever && (x.state == ThreadState::sleeping || x.state == ThreadState::waiting_recv))
                printf(" until tick %llu", static_cast<unsigned long long>(x.wake_tick));
            printf(", IP %u (%+lld), A %u B %u C %u D %u\n", ctx.r.ip,
                   static_cast<long long>(ctx.r.ip) - static_cast<long long>(s.base), ctx.r.a, ctx.r.b, ctx.r.c,
                   ctx.r.d);
        }
    }
    for (u32 i = 0; i < tr.seen.size(); ++i) // parents, among the living
        for (u32 j = 0; j < os.program_capacity(); ++j) {
            const Program& pr = os.program(kFirstProgramHolder + j);
            if (pr.alive && pr.uid == tr.seen[i].uid) {
                tr.seen[i].parent = tr.index_of(parent_of(pr.parent_uid));
                break;
            }
        }
    if (t.list) {
        delete w;
        return 0;
    }
    if (t.program && !found) {
        fprintf(stderr, "nova2: trace: program %llu isn't alive in place %u (--list shows who is)\n",
                static_cast<unsigned long long>(t.program), place);
        delete w;
        return 2;
    }
    if (t.program) {
        const i32 who = tr.index_of(t.program);
        printf("tracing program %llu as p%d, and the children it makes; the place's other threads stand still\n",
               static_cast<unsigned long long>(t.program), who);
    }
    if (t.wake) { // as if their waits had ended: SLEEP's ticks passed, RECV's time ran out
        u32 woken = 0;
        for (u32 i = 0; i < os.thread_capacity(); ++i) {
            const ThreadState s = os.thread(i).state;
            if (s == ThreadState::sleeping || s == ThreadState::waiting_interrupt || s == ThreadState::waiting_recv) {
                os.wake(i);
                ++woken;
            }
        }
        printf("woke %u threads\n", woken);
    }
    const CostTable& costs = p.cores[0].kind == DeviceKind::e_core ? w->settings().e_costs : w->settings().p_costs;
    const u64 done = run_threads(tr, costs, t, true);
    print_end(tr, done);
    delete w;
    return 0;
}

} // namespace

bool trace_options(int argc, char** argv, TraceOptions* t, int* rest_argc, char** rest) {
    int i = 2;
    if (argc >= 3 && argv[2][0] != '-') {
        t->hex = argv[2];
        i = 3;
    }
    int n = 0;
    rest[n++] = argv[0];
    rest[n++] = argv[1];
    for (; i < argc; ++i) {
        const char* a = argv[i];
        const bool has = i + 1 < argc;
        if (strcmp(a, "--page") == 0 && has)
            t->page = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (strcmp(a, "--copies") == 0 && has)
            t->copies = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (strcmp(a, "--at") == 0 && has)
            t->at = static_cast<u32>(strtoul(argv[++i], nullptr, 0));
        else if (strcmp(a, "--a") == 0 && has)
            t->a = static_cast<u32>(strtoul(argv[++i], nullptr, 0));
        else if (strcmp(a, "--steps") == 0 && has)
            t->steps = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (strcmp(a, "--turn") == 0 && has)
            t->turn = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (strcmp(a, "--snapshot") == 0 && has)
            t->snapshot = argv[++i];
        else if (strcmp(a, "--place") == 0 && has)
            t->place = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        else if (strcmp(a, "--program") == 0 && has)
            t->program = strtoull(argv[++i], nullptr, 10);
        else if (strcmp(a, "--wake") == 0)
            t->wake = true;
        else if (strcmp(a, "--list") == 0)
            t->list = true;
        else if (strcmp(a, "--strict") == 0)
            t->strict = true;
        else if (strcmp(a, "--zero") == 0)
            t->zero = true;
        else if (strcmp(a, "--quiet") == 0)
            t->quiet = true;
        else if (n < 62)
            rest[n++] = argv[i];
    }
    *rest_argc = n;
    if (!t->hex && !t->snapshot) {
        fprintf(stderr, "nova2: trace needs a genome in hex, or --snapshot and a place snapshot\n");
        return false;
    }
    return true;
}

int cmd_trace(Config& cfg, const TraceOptions& t) {
    if (t.snapshot)
        return trace_snapshot(t);
    Array<u8> genome, after;
    if (!parse_hex(t.hex, &genome, &after)) {
        fprintf(stderr, "nova2: trace: '%s' isn't a genome in hex\n", t.hex);
        return 2;
    }
    WorldSettings ws;
    read_world_settings(cfg, &ws);
    if (!cfg.ok()) {
        fprintf(stderr, "nova2: %s\n", cfg.value_error());
        return 2;
    }
    ws.page_sizes[0] = t.page;
    ws.page_size_count = 1;
    ws.strict_share = t.strict ? 1.0 : 0.0;
    Place* p = new Place;
    TextBuf why;
    if (!p->init(0, ws, &why)) {
        fprintf(stderr, "nova2: trace: %s\n", why.c_str());
        delete p;
        return 2;
    }
    Memory& m = p->mem;
    Os& os = p->os;
    if (t.zero)
        memset(m.bytes(), 0, m.size());
    // The genome as a program (permission "both" on a lax place, executable
    // on a strict one), and its copies held by run 1, which never runs.
    const u32 perm = t.strict ? 1 : 2;
    u32 at = m.alloc(1, static_cast<u32>(genome.size()), perm, 0);
    while (at && at < t.at) // the blocks before it stay with run 1, as static
        at = m.alloc(1, static_cast<u32>(genome.size()), perm, 0);
    if (!at) {
        fprintf(stderr, "nova2: trace: no room for the genome\n");
        delete p;
        return 2;
    }
    memcpy(m.bytes() + at, genome.data(), genome.size());
    if (after.size()) {
        const u32 end = at + static_cast<u32>(genome.size());
        const u32 n = end + after.size() <= m.size() ? static_cast<u32>(after.size()) : m.size() - end;
        memcpy(m.bytes() + end, after.data(), n);
    }
    for (u32 k = 1; k < t.copies; ++k) {
        const u32 c = m.alloc(1, static_cast<u32>(genome.size()), perm, 0);
        if (!c)
            break;
        memcpy(m.bytes() + c, genome.data(), genome.size());
        printf("copy %u at %u\n", k, c);
    }
    if (!os.place_by_hand(1, at, at, t.a)) {
        fprintf(stderr, "nova2: trace: couldn't make the program\n");
        delete p;
        return 2;
    }
    printf("trace: %u bytes on a %s place with %u-byte pages; slots %u\n", static_cast<u32>(genome.size()),
           t.strict ? "strict" : "lax", t.page, os.settings().slot_count);

    Trace tr;
    tr.p = p;
    tr.hand_genome = genome.data();
    tr.hand_len = static_cast<u32>(genome.size());
    tr.drain(0);
    const Core& c = p->cores[0];
    const u64 done = run_threads(tr, c.kind == DeviceKind::e_core ? ws.e_costs : ws.p_costs, t, false);
    print_end(tr, done);
    delete p;
    return 0;
}

} // namespace nova2
