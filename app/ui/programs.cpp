#include "programs.h"

#include <cstdio>  // snprintf
#include <cstring> // memcmp, memset

#include "figures.h"
#include "nova2/observe.h"
#include "nova2/world.h"
#include "theme.h"
#include "viewer.h"
#include "widgets.h"

namespace nova2 {

namespace {

u64 sort_key(const ProgramRow& r, ProgramSort by) {
    switch (by) {
    case ProgramSort::lineage:
        return r.origin;
    case ProgramSort::threads:
        return r.threads;
    case ProgramSort::bytes:
        return r.bytes;
    case ProgramSort::born:
        return static_cast<u64>(r.cause);
    default:
        return r.born_tick; // by age, the oldest first
    }
}

// A row's place in the order: its key, its uid for ties (the older first),
// and where it is in the list.
struct SortKey {
    u64 key, uid;
    u32 at;
};

bool before(const SortKey& a, const SortKey& b, bool down) {
    if (a.key != b.key)
        return down ? a.key > b.key : a.key < b.key;
    return a.uid < b.uid;
}

// The rows in order. A merge sort of their keys, then the rows moved once:
// a shell sort of the rows themselves took 2 ms of every frame at a place
// of 16,000 programs (build-plan-viewer.md step 12).
void sort_rows(Array<ProgramRow>* rows, ProgramSort by, bool down) {
    static Array<SortKey> keys, spare; // scratch, kept (the views are drawn by one thread)
    static Array<ProgramRow> moved;
    const usize n = rows->size();
    keys.resize_unfilled(n);
    spare.resize_unfilled(n);
    for (usize i = 0; i < n; ++i)
        keys[i] = SortKey{sort_key((*rows)[i], by), (*rows)[i].uid, static_cast<u32>(i)};
    SortKey* from = keys.data();
    SortKey* to = spare.data();
    for (usize width = 1; width < n; width *= 2) {
        for (usize lo = 0; lo < n; lo += 2 * width) {
            const usize mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
            usize a = lo, b = mid, k = lo;
            while (a < mid && b < hi)
                to[k++] = before(from[b], from[a], down) ? from[b++] : from[a++];
            while (a < mid)
                to[k++] = from[a++];
            while (b < hi)
                to[k++] = from[b++];
        }
        SortKey* t = from;
        from = to;
        to = t;
    }
    moved.resize_unfilled(n);
    for (usize i = 0; i < n; ++i)
        moved[i] = (*rows)[from[i].at];
    for (usize i = 0; i < n; ++i)
        (*rows)[i] = moved[i];
}

// A thread's registers: its core's while it is on one, or as they lie at
// the base of its stack (A.3).
Regs regs_of(const Place& p, u32 t) {
    const Thread& th = p.os.thread(t);
    if (th.core)
        return p.cores[th.core - 1].x.r;
    ExecContext x;
    p.os.load(t, x);
    return x.r;
}

// A row's bytes (a walk of its allocations) and lineage (a look-up in the
// observer's records), the dear parts of a row.
void row_bytes(const Place& p, ProgramRow* out) {
    out->bytes = 0;
    for (u32 id = p.mem.first_of(out->holder); id; id = p.mem.record(id).next)
        out->bytes += p.mem.record(id).extent;
}

void row_origin(const Observer* obs, ProgramRow* out) {
    const ProgramRec* r = obs ? obs->program(out->uid) : nullptr;
    out->known = r != nullptr;
    out->origin = r ? r->origin : out->uid;
}

// A row's cheap parts, from the program's own record.
void row_record(const Place& p, u32 holder, ProgramRow* out) {
    const Program& g = p.os.program(holder);
    out->uid = g.uid;
    out->born_tick = g.born_tick;
    out->holder = holder;
    out->threads = g.threads;
    out->cause = g.cause;
}

void fill_row(const Place& p, const Observer* obs, u32 holder, ProgramRow* out) {
    row_record(p, holder, out);
    row_bytes(p, out);
    row_origin(obs, out);
}

} // namespace

void uid_text(u64 uid, char* out, u32 size) {
    const u64 place = uid >> 40, n = uid & ((1ull << 40) - 1);
    if (place)
        snprintf(out, size, "%llu.%llu", static_cast<unsigned long long>(place - 1),
                 static_cast<unsigned long long>(n));
    else
        snprintf(out, size, "%llu", static_cast<unsigned long long>(n));
}

const char* sort_label(ProgramSort s) {
    static const char* const kNames[] = {"age", "lineage", "threads", "bytes", "born"};
    return kNames[static_cast<u32>(s) % 5];
}

const char* cause_name(BirthCause c) {
    static const char* const kNames[] = {"a thread", "static bytes", "an arrival", "a boot", "by hand"};
    return kNames[static_cast<u32>(c) % 5];
}

const char* state_name(ThreadState s) {
    static const char* const kNames[] = {"free",     "ready",    "running",   "sleeping",
                                         "wait int", "wait msg", "wait disk", "wait echo"};
    return kNames[static_cast<u32>(s) % 8];
}

void list_programs(const Place& p, const Observer* obs, ProgramSort by, bool down, Array<ProgramRow>* out,
                   bool whole) {
    out->clear();
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        if (!p.os.program(kFirstProgramHolder + i).alive)
            continue;
        ++met;
        ProgramRow row;
        row_record(p, kFirstProgramHolder + i, &row);
        if (whole || by == ProgramSort::bytes)
            row_bytes(p, &row);
        if (whole || by == ProgramSort::lineage)
            row_origin(obs, &row);
        out->push(row);
    }
    sort_rows(out, by, down);
}

void finish_row(const Place& p, const Observer* obs, ProgramSort by, ProgramRow* row) {
    if (by != ProgramSort::bytes)
        row_bytes(p, row);
    if (by != ProgramSort::lineage)
        row_origin(obs, row);
}

bool find_program(const Place& p, const Observer* obs, u64 uid, ProgramRow* out, u32 holder) {
    if (!uid)
        return false;
    if (holder >= kFirstProgramHolder && holder - kFirstProgramHolder < p.os.program_capacity() &&
        p.os.program(holder).alive && p.os.program(holder).uid == uid) {
        fill_row(p, obs, holder, out);
        return true;
    }
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        const Program& g = p.os.program(kFirstProgramHolder + i);
        met += g.alive ? 1 : 0;
        if (g.alive && g.uid == uid) {
            fill_row(p, obs, kFirstProgramHolder + i, out);
            return true;
        }
    }
    return false;
}

u32 line_at(const Array<CodeLine>& lines, u32 off) {
    usize lo = 0, hi = lines.size();
    while (lo < hi) {
        const usize mid = (lo + hi) / 2;
        if (lines[mid].at + lines[mid].len <= off)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < lines.size() && lines[lo].at <= off ? static_cast<u32>(lo) : ~0u;
}

bool program_code(const Place& p, const Observer* obs, u64 uid, u64 genome, const ProgramRow* row,
                  ProgramCode* code, Array<CodeLine>* lines) {
    *code = ProgramCode{};
    lines->clear();
    const Memory& m = p.mem;
    const ProgramRec* rec = obs && row ? obs->program(uid) : nullptr;
    const GenomeRec* g = nullptr;
    if (rec)
        g = obs->genome(rec->genome);
    else if (obs && genome)
        g = obs->genome(genome);
    if (g && g->len) {
        code->bytes = obs->genome_bytes(*g);
        code->len = g->len;
        code->genome = g->hash;
        code->birth = true;
        // Where it lies, while the allocation it was born in is still its own.
        const u32 id = rec ? m.alloc_at(rec->base) : 0;
        if (id) {
            const Alloc& a = m.record(id);
            if (a.holder == row->holder && a.base == rec->base && rec->wlo + g->len <= a.size)
                code->addr = rec->base + rec->wlo;
        }
    } else if (row) {
        // No record of its birth: the allocation it has held longest (the
        // last of its list), as it lies now.
        u32 last = 0;
        for (u32 id = m.first_of(row->holder); id; id = m.record(id).next)
            last = id;
        if (!last)
            return false;
        code->bytes = m.bytes() + m.record(last).base;
        code->len = m.record(last).size;
        code->addr = m.record(last).base;
    } else {
        return false;
    }
    const u8* live = code->birth && code->addr != kNowhere ? m.bytes() + code->addr : nullptr;
    for (u32 at = 0; at < code->len;) {
        CodeLine l;
        l.at = at;
        l.len = (code->bytes[at] & 7) == kImm && at + 1 < code->len ? 2 : 1;
        l.changed = live && memcmp(live + at, code->bytes + at, l.len) != 0;
        lines->push(l);
        at += l.len;
    }
    if (row && code->addr != kNowhere) {
        for (u32 t = p.os.program(row->holder).first_thread; t; t = p.os.thread(t - 1).next) {
            const u32 k = line_at(*lines, regs_of(p, t - 1).ip - code->addr);
            if (k != ~0u && (*lines)[k].threads < 0xFFFF)
                ++(*lines)[k].threads;
        }
    }
    return true;
}

void line_text(const ProgramCode& code, const CodeLine& line, TextBuf* out) {
    out->clear();
    disassemble(code.bytes + line.at, line.len, out);
}

void code_text(const ProgramCode& code, const Array<CodeLine>& lines, const ProgramRow& row, u32 place, u64 tick,
               TextBuf* out) {
    out->clear();
    char buf[160], id[40], origin[40];
    uid_text(row.uid, id, sizeof id);
    uid_text(row.origin, origin, sizeof origin);
    snprintf(buf, sizeof buf, "Program %s at place %u, tick %llu; lineage %s\n", id, place,
             static_cast<unsigned long long>(tick), origin);
    out->append(Str(buf));
    if (code.birth)
        snprintf(buf, sizeof buf, "Its genome: %u bytes, %u instructions, hash %016llx\n", code.len,
                 static_cast<u32>(lines.size()), static_cast<unsigned long long>(code.genome));
    else
        snprintf(buf, sizeof buf, "Its bytes as they lie now at %u: %u bytes, %u instructions (no record of its birth)\n",
                 code.addr, code.len, static_cast<u32>(lines.size()));
    out->append(Str(buf));
    out->append("Hex (nova2 trace reads it):\n");
    for (u32 i = 0; i < code.len; ++i) {
        snprintf(buf, sizeof buf, "%02x", code.bytes[i]);
        out->append(Str(buf));
    }
    bool changed = false;
    for (u32 k = 0; k < lines.size(); ++k)
        changed = changed || lines[k].changed;
    out->append(changed ? "\nCode (*: its bytes have changed in memory since the birth):\n" : "\nCode:\n");
    TextBuf one;
    for (u32 k = 0; k < lines.size(); ++k) {
        const CodeLine& l = lines[k];
        char bytes[8];
        if (l.len == 2)
            snprintf(bytes, sizeof bytes, "%02x %02x", code.bytes[l.at], code.bytes[l.at + 1]);
        else
            snprintf(bytes, sizeof bytes, "%02x", code.bytes[l.at]);
        line_text(code, l, &one);
        snprintf(buf, sizeof buf, "%5u  %-5s  %s%s\n", l.at, bytes, one.c_str(), l.changed ? "  *" : "");
        out->append(Str(buf));
    }
}

void list_threads(const Place& p, const ProgramRow& row, Array<ThreadRow>* out) {
    out->clear();
    for (u32 t = p.os.program(row.holder).first_thread; t; t = p.os.thread(t - 1).next) {
        const Thread& th = p.os.thread(t - 1);
        ThreadRow r;
        r.record = t - 1;
        r.id = th.id;
        r.core = th.core;
        r.state = th.state;
        r.r = regs_of(p, t - 1);
        out->push(r);
    }
}

void picked_pages(const Place& p, const Observer* obs, const ViewState& st, Array<u8>* out) {
    out->clear();
    ProgramRow row;
    if (!st.program || st.program_place != p.id() || !find_program(p, obs, st.program, &row, st.picked.holder))
        return;
    out->resize(p.mem.page_count());
    const u32 ps = p.mem.page_size();
    for (u32 id = p.mem.first_of(row.holder); id; id = p.mem.record(id).next) {
        const Alloc& a = p.mem.record(id);
        const u32 last = (a.base + (a.extent ? a.extent - 1 : 0)) / ps;
        for (u32 pg = a.base / ps; pg <= last && pg < out->size(); ++pg)
            (*out)[pg] = 1;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace {

// The frame's rows, kept between frames so a frame allocates nothing once
// they have grown (the views are drawn by one thread).
Array<ProgramRow> g_rows;
Array<CodeLine> g_lines;
Array<ThreadRow> g_threads;
TextBuf g_text;
TextBuf g_copy; // what the copy button last made

const char* const kCauseShort[] = {"thread", "static", "arrival", "boot", "hand"};

// The list's columns: lineage, id, threads, bytes, age and how it was born.
struct Columns {
    i32 x[7];
};

Columns columns(Canvas& c, const Rect& r) {
    const i32 w[5] = {c.px(60), c.px(70), c.px(54), c.px(44), c.px(48)};
    Columns k;
    k.x[0] = r.x;
    for (u32 i = 0; i < 5; ++i)
        k.x[i + 1] = k.x[i] + w[i];
    k.x[6] = r.right();
    return k;
}

void swatch(Canvas& c, i32 x, i32 y, i32 h, u64 origin) {
    const i32 d = h - c.px(6);
    c.fill(Rect{x, y + (h - d) / 2, d, d}, lineage_colour(origin));
}

// A count, right-aligned in its column; a large one in thousands or
// millions, to fit.
void number(Canvas& c, i32 right, i32 y, u64 v, u32 colour, i32 max_w) {
    char buf[32];
    if (v >= 10000000)
        snprintf(buf, sizeof buf, "%.1fM", static_cast<double>(v) / 1e6);
    else if (v >= 100000)
        snprintf(buf, sizeof buf, "%.0fk", static_cast<double>(v) / 1e3);
    else
        snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v));
    c.text(right - c.px(6), y, buf, kFontMono, colour, kAlignRight, max_w);
}

void uid_at(Canvas& c, i32 right, i32 y, u64 uid, u32 colour, i32 max_w) {
    char buf[40];
    uid_text(uid, buf, sizeof buf);
    c.text(right - c.px(6), y, buf, kFontMono, colour, kAlignRight, max_w);
}

void pick(ViewState& st, const Place& p, const ProgramRow& row) {
    st.program = row.uid;
    st.program_place = p.id();
    st.picked = row;
    st.picked_alive = true;
    st.picked_genome = 0;
    st.thread = ~0u;
    st.code_scroll = Scroll{};
    st.threads_scroll = Scroll{};
}

// The list of the place's programs in r: a head of buttons that sort it,
// and a row a program. A click picks one.
void draw_list(Canvas& c, const Rect& r, ViewState& st, const Place& p, const Observer* obs, u64 now,
               const UiInput& in) {
    // The rows' dear parts only for the rows shown (a crowded place lists
    // thousands).
    list_programs(p, obs, st.sort, st.sort_down, &g_rows, false);
    const i32 bh = button_h(c), rh = c.line_h(kFontMono) + c.px(2);
    const Rect body{r.x, r.y + bh + c.px(2), r.w, r.h - bh - c.px(2)};
    ListRows rows = scroll_list(c, in, body, rh, static_cast<u32>(g_rows.size()), st.programs_scroll);
    for (u32 i = rows.first; i < rows.first + rows.count; ++i)
        finish_row(p, obs, st.sort, &g_rows[i]);
    const Columns k = columns(c, rows.rows);
    // The head: lineage, then the id (the order of age), then the rest.
    const ProgramSort by[6] = {ProgramSort::lineage, ProgramSort::count, ProgramSort::threads,
                               ProgramSort::bytes,   ProgramSort::age,   ProgramSort::born};
    for (u32 i = 0; i < 6; ++i) {
        const Rect h{k.x[i], r.y, k.x[i + 1] - k.x[i] - c.px(2), bh};
        if (by[i] == ProgramSort::count) {
            c.text(h.right() - c.px(6), h.y + (bh - c.line_h(kFontSmall)) / 2, "id", kFontSmall, kDim, kAlignRight);
            continue;
        }
        if (button(c, in, h, sort_label(by[i]), st.sort == by[i])) {
            st.sort_down = st.sort == by[i] ? !st.sort_down : false;
            st.sort = by[i];
        }
    }
    const bool here = st.program && st.program_place == p.id();
    for (u32 i = rows.first; i < rows.first + rows.count; ++i) {
        const ProgramRow& g = g_rows[i];
        const Rect row = rows.row(i);
        note(in, row, "program row");
        const bool hot = row.contains(in.mx, in.my), sel = here && g.uid == st.program;
        if (sel || hot)
            c.fill(row, sel ? kRowSel : kRowHot);
        const i32 y = row.y + c.px(1);
        swatch(c, k.x[0] + c.px(4), row.y, rh, g.origin);
        uid_at(c, k.x[1], y, g.origin, g.known ? kDim : kFaint, k.x[1] - k.x[0] - rh - c.px(6));
        uid_at(c, k.x[2], y, g.uid, kText, k.x[2] - k.x[1] - c.px(6));
        number(c, k.x[3], y, g.threads, kText, k.x[3] - k.x[2]);
        number(c, k.x[4], y, g.bytes, kText, k.x[4] - k.x[3]);
        number(c, k.x[5], y, now > g.born_tick ? now - g.born_tick : 0, kText, k.x[5] - k.x[4]);
        c.text(k.x[5] + c.px(4), y, kCauseShort[static_cast<u32>(g.cause) % 5], kFontMono, kDim, kAlignLeft,
               k.x[6] - k.x[5] - c.px(4));
        if (hot && in.pressed)
            pick(st, p, g);
    }
    if (g_rows.empty())
        c.text(r.x, body.y + c.px(2), "no programs alive here", kFontUi, kDim, kAlignLeft, r.w);
}

// The picked program's code in r: a line an instruction, its offset first;
// a line a thread stands at is lit and says how many; a line whose bytes
// have changed in memory since the birth is marked. A click on a lit line
// follows its first thread.
void draw_code(Canvas& c, const Rect& r, ViewState& st, const ProgramCode& code, u32 follow_line, bool alive,
               const UiInput& in) {
    const i32 rh = c.line_h(kFontMono) + c.px(2);
    const u32 fit = r.h > 0 ? static_cast<u32>(r.h / rh) : 0;
    // The thread followed stays in view.
    if (follow_line != ~0u && fit) {
        if (follow_line < st.code_scroll.first)
            st.code_scroll.first = follow_line;
        else if (follow_line >= st.code_scroll.first + fit)
            st.code_scroll.first = follow_line - fit + 1;
    }
    ListRows rows = scroll_list(c, in, r, rh, static_cast<u32>(g_lines.size()), st.code_scroll);
    char buf[96];
    for (u32 i = rows.first; i < rows.first + rows.count; ++i) {
        const CodeLine& l = g_lines[i];
        const Rect row = rows.row(i);
        note(in, row, "code line");
        if (i == follow_line)
            c.fill(row, kRowSel);
        else if (l.threads)
            c.fill(row, kRowHot);
        line_text(code, l, &g_text);
        snprintf(buf, sizeof buf, "%5u  %s%s", l.at, g_text.c_str(), l.changed ? "  *" : "");
        c.text(row.x + c.px(2), row.y + c.px(1), buf, kFontMono, !alive ? kDim : (l.changed ? kWarn : kText),
               kAlignLeft, row.w - c.px(70));
        if (l.threads) {
            snprintf(buf, sizeof buf, "%u here", l.threads);
            c.text(row.right() - c.px(4), row.y + c.px(1), buf, kFontMono, kAccent, kAlignRight);
            if (row.contains(in.mx, in.my) && in.pressed)
                for (u32 t = 0; t < g_threads.size(); ++t)
                    if (line_at(g_lines, g_threads[t].r.ip - code.addr) == i) {
                        st.thread = g_threads[t].record;
                        break;
                    }
        }
    }
}

// The picked program's threads in r: a row a thread (its id, state, core,
// and where it stands: an offset in the code, or an address outside it). A
// click follows one, or lets it go.
void draw_threads(Canvas& c, const Rect& r, ViewState& st, const ProgramCode& code, const UiInput& in) {
    const i32 rh = c.line_h(kFontMono) + c.px(2);
    ListRows rows = scroll_list(c, in, r, rh, static_cast<u32>(g_threads.size()), st.threads_scroll);
    char buf[96], where[32], core[8];
    for (u32 i = rows.first; i < rows.first + rows.count; ++i) {
        const ThreadRow& t = g_threads[i];
        const Rect row = rows.row(i);
        note(in, row, "thread row");
        const bool hot = row.contains(in.mx, in.my), sel = t.record == st.thread;
        if (sel || hot)
            c.fill(row, sel ? kRowSel : kRowHot);
        if (t.r.ip - code.addr < code.len)
            snprintf(where, sizeof where, "+%u", t.r.ip - code.addr);
        else
            snprintf(where, sizeof where, "at %u", t.r.ip);
        if (t.core)
            snprintf(core, sizeof core, "c%u", t.core - 1);
        else
            snprintf(core, sizeof core, "--");
        snprintf(buf, sizeof buf, "%6u  %-9s  %-3s  %s", t.id, state_name(t.state), core, where);
        c.text(row.x + c.px(2), row.y + c.px(1), buf, kFontMono, t.core ? kText : kDim, kAlignLeft, row.w - c.px(4));
        if (hot && in.pressed)
            st.thread = sel ? ~0u : t.record;
    }
}

} // namespace

const char* copied_text() { return g_copy.c_str(); }

void draw_programs(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                   const UiInput& in) {
    char buf[200];
    const u64 now = w.tick_count();
    const i32 lh = c.line_h(kFontUi) + c.px(2), rh = c.line_h(kFontMono) + c.px(2), g = c.px(6);
    const bool here = st.program && st.program_place == p.id();
    if (!here) {
        // The list alone, and under it what a click does (or where the
        // program picked is).
        const Rect list{r.x, r.y, r.w, r.h - lh - g};
        draw_list(c, list, st, p, obs, now, in);
        if (st.program) {
            char id[40];
            uid_text(st.program, id, sizeof id);
            snprintf(buf, sizeof buf, "program %s, picked in place %u", id, st.program_place);
            const i32 bw = button_w(c, "go to it");
            c.text(r.x, r.bottom() - lh, buf, kFontUi, kDim, kAlignLeft, r.w - bw - g);
            if (button(c, in, Rect{r.right() - bw, r.bottom() - lh - c.px(2), bw, button_h(c)}, "go to it") &&
                st.program_place < w.place_count())
                st.place = st.program_place;
        } else {
            c.text(r.x, r.bottom() - lh, "click a program for its code and threads", kFontUi, kDim, kAlignLeft, r.w);
        }
        return;
    }
    // The program picked, as it is now; dead, as it was last seen.
    ProgramRow row;
    const bool alive = find_program(p, obs, st.program, &row, st.picked.holder);
    if (alive)
        st.picked = row;
    st.picked_alive = alive;
    ProgramCode code;
    const bool coded = program_code(p, obs, st.program, st.picked_genome, alive ? &row : nullptr, &code, &g_lines);
    if (coded && code.birth)
        st.picked_genome = code.genome;
    g_threads.clear();
    if (alive)
        list_threads(p, row, &g_threads);
    u32 followed = ~0u; // the thread followed, among g_threads
    for (u32 t = 0; t < g_threads.size(); ++t)
        followed = g_threads[t].record == st.thread ? t : followed;
    if (followed == ~0u)
        st.thread = ~0u;

    // Top to bottom: the list (three tenths), the program, its code, its
    // threads (five rows at least) and the followed thread's registers.
    i32 list_h = r.h * 3 / 10;
    list_h = list_h < button_h(c) + 4 * rh ? button_h(c) + 4 * rh : list_h;
    const i32 threads_h = 5 * rh, regs_h = 2 * rh;
    i32 y = r.y;
    draw_list(c, Rect{r.x, y, r.w, list_h}, st, p, obs, now, in);
    y += list_h + g;
    const ProgramRow& pr = st.picked;
    const u32 ink = alive ? kText : kDim;
    char id[40];
    uid_text(pr.uid, id, sizeof id);
    snprintf(buf, sizeof buf, "Program %s", id);
    c.text(r.x, y, buf, kFontBold, ink);
    const i32 name_w = c.text_w(buf, kFontBold) + c.px(10);
    swatch(c, r.x + name_w, y, lh, pr.origin);
    uid_text(pr.origin, id, sizeof id);
    snprintf(buf, sizeof buf, "lineage %s%s", id, pr.known ? "" : " (its own: no record)");
    c.text(r.x + name_w + lh, y, buf, kFontUi, kDim, kAlignLeft, r.w - name_w - lh - button_w(c, "let go") - g);
    if (button(c, in, Rect{r.right() - button_w(c, "let go"), y, button_w(c, "let go"), button_h(c)}, "let go")) {
        st.program = 0;
        st.thread = ~0u;
    }
    y += lh;
    if (alive)
        snprintf(buf, sizeof buf, "born at tick %llu from %s; %u thread%s, %u bytes",
                 static_cast<unsigned long long>(pr.born_tick), cause_name(pr.cause), pr.threads,
                 pr.threads == 1 ? "" : "s", pr.bytes);
    else
        snprintf(buf, sizeof buf, "it has died (born at tick %llu from %s)",
                 static_cast<unsigned long long>(pr.born_tick), cause_name(pr.cause));
    c.text(r.x, y, buf, kFontUi, alive ? kText : kWarn, kAlignLeft, r.w);
    y += lh;
    // Its code and threads, or its family (build-plan-viewer.md step 3).
    ButtonRow panes{c, in, r.right(), y, button_h(c)};
    if (panes.left("family", st.family))
        st.family = true;
    if (panes.left("code", !st.family))
        st.family = false;
    if (st.family) {
        c.text(r.x, y, "Family", kFontUi, kDim, kAlignLeft, panes.x - r.x);
        y += lh;
        draw_family(c, Rect{r.x, y, r.w, r.bottom() - y}, st, w, p, obs, pr, alive, in);
        return;
    }
    if (!coded)
        snprintf(buf, sizeof buf, "Code: none to show (no record of its birth)");
    else if (code.birth)
        snprintf(buf, sizeof buf, "Code: its genome, %u bytes, %u instructions", code.len,
                 static_cast<u32>(g_lines.size()));
    else
        snprintf(buf, sizeof buf, "Code: %u bytes as they lie now (no record of its birth)", code.len);
    // The code onto the clipboard (the window does that, when the frame is
    // done): the button says so once it has, for this program.
    if (coded) {
        panes.x -= c.px(10);
        if (panes.left(st.copied == st.program ? "copied" : "copy")) {
            code_text(code, g_lines, pr, st.program_place, now, &g_copy);
            st.copy_asked = true;
            st.copied = st.program;
        }
    }
    c.text(r.x, y, buf, kFontUi, kDim, kAlignLeft, panes.x - r.x - c.px(4));
    y += lh + c.px(2);
    const i32 threads_top = r.bottom() - regs_h - threads_h - lh;
    u32 follow_line = ~0u;
    if (followed != ~0u && coded)
        follow_line = line_at(g_lines, g_threads[followed].r.ip - code.addr);
    if (coded && threads_top - g > y + rh)
        draw_code(c, Rect{r.x, y, r.w, threads_top - g - y}, st, code, follow_line, alive, in);
    y = threads_top;
    c.text(r.x, y, alive ? "Threads (click one to follow it)" : "Threads: none, it has died", kFontUi, kDim,
           kAlignLeft, r.w);
    y += lh;
    if (alive)
        draw_threads(c, Rect{r.x, y, r.w, threads_h}, st, code, in);
    y += threads_h;
    if (followed != ~0u) {
        const Regs& x = g_threads[followed].r;
        snprintf(buf, sizeof buf, "A %-10u B %-10u C %-10u", x.a, x.b, x.c);
        c.text(r.x + c.px(2), y, buf, kFontMono, kText, kAlignLeft, r.w);
        snprintf(buf, sizeof buf, "D %-10u IP %-9u SP %-9u", x.d, x.ip, x.sp);
        c.text(r.x + c.px(2), y + rh, buf, kFontMono, kText, kAlignLeft, r.w);
    }
}

} // namespace nova2
