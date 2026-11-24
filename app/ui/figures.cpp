#include "figures.h"

#include <cstdio>  // snprintf
#include <cstring> // memcpy

#include "nova2/world.h"
#include "theme.h"
#include "viewer.h"
#include "widgets.h"

namespace nova2 {

namespace {

bool better(const Share& a, const Share& b) { return a.count > b.count || (a.count == b.count && a.key < b.key); }

// Keeps the kTop best of what it is offered, the best first.
void offer(Share* top, u32* n, const Share& s) {
    u32 i = *n;
    if (i < kTop)
        ++*n;
    else if (!better(s, top[kTop - 1]))
        return;
    else
        i = kTop - 1;
    for (; i > 0 && better(s, top[i - 1]); --i)
        top[i] = top[i - 1];
    top[i] = s;
}

void sort_keys(Array<u64>& v) {
    u64* a = v.data();
    const usize n = v.size();
    for (usize gap = n / 2; gap > 0; gap = gap == 2 ? 1 : gap * 5 / 11) {
        for (usize i = gap; i < n; ++i) {
            const u64 x = a[i];
            usize j = i;
            for (; j >= gap && x < a[j - gap]; j -= gap)
                a[j] = a[j - gap];
            a[j] = x;
        }
    }
}

// The commonest keys of v (which it sorts), and how many distinct ones.
u32 commonest(Array<u64>& v, Share* top, u32* shown) {
    sort_keys(v);
    u32 distinct = 0;
    *shown = 0;
    for (usize i = 0; i < v.size();) {
        usize j = i;
        while (j < v.size() && v[j] == v[i])
            ++j;
        Share s;
        s.key = v[i];
        s.count = j - i;
        offer(top, shown, s);
        ++distinct;
        i = j;
    }
    return distinct;
}

} // namespace

const char* death_name(u32 cause) {
    static const char* const kNames[kDeathCauses] = {"exit",        "fault",      "cut-out", "device failed",
                                                     "memory lost", "cold start", "heat",    "out of memory",
                                                     "watchdog"};
    return kNames[cause % kDeathCauses];
}

void family_of(World& w, const Place& p, const Observer* obs, const ProgramRow& row, Family* out) {
    *out = Family{};
    const Program& g = p.os.program(row.holder);
    out->parent = parent_of(g.parent_uid); // (without the current's mark, 12.6)
    out->origin = row.origin;
    if (out->parent) {
        const u32 at = uid_place(out->parent);
        ProgramRow parent;
        if (at < w.place_count() && find_program(w.place(at), obs, out->parent, &parent)) {
            out->parent_alive = true;
            out->parent_place = at;
        }
    }
    // Its children here, the oldest first (the records are not in order of
    // age: the first kShown are kept sorted as they are found).
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        const Program& k = p.os.program(kFirstProgramHolder + i);
        met += k.alive ? 1 : 0; // the walk ends once every living program is met (programs.h)
        if (!k.alive || parent_of(k.parent_uid) != row.uid)
            continue;
        const u32 kept = out->children < Family::kShown ? out->children : Family::kShown;
        ++out->children;
        if (kept == Family::kShown && k.uid > out->child[kept - 1])
            continue;
        u32 at = kept < Family::kShown ? kept : kept - 1;
        for (; at > 0 && out->child[at - 1] > k.uid; --at)
            out->child[at] = out->child[at - 1];
        out->child[at] = k.uid;
    }
    const ProgramRec* r = obs ? obs->program(row.uid) : nullptr;
    if (!r)
        return;
    out->known = true;
    out->depth = r->depth;
    out->fdepth = r->fdepth;
    out->faithful = r->faithful;
    out->copy = r->copy;
    out->arrived = r->arrived;
    out->group = r->group;
    if (const GenomeRec* gr = obs->genome(r->genome)) {
        out->group = gr->group; // its genome's now: kept since the birth, or not yet (13.2)
        out->genome_len = gr->len;
        out->genome_births = gr->count;
    }
    if (const LineageRec* L = obs->lineage(r->origin)) {
        out->lineage_alive = L->alive;
        out->lineage_births = L->births;
        out->lineage_faithful = L->faithful;
        out->lineage_places = L->home_alive ? 1 : 0;
        for (u32 k = L->first_colony; k; k = obs->colony_at(k - 1).next)
            out->lineage_places += obs->colony_at(k - 1).alive ? 1 : 0;
    }
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        const Program& k = p.os.program(kFirstProgramHolder + i);
        if (!k.alive)
            continue;
        ++met;
        const ProgramRec* kr = obs->program(k.uid);
        out->lineage_here += kr && kr->origin == r->origin ? 1 : 0;
    }
}

void place_figures(const Place& p, const Observer* obs, PlaceFigures* out) {
    static Array<u64> origins, genomes; // the frame's scratch (the views are drawn by one thread)
    *out = PlaceFigures{};
    out->programs = p.os.live_programs();
    out->threads = p.os.live_threads();
    out->births = p.births;
    out->deaths = p.deaths;
    if (!obs)
        return;
    origins.clear();
    genomes.clear();
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        const Program& g = p.os.program(kFirstProgramHolder + i);
        met += g.alive ? 1 : 0;
        const ProgramRec* r = g.alive ? obs->program(g.uid) : nullptr;
        if (!r)
            continue;
        ++out->known;
        out->faithful += r->faithful ? 1 : 0;
        out->copies += r->copy ? 1 : 0;
        origins.push(r->origin);
        genomes.push(r->genome);
    }
    out->lineage_count = commonest(origins, out->lineages, &out->lineages_shown);
    out->genome_count = commonest(genomes, out->genomes, &out->genomes_shown);
    for (u32 k = 0; k < out->genomes_shown; ++k)
        if (const GenomeRec* g = obs->genome(out->genomes[k].key))
            out->genomes[k].len = g->len;
    out->window = obs->settings().turnover.window;
    if (p.id() < obs->turnover.size()) {
        out->watched = true;
        out->turnover = obs->turnover[p.id()];
        memcpy(out->deaths_by_cause, out->turnover.deaths_by_cause, sizeof out->deaths_by_cause);
    }
}

void world_figures(World& w, const Observer* obs, WorldFigures* out) {
    *out = WorldFigures{};
    for (u32 i = 0; i < w.place_count(); ++i) {
        out->programs += w.place(i).os.live_programs();
        out->threads += w.place(i).os.live_threads();
    }
    if (!obs)
        return;
    out->births = obs->total_births;
    out->faithful = obs->total_faithful;
    out->deaths = obs->total_deaths;
    memcpy(out->deaths_by_cause, obs->deaths_by_cause, sizeof out->deaths_by_cause);
    memcpy(out->births_by_cause, obs->births_by_cause, sizeof out->births_by_cause);
    out->cold_starts = obs->cold_starts;
    out->genomes = obs->genomes_kept();
    out->genomes_held = obs->genomes_held();
    out->groups = obs->group_count();
    out->lineages = obs->lineages_ever(); // those let go included (3.3)
    out->colonies = obs->colony_count();
    for (u32 i = 0; i < obs->colony_count(); ++i)
        out->colonies_alive += obs->colony_at(i).alive ? 1 : 0;
    for (u32 i = 0; i < obs->lineage_count(); ++i) {
        const LineageRec& L = obs->lineage_at(i);
        if (!L.alive)
            continue;
        ++out->lineages_alive;
        Share s;
        s.key = L.origin;
        s.count = L.alive;
        offer(out->top_lineages, &out->lineages_shown, s);
    }
    for (u32 i = 0; i < obs->genome_count(); ++i) {
        const GenomeRec& g = obs->genome_at(i);
        if (g.state != GenomeState::kept)
            continue;
        Share s;
        s.key = g.hash;
        s.count = g.count;
        s.len = g.len;
        offer(out->top_genomes, &out->genomes_shown, s);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace {

// Lines of text down a column.
struct Lines {
    Canvas& c;
    i32 x, y, w, lh;
    void line(const char* s, u32 colour = kText, u32 font = kFontUi) {
        c.text(x, y, s, font, colour, kAlignLeft, w);
        y += lh;
    }
    void head(const char* s) {
        y += c.px(6);
        line(s, kDim, kFontBold);
    }
    // A lineage: its colour, its first program, and what is said of it.
    void lineage(u64 origin, const char* said) {
        const i32 d = lh - c.px(6);
        c.fill(Rect{x, y + c.px(2), d, d}, lineage_colour(origin));
        char id[40], buf[160];
        uid_text(origin, id, sizeof id);
        snprintf(buf, sizeof buf, "%s  %s", id, said);
        c.text(x + lh, y, buf, kFontUi, kText, kAlignLeft, w - lh);
        y += lh;
    }
};

double percent(u64 part, u64 whole) {
    return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0;
}

unsigned long long ull(u64 v) { return static_cast<unsigned long long>(v); }

// The labels of the family's buttons: they must outlast the frame (the
// tests ask where each button was).
char g_child_label[Family::kShown][40];
char g_parent_label[48];

void pick_uid(ViewState& st, World& w, const Observer* obs, u64 uid, u32 place) {
    ProgramRow row;
    if (place >= w.place_count() || !find_program(w.place(place), obs, uid, &row))
        return;
    st.place = place;
    st.program = uid;
    st.program_place = place;
    st.picked = row;
    st.picked_alive = true;
    st.picked_genome = 0;
    st.thread = ~0u;
    st.code_scroll = Scroll{};
    st.threads_scroll = Scroll{};
}

void shares(Lines& L, const Share* top, u32 shown, u64 whole, bool lineages) {
    char buf[160];
    for (u32 k = 0; k < shown; ++k) {
        if (lineages) {
            snprintf(buf, sizeof buf, "%llu alive, %.1f%%", ull(top[k].count), percent(top[k].count, whole));
            L.lineage(top[k].key, buf);
        } else if (whole) {
            snprintf(buf, sizeof buf, "%08x  %u bytes: %llu, %.1f%%", static_cast<u32>(top[k].key >> 32), top[k].len,
                     ull(top[k].count), percent(top[k].count, whole));
            L.line(buf, kText, kFontMono);
        } else {
            snprintf(buf, sizeof buf, "%08x  %u bytes: %llu births", static_cast<u32>(top[k].key >> 32), top[k].len,
                     ull(top[k].count));
            L.line(buf, kText, kFontMono);
        }
    }
    if (!shown)
        L.line("none", kDim);
}

void causes(Lines& L, const u64* by, u64 total) {
    char buf[160];
    constexpr u32 kFirstSix = 6; // gate 3's causes, always shown
    for (u32 k = 0; k + 1 < kFirstSix; k += 2) {
        snprintf(buf, sizeof buf, "%s %llu (%.0f%%), %s %llu (%.0f%%)", death_name(k), ull(by[k]),
                 percent(by[k], total), death_name(k + 1), ull(by[k + 1]), percent(by[k + 1], total));
        L.line(buf);
    }
    // Gate 4's and gate 5's, once there are any, two to a line.
    u32 later[kDeathCauses], n = 0;
    for (u32 k = kFirstSix; k < kDeathCauses; ++k)
        if (by[k])
            later[n++] = k;
    for (u32 i = 0; i < n; i += 2) {
        if (i + 1 < n)
            snprintf(buf, sizeof buf, "%s %llu (%.0f%%), %s %llu (%.0f%%)", death_name(later[i]), ull(by[later[i]]),
                     percent(by[later[i]], total), death_name(later[i + 1]), ull(by[later[i + 1]]),
                     percent(by[later[i + 1]], total));
        else
            snprintf(buf, sizeof buf, "%s %llu (%.0f%%)", death_name(later[i]), ull(by[later[i]]),
                     percent(by[later[i]], total));
        L.line(buf);
    }
}

void draw_place_figures(Canvas& c, Lines& L, Place& p, const Observer* obs) {
    (void)c;
    char buf[200];
    PlaceFigures f;
    place_figures(p, obs, &f);
    snprintf(buf, sizeof buf, "programs %u, threads %u", f.programs, f.threads);
    L.line(buf);
    snprintf(buf, sizeof buf, "births %llu, deaths %llu (since built or opened)", ull(f.births), ull(f.deaths));
    L.line(buf);
    if (!obs) {
        L.line("no observer: no more is known", kDim);
        return;
    }
    snprintf(buf, sizeof buf, "of the living: %u born faithful, %u as copies", f.faithful, f.copies);
    L.line(buf);
    if (f.watched) {
        u64 total = 0;
        for (u32 k = 0; k < kDeathCauses; ++k)
            total += f.deaths_by_cause[k];
        L.head("Deaths by cause");
        causes(L, f.deaths_by_cause, total);
    }
    snprintf(buf, sizeof buf, "Lineages alive here: %u", f.lineage_count);
    L.head(buf);
    shares(L, f.lineages, f.lineages_shown, f.known, true);
    snprintf(buf, sizeof buf, "Genomes alive here: %u", f.genome_count);
    L.head(buf);
    shares(L, f.genomes, f.genomes_shown, f.known ? f.known : 1, false);
    if (f.watched) {
        const PlaceTurnover& t = f.turnover;
        snprintf(buf, sizeof buf, "Turnover, windows of %llu ticks", ull(f.window));
        L.head(buf);
        snprintf(buf, sizeof buf, "counted %u, turned over %u; crowded %u, turned %u", t.windows, t.turned,
                 t.crowded_windows, t.crowded_turned);
        L.line(buf);
        snprintf(buf, sizeof buf, "without turnover: %u in a row now, %u at most", t.streak, t.longest);
        L.line(buf);
        snprintf(buf, sizeof buf, "this window (from tick %llu): %u of %u starters dead", ull(t.start),
                 t.starters_died, t.starters);
        L.line(buf);
        snprintf(buf, sizeof buf, "this window: births %u, deaths %u%s%s", t.births, t.deaths,
                 t.crowded ? ", crowded" : "", t.cold ? ", a cold start" : "");
        L.line(buf);
    }
}

void draw_world_figures(Canvas& c, Lines& L, const ViewState& st, World& w, const Observer* obs) {
    (void)c;
    // The observer's lists can be long. While they are short the world's
    // figures are worked out every frame; once they hold 200,000 lineages
    // and genomes, every fifteenth frame.
    static WorldFigures f;
    static u32 frames = 0;
    const u64 size = obs ? static_cast<u64>(obs->lineage_count()) + obs->genome_count() : 0;
    if (size < 200000 || frames == 0)
        world_figures(w, obs, &f);
    frames = size < 200000 ? 0 : (frames + 1) % 15;
    char buf[200];
    snprintf(buf, sizeof buf, "programs %llu, threads %llu", ull(f.programs), ull(f.threads));
    L.line(buf);
    if (!obs) {
        L.line("no observer: no more is known", kDim);
        return;
    }
    snprintf(buf, sizeof buf, "births %llu, %llu faithful (%.1f%%)", ull(f.births), ull(f.faithful),
             percent(f.faithful, f.births));
    L.line(buf);
    snprintf(buf, sizeof buf, "deaths %llu, cold starts %llu", ull(f.deaths), ull(f.cold_starts));
    L.line(buf);
    // A run's seed: the stages it has reached (10.6), from the run's own
    // record of them.
    if (st.seeds && st.seed_shown < st.seed_count) {
        const SeedTab& t = st.seeds[st.seed_shown];
        snprintf(buf, sizeof buf, "Seed %llu's stages", ull(t.seed));
        L.head(buf);
        char part[2][64];
        for (u32 s = 1; s <= 5; ++s) {
            char* out = part[(s - 1) % 2];
            if (t.stage_tick[s] != ~0ull)
                snprintf(out, sizeof part[0], "%u at tick %llu", s, ull(t.stage_tick[s]));
            else
                snprintf(out, sizeof part[0], "%u not yet", s);
            if (s % 2 == 0 || s == 5) {
                if (s % 2 == 0)
                    snprintf(buf, sizeof buf, "%s; %s", part[0], part[1]);
                else
                    snprintf(buf, sizeof buf, "%s", part[0]);
                L.line(buf);
            }
        }
        if (t.saved != ~0ull) {
            snprintf(buf, sizeof buf, "its world last saved at tick %llu", ull(t.saved));
            L.line(buf);
        }
    }
    L.head("Births by cause");
    snprintf(buf, sizeof buf, "from a thread %llu, static bytes %llu", ull(f.births_by_cause[0]),
             ull(f.births_by_cause[1]));
    L.line(buf);
    snprintf(buf, sizeof buf, "an arrival %llu, a boot %llu, by hand %llu", ull(f.births_by_cause[2]),
             ull(f.births_by_cause[3]), ull(f.births_by_cause[4]));
    L.line(buf);
    L.head("Deaths by cause");
    causes(L, f.deaths_by_cause, f.deaths);
    snprintf(buf, sizeof buf, "Lineages: %llu alive of %llu", ull(f.lineages_alive), ull(f.lineages));
    L.head(buf);
    shares(L, f.top_lineages, f.lineages_shown, f.programs, true);
    snprintf(buf, sizeof buf, "colonies %llu, %llu with a member alive", ull(f.colonies), ull(f.colonies_alive));
    L.line(buf);
    if (f.genomes_held) // kept, and those not yet (13.2)
        snprintf(buf, sizeof buf, "Genomes: %llu kept in %llu groups, %llu held; the most born", ull(f.genomes),
                 ull(f.groups), ull(f.genomes_held));
    else
        snprintf(buf, sizeof buf, "Genomes: %llu in %llu groups; the most born", ull(f.genomes), ull(f.groups));
    L.head(buf);
    shares(L, f.top_genomes, f.genomes_shown, 0, false);
}

} // namespace

void draw_family(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                 const ProgramRow& row, bool alive, const UiInput& in) {
    Lines L{c, r.x, r.y, r.w, c.line_h(kFontUi) + c.px(2)};
    if (!alive) {
        L.line("it has died: its family is no longer known", kDim);
        return;
    }
    char buf[200], id[40];
    Family f;
    family_of(w, p, obs, row, &f);
    const i32 bh = button_h(c);
    // Its parent, a click away while it lives.
    L.head("Parent");
    if (!f.parent) {
        snprintf(buf, sizeof buf, "none: born from %s", cause_name(row.cause));
        L.line(buf);
    } else if (f.parent_alive) {
        uid_text(f.parent, id, sizeof id);
        snprintf(g_parent_label, sizeof g_parent_label, "parent %s", id);
        const Rect b{L.x, L.y, button_w(c, g_parent_label), bh};
        const bool go = button(c, in, b, g_parent_label);
        snprintf(buf, sizeof buf, "alive, in place %u", f.parent_place);
        c.text(b.right() + c.px(8), L.y + c.px(2), buf, kFontUi, kText, kAlignLeft, r.right() - b.right() - c.px(8));
        L.y += bh + c.px(4);
        if (go)
            pick_uid(st, w, obs, f.parent, f.parent_place);
    } else {
        uid_text(f.parent, id, sizeof id);
        snprintf(buf, sizeof buf, "%s, which has died", id);
        L.line(buf);
    }
    // Its children here.
    snprintf(buf, sizeof buf, "Children alive here: %u", f.children);
    L.head(buf);
    const u32 shown = f.children < Family::kShown ? f.children : Family::kShown;
    i32 bx = L.x;
    u64 go = 0;
    for (u32 k = 0; k < shown; ++k) {
        uid_text(f.child[k], g_child_label[k], sizeof g_child_label[k]);
        const i32 bw = button_w(c, g_child_label[k]);
        if (bx + bw > r.right() && bx > L.x) {
            bx = L.x;
            L.y += bh + c.px(3);
        }
        if (button(c, in, Rect{bx, L.y, bw, bh}, g_child_label[k]))
            go = f.child[k];
        bx += bw + c.px(3);
    }
    if (shown)
        L.y += bh + c.px(4);
    if (f.children > shown) {
        snprintf(buf, sizeof buf, "and %u more (the oldest are shown)", f.children - shown);
        L.line(buf, kDim);
    }
    if (go)
        pick_uid(st, w, obs, go, p.id());
    if (!f.known) {
        L.head("Lineage");
        L.line("no record of its birth: born before the observer watched", kDim);
        return;
    }
    L.head("Lineage");
    snprintf(buf, sizeof buf, "%u alive in the world, %u here", f.lineage_alive, f.lineage_here);
    L.lineage(f.origin, buf);
    snprintf(buf, sizeof buf, "in %u place%s; %llu births, %llu faithful", f.lineage_places,
             f.lineage_places == 1 ? "" : "s", ull(f.lineage_births), ull(f.lineage_faithful));
    L.line(buf);
    snprintf(buf, sizeof buf, "generation %u; %u faithful births in a row", f.depth, f.fdepth);
    L.line(buf);
    snprintf(buf, sizeof buf, "born %s%s", f.faithful ? "faithful" : (f.copy ? "a near copy" : "unlike its parent"),
             f.arrived ? ", from an arrival" : "");
    L.line(buf);
    L.head("Genome");
    if (f.group == kNoGroup)
        snprintf(buf, sizeof buf, "%u bytes, not yet kept; born %llu times in the world", f.genome_len,
                 ull(f.genome_births));
    else
        snprintf(buf, sizeof buf, "%u bytes, group %u; born %llu times in the world", f.genome_len, f.group,
                 ull(f.genome_births));
    L.line(buf);
}

void draw_figures(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                  const UiInput& in) {
    // Whose figures: the place's, or the world's.
    ButtonRow which{c, in, r.x, r.y, button_h(c)};
    if (which.right("this place", !st.world_figures))
        st.world_figures = false;
    if (which.right("the world", st.world_figures))
        st.world_figures = true;
    Lines L{c, r.x, r.y + button_h(c) + c.px(6), r.w, c.line_h(kFontUi) + c.px(2)};
    c.clip(r); // a short window cuts the foot off
    if (st.world_figures)
        draw_world_figures(c, L, st, w, obs);
    else
        draw_place_figures(c, L, p, obs);
    c.unclip();
}

} // namespace nova2
