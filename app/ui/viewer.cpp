#include "viewer.h"

#include <cmath>  // log
#include <cstdio> // snprintf
#include <cstring> // memset

#include "nova2/compress.h"
#include "nova2/hash.h"
#include "nova2/observe.h"
#include "nova2/pool.h" // clock_seconds
#include "nova2/world.h"
#include "figures.h"
#include "programs.h"
#include "settings.h"
#include "theme.h"
#include "widgets.h"

namespace nova2 {

namespace {

const char* const kColourNames[] = {"owner", "lineage", "permission", "size class"};

u32 rgb(float r, float g, float b) {
    const u32 R = static_cast<u32>(r * 255 + 0.5f), G = static_cast<u32>(g * 255 + 0.5f),
              B = static_cast<u32>(b * 255 + 0.5f);
    return R << 16 | G << 8 | B;
}

u32 hsv(float h, float s, float v) {
    const float c = v * s;
    const float hh = h / 60.0f;
    // hh mod 2, exactly as fmodf has it for hh in [0, 6), without its cost.
    const float mod2 = hh - 2.0f * static_cast<float>(static_cast<i32>(hh * 0.5f));
    const float x = c * (1 - fabsf(mod2 - 1));
    float r = 0, g = 0, b = 0;
    if (hh < 1) {
        r = c;
        g = x;
    } else if (hh < 2) {
        r = x;
        g = c;
    } else if (hh < 3) {
        g = c;
        b = x;
    } else if (hh < 4) {
        g = x;
        b = c;
    } else if (hh < 5) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const float m = v - c;
    return rgb(r + m, g + m, b + m);
}

// A steady colour for anything with an id. Each is worked out once and kept
// in a small table by its hash: hsv's arithmetic took 50 ns a colour, and a
// frame colours tens of thousands of programs (build-plan-viewer.md step
// 12). The views are drawn by one thread.
u32 colour_of(u64 key) {
    static u64 keys[4096];
    static u32 colours[4096];
    static bool used[4096];
    const u64 h = mix64(key + 0x9E37);
    const u32 slot = static_cast<u32>(h >> 52);
    if (used[slot] && keys[slot] == key)
        return colours[slot];
    const u32 col = hsv(static_cast<float>(h % 360), 0.55f + static_cast<float>((h >> 20) % 30) / 100.0f, 0.85f);
    keys[slot] = key;
    colours[slot] = col;
    used[slot] = true;
    return col;
}

} // namespace

u32 lineage_colour(u64 origin) { return colour_of(origin); }

namespace {

// 0 to 1, dark to warm.
u32 heat(double t) {
    if (t <= 0)
        return 0x2A2E38;
    if (t > 1)
        t = 1;
    return hsv(static_cast<float>(220 - 190 * t), 0.7f, static_cast<float>(0.35 + 0.6 * t));
}

// A colour darkened to f of itself (1: as it is).
u32 shade(u32 col, double f) {
    if (f >= 1)
        return col;
    if (f < 0)
        f = 0;
    const double r = (col >> 16) & 255, g = (col >> 8) & 255, b = col & 255;
    return static_cast<u32>(r * f + 0.5) << 16 | static_cast<u32>(g * f + 0.5) << 8 | static_cast<u32>(b * f + 0.5);
}

// How brightly a place's tile shows (build-plan-gate2.md step 8): a quarter
// at 0 V, all of it at its source's voltage.
double volt_shade(const Place& p) {
    const i32 vs = p.supply.source_mv();
    double s = vs > 0 ? static_cast<double>(p.env.voltage_mv) / vs : 1;
    s = s < 0 ? 0 : (s > 1 ? 1 : s);
    return 0.25 + 0.75 * s;
}

// A device's power state (4.4): on; or cut out, with the off-time left (the
// next tick to run counted); or cut out, the off-time over, waiting for the
// voltage that restarts it.
void power_state(char* out, usize n, const Place& p, bool on, u64 off_at, i32 threshold) {
    if (on) {
        snprintf(out, n, "on");
        return;
    }
    const PowerSettings& s = p.power();
    const u64 off = p.env.tick - off_at;
    if (off < s.off_time)
        snprintf(out, n, "cut out, %llu tick%s left", static_cast<unsigned long long>(s.off_time - off),
                 s.off_time - off == 1 ? "" : "s");
    else
        snprintf(out, n, "cut out until %d mV", threshold + s.restart_gap);
}

// What is happening at a place now (Damian, 2026-11-20: "some sort of
// indicator ... to show what is currently happening on a node"), the worst
// first. A place is marked in its worst's colour (theme.h's kNowColor): its
// tile's corner among the places, a ring on the map; the places' panel
// counts each across the world, and the inspector's place page says each in
// words.
// - cold start: one in the last kColdShown ticks (its memory wiped).
// - memory fading: a memory device cut out, its bits fading (9.4).
// - blackout: the place's own supply in a blackout (before gate 4's
//   sources), or a voltage under every core's threshold.
// - heat trip: a device off by its heat (gate 4's heat).
// - power low: a drop-out of its own supply, a voltage under the P cores'
//   threshold, or a core or the disk cut out by the voltage.
// - swell: one of the sources at the place swelling (gate 4).
// - network off: its network device off, cut out or tripped.
enum NowKind : u32 { kNowCold, kNowFading, kNowBlackout, kNowHeat, kNowLow, kNowSwell, kNowNetOff, kNowKinds };
const char* const kNowNames[kNowKinds] = {"cold start", "memory fading", "blackout",   "heat trip",
                                          "power low",  "swell",         "network off"};

// A cold start is marked this long after it. The views see a place's count
// of them go up from one frame to the next, so none before a view's first
// frame of a world is marked.
const u64 kColdShown = 2000;

// The cold starts seen, a place each: the count, and the tick it last went
// up (~0: not since the view began). Kept for one view of one world at a
// time, as the map's lineages are.
struct ColdSeen {
    Array<u64> count, at;
    u32 view = 0;
    const World* world = nullptr;
    u64 seed = 0;
};
ColdSeen g_cold;

void see_cold_starts(const ViewState& st, World& w) {
    const u32 n = w.place_count();
    const bool anew = g_cold.view != st.id || g_cold.world != &w || g_cold.seed != w.seed() || g_cold.count.size() != n;
    if (anew) {
        g_cold.count.resize_unfilled(n);
        g_cold.at.resize_unfilled(n);
        for (u32 i = 0; i < n; ++i) {
            g_cold.count[i] = w.place(i).cold_starts;
            g_cold.at[i] = ~0ull;
        }
        g_cold.view = st.id;
        g_cold.world = &w;
        g_cold.seed = w.seed();
        return;
    }
    for (u32 i = 0; i < n; ++i)
        if (w.place(i).cold_starts != g_cold.count[i]) {
            g_cold.count[i] = w.place(i).cold_starts;
            g_cold.at[i] = w.tick_count();
        }
}

// The ticks since place i's last cold start seen, or ~0.
u64 cold_ago(World& w, u32 i) {
    if (g_cold.world != &w || i >= g_cold.at.size() || g_cold.at[i] == ~0ull || g_cold.at[i] > w.tick_count())
        return ~0ull;
    return w.tick_count() - g_cold.at[i];
}

// The drop-out a place's own supply is in at the last tick run (or the next
// to run): its share left, kCapacityOne if none. None with gate 4's sources.
u32 own_dropout(World& w, const Place& p, Dropout* drop) {
    if (w.powered())
        return kCapacityOne;
    const u64 last = p.env.tick ? p.env.tick - 1 : 0;
    u32 keep = p.supply.dropout(last, drop);
    if (keep == kCapacityOne)
        keep = p.supply.dropout(p.env.tick, drop);
    return keep;
}

// What is happening at place i now: a bit a NowKind.
u32 now_at(World& w, u32 i) {
    const Place& p = w.place(i);
    u32 now = 0;
    if (cold_ago(w, i) < kColdShown)
        now |= 1u << kNowCold;
    for (u32 d = 0; d < p.mem.device_count(); ++d) {
        now |= !p.memory_state[d].on ? 1u << kNowFading : 0;
        now |= p.memory_tripped[d] ? 1u << kNowHeat : 0;
    }
    const PowerSettings& ps = p.power();
    const i32 v = p.env.voltage_mv, lowest = ps.p.threshold < ps.e.threshold ? ps.p.threshold : ps.e.threshold;
    Dropout drop;
    const u32 keep = own_dropout(w, p, &drop);
    if (keep == 0 || v < lowest)
        now |= 1u << kNowBlackout;
    else if (keep < kCapacityOne || v < ps.p.threshold)
        now |= 1u << kNowLow;
    for (u32 k = 0; k < p.core_count(); ++k) {
        if (p.core_tripped[k])
            now |= 1u << kNowHeat;
        else if (!p.cores[k].working)
            now |= 1u << kNowLow;
    }
    if (p.disk_tripped || p.network_tripped || p.cooler_tripped)
        now |= 1u << kNowHeat;
    if (!p.disk_state.on && !p.disk_tripped)
        now |= 1u << kNowLow;
    if (!p.network_state.on || p.network_tripped)
        now |= 1u << kNowNetOff;
    return now;
}

// The swells now, a bit a place (gate 4's sources: one swelling at a place).
void swells_now(World& w, Array<u8>* out) {
    out->resize_unfilled(w.place_count());
    memset(out->data(), 0, out->size());
    if (!w.powered())
        return;
    const Sources& s = w.sources();
    for (u32 k = 0; k < s.count(); ++k)
        if (s.state(k).swell > kCapacityOne && s.source(k).place < out->size())
            (*out)[s.source(k).place] = 1;
}

// The worst of a set of NowKinds (kNowKinds: none).
u32 worst_now(u32 now) {
    for (u32 k = 0; k < kNowKinds; ++k)
        if (now >> k & 1)
            return k;
    return kNowKinds;
}

// Everything at every place now, for a frame: worked out once (the places'
// panel, the map and the inspector all mark it).
Array<u32> g_now;

void work_out_now(World& w) {
    static Array<u8> swelling;
    swells_now(w, &swelling);
    g_now.resize_unfilled(w.place_count());
    for (u32 i = 0; i < w.place_count(); ++i)
        g_now[i] = now_at(w, i) | (swelling[i] ? 1u << kNowSwell : 0);
}

u32 now_of(u32 i) { return i < g_now.size() ? g_now[i] : 0; }

bool key(const UiInput& in, u32 vk) {
    for (u32 i = 0; i < in.nkeys; ++i)
        if (in.keys[i] == vk)
            return true;
    return false;
}

void panel(Canvas& c, const Rect& r, const char* title) {
    c.fill(r, kPanel);
    Rect head{r.x, r.y, r.w, c.line_h(kFontBold) + c.px(8)};
    c.fill(head, kHead);
    c.text(r.x + c.px(8), r.y + c.px(4), title, kFontBold, kText);
}

i32 body_top(Canvas& c, const Rect& r) { return r.y + c.line_h(kFontBold) + c.px(14); }

void faster(ViewState& st) { st.ticks_per_frame = st.ticks_per_frame < 1024 ? st.ticks_per_frame * 2 : 1024; }

void slower(ViewState& st) { st.ticks_per_frame = st.ticks_per_frame > 1 ? st.ticks_per_frame / 2 : 1; }

// The bar across the top: what's open, at the left, and the world's
// controls as buttons, at the right (build-plan-viewer.md 2, choice 6): run
// or pause, one tick, slower and faster with the speed between them, and,
// with a map, which row of charts shows.
void seed_button(Canvas& c, ButtonRow& row, ViewState& st); // a run's seeds, below

void draw_top(Canvas& c, const Rect& r, ViewState& st, World& w, const UiInput& in) {
    c.fill(r, kHead);
    const i32 bh = button_h(c);
    ButtonRow row{c, in, r.right() - c.px(10), r.y + (r.h - bh) / 2, bh};
    // The settings window, open or shut (step 5).
    if (row.left("settings", st.settings_open))
        st.settings_open = !st.settings_open;
    row.x -= c.px(14);
    if (w.map().placed()) {
        // Gate 4's row, in a world with sources or heat; gate 5's, in a
        // world with any of its strengths on.
        const bool wear_row = st.wear_row && gate5_world(w);
        const bool heat_row = st.heat_row && gate4_world(w) && !wear_row;
        if (gate5_world(w) && row.left("failures", wear_row))
            st.wear_row = true;
        if (gate4_world(w) && row.left("heat", heat_row)) {
            st.heat_row = true;
            st.wear_row = false;
        }
        if (row.left("power", st.power_row && !heat_row && !wear_row)) {
            st.power_row = true;
            st.heat_row = st.wear_row = false;
        }
        if (row.left("traffic", !st.power_row && !heat_row && !wear_row)) {
            st.power_row = false;
            st.heat_row = st.wear_row = false;
        }
        row.words_left("charts", kDim);
        row.x -= c.px(14);
    }
    char buf[256];
    if (st.watching) {
        // The window on a run only watches (build-plan-viewer.md 2, choice
        // 2): the seed's speed, and no buttons to run or pause it.
        if (st.tps > 0)
            snprintf(buf, sizeof buf, "watching a run, %.0f ticks/s", st.tps);
        else
            snprintf(buf, sizeof buf, "watching a run");
        row.words_left(buf, kDim, c.text_w("watching a run, 0000000 ticks/s", kFontSmall));
        row.x -= c.px(8);
        // The seed shown saves its whole world (step 8); lit until it has.
        if (st.can_save && st.seeds && st.seed_shown < st.seed_count) {
            if (row.left("save world", st.seeds[st.seed_shown].saving))
                st.save_asked = true;
            row.x -= c.px(8);
        }
    } else {
        if (row.left("faster"))
            faster(st);
        if (st.tps > 0)
            snprintf(buf, sizeof buf, "%u tick%s a frame, %.0f ticks/s", st.ticks_per_frame,
                     st.ticks_per_frame == 1 ? "" : "s", st.tps);
        else
            snprintf(buf, sizeof buf, "%u tick%s a frame", st.ticks_per_frame, st.ticks_per_frame == 1 ? "" : "s");
        // A fixed room for the speed, so the buttons beside it stay still.
        row.words_left(buf, kDim, c.text_w("1024 ticks a frame, 0000000 ticks/s", kFontSmall));
        if (row.left("slower"))
            slower(st);
        row.x -= c.px(8);
        if (row.left("one tick")) {
            st.step = true;
            st.paused = true;
        }
        if (row.left("pause", st.paused))
            st.paused = true;
        if (row.left("run", !st.paused))
            st.paused = false;
    }
    seed_button(c, row, st); // a run's seeds
    snprintf(buf, sizeof buf, "Nova 2   %s   seed %llu   tick %llu   %u places", st.title,
             static_cast<unsigned long long>(w.seed()), static_cast<unsigned long long>(w.tick_count()),
             w.place_count());
    c.text(r.x + c.px(10), r.y + c.px(6), buf, kFontBold, kText, kAlignLeft, row.x - r.x - c.px(24));
}

// A run's seeds (build-plan-viewer.md step 4) are chosen from a dropdown in
// the top bar: its button says which seed is shown, how far through it is
// and the best stage it has reached, and opens a list of them all, a row a
// seed. A click on a running seed's row shows that seed and shuts the
// list; a seed that waits or has ended has no world to show, and its row
// does nothing. A click anywhere else under the bar, or Escape, shuts the
// list, and while it is open nothing under it answers the pointer.
char g_seed_label[kMostSeedTabs][48]; // the labels outlast the frame (the tests ask where each row was)
Rect g_seed_button;                   // where the dropdown's button was drawn this frame

void seed_label(const SeedTab& t, char* out, usize size) {
    const unsigned long long seed = static_cast<unsigned long long>(t.seed);
    if (t.state == kTabRunning)
        snprintf(out, size, "seed %llu  %u%%  stage %u", seed, t.permille / 10, t.stage);
    else if (t.state == kTabDone)
        snprintf(out, size, "seed %llu  done  stage %u", seed, t.stage);
    else
        snprintf(out, size, "seed %llu  waiting", seed);
}

u32 seed_rows(const ViewState& st) { return st.seed_count < kMostSeedTabs ? st.seed_count : kMostSeedTabs; }

// The dropdown's button, at the left end of a row of buttons laid leftwards.
void seed_button(Canvas& c, ButtonRow& row, ViewState& st) {
    static char label[48];
    g_seed_button = Rect{};
    if (!st.seeds || !st.seed_count) {
        st.seeds_open = false;
        return;
    }
    seed_label(st.seeds[st.seed_shown < seed_rows(st) ? st.seed_shown : 0], label, sizeof label);
    // Room for the longest it gets, so the bar's other things stay still.
    const i32 w = drop_button_w(c, "seed 00000  100%  stage 5");
    row.x -= w;
    g_seed_button = Rect{row.x, row.y, w, row.h};
    row.x -= c.px(10);
    if (drop_button(c, row.in, g_seed_button, label, st.seeds_open))
        st.seeds_open = !st.seeds_open;
}

// The open dropdown's list, under its button, over whatever is there.
void draw_seed_list(Canvas& c, ViewState& st, const UiInput& in, i32 top_h) {
    const u32 n = seed_rows(st);
    if (!st.seeds_open || !n)
        return;
    const i32 rh = c.line_h(kFontSmall) + c.px(7), pad = c.px(8);
    const i32 col_seed = c.text_w("seed 00000", kFontSmall) + c.px(14), col_state = c.px(64), col_stage = c.px(58),
              col_births = c.text_w("000,000,000 births", kFontSmall);
    i32 w = pad + col_seed + col_state + col_stage + col_births + pad + c.px(12);
    w = w < g_seed_button.w ? g_seed_button.w : w;
    i32 x = g_seed_button.x;
    x = x + w > c.width() - c.px(6) ? c.width() - c.px(6) - w : x;
    const i32 y = g_seed_button.bottom() + c.px(2), room = c.height() - y - c.px(6);
    i32 h = static_cast<i32>(n) * rh + 2;
    h = h > room ? room : h;
    const Rect r{x, y, w, h};
    // A press anywhere else under the bar shuts the list, and goes no further.
    if (in.pressed && !r.contains(in.mx, in.my) && in.my >= top_h) {
        st.seeds_open = false;
        return;
    }
    c.fill(r, kPanel);
    const ListRows rows = scroll_list(c, in, Rect{r.x + 1, r.y + 1, r.w - 2, r.h - 2}, rh, n, st.seeds_scroll);
    c.clip(rows.rows);
    char buf[64];
    for (u32 k = rows.first; k < rows.first + rows.count; ++k) {
        const SeedTab& t = st.seeds[k];
        const Rect at = rows.row(k);
        const bool running = t.state == kTabRunning, hot = running && at.contains(in.mx, in.my);
        seed_label(t, g_seed_label[k], sizeof g_seed_label[k]);
        note(in, at, g_seed_label[k]);
        if (k == st.seed_shown && running)
            c.fill(at, kRowSel);
        else if (hot)
            c.fill(at, kRowHot);
        const u32 ink = running ? kText : (t.state == kTabDone ? kDim : kFaint);
        const i32 ty = at.y + (rh - c.line_h(kFontSmall)) / 2;
        i32 tx = at.x + pad;
        snprintf(buf, sizeof buf, "seed %llu", static_cast<unsigned long long>(t.seed));
        c.text(tx, ty, buf, kFontSmall, ink);
        tx += col_seed;
        if (running)
            snprintf(buf, sizeof buf, "%u%%", t.permille / 10);
        else
            snprintf(buf, sizeof buf, "%s", t.state == kTabDone ? "done" : "waiting");
        c.text(tx, ty, buf, kFontSmall, ink);
        tx += col_state;
        if (t.state != kTabWaiting) {
            snprintf(buf, sizeof buf, "stage %u", t.stage);
            c.text(tx, ty, buf, kFontSmall, ink);
            snprintf(buf, sizeof buf, "%llu births", static_cast<unsigned long long>(t.births));
            c.text(at.right() - pad, ty, buf, kFontSmall, ink, kAlignRight);
        }
        if (hot && in.pressed) {
            st.seed_shown = k;
            st.seeds_open = false;
        }
    }
    c.unclip();
    c.frame(r, kAccent);
}

// What the views under an open dropdown are given: the keys, and no pointer.
UiInput no_pointer(const UiInput& in) {
    UiInput u = in;
    u.mx = u.my = -100000;
    u.down = u.pressed = u.released = false;
    u.wheel = 0;
    return u;
}

// The middle panel's head: with a map, the tabs that choose the map or the
// memory; then, from its right edge leftwards, the buttons its caller adds
// to `row` (a view's colourings); and between them what it shows, in words
// (head_words, once the buttons are in).
struct MiddleHead {
    ButtonRow row;
    i32 words_x, words_y;
};

MiddleHead middle_head(Canvas& c, const Rect& r, ViewState& st, const UiInput& in, bool mapped) {
    c.fill(r, kPanel);
    const Rect head{r.x, r.y, r.w, c.line_h(kFontBold) + c.px(8)};
    c.fill(head, kHead);
    const i32 bh = head.h - c.px(4);
    ButtonRow tabs{c, in, r.x + c.px(4), r.y + c.px(2), bh};
    if (mapped) {
        if (tabs.right("map", st.map))
            st.map = true;
        if (tabs.right("memory", !st.map))
            st.map = false;
        tabs.gap(c.px(6));
    } else {
        tabs.gap(c.px(4));
    }
    return MiddleHead{ButtonRow{c, in, r.right() - c.px(4), r.y + c.px(2), bh}, tabs.x, r.y + c.px(4)};
}

void head_words(Canvas& c, const MiddleHead& h, const char* words) {
    if (h.row.x - h.words_x > c.px(40))
        c.text(h.words_x, h.words_y, words, kFontBold, kText, kAlignLeft, h.row.x - h.words_x - c.px(8));
}

void draw_places(Canvas& c, const Rect& r, ViewState& st, World& w, const UiInput& in) {
    panel(c, r, "Places");
    const u32 n = w.place_count();
    const i32 top = body_top(c, r);
    // The tiles have the room the words under them leave (at least a third
    // of the panel): two lines, the key's heading and its rows, three of
    // totals and two of messages.
    const i32 lh = c.line_h(kFontUi) + c.px(2), sh = c.line_h(kFontSmall) + c.px(2);
    const i32 words = c.px(10) + (6 + (w.net().active() ? 2 : 0)) * lh + static_cast<i32>((kNowKinds + 1) / 2) * sh +
                      c.px(8);
    const i32 room = r.bottom() - top - words;
    const Rect area{r.x + c.px(8), top, r.w - c.px(16), room > r.h / 3 ? room : r.h / 3};
    // The largest square tiles that fit.
    i32 tile = 4;
    for (i32 s = area.w; s >= 4; --s) {
        const i32 cols = area.w / s, rows = area.h / s;
        if (cols > 0 && static_cast<u32>(cols * rows) >= n) {
            tile = s;
            break;
        }
    }
    const i32 cols = area.w / tile;
    u32 most = 1;
    for (u32 i = 0; i < n; ++i)
        most = w.place(i).os.live_programs() > most ? w.place(i).os.live_programs() : most;
    char buf[64];
    for (u32 i = 0; i < n; ++i) {
        const Rect t{area.x + static_cast<i32>(i % cols) * tile, area.y + static_cast<i32>(i / cols) * tile, tile - 2,
                     tile - 2};
        Place& p = w.place(i);
        const u32 progs = p.os.live_programs();
        c.fill(t, shade(progs ? heat(log(1.0 + progs) / log(1.0 + most)) : 0x2A2E38, volt_shade(p)));
        if (p.mem.strict())
            c.fill(Rect{t.x, t.bottom() - c.px(3), t.w, c.px(3)}, kWarn); // a strict OS
        // What is happening there now, the worst of it (a place that draws
        // nothing is back at full voltage, so this is what says it is out).
        const u32 worst = worst_now(now_of(i));
        if (worst < kNowKinds) {
            const i32 m = t.w / 4 > c.px(5) ? t.w / 4 : c.px(5);
            c.fill(Rect{t.right() - m, t.y, m, m}, kNowColor[worst]);
        }
        if (i == st.place)
            c.frame(t, kAccent);
        if (tile >= c.px(26)) {
            snprintf(buf, sizeof buf, "%u", i);
            c.text(t.x + c.px(3), t.y + c.px(2), buf, kFontSmall, kText);
        }
        if (in.pressed && t.contains(in.mx, in.my))
            st.place = i;
    }
    // World totals.
    u64 programs = 0, threads = 0, births = 0, deaths = 0, cut = 0, cold = 0;
    u32 strict = 0;
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        programs += p.os.live_programs();
        threads += p.os.live_threads();
        births += p.births;
        deaths += p.deaths;
        strict += p.mem.strict() ? 1 : 0;
        cut += p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts;
        cold += p.cold_starts;
    }
    i32 y = area.y + (static_cast<i32>((n + cols - 1) / cols)) * tile + c.px(10);
    snprintf(buf, sizeof buf, "%u places, %u strict (marked)", n, strict);
    c.text(r.x + c.px(8), y, buf, kFontUi, kDim);
    y += lh;
    c.text(r.x + c.px(8), y, "dimmer as the voltage falls", kFontUi, kDim);
    y += lh;
    // The key to the corners (and the map's rings), each kind with the
    // places it is happening at now, two to a line.
    c.text(r.x + c.px(8), y, "corner, ring on the map: happening now", kFontUi, kDim, kAlignLeft, r.w - c.px(16));
    y += lh;
    u32 at[kNowKinds] = {};
    for (u32 i = 0; i < n; ++i)
        for (u32 k = 0; k < kNowKinds; ++k)
            at[k] += now_of(i) >> k & 1;
    const i32 half = (r.w - c.px(16)) / 2, sw = c.line_h(kFontSmall) - c.px(4);
    for (u32 k = 0; k < kNowKinds; ++k) {
        const i32 x = r.x + c.px(8) + static_cast<i32>(k % 2) * half;
        c.fill(Rect{x, y + (sh - sw) / 2, sw, sw}, kNowColor[k]);
        snprintf(buf, sizeof buf, "%s %u", kNowNames[k], at[k]);
        c.text(x + sw + c.px(5), y + c.px(1), buf, kFontSmall, at[k] ? kText : kDim, kAlignLeft, half - sw - c.px(8));
        if (k % 2 == 1 || k + 1 == kNowKinds)
            y += sh;
    }
    y += c.px(4);
    snprintf(buf, sizeof buf, "programs %llu, threads %llu", static_cast<unsigned long long>(programs),
             static_cast<unsigned long long>(threads));
    c.text(r.x + c.px(8), y, buf, kFontUi, kText);
    y += lh;
    snprintf(buf, sizeof buf, "births %llu, deaths %llu", static_cast<unsigned long long>(births),
             static_cast<unsigned long long>(deaths));
    c.text(r.x + c.px(8), y, buf, kFontUi, kText);
    y += lh;
    snprintf(buf, sizeof buf, "cut-outs %llu, cold starts %llu", static_cast<unsigned long long>(cut),
             static_cast<unsigned long long>(cold));
    c.text(r.x + c.px(8), y, buf, kFontUi, kText);
    // Gate 3's network, with a map: its messages since built or opened.
    if (w.net().active()) {
        y += lh;
        const NetCounts& nc = w.net().counts();
        snprintf(buf, sizeof buf, "messages sent %llu, delivered %llu", static_cast<unsigned long long>(nc.sent),
                 static_cast<unsigned long long>(nc.delivered));
        c.text(r.x + c.px(8), y, buf, kFontUi, kText, kAlignLeft, r.w - c.px(16));
        y += lh;
        snprintf(buf, sizeof buf, "messages on their way %u", w.net().in_flight());
        c.text(r.x + c.px(8), y, buf, kFontUi, kText, kAlignLeft, r.w - c.px(16));
    }
}

// Words along a line in colours of their own, on to the next line when the
// next will not fit: the inspector's "Now".
struct Flow {
    Canvas& c;
    i32 x0, x, y, right, lh;
    void word(const char* s, u32 colour) {
        const i32 w = c.text_w(s, kFontUi);
        if (x > x0 && x + w > right) {
            x = x0;
            y += lh;
        }
        c.text(x, y, s, kFontUi, colour, kAlignLeft, right - x);
        x += w;
    }
    // One of a list: a comma before all but the first.
    void item(const char* s, u32 colour, bool* first) {
        if (!*first)
            word(", ", kDim);
        *first = false;
        word(s, colour);
    }
};

// What is happening at the place now (now_at), in words, each in its
// kind's colour, from (x, y); the y under it.
i32 draw_now(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p) {
    const u32 now = now_of(p.id());
    Flow f{c, x, x, y, x + mw, lh};
    f.word("Now: ", kDim);
    if (!now) {
        f.word("nothing out of the ordinary", kDim);
        return f.y + lh + c.px(4);
    }
    char s[128];
    bool first = true;
    const unsigned long long ago = cold_ago(w, p.id());
    if (now >> kNowCold & 1) {
        snprintf(s, sizeof s, "a cold start %llu tick%s ago", ago, ago == 1 ? "" : "s");
        f.item(s, kNowColor[kNowCold], &first);
    }
    // The memory devices off, as one: "memory 2 fading" or "4 memories
    // fading", with the share of their bits faded.
    u32 off = 0, which = 0;
    double faded = 0, bits = 0;
    for (u32 d = 0; d < p.mem.device_count(); ++d)
        if (!p.memory_state[d].on) {
            ++off;
            which = d;
            faded += static_cast<double>(p.noise.faded(d));
            bits += static_cast<double>(p.mem.device(d).size) * 8;
        }
    if (off == 1)
        snprintf(s, sizeof s, "memory %u fading, %.1f%% faded", which, 100.0 * faded / bits);
    else if (off)
        snprintf(s, sizeof s, "%u memories fading, %.1f%% faded", off, 100.0 * faded / bits);
    if (off)
        f.item(s, kNowColor[kNowFading], &first);
    Dropout drop;
    const u32 keep = own_dropout(w, p, &drop);
    const unsigned long long until = drop.start + drop.length - 1;
    if (now >> kNowBlackout & 1) {
        if (keep == 0)
            snprintf(s, sizeof s, "a blackout to tick %llu", until);
        else
            snprintf(s, sizeof s, "a blackout: %d mV, under every core's threshold", p.env.voltage_mv);
        f.item(s, kNowColor[kNowBlackout], &first);
    }
    u32 hot = 0, cut = 0;
    for (u32 k = 0; k < p.core_count(); ++k) {
        hot += p.core_tripped[k] ? 1 : 0;
        cut += !p.cores[k].working && !p.core_tripped[k] ? 1 : 0;
    }
    if (now >> kNowHeat & 1) {
        u32 n = 0;
        for (u32 d = 0; d < p.mem.device_count(); ++d)
            n += p.memory_tripped[d] ? 1 : 0;
        // Only the parts tripped: "heat trip: 3 cores, the disk".
        char cores[24] = "", mems[24] = "";
        if (hot)
            snprintf(cores, sizeof cores, ", %u core%s", hot, hot == 1 ? "" : "s");
        if (n)
            snprintf(mems, sizeof mems, ", %u memor%s", n, n == 1 ? "y" : "ies");
        snprintf(s, sizeof s, "heat trip%s%s%s%s%s", cores, mems, p.disk_tripped ? ", the disk" : "",
                 p.network_tripped ? ", the network" : "", p.cooler_tripped ? ", the cooler" : "");
        if (s[9] == ',')
            s[9] = ':'; // the first part's comma
        f.item(s, kNowColor[kNowHeat], &first);
    }
    // Power low: a drop-out, the voltage under the P cores' threshold, or
    // devices cut out by low power and not yet back (their off-time; the
    // voltage may be back already). In a blackout every core is cut out,
    // which goes without saying.
    if (now >> kNowLow & 1) {
        if (keep > 0 && keep < kCapacityOne) {
            snprintf(s, sizeof s, "a drop-out to %.2f to tick %llu", static_cast<double>(keep) / kCapacityOne, until);
            f.item(s, kNowColor[kNowLow], &first);
        } else if (!(now >> kNowBlackout & 1) && p.env.voltage_mv < p.power().p.threshold) {
            snprintf(s, sizeof s, "power low, %d mV", p.env.voltage_mv);
            f.item(s, kNowColor[kNowLow], &first);
        }
        if (cut && !(now >> kNowBlackout & 1)) {
            snprintf(s, sizeof s, "%u of %u cores cut out by low power", cut, p.core_count());
            f.item(s, kNowColor[kNowLow], &first);
        }
        if (!p.disk_state.on && !p.disk_tripped)
            f.item("the disk cut out", kNowColor[kNowLow], &first);
    }
    if (now >> kNowSwell & 1) {
        snprintf(s, sizeof s, "a source here swelling, %d mV", p.env.voltage_mv);
        f.item(s, kNowColor[kNowSwell], &first);
    }
    if (now >> kNowNetOff & 1)
        f.item("the network off", kNowColor[kNowNetOff], &first);
    return f.y + lh + c.px(4);
}

// The place page of the inspector, from (x, y) in lines `mw` wide: the y of
// its last line.
i32 draw_place_page(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, Place& p, bool gate5) {
    char buf[200], state[64];
    y = draw_now(c, x, y, mw, lh, w, p);
    char lim[40];
    thread_limit_text(w.settings(), p.os.settings().thread_limit, lim, sizeof lim); // "" with no limits (9.6)
    snprintf(buf, sizeof buf, "OS: %s, %u-byte pages%s", p.mem.strict() ? "strict" : "lax", p.mem.page_size(), lim);
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    y += lh;
    snprintf(buf, sizeof buf, "programs %u, threads %u, births %llu, deaths %llu", p.os.live_programs(),
             p.os.live_threads(), static_cast<unsigned long long>(p.births), static_cast<unsigned long long>(p.deaths));
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    // Power (gate 2): the last tick's voltage and capacity, and the drop-out
    // it was in (or the next tick is).
    y += lh + c.px(6);
    c.text(x, y, "Power", kFontBold, kDim);
    y += lh;
    Dropout drop;
    const u64 last = p.env.tick ? p.env.tick - 1 : 0;
    if (w.powered()) {
        // Gate 4's: the voltage solved from the sources, what the place
        // draws, and the sources at it (power_view.h).
        y = draw_power_lines(c, x, y, mw, lh, w, p);
    } else {
        snprintf(buf, sizeof buf, "%d mV (the source's %d), capacity %.3f", p.env.voltage_mv, p.supply.source_mv(),
                 static_cast<double>(p.capacity) / kCapacityOne);
        c.text(x, y, buf, kFontUi, p.env.voltage_mv < p.supply.source_mv() / 2 ? kWarn : kText, kAlignLeft, mw);
        y += lh;
        if ((p.env.tick && p.supply.dropout(last, &drop) < kCapacityOne) ||
            p.supply.dropout(p.env.tick, &drop) < kCapacityOne) {
            if (drop.blackout())
                snprintf(buf, sizeof buf, "a blackout, ticks %llu to %llu",
                         static_cast<unsigned long long>(drop.start),
                         static_cast<unsigned long long>(drop.start + drop.length - 1));
            else
                snprintf(buf, sizeof buf, "a drop-out to %.2f, ticks %llu to %llu",
                         static_cast<double>(drop.keep) / kCapacityOne, static_cast<unsigned long long>(drop.start),
                         static_cast<unsigned long long>(drop.start + drop.length - 1));
            c.text(x, y, buf, kFontUi, kWarn, kAlignLeft, mw);
        } else {
            c.text(x, y, "no drop-out", kFontUi, kDim, kAlignLeft, mw);
        }
        y += lh;
    }
    snprintf(buf, sizeof buf, "cut-outs: P %llu, E %llu, memory %llu, disk %llu; cold starts %llu",
             static_cast<unsigned long long>(p.cutouts[0]), static_cast<unsigned long long>(p.cutouts[1]),
             static_cast<unsigned long long>(p.memory_cutouts), static_cast<unsigned long long>(p.disk_cutouts),
             static_cast<unsigned long long>(p.cold_starts));
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    y += lh + c.px(6);
    c.text(x, y, "Cores", kFontBold, kDim);
    y += lh;
    for (u32 k = 0; k < p.core_count(); ++k) {
        const Core& core = p.cores[k];
        const IdleRun& run = p.region.state(k);
        char what[96];
        const CorePower& kp = core.kind == DeviceKind::e_core ? p.power().e : p.power().p;
        if (!core.working) {
            power_state(what, sizeof what, p, false, core.off_at, kp.threshold);
        } else if (core.thread) {
            const Thread& th = p.os.thread(core.thread - 1);
            snprintf(what, sizeof what, "thread %u, IP %u", th.id, core.x.r.ip);
        } else if (core.idle_run) {
            snprintf(what, sizeof what, "idle run from +%u", run.started_at);
        } else if (p.os.live_threads() > 0) {
            snprintf(what, sizeof what, "between threads"); // a turn just ended; the next starts next quantum
        } else if (run.phase == RunPhase::halted) {
            snprintf(what, sizeof what, "halted");
        } else {
            snprintf(what, sizeof what, "idle");
        }
        // Its speed this tick, at the voltage (0 while cut out; 7.5).
        snprintf(buf, sizeof buf, "%u  %s %5u c/t  %s", k, core.kind == DeviceKind::e_core ? "E" : "P", core.speed,
                 what);
        c.text(x, y, buf, kFontMono, !core.working ? kCutOut : (core.thread ? kText : kDim), kAlignLeft, mw);
        y += lh;
    }
    y += c.px(6);
    c.text(x, y, "Memory", kFontBold, kDim);
    y += lh;
    for (u32 d = 0; d < p.mem.device_count(); ++d) {
        const MemDevice& dv = p.mem.device(d);
        snprintf(buf, sizeof buf, "%u  %3u KB  %2u cyc  %u mV  %3u KB free%s", d, dv.size / 1024, dv.latency16 / 16,
                 dv.threshold, p.mem.device_free(d) / 1024, d == p.mem.region_device() ? "  region" : "");
        c.text(x, y, buf, kFontMono, kText, kAlignLeft, mw);
        y += lh;
        // A device that's cut out, and how much of it has faded (9.4).
        if (!p.memory_state[d].on) {
            power_state(state, sizeof state, p, false, p.memory_state[d].off_at, static_cast<i32>(dv.threshold));
            snprintf(buf, sizeof buf, "   %s, %.1f%% faded", state,
                     100.0 * static_cast<double>(p.noise.faded(d)) / (static_cast<double>(dv.size) * 8));
            c.text(x, y, buf, kFontMono, kCutOut, kAlignLeft, mw);
            y += lh;
        }
    }
    power_state(state, sizeof state, p, p.disk_state.on, p.disk_state.off_at, p.power().disk_threshold);
    snprintf(buf, sizeof buf, "disk  %u KB, raw, %s", p.os.disk_size() / 1024, state);
    c.text(x, y, buf, kFontMono, p.disk_state.on ? kText : kCutOut, kAlignLeft, mw);
    y += lh;
    power_state(state, sizeof state, p, p.network_state.on, p.network_state.off_at, p.power().network_threshold);
    snprintf(buf, sizeof buf, "network  %s", state);
    c.text(x, y, buf, kFontMono, p.network_state.on ? kText : kCutOut, kAlignLeft, mw);
    y += lh + c.px(6);
    // Gate 4's, with heat on: the place's temperature and cooling, and each
    // device's temperature and whether it is tripped (power_view.h).
    if (w.heated())
        y = draw_heat_lines(c, x, y, mw, lh, w, p) + c.px(6);
    // Gate 5's: the visits, and each device's wear (wear_view.h).
    if (gate5)
        y = draw_wear_lines(c, x, y, mw, lh, w, p) + c.px(6);
    // Gate 3's, with a map: the place's queues on its links, its comms ports'
    // inboxes, and the messages dropped here since the world was opened.
    Network& net = w.net();
    if (net.active()) {
        c.text(x, y, "Network", kFontBold, kDim);
        y += lh;
        const Map& m = w.map();
        const u32 i = p.id();
        u64 queued = 0;
        u32 bridges = 0, full_q = 0, full_cap = 1;
        for (u32 j = 0; j < m.degree(i); ++j) {
            const u32 k = m.link_at(i, j), q = net.queued(k, i), cap = net.capacity(k);
            queued += q;
            bridges += m.link(k).bridge ? 1 : 0;
            if (static_cast<u64>(q) * full_cap > static_cast<u64>(full_q) * cap || j == 0) {
                full_q = q;
                full_cap = cap ? cap : 1;
            }
        }
        snprintf(buf, sizeof buf, "%u links (%u bridges), %llu bytes queued, the fullest %u of %u", m.degree(i),
                 bridges, static_cast<unsigned long long>(queued), full_q, m.degree(i) ? full_cap : 0);
        c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
        y += lh;
        u64 inbox = 0;
        u32 most = 0, most_port = 0, busy = 0;
        for (u32 port = 0; port < p.os.settings().ports; ++port) {
            const u32 b = p.os.inbox_bytes(port);
            inbox += b;
            busy += b ? 1 : 0;
            if (b > most) {
                most = b;
                most_port = port;
            }
        }
        if (busy)
            snprintf(buf, sizeof buf, "inboxes: %llu bytes on %u port%s, the most %u on port %u",
                     static_cast<unsigned long long>(inbox), busy, busy == 1 ? "" : "s", most, most_port);
        else
            snprintf(buf, sizeof buf, "inboxes empty");
        c.text(x, y, buf, kFontUi, busy ? kText : kDim, kAlignLeft, mw);
        y += lh;
        snprintf(buf, sizeof buf, "dropped here since opened: %llu",
                 static_cast<unsigned long long>(net.dropped_at(i)));
        c.text(x, y, buf, kFontUi, net.dropped_at(i) ? kWarn : kDim, kAlignLeft, mw);
        y += lh + c.px(6);
    }
    c.text(x, y, "Reset region", kFontBold, kDim);
    y += lh;
    const Compressibility cz =
        estimate_compressibility(p.mem.bytes() + p.mem.region_base(), p.mem.region_size());
    snprintf(buf, sizeof buf, "%u bytes, compressibility %.3f", p.mem.region_size(), cz.ratio);
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    y += lh;
    snprintf(buf, sizeof buf, "idle runs %llu: %llu faulted, %llu capped",
             static_cast<unsigned long long>(p.region.started), static_cast<unsigned long long>(p.region.faulted),
             static_cast<unsigned long long>(p.region.capped));
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    y += lh;
    snprintf(buf, sizeof buf, "distinct paths %llu of %llu", static_cast<unsigned long long>(p.region.paths_new),
             static_cast<unsigned long long>(p.region.paths_ended));
    c.text(x, y, buf, kFontUi, kText, kAlignLeft, mw);
    return y;
}

void draw_inspector(Canvas& c, const Rect& r, ViewState& st, World& w, Place& p, const Observer* obs,
                    const UiInput& in) {
    char buf[200];
    snprintf(buf, sizeof buf, "Place %u", p.id());
    panel(c, r, buf);
    // Its pages, as tabs at the head's right: the place itself, or its
    // programs (build-plan-viewer.md step 2).
    ButtonRow tabs{c, in, r.right() - c.px(4), r.y + c.px(2), c.line_h(kFontBold) + c.px(4)};
    // Gate 5's events (wear_view.h), in a world with its strengths.
    const bool gate5 = gate5_world(w);
    if (!gate5 && st.inspect == Inspect::events)
        st.inspect = Inspect::place;
    if (gate5 && tabs.left("events", st.inspect == Inspect::events))
        st.inspect = Inspect::events;
    if (tabs.left("figures", st.inspect == Inspect::figures))
        st.inspect = Inspect::figures;
    if (tabs.left("programs", st.inspect == Inspect::programs))
        st.inspect = Inspect::programs;
    if (tabs.left("place", st.inspect == Inspect::place))
        st.inspect = Inspect::place;
    i32 y = body_top(c, r);
    if (st.inspect == Inspect::programs) {
        draw_programs(c, Rect{r.x + c.px(8), y, r.w - c.px(16), r.bottom() - y - c.px(8)}, st, w, p, obs, in);
        return;
    }
    if (st.inspect == Inspect::figures) {
        draw_figures(c, Rect{r.x + c.px(8), y, r.w - c.px(16), r.bottom() - y - c.px(8)}, st, w, p, obs, in);
        return;
    }
    if (st.inspect == Inspect::events) {
        draw_events(c, Rect{r.x + c.px(8), y, r.w - c.px(16), r.bottom() - y - c.px(8)}, w, obs, in, st.events,
                    &st.place);
        return;
    }
    // The page, drawn once with nothing shown to measure it: drawn as it
    // is when it fits, and scrolled by the wheel and a bar (widgets.h) when
    // it runs past the panel's foot (Damian, 2026-11-21).
    const i32 x = r.x + c.px(8), lh = c.line_h(kFontUi) + c.px(2), mw = r.w - c.px(16);
    c.clip(Rect{r.x, y, 0, 0});
    const i32 end = draw_place_page(c, x, y, mw, lh, w, p, gate5) + lh;
    c.unclip();
    if (end <= r.bottom()) {
        st.place_scroll = Scroll{};
        c.clip(r);
        draw_place_page(c, x, y, mw, lh, w, p, gate5);
        c.unclip();
        return;
    }
    const Rect body{r.x + c.px(2), y, r.w - c.px(6), r.bottom() - y - c.px(1)};
    const ListRows lr = scroll_list(c, in, body, lh, static_cast<u32>((end - y + lh - 1) / lh), st.place_scroll);
    c.clip(lr.rows);
    draw_place_page(c, x, y - static_cast<i32>(lr.first) * lh, lr.rows.right() - x - c.px(4), lh, w, p, gate5);
    c.unclip();
}

// The colours of the holders met so far in a frame's memory map, by holder:
// a program holds many pages, and its colour (by lineage, a look-up in the
// observer's records) is worked out once a frame. `frame` says which frame
// each was worked out in.
struct HolderColours {
    Array<u32> colour, frame;
    u32 now = 0;
};

// The colour of one page.
u32 page_colour(Place& p, const Observer* obs, MemColour mode, u32 page, HolderColours& seen) {
    const Memory& m = p.mem;
    const Page& pg = m.page(page);
    if (pg.state == PageState::free)
        return kPageFree;
    if (pg.state == PageState::reserved)
        return kPageReserved;
    if (pg.state == PageState::region)
        return kPageRegion;
    if (mode == MemColour::permission)
        return pg.perm == Perm::data ? kPermData : (pg.perm == Perm::exec ? kPermExec : kPermBoth);
    if (mode == MemColour::size_class) {
        if (pg.state == PageState::span)
            return 0xE0E0E0;
        static u32 by_class[256];
        static bool made[256];
        if (!made[pg.cls & 255]) {
            by_class[pg.cls & 255] = hsv(static_cast<float>(pg.cls * 37 % 360), 0.5f, 0.8f);
            made[pg.cls & 255] = true;
        }
        return by_class[pg.cls & 255];
    }
    // Owner and lineage: the holder of the page's first allocation.
    u32 id = pg.span;
    if (pg.state == PageState::slab) {
        const u32 cs = m.class_size(pg.cls);
        for (u32 s = 0; s < pg.slots && !id; ++s)
            id = m.alloc_at(page * m.page_size() + s * cs);
    }
    if (!id)
        return kPageFree;
    const u32 holder = m.record(id).holder;
    if (is_run_holder(holder))
        return kRunHeld; // held by an idle run, mid-birth
    const bool keep = holder < seen.frame.size();
    if (keep && seen.frame[holder] == seen.now)
        return seen.colour[holder];
    const Program& prog = p.os.program(holder);
    const ProgramRec* r = mode == MemColour::lineage && obs ? obs->program(prog.uid) : nullptr;
    const u32 col = colour_of(r ? r->origin : prog.uid);
    if (keep) {
        seen.frame[holder] = seen.now;
        seen.colour[holder] = col;
    }
    return col;
}

void draw_memory(Canvas& c, const Rect& r, ViewState& st, Place& p, const Observer* obs, const UiInput& in,
                 bool mapped) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s%u pages of %u bytes", mapped ? "" : "Memory: ", p.mem.page_count(),
             p.mem.page_size());
    MiddleHead head = middle_head(c, r, st, in, mapped);
    for (u32 k = static_cast<u32>(MemColour::count); k-- > 0;)
        if (head.row.left(kColourNames[k], static_cast<u32>(st.colour) == k))
            st.colour = static_cast<MemColour>(k);
    head_words(c, head, buf);
    const i32 top = body_top(c, r);
    // Gate 5's: the disk's blocks by wear, in a strip at the foot.
    const i32 strip = disk_strip_h(c, p, r.w - c.px(16));
    const Rect area{r.x + c.px(8), top, r.w - c.px(16), r.bottom() - top - c.px(8) - (strip ? strip + c.px(8) : 0)};
    if (strip)
        draw_disk_strip(c, Rect{area.x, area.bottom() + c.px(8), area.w, strip}, p);
    const u32 n = p.mem.page_count();
    i32 cell = 2;
    for (i32 s = 32; s >= 2; --s) {
        const i32 cols = area.w / s, rows = area.h / s;
        if (cols > 0 && static_cast<u32>(cols) * rows >= n) {
            cell = s;
            break;
        }
    }
    const i32 cols = area.w / cell;
    const i32 gap = cell >= 5 ? 1 : 0;
    st.mem_x = area.x;
    st.mem_y = area.y;
    st.mem_cols = cols;
    st.mem_cell = cell;
    // The pages the program picked (the inspector's programs page) holds
    // something in are framed in white.
    static Array<u8> held;
    picked_pages(p, obs, st, &held);
    // The pages are one picture, a pixel a page, drawn `cell` times its
    // size: a place of 65,536 pages took 30 ms a frame as a rectangle a
    // page, with the watched seed held all the while (build-plan-viewer.md
    // step 11). The gaps between cells are then lines in the panel's colour.
    static Array<u32> pixels;
    const i32 rows = static_cast<i32>((n + static_cast<u32>(cols) - 1) / static_cast<u32>(cols));
    pixels.resize_unfilled(static_cast<usize>(cols) * rows);
    static HolderColours seen;
    if (seen.frame.size() < p.mem.holder_capacity()) {
        seen.frame.resize(p.mem.holder_capacity());
        seen.colour.resize(p.mem.holder_capacity());
    }
    if (++seen.now == 0) { // round again after 2^32 frames: none is from this one
        memset(seen.frame.data(), 0, seen.frame.size() * sizeof(u32));
        seen.now = 1;
    }
    for (u32 pg = 0; pg < n; ++pg)
        pixels[pg] = page_colour(p, obs, st.colour, pg, seen);
    for (usize i = n; i < pixels.size(); ++i)
        pixels[i] = kPanel;
    c.image(area.x, area.y, pixels.data(), cols, rows, cell);
    if (gap) {
        for (i32 k = 0; k < cols; ++k)
            c.fill(Rect{area.x + k * cell + cell - 1, area.y, 1, rows * cell}, kPanel);
        for (i32 k = 0; k < rows; ++k)
            c.fill(Rect{area.x, area.y + k * cell + cell - 1, cols * cell, 1}, kPanel);
    }
    for (u32 pg = 0; pg < n; ++pg) {
        const bool picked = pg < held.size() && held[pg];
        if (!picked && (cell < 4 || p.mem.page(pg).state != PageState::region))
            continue;
        const Rect t{area.x + static_cast<i32>(pg % cols) * cell, area.y + static_cast<i32>(pg / cols) * cell,
                     cell - gap, cell - gap};
        c.frame(t, picked ? kText : kAccent);
    }
    // Where each memory device starts.
    for (u32 d = 1; d < p.mem.device_count(); ++d) {
        const u32 first = p.mem.device(d).first_page;
        const i32 x = area.x + static_cast<i32>(first % cols) * cell;
        const i32 y = area.y + static_cast<i32>(first / cols) * cell;
        c.fill(Rect{x, y, c.px(2) > 1 ? c.px(2) : 2, cell}, kText);
    }
    (void)in;
}

const char* const kMapShort[] = {"population", "lineage", "network", "queues"}; // the map's colourings, as buttons

double within(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Zooms the map by `by` about the pixel (px, py) of its area: the point of
// the map under that pixel stays under it. Zoom 1 is the whole map; 32 the
// closest.
void zoom_map(ViewState& st, double s0, const Rect& area, i32 px, i32 py, double by) {
    const double was = s0 * st.map_zoom;
    double z = st.map_zoom * by;
    z = z < 1.02 ? 1 : (z > 32 ? 32 : z);
    const double now = s0 * z;
    const double dx = px - (area.x + area.w / 2), dy = py - (area.y + area.h / 2);
    st.map_cx += dx / was - dx / now;
    st.map_cy += dy / was - dy / now;
    st.map_zoom = z;
}

// A place's largest lineage now (the observer's), and its share of the
// place's programs it knows. False: none, or no observer.
bool main_lineage(const Place& p, const Observer* obs, u64* origin, double* share) {
    if (!obs || !p.os.live_programs())
        return false;
    const u32 kMost = 32; // lineages counted at a place; any beyond are left out
    u64 seen[kMost];
    u32 count[kMost], k = 0, total = 0;
    for (u32 i = 0, met = 0; i < p.os.program_capacity() && met < p.os.live_programs(); ++i) {
        const Program& g = p.os.program(kFirstProgramHolder + i);
        if (!g.alive)
            continue;
        ++met;
        const ProgramRec* r = obs->program(g.uid);
        if (!r)
            continue;
        ++total;
        u32 j = 0;
        while (j < k && seen[j] != r->origin)
            ++j;
        if (j == k) {
            if (k == kMost)
                continue;
            seen[k] = r->origin;
            count[k++] = 0;
        }
        ++count[j];
    }
    if (!k)
        return false;
    u32 best = 0;
    for (u32 j = 1; j < k; ++j)
        best = count[j] > count[best] ? j : best;
    *origin = seen[best];
    *share = static_cast<double>(count[best]) / total;
    return true;
}

// The frames drawn, by every view (draw_viewer counts them), and the views
// that have drawn one (each ViewState is given a number at its first).
u64 g_frames = 0;
u32 g_views = 0;

// The map's colouring by lineage: each place's largest lineage, kept
// between frames and worked out again a few places a frame, in turn, some
// 20,000 programs' records looked up a frame. Every place, every frame, was
// a look-up for each of a crowded world's programs (250,000 of them in a
// run of 200 places of 4 MB): build-plan-viewer.md step 12. A view's first
// such frame, or one after a frame without it, works out every place.
struct MainLineage {
    u64 origin;
    double share;
    bool any;
};

void main_lineages(const ViewState& st, World& w, const Observer* obs, const MainLineage** out) {
    static Array<MainLineage> mains;
    static u32 view = 0, next = 0;
    static const World* world = nullptr;
    static const Observer* seen_by = nullptr;
    static u64 seed = 0, frame = 0;
    const u32 n = w.place_count();
    const bool all = view != st.id || world != &w || seed != w.seed() || seen_by != obs || mains.size() != n ||
                     frame + 1 != g_frames;
    if (all) {
        mains.resize(n);
        next = 0;
    }
    const u64 kBudget = 20000;
    u64 spent = 0;
    for (u32 k = 0; k < n && (all || spent < kBudget); ++k) {
        const u32 i = all ? k : next;
        MainLineage& m = mains[i];
        m.any = main_lineage(w.place(i), obs, &m.origin, &m.share);
        spent += w.place(i).os.live_programs() + 1;
        next = i + 1 < n ? i + 1 : 0;
    }
    view = st.id;
    world = &w;
    seen_by = obs;
    seed = w.seed();
    frame = g_frames;
    *out = mains.data();
}

// Where the map's box [0, width] x [0, 1] is drawn: its top left, and pixels
// to a unit.
struct MapView {
    i32 ox, oy;
    double s;
    i32 x(const Map& m, u32 i) const { return ox + static_cast<i32>(m.x(i) * s + 0.5); }
    i32 y(const Map& m, u32 i) const { return oy + static_cast<i32>(m.y(i) * s + 0.5); }
};

// The shading of a sun from 0 (midnight) to 1 (noon): a night blue, as
// opaque as it is dark, and round noon a faint warmth; the colour already
// multiplied by its opacity (Canvas::overlay).
u32 sun_shade(double sun) {
    const double dark = 0.55 * (1 - sun), light = 0.12 * sun * sun * sun * sun;
    const double a = dark + light, r = dark * 0x0B + light * 0xFF, g = dark * 0x12 + light * 0xE9,
                 b = dark * 0x30 + light * 0xA8;
    return static_cast<u32>(a * 255 + 0.5) << 24 | static_cast<u32>(r + 0.5) << 16 | static_cast<u32>(g + 0.5) << 8 |
           static_cast<u32>(b + 0.5);
}

// The time of day over the map (Damian, 2026-11-20: "a highlighted/shadowed
// gradient wave going across the map"): darker where it is night, faintly
// warm round noon. With gate 4's sources the day sweeps the map from east
// to west (sources.h), so the sun goes by a point's position alone, and the
// night crosses the map as a wave. Before them each place keeps a day of
// its own, at a phase drawn for it (supply.h), so each land cell takes its
// place's: a patchwork, each place's land on its own clock, as dark as the
// day leaves its source (the night's share at midnight); water belongs to
// no place there and is left as it is. What it says, for the legend; null
// if there is no day to show (a world whose supply has no night, or a
// grid's or a ring's, with no land, before the sources).
const char* night_shade(Canvas& c, World& w, const Map& m, const MapView& v) {
    static Array<u32> px, by_place;
    const u32 rw = m.raster_w(), rh = m.raster_h(), n = w.place_count();
    const u64 tick = w.tick_count();
    const double kPi = 3.14159265358979323846;
    if (w.powered()) {
        const SourceSettings& s = w.sources().settings();
        if (!s.day)
            return nullptr;
        // A row of cells across the map: the day goes by x alone.
        const double wide = m.width() > 0 ? m.width() : 1;
        const u32 cols = rw && rh ? rw : 64;
        const double units = rw && rh ? static_cast<double>(rw) / rh : wide;
        const double now = static_cast<double>(tick % s.day) / static_cast<double>(s.day);
        px.resize_unfilled(cols);
        for (u32 k = 0; k < cols; ++k) {
            double f = (k + 0.5) / cols * units / wide * s.day_span;
            f -= floor(f);
            double at = now + f; // a share of the day past midnight
            at -= floor(at);
            const double sn = sin(kPi * at);
            px[k] = sun_shade(sn * sn);
        }
        c.overlay(Rect{v.ox, v.oy, static_cast<i32>(units * v.s + 0.5), static_cast<i32>(v.s + 0.5)}, px.data(),
                  static_cast<i32>(cols), 1);
        return "shaded by the time of day, night sweeping east to west";
    }
    if (!rw || !rh)
        return nullptr;
    by_place.resize_unfilled(n);
    bool night = false;
    for (u32 i = 0; i < n; ++i) {
        const u32 d = w.place(i).supply.day(tick);
        by_place[i] = sun_shade(static_cast<double>(d) / kCapacityOne);
        night = night || d != kCapacityOne;
    }
    if (!night)
        return nullptr; // every place at full sun: supply.night is 1
    px.resize_unfilled(static_cast<usize>(rw) * rh);
    for (u32 cy = 0; cy < rh; ++cy)
        for (u32 cx = 0; cx < rw; ++cx) {
            const u32 i = m.cell_place(cx, cy);
            px[static_cast<usize>(cy) * rw + cx] = i < n ? by_place[i] : 0;
        }
    c.overlay(Rect{v.ox, v.oy, static_cast<i32>(static_cast<double>(rw) / rh * v.s + 0.5),
                   static_cast<i32>(v.s + 0.5)},
              px.data(), static_cast<i32>(rw), static_cast<i32>(rh));
    return "land shaded by its place's own time of day";
}

// The map (13.4; build-plan-gate3.md step 6): the land and water, the data
// links (bridges lighter), and the places at their positions, coloured by
// st.map_colour; the inspected place framed, its links marked. A click picks
// the nearest place.
void draw_map(Canvas& c, const Rect& r, ViewState& st, World& w, const Observer* obs, const UiInput& in) {
    const Map& m = w.map();
    Network& net = w.net();
    const u32 n = w.place_count();
    char buf[200];
    snprintf(buf, sizeof buf, "%u places, %u links, %u bridges", n, m.link_count(), m.bridges());
    MiddleHead head = middle_head(c, r, st, in, true);
    // Gate 4's colourings take the map's own's place while one is chosen.
    const bool gate4 = gate4_world(w), gate5 = gate5_world(w);
    if (!gate4)
        st.power_colour = PowerColour::none;
    if (!gate5)
        st.wear_colour = WearColour::none;
    for (u32 k = static_cast<u32>(MapColour::count); k-- > 0;)
        if (head.row.left(kMapShort[k], static_cast<u32>(st.map_colour) == k && st.power_colour == PowerColour::none &&
                                            st.wear_colour == WearColour::none)) {
            st.map_colour = static_cast<MapColour>(k);
            st.power_colour = PowerColour::none;
            st.wear_colour = WearColour::none;
        }
    head_words(c, head, buf);
    const i32 top = body_top(c, r), foot = c.line_h(kFontSmall) + c.px(6);
    const Rect area{r.x + c.px(8), top, r.w - c.px(16), r.bottom() - top - foot - c.px(8)};
    if (area.w < 8 || area.h < 8)
        return;
    // The zoom's buttons, in the map's top right corner. They are drawn
    // last, over the map, but a press on them isn't one on the map.
    const i32 bh = button_h(c);
    const char* const kWhole = "whole map";
    const Rect b_whole{area.right() - c.px(6) - button_w(c, kWhole), area.y + c.px(6), button_w(c, kWhole), bh};
    const Rect b_in{b_whole.x - c.px(3) - button_w(c, "+"), b_whole.y, button_w(c, "+"), bh};
    const Rect b_out{b_in.x - c.px(3) - button_w(c, "-"), b_whole.y, button_w(c, "-"), bh};
    // Gate 4's buttons, in its top left corner: its colourings, then what
    // is drawn over the map.
    const char* const kOver[2] = {"sources", "grid"};
    Rect b_power[6];
    i32 b_power_n = 0, bx = area.x + c.px(6);
    for (u32 k = 1; gate4 && k < static_cast<u32>(PowerColour::count) + 2; ++k) {
        const u32 colours = static_cast<u32>(PowerColour::count);
        const char* label = k < colours ? power_colour_label(static_cast<PowerColour>(k)) : kOver[k - colours];
        if (k == colours)
            bx += c.px(8);
        b_power[b_power_n++] = Rect{bx, area.y + c.px(6), button_w(c, label), bh};
        bx += button_w(c, label) + c.px(3);
    }
    // Gate 5's, under them (or in their place): its colourings, then what
    // is drawn over the map.
    const char* const kWearOver[2] = {"patches", "current"};
    Rect b_wear[4];
    i32 b_wear_n = 0;
    bx = area.x + c.px(6);
    for (u32 k = 0; gate5 && k < 4; ++k) {
        const char* label = k < 2 ? wear_colour_label(static_cast<WearColour>(k + 1)) : kWearOver[k - 2];
        if (k == 2)
            bx += c.px(8);
        b_wear[b_wear_n++] = Rect{bx, area.y + c.px(6) + (gate4 ? bh + c.px(3) : 0), button_w(c, label), bh};
        bx += button_w(c, label) + c.px(3);
    }
    bool on_buttons =
        b_whole.contains(in.mx, in.my) || b_in.contains(in.mx, in.my) || b_out.contains(in.mx, in.my);
    for (i32 k = 0; k < b_power_n; ++k)
        on_buttons = on_buttons || b_power[k].contains(in.mx, in.my);
    for (i32 k = 0; k < b_wear_n; ++k)
        on_buttons = on_buttons || b_wear[k].contains(in.mx, in.my);
    const bool over = area.contains(in.mx, in.my) && !on_buttons;
    // The whole map as large as fits is zoom 1; the wheel zooms about the
    // pointer, and a drag moves the map under it.
    const double wide = m.width() > 0 ? m.width() : 1;
    double s0 = area.w / wide;
    if (area.h < s0)
        s0 = area.h;
    if (st.map_zoom < 1 || st.map_cx < 0) {
        st.map_zoom = 1;
        st.map_cx = wide / 2;
        st.map_cy = 0.5;
    }
    if (in.wheel != 0 && over)
        zoom_map(st, s0, area, in.mx, in.my, pow(1.25, in.wheel));
    if (in.pressed && over) {
        st.map_held = true;
        st.map_dragged = false;
        st.held_x = in.mx;
        st.held_y = in.my;
    }
    if (st.map_held && in.down) {
        const i32 dx = in.mx - st.held_x, dy = in.my - st.held_y;
        if (st.map_dragged || dx > c.px(3) || dx < -c.px(3) || dy > c.px(3) || dy < -c.px(3)) {
            st.map_dragged = true;
            st.map_cx -= dx / (s0 * st.map_zoom);
            st.map_cy -= dy / (s0 * st.map_zoom);
            st.held_x = in.mx;
            st.held_y = in.my;
        }
    }
    // The centre stays where the map still fills the view (at zoom 1, the
    // middle of the map).
    const double s = s0 * st.map_zoom;
    const double half_w = area.w / (2 * s), half_h = area.h / (2 * s);
    st.map_cx = 2 * half_w >= wide ? wide / 2 : within(st.map_cx, half_w, wide - half_w);
    st.map_cy = 2 * half_h >= 1 ? 0.5 : within(st.map_cy, half_h, 1 - half_h);
    MapView v{0, 0, s};
    const i32 fw = static_cast<i32>(wide * v.s), fh = static_cast<i32>(v.s);
    if (st.map_zoom == 1) { // the box as large as fits, centred
        v.ox = area.x + (area.w - fw) / 2;
        v.oy = area.y + (area.h - fh) / 2;
    } else {
        v.ox = area.x + area.w / 2 - static_cast<i32>(st.map_cx * v.s + 0.5);
        v.oy = area.y + area.h / 2 - static_cast<i32>(st.map_cy * v.s + 0.5);
    }
    // The part of the map's box in view (all of it at zoom 1), in the box's
    // own pixels.
    const i32 px0 = v.ox < area.x ? area.x - v.ox : 0, px1 = v.ox + fw > area.right() ? area.right() - v.ox : fw;
    const i32 py0 = v.oy < area.y ? area.y - v.oy : 0, py1 = v.oy + fh > area.bottom() ? area.bottom() - v.oy : fh;
    const Rect shown{v.ox + px0, v.oy + py0, px1 - px0, py1 - py0};
    c.clip(area);
    // Land and water (a grid or a ring has no raster: it's all land), a
    // picture a pixel a pixel. A raster cell is 1 / raster_h of a unit
    // square. The picture is kept, and worked out again only when the view
    // moves or zooms or the map is another: a pixel at a time it took 2 to 3
    // ms of every frame at 1900 by 1000 (build-plan-viewer.md step 12).
    const u32 rw = m.raster_w(), rh = m.raster_h();
    if (!rw || !rh) {
        c.fill(shown, kLand);
    } else if (shown.w > 0 && shown.h > 0) {
        static Array<u32> land;
        static const Map* land_map = nullptr;
        static u64 land_seed = 0;
        static i32 land_x0 = 0, land_y0 = 0, land_w = 0, land_h = 0;
        static double land_s = 0;
        const bool same = land_map == &m && land_seed == w.seed() && land_x0 == px0 && land_y0 == py0 &&
                          land_w == shown.w && land_h == shown.h && land_s == v.s;
        if (!same) {
            land.resize_unfilled(static_cast<usize>(shown.w) * shown.h);
            for (i32 py = py0; py < py1; ++py) {
                u32 cy = static_cast<u32>((py + 0.5) / v.s * rh);
                cy = cy < rh ? cy : rh - 1;
                u32* row = land.data() + static_cast<usize>(py - py0) * shown.w;
                for (i32 px = px0; px < px1; ++px) {
                    u32 cx = static_cast<u32>((px + 0.5) / v.s * rh);
                    row[px - px0] = m.land(cx < rw ? cx : rw - 1, cy) ? kLand : kWater;
                }
            }
            land_map = &m;
            land_seed = w.seed();
            land_x0 = px0;
            land_y0 = py0;
            land_w = shown.w;
            land_h = shown.h;
            land_s = v.s;
        }
        c.image(shown.x, shown.y, land.data(), shown.w, shown.h, 1, 1, same);
    }
    const char* night = night_shade(c, w, m, v);
    // The links; coloured by queues, one with bytes waiting at either end
    // shows the fuller end's share of its room.
    const MapColour mode = st.map_colour;
    const float thin = c.scale(), thick = 2 * c.scale();
    for (u32 k = 0; k < m.link_count(); ++k) {
        const MapLink& l = m.link(k);
        u32 col = l.bridge ? kBridge : kLink;
        float lw = l.bridge ? 1.5f * thin : thin;
        if (mode == MapColour::queues && net.active()) {
            const u32 qa = net.queued(k, l.a), qb = net.queued(k, l.b), q = qa > qb ? qa : qb;
            if (q) {
                const u32 cap = net.capacity(k);
                col = heat(cap ? static_cast<double>(q) / cap : 1);
                lw = thick;
            }
        }
        const u32 down = gate5 ? link_down(w, k) : 0;
        if (down)
            broken_line(c, v.x(m, l.a), v.y(m, l.a), v.x(m, l.b), v.y(m, l.b), down == 2 ? 0x3C78B0 : kBad,
                        lw > 1.5f * thin ? lw : 1.5f * thin);
        else
            c.line(v.x(m, l.a), v.y(m, l.a), v.x(m, l.b), v.y(m, l.b), col, lw);
    }
    // Gate 4's proximity links, over the data links.
    const MapAt map_at{v.ox, v.oy, v.s};
    if (gate4 && st.show_grid)
        draw_power_links(c, w, map_at, c.scale());
    const u32 sel = st.place < n ? st.place : 0;
    for (u32 j = 0; j < m.degree(sel); ++j) {
        const MapLink& l = m.link(m.link_at(sel, j));
        c.line(v.x(m, l.a), v.y(m, l.a), v.x(m, l.b), v.y(m, l.b), kAccent, thick);
    }
    // The places: squares about four tenths of a link across, within bounds
    // (wider ones when zoomed in).
    const double d_most = st.map_zoom == 1 ? c.px(12) : c.px(28);
    double d0 = (m.mean_length() > 0 ? m.mean_length() : 0.05) * v.s * 0.4;
    d0 = d0 < c.px(3) ? c.px(3) : (d0 > d_most ? d_most : d0);
    const i32 d = static_cast<i32>(d0);
    const i32 ring = c.px(2) > 1 ? c.px(2) : 2; // round a place something is happening at
    const MainLineage* mains = nullptr;
    if (mode == MapColour::lineage)
        main_lineages(st, w, obs, &mains);
    const PowerColour power_mode = st.power_colour;
    PowerScale power_sc;
    if (power_mode != PowerColour::none)
        power_scale(w, &power_sc);
    u32 most = 1;
    u64 most_drops = 1;
    for (u32 i = 0; i < n; ++i) {
        const u32 progs = w.place(i).os.live_programs();
        most = progs > most ? progs : most;
        most_drops = net.dropped_at(i) > most_drops ? net.dropped_at(i) : most_drops;
    }
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        u32 col = kEmptyPlace;
        if (st.wear_colour != WearColour::none) {
            col = wear_colour(w, i, st.wear_colour);
        } else if (power_mode != PowerColour::none) {
            col = power_colour(w, i, power_mode, power_sc);
        } else if (mode == MapColour::population) {
            const u32 progs = p.os.live_programs();
            col = shade(progs ? heat(log(1.0 + progs) / log(1.0 + most)) : kEmptyPlace, volt_shade(p));
        } else if (mode == MapColour::lineage) {
            if (mains[i].any)
                col = shade(colour_of(mains[i].origin), 0.45 + 0.55 * mains[i].share);
        } else if (mode == MapColour::network) {
            col = p.network_state.on ? kGood : kBad;
        } else {
            const u64 drops = net.dropped_at(i);
            if (drops)
                col = heat(log(1.0 + static_cast<double>(drops)) / log(1.0 + static_cast<double>(most_drops)));
        }
        c.fill(Rect{v.x(m, i) - d / 2, v.y(m, i) - d / 2, d, d}, col);
        // What is happening there now, the worst of it: a ring round it in
        // its colour (the places' panel has the key).
        const u32 worst = worst_now(now_of(i));
        if (worst < kNowKinds)
            for (i32 k = 1; k <= ring; ++k)
                c.frame(Rect{v.x(m, i) - d / 2 - k, v.y(m, i) - d / 2 - k, d + 2 * k, d + 2 * k}, kNowColor[worst]);
        if (d >= c.px(16)) { // zoomed in far enough for the place's number, and what is happening, to have room
            char num[48];
            snprintf(num, sizeof num, "%u", i);
            const i32 tx = v.x(m, i) + d / 2 + ring + c.px(4), ty = v.y(m, i) - c.line_h(kFontSmall) / 2;
            c.text(tx, ty, num, kFontSmall, kText);
            if (worst < kNowKinds)
                c.text(tx + c.text_w(num, kFontSmall) + c.px(5), ty, kNowNames[worst], kFontSmall, kNowColor[worst]);
        }
    }
    // Gate 4's: a mark on a place with a cooling device while the places
    // are coloured by cooling, and the sources round their places.
    if (power_mode == PowerColour::cooling)
        draw_coolers(c, w, map_at, d);
    if (gate4 && st.show_sources)
        draw_sources(c, w, map_at, d, c.scale());
    // Gate 5's: the corners marked under its wear colouring, the patches
    // and strikes, and the current.
    if (st.wear_colour == WearColour::wear)
        draw_wear_marks(c, w, map_at, d);
    if (gate5 && st.show_patches)
        draw_patches(c, w, map_at, d, c.scale());
    if (gate5 && st.show_current)
        draw_current(c, w, map_at, area, c.scale());
    const i32 hx = v.x(m, sel) - d / 2 - ring - c.px(2), hy = v.y(m, sel) - d / 2 - ring - c.px(2);
    c.frame(Rect{hx, hy, d + 2 * ring + c.px(4), d + 2 * ring + c.px(4)}, kAccent);
    c.unclip();
    // Gate 4's buttons.
    for (i32 k = 0; k < b_power_n; ++k) {
        const u32 colours = static_cast<u32>(PowerColour::count) - 1;
        const u32 ku = static_cast<u32>(k);
        if (ku < colours) {
            const PowerColour pc = static_cast<PowerColour>(ku + 1);
            if (button(c, in, b_power[k], power_colour_label(pc), st.power_colour == pc)) {
                st.power_colour = st.power_colour == pc ? PowerColour::none : pc;
                st.wear_colour = WearColour::none;
            }
        } else if (ku == colours) {
            if (button(c, in, b_power[k], kOver[0], st.show_sources))
                st.show_sources = !st.show_sources;
        } else if (button(c, in, b_power[k], kOver[1], st.show_grid)) {
            st.show_grid = !st.show_grid;
        }
    }
    // Gate 5's buttons.
    for (i32 k = 0; k < b_wear_n; ++k) {
        if (k < 2) {
            const WearColour wc = static_cast<WearColour>(k + 1);
            if (button(c, in, b_wear[k], wear_colour_label(wc), st.wear_colour == wc)) {
                st.wear_colour = st.wear_colour == wc ? WearColour::none : wc;
                st.power_colour = PowerColour::none;
            }
        } else if (k == 2) {
            if (button(c, in, b_wear[k], kWearOver[0], st.show_patches))
                st.show_patches = !st.show_patches;
        } else if (button(c, in, b_wear[k], kWearOver[1], st.show_current)) {
            st.show_current = !st.show_current;
        }
    }
    // The zoom's buttons.
    if (button(c, in, b_out, "-"))
        zoom_map(st, s0, area, area.x + area.w / 2, area.y + area.h / 2, 1 / 1.5);
    if (button(c, in, b_in, "+"))
        zoom_map(st, s0, area, area.x + area.w / 2, area.y + area.h / 2, 1.5);
    if (button(c, in, b_whole, kWhole, st.map_zoom == 1))
        st.map_zoom = 0; // the whole map again, from the next frame
    // What the colours say.
    const char* legend = "";
    char power_words[320];
    if (st.wear_colour != WearColour::none) {
        wear_legend(w, st.wear_colour, power_words, sizeof power_words);
        legend = power_words;
    } else if (power_mode != PowerColour::none) {
        power_legend(w, power_mode, power_sc, power_words, sizeof power_words);
        legend = power_words;
    } else if (mode == MapColour::population)
        legend = "programs per place (log scale), dimmer as the voltage falls; bridges lighter";
    else if (mode == MapColour::lineage)
        legend = obs ? "each place's largest lineage, dimmer as its share falls; grey: none"
                     : "no observer, so no lineages";
    else if (mode == MapColour::network)
        legend = "network device: green on, red off";
    else
        legend = "links: the fuller end's queue, of its room; places: messages dropped there since opened (log)";
    // The shading's words when there is room for them (the shading speaks
    // for itself in a small window).
    char words[640];
    if (night) {
        snprintf(words, sizeof words, "%s; %s", legend, night);
        if (c.text_w(words, kFontSmall) <= r.w - c.px(16))
            legend = words;
    }
    c.text(r.x + c.px(8), area.bottom() + c.px(4), legend, kFontSmall, kDim, kAlignLeft, r.w - c.px(16));
    // A click (a press let go where it began, not a drag) picks the nearest
    // place.
    if (st.map_held && !in.down) {
        if (in.released && !st.map_dragged && area.contains(in.mx, in.my)) {
            u32 best = sel;
            i64 best_d = -1;
            for (u32 i = 0; i < n; ++i) {
                const i64 dx = v.x(m, i) - in.mx, dy = v.y(m, i) - in.my;
                if (best_d < 0 || dx * dx + dy * dy < best_d) {
                    best_d = dx * dx + dy * dy;
                    best = i;
                }
            }
            st.place = best;
        }
        st.map_held = false;
    }
}

struct Series {
    const char* name;
    u32 colour;
    float values[256];
    u32 n;
};

// A line chart's plotting area and the value at its top, for marks drawn
// over it.
struct Plot {
    Rect area;
    float top;
};

// Lines, one a series, scaled to the largest value (or to `top`, if it's
// larger), each series named in a legend along the top, which wraps when
// it's wider than the chart. A series with no values is a legend entry
// alone, and one in the faint colour is named faintly (the voltage chart's
// thresholds).
Plot chart(Canvas& c, const Rect& r, const char* title, const Series* s, u32 count, float top = 1) {
    panel(c, r, title);
    const i32 head = body_top(c, r), x0 = r.x + c.px(8), lh = c.line_h(kFontSmall);
    float most = top;
    for (u32 k = 0; k < count; ++k)
        for (u32 i = 0; i < s[k].n; ++i)
            most = s[k].values[i] > most ? s[k].values[i] : most;
    char buf[64];
    snprintf(buf, sizeof buf, "%.0f", static_cast<double>(most));
    c.text(r.right() - c.px(10), head - c.px(2), buf, kFontSmall, kDim, kAlignRight);
    // The legend, wrapping short of the top value's label on its first line.
    const i32 right = r.right() - c.px(8);
    i32 lx = x0, ly = head, line_end = right - c.text_w(buf, kFontSmall) - c.px(8);
    for (u32 k = 0; k < count; ++k) {
        const i32 w = c.px(14) + c.text_w(s[k].name, kFontSmall);
        if (lx > x0 && lx + w > line_end) {
            lx = x0;
            ly += lh;
            line_end = right;
        }
        c.fill(Rect{lx, ly + c.px(2), c.px(8), c.px(8)}, s[k].colour);
        c.text(lx + c.px(11), ly - c.px(2), s[k].name, kFontSmall, s[k].colour == kFaint ? kFaint : kDim);
        lx += w + c.px(8);
    }
    const Rect area{x0, ly + lh, r.w - c.px(16), r.bottom() - ly - lh - c.px(8)};
    c.frame(area, kEdge);
    for (u32 k = 0; k < count; ++k) {
        if (s[k].n < 2)
            continue;
        float xy[512];
        for (u32 i = 0; i < s[k].n; ++i) {
            xy[2 * i] = static_cast<float>(area.x) + 1 + static_cast<float>(i) * (area.w - 2) / (s[k].n - 1);
            xy[2 * i + 1] = static_cast<float>(area.bottom()) - 1 - s[k].values[i] / most * (area.h - 2);
        }
        c.polyline(xy, s[k].n, s[k].colour);
    }
    return Plot{area, most};
}

// A level marked across a plot: a threshold on the voltage chart (named in
// its legend).
void mark(Canvas& c, const Plot& pl, float v) {
    if (v <= 0 || v >= pl.top)
        return;
    const i32 y = pl.area.bottom() - 1 - static_cast<i32>(v / pl.top * (pl.area.h - 2) + 0.5f);
    c.line(pl.area.x + 1, y, pl.area.right() - 1, y, kFaint);
}

// Words in a plot with nothing to draw yet.
void empty(Canvas& c, const Plot& pl, const char* why) {
    c.text(pl.area.x + pl.area.w / 2, pl.area.y + pl.area.h / 2 - c.line_h(kFontSmall) / 2, why, kFontSmall, kDim,
           kAlignCenter);
}

const char* const kCauseNames[kDeathCauses] = {"exit",       "fault", "cut-out",       "device",  "memory",
                                               "cold start", "heat",  "out of memory", "watchdog"};

// Turnover per window (build-plan-gate2.md steps 1 and 8): for each window,
// the world's counted windows (places that started it with the members),
// those that turned over, and of those the ones with a cold start in them,
// as bars side by side.
void turnover_chart(Canvas& c, const Rect& r, const Observer* obs) {
    char title[96];
    const u64 len = obs ? obs->settings().turnover.window : 0;
    snprintf(title, sizeof title, "Turnover per window of %llu ticks", static_cast<unsigned long long>(len));
    const u32 kMost = 24;
    u64 start[kMost];
    u32 counted[kMost], turned[kMost], cold[kMost];
    u32 n = 0;
    // The last kMost windows, oldest first. Windows come in tick order, so
    // walk back to the kMost-th distinct start.
    const usize total = obs ? obs->windows.size() : 0;
    usize from = total;
    u32 starts = 0;
    while (from > 0) {
        const u64 s = obs->windows[from - 1].start;
        if (from == total || s != obs->windows[from].start) {
            if (starts == kMost)
                break;
            ++starts;
        }
        --from;
    }
    for (usize i = from; i < total; ++i) {
        const TurnoverWindow& tw = obs->windows[i];
        if (n == 0 || start[n - 1] != tw.start) {
            start[n] = tw.start;
            counted[n] = turned[n] = cold[n] = 0;
            ++n;
        }
        counted[n - 1] += tw.counted ? 1 : 0;
        turned[n - 1] += tw.turned ? 1 : 0;
        cold[n - 1] += tw.turned && tw.cold ? 1 : 0;
    }
    static Series s[3];
    s[0] = Series{"counted", kSeriesColor[0], {}, 0};
    s[1] = Series{"turned over", kSeriesColor[2], {}, 0};
    s[2] = Series{"with a cold start", kBad, {}, 0};
    u32 most = 1;
    for (u32 i = 0; i < n; ++i)
        most = counted[i] > most ? counted[i] : most;
    const Plot pl = chart(c, r, title, s, 3, static_cast<float>(most));
    if (n == 0) {
        empty(c, pl, obs ? "no window yet that began with a program alive" : "no observer");
        return;
    }
    const float slot = static_cast<float>(pl.area.w - 2) / kMost;
    const i32 bw = slot >= 6 ? static_cast<i32>(slot / 4) : 1;
    for (u32 i = 0; i < n; ++i) {
        const i32 x0 = pl.area.x + 1 + static_cast<i32>(slot * (kMost - n + i) + slot / 8);
        const u32 v[3] = {counted[i], turned[i], cold[i]};
        for (u32 k = 0; k < 3; ++k) {
            const i32 h = static_cast<i32>(static_cast<float>(v[k]) / pl.top * (pl.area.h - 2) + 0.5f);
            if (h > 0)
                c.fill(Rect{x0 + static_cast<i32>(k) * bw, pl.area.bottom() - 1 - h, bw, h}, s[k].colour);
        }
    }
}

// A running total's step from one sample to the next (one cleared between
// them starts again from 0).
float step_of(u64 now, u64 was) { return static_cast<float>(now >= was ? now - was : now); }

// Gate 3's row (build-plan-gate3.md step 6), for a world with a map: the
// network's traffic and drops per sample, and the colonists per sample with
// the colonies alive and settled. The colony counts are levels, drawn from
// the second sample so they line up with the steps.
void gate3_charts(Canvas& c, const Rect* cell, const Observer* obs, u32 from, u32 n, Series* s) {
    const u32 dn = n > 1 ? n - 1 : 0;
    s[0] = Series{"sent", kSeriesColor[0], {}, dn};
    s[1] = Series{"delivered", kSeriesColor[2], {}, dn};
    s[2] = Series{"PINGs answered", kSeriesColor[1], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = step_of(now.net_sent, was.net_sent);
        s[1].values[i - 1] = step_of(now.net_delivered, was.net_delivered);
        s[2].values[i - 1] = step_of(now.net_pings, was.net_pings);
    }
    chart(c, cell[0], "Messages per sample", s, 3);
    s[0] = Series{"no room", kWarn, {}, dn};
    s[1] = Series{"stranded", kSeriesColor[3], {}, dn};
    s[2] = Series{"lost at a dark place", kBad, {}, dn};
    s[3] = Series{"cut at a dark device", kCauseColor[3], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = step_of(now.net_full, was.net_full);
        s[1].values[i - 1] = step_of(now.net_stranded, was.net_stranded);
        s[2].values[i - 1] = step_of(now.net_lost, was.net_lost);
        s[3].values[i - 1] = step_of(now.net_cut, was.net_cut);
    }
    chart(c, cell[1], "Messages dropped per sample", s, 4);
    s[0] = Series{"colonists", kSeriesColor[1], {}, dn};
    s[1] = Series{"colonies alive", kSeriesColor[0], {}, dn};
    s[2] = Series{"settled", kGood, {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        s[0].values[i - 1] = step_of(now.colonists, obs->samples[from + i - 1].colonists);
        s[1].values[i - 1] = static_cast<float>(now.colonies_alive);
        s[2].values[i - 1] = static_cast<float>(now.settled);
    }
    chart(c, cell[2], "Colonists per sample; colonies alive and settled", s, 3);
}

// Gate 4's row (build-plan-gate4.md step 9), for a world with sources or
// heat: the power the sources give, the places draw, the links and sources
// lose and that leaves as heat, at each sample; the places' temperatures,
// their mean and the highest; and heat trips and cut-outs per sample.
void gate4_charts(Canvas& c, const Rect* cell, const Observer* obs, u32 from, u32 n, Series* s) {
    const u32 dn = n > 1 ? n - 1 : 0;
    s[0] = Series{"given by the sources", kVoltMean, {}, n};
    s[1] = Series{"drawn by devices", kSeriesColor[0], {}, n};
    s[2] = Series{"lost in links and sources", kSeriesColor[3], {}, n};
    s[3] = Series{"leaving as heat", kCauseColor[6], {}, n};
    for (u32 i = 0; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        s[0].values[i] = now.power_given > 0 ? static_cast<float>(static_cast<double>(now.power_given) / 1e6) : 0;
        s[1].values[i] = static_cast<float>(static_cast<double>(now.power_drawn) / 1e6);
        s[2].values[i] = static_cast<float>(static_cast<double>(now.power_lost) / 1e6);
        s[3].values[i] = now.heat_out > 0 ? static_cast<float>(static_cast<double>(now.heat_out) / 1e6) : 0;
    }
    chart(c, cell[0], "Power in and out (W)", s, 4);
    s[0] = Series{"the hottest place", kBad, {}, n};
    s[1] = Series{"the mean", kSeriesColor[1], {}, n};
    for (u32 i = 0; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        s[0].values[i] = static_cast<float>(static_cast<double>(now.temp_high) / kDegree);
        s[1].values[i] = static_cast<float>(static_cast<double>(now.temp_mean) / kDegree);
    }
    chart(c, cell[1], "Places' temperature (degrees)", s, 2);
    s[0] = Series{"core trips", kCauseColor[6], {}, dn};
    s[1] = Series{"other devices' trips", kSeriesColor[3], {}, dn};
    s[2] = Series{"core cut-outs", kCauseColor[2], {}, dn};
    s[3] = Series{"sources swelling", kNowColor[5], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        u64 other_now = 0, other_was = 0;
        for (u32 k = kHeatMemory; k < kHeatKinds; ++k) {
            other_now += now.trips[k];
            other_was += was.trips[k];
        }
        s[0].values[i - 1] = step_of(now.trips[kHeatP] + now.trips[kHeatE], was.trips[kHeatP] + was.trips[kHeatE]);
        s[1].values[i - 1] = step_of(other_now, other_was);
        s[2].values[i - 1] = step_of(now.cutouts, was.cutouts);
        s[3].values[i - 1] = static_cast<float>(now.sources_swell);
    }
    chart(c, cell[2], "Heat trips and cut-outs per sample", s, 4);
}

// A total summed over places, from one sample to the next: a place built
// again (7.8) starts its own counts afresh, so a fall is no step.
float rise_of(u64 now, u64 was) { return static_cast<float>(now >= was ? now - was : 0); }

// Gate 5's row (build-plan-gate5.md step 13), for a world with any of its
// strengths on: devices failed and replaced, visits and respawns per
// sample; the disasters per sample, the largest cascades in the window
// marked and labelled with their size; and first leads, the run cut into
// eras (design.md 14.1).
void gate5_charts(Canvas& c, const Rect* cell, const Observer* obs, u32 from, u32 n, Series* s) {
    const u32 dn = n > 1 ? n - 1 : 0;
    s[0] = Series{"devices failed", kBad, {}, dn};
    s[1] = Series{"replaced at visits", kGood, {}, dn};
    s[2] = Series{"visits", kSeriesColor[0], {}, dn};
    s[3] = Series{"places built again", kSeriesColor[3], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = rise_of(now.failures, was.failures);
        s[1].values[i - 1] = rise_of(now.replaced, was.replaced);
        s[2].values[i - 1] = rise_of(now.visits, was.visits);
        s[3].values[i - 1] = step_of(now.respawns, was.respawns);
    }
    chart(c, cell[0], "Wear: failures, replacements and visits per sample", s, 4);
    s[0] = Series{"links lost", kSeriesColor[1], {}, dn};
    s[1] = Series{"strikes", kVoltMean, {}, dn};
    s[2] = Series{"straits parted", 0x3C78B0, {}, dn};
    s[3] = Series{"deaths: failed device", kCauseColor[3], {}, dn};
    s[4] = Series{"out of memory", kNowColor[5], {}, dn};
    s[5] = Series{"watchdog", kNowColor[6], {}, dn};
    const u32 dev = static_cast<u32>(DeathCause::device_failed), oom = static_cast<u32>(DeathCause::oom),
              dog = static_cast<u32>(DeathCause::watchdog);
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = step_of(now.link_cuts + now.link_worn, was.link_cuts + was.link_worn);
        s[1].values[i - 1] = step_of(now.strikes, was.strikes);
        s[2].values[i - 1] = step_of(now.partings, was.partings);
        s[3].values[i - 1] = step_of(now.deaths[dev], was.deaths[dev]);
        s[4].values[i - 1] = step_of(now.deaths[oom], was.deaths[oom]);
        s[5].values[i - 1] = step_of(now.deaths[dog], was.deaths[dog]);
    }
    const Plot pl = chart(c, cell[1], "Disasters per sample; the largest cascades", s, 6);
    // The three largest cascades of two places or more begun in the window,
    // each at the sample step it began in.
    if (dn >= 2) {
        const u64 t0 = obs->samples[from].tick, t1 = obs->samples[from + n - 1].tick;
        const Array<CascadeRec>& cs = obs->cascade_watch.cascades;
        u32 best[3] = {~0u, ~0u, ~0u};
        for (usize k = cs.size(); k-- > 0;) {
            if (cs[k].start <= t0)
                break; // they begin in order
            if (cs[k].start > t1 || cs[k].size < 2)
                continue;
            for (u32 j = 0; j < 3; ++j)
                if (best[j] == ~0u || cs[k].size > cs[best[j]].size) {
                    for (u32 m = 2; m > j; --m)
                        best[m] = best[m - 1];
                    best[j] = static_cast<u32>(k);
                    break;
                }
        }
        for (u32 j = 0; j < 3 && best[j] != ~0u; ++j) {
            const CascadeRec& cr = cs[best[j]];
            u32 i = 0; // the step (sample from + i to from + i + 1) it began in
            while (i + 1 < dn && obs->samples[from + i + 1].tick < cr.start)
                ++i;
            const i32 x = pl.area.x + 1 + static_cast<i32>(static_cast<i64>(i) * (pl.area.w - 2) / (dn - 1));
            c.line(x, pl.area.y + 1, x, pl.area.bottom() - 1, kFaint);
            char label[32];
            snprintf(label, sizeof label, "%u", cr.size);
            c.text(x + c.px(2), pl.area.y + c.px(1) + static_cast<i32>(j) * c.line_h(kFontSmall), label, kFontSmall,
                   kDim);
        }
    }
    // First leads: the run from its start to the last sample, cut into 16
    // eras, a bar each. Read again only when the observer, its ground
    // record or the last sample changes.
    static const Observer* obs_read = nullptr;
    static usize rows_read = 0;
    static u64 end_read = 0, era_read = 0;
    static u32 per_era[16], eras = 0, led = 0;
    static Array<FirstLead> firsts;
    const u64 end = n ? obs->samples[from + n - 1].tick : 0;
    const usize rows = obs ? obs->ground_windows.size() : 0;
    if (obs != obs_read || rows != rows_read || end != end_read) {
        obs_read = obs;
        rows_read = rows;
        end_read = end;
        era_read = end / 16;
        eras = led = 0;
        for (u32 k = 0; k < 16; ++k)
            per_era[k] = 0;
        firsts.clear();
        if (obs && era_read) {
            EraTally tally;
            tally_eras(obs->ground_windows, 0, end, era_read, StageCriteria{}, &tally, &firsts);
            eras = tally.eras < 16 ? tally.eras : 16;
            for (usize k = 0; k < firsts.size(); ++k)
                if (firsts[k].era != kNoEra && firsts[k].era < 16) {
                    ++per_era[firsts[k].era];
                    ++led;
                }
        }
    }
    char title[96];
    if (era_read)
        snprintf(title, sizeof title, "First leads, by era of %llu ticks", static_cast<unsigned long long>(era_read));
    else
        snprintf(title, sizeof title, "First leads, by era");
    s[0] = Series{"groups leading a place for the first time", kSeriesColor[2], {}, 0};
    u32 most = 1;
    for (u32 k = 0; k < eras; ++k)
        most = per_era[k] > most ? per_era[k] : most;
    const Plot pe = chart(c, cell[2], title, s, 1, static_cast<float>(most));
    if (eras == 0 || led == 0) {
        empty(c, pe, !obs ? "no observer" : !rows ? "no ground record yet" : !eras ? "no whole era yet"
                                                                                    : "no group has led a place yet");
        return;
    }
    const float slot = static_cast<float>(pe.area.w - 2) / static_cast<float>(eras);
    for (u32 k = 0; k < eras; ++k) {
        const i32 h = static_cast<i32>(static_cast<float>(per_era[k]) / pe.top * (pe.area.h - 2) + 0.5f);
        const i32 x0 = pe.area.x + 1 + static_cast<i32>(slot * static_cast<float>(k) + slot / 6);
        const i32 bw = static_cast<i32>(slot * 2 / 3) > 0 ? static_cast<i32>(slot * 2 / 3) : 1;
        if (h > 0)
            c.fill(Rect{x0, pe.area.bottom() - 1 - h, bw, h}, s[0].colour);
    }
}

void draw_charts(Canvas& c, const Rect& r, const Observer* obs, const Place& p, bool gate3, bool gate4,
                 bool gate5) {
    const i32 gap = c.px(6), w3 = (r.w - 2 * gap) / 3, rh = (r.h - gap) / 2;
    Rect cell[6];
    for (u32 k = 0; k < 6; ++k) {
        const i32 col = static_cast<i32>(k % 3), row = static_cast<i32>(k / 3);
        cell[k] = Rect{r.x + col * (w3 + gap), r.y + row * (rh + gap), col == 2 ? r.w - 2 * (w3 + gap) : w3,
                       row == 1 ? r.h - rh - gap : rh};
    }
    static Series s[kDeathCauses + 1];
    const u32 total = obs ? static_cast<u32>(obs->samples.size()) : 0;
    const u32 n = total < 256 ? total : 256;
    const u32 from = total - n;
    const u32 dn = n > 1 ? n - 1 : 0; // per-sample differences
    // Gate 1's: programs and threads; births and deaths, now by cause;
    // distinct paths tried.
    s[0] = Series{"programs", kSeriesColor[0], {}, n};
    s[1] = Series{"threads", kSeriesColor[1], {}, n};
    for (u32 i = 0; i < n; ++i) {
        s[0].values[i] = static_cast<float>(obs->samples[from + i].programs);
        s[1].values[i] = static_cast<float>(obs->samples[from + i].threads);
    }
    chart(c, cell[0], "Programs and threads", s, 2);
    // Births, and deaths by cause (no device fails before wear, gate 4).
    const u32 causes[5] = {static_cast<u32>(DeathCause::exit), static_cast<u32>(DeathCause::fault),
                           static_cast<u32>(DeathCause::cut_out), static_cast<u32>(DeathCause::memory_lost),
                           static_cast<u32>(DeathCause::cold_start)};
    s[0] = Series{"births", kSeriesColor[2], {}, dn};
    for (u32 k = 0; k < 5; ++k)
        s[1 + k] = Series{kCauseNames[causes[k]], kCauseColor[causes[k]], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = static_cast<float>(now.births - was.births);
        for (u32 k = 0; k < 5; ++k)
            s[1 + k].values[i - 1] = static_cast<float>(now.deaths[causes[k]] - was.deaths[causes[k]]);
    }
    chart(c, cell[1], "Births, and deaths by cause, per sample", s, 6);
    s[0] = Series{"distinct paths", kSeriesColor[0], {}, dn};
    for (u32 i = 1; i < n; ++i)
        s[0].values[i - 1] =
            static_cast<float>(obs->samples[from + i].paths_new - obs->samples[from + i - 1].paths_new);
    chart(c, cell[2], "Distinct paths tried per sample", s, 1);
    if (gate5) {
        gate5_charts(c, cell + 3, obs, from, n, s);
        return;
    }
    if (gate4) {
        gate4_charts(c, cell + 3, obs, from, n, s);
        return;
    }
    if (gate3) {
        gate3_charts(c, cell + 3, obs, from, n, s);
        return;
    }

    // Gate 2's (build-plan-gate2.md step 8). The place's voltage: its mean
    // and lowest over each sample, against its devices' thresholds.
    char title[96];
    snprintf(title, sizeof title, "Voltage, place %u (mV)", p.id());
    const bool have =
        obs && p.id() < obs->volt_places && obs->volts.size() >= static_cast<usize>(total) * obs->volt_places;
    const u32 vn = have ? n : 0;
    s[0] = Series{"mean", kVoltMean, {}, vn};
    s[1] = Series{"lowest", kVoltLow, {}, vn};
    for (u32 i = 0; i < vn; ++i) {
        const PlaceVolts v = obs->volt(from + i, p.id());
        s[0].values[i] = static_cast<float>(v.mean);
        s[1].values[i] = static_cast<float>(v.low);
    }
    const i32 region_mv = static_cast<i32>(p.mem.device(p.mem.region_device()).threshold);
    char thresholds[96];
    snprintf(thresholds, sizeof thresholds, "thresholds: P %d, E %d, the region's memory %d", p.power().p.threshold,
             p.power().e.threshold, region_mv);
    s[2] = Series{thresholds, kFaint, {}, 0};
    const Plot pl = chart(c, cell[3], title, s, 3, static_cast<float>(p.supply.source_mv()));
    mark(c, pl, static_cast<float>(p.power().p.threshold));
    mark(c, pl, static_cast<float>(p.power().e.threshold));
    mark(c, pl, static_cast<float>(region_mv));
    // Cold starts, and births from a boot block a program wrote.
    s[0] = Series{"cold starts", kBad, {}, dn};
    s[1] = Series{"births from boots", kSeriesColor[2], {}, dn};
    for (u32 i = 1; i < n; ++i) {
        const Sample& now = obs->samples[from + i];
        const Sample& was = obs->samples[from + i - 1];
        s[0].values[i - 1] = static_cast<float>(now.cold_starts - was.cold_starts);
        s[1].values[i - 1] = static_cast<float>(now.boots - was.boots);
    }
    chart(c, cell[4], "Cold starts and boots per sample", s, 2);
    turnover_chart(c, cell[5], obs);
}

PanelTimes* g_times = nullptr;

// Adds the time since t to a part's sum, and moves t on.
void lap(double* part, double* at) {
    if (!g_times)
        return;
    const double now = clock_seconds();
    *part += now - *at;
    *at = now;
}

} // namespace

void time_panels(PanelTimes* t) { g_times = t; }

void draw_viewer(Canvas& c, ViewState& st, World& w, const Observer* obs, const UiInput& in) {
    ++g_frames;
    if (!st.id)
        st.id = ++g_views;
    // What is happening at each place now: the cold starts since the last
    // frame, then the rest.
    see_cold_starts(st, w);
    work_out_now(w);
    // Input first: the keys the viewer has always had. Every one of them has
    // a button too, answered where it is drawn.
    // (A window on a run only watches: the keys that run, pause and step a
    // world do nothing there.)
    if (!st.watching) {
        if (key(in, kVkSpace))
            st.paused = !st.paused;
        if (key(in, 'N')) {
            st.step = true;
            st.paused = true;
        }
        if (key(in, kVkPlus) || key(in, kVkAdd))
            faster(st);
        if (key(in, kVkMinus) || key(in, kVkSubtract))
            slower(st);
    }
    if (key(in, kVkHome))
        st.map_zoom = 0; // the whole map again
    if (key(in, 'M'))
        st.colour = static_cast<MemColour>((static_cast<u32>(st.colour) + 1) % static_cast<u32>(MemColour::count));
    if (key(in, 0x25) && st.place > 0) // left
        --st.place;
    if (key(in, 0x27) && st.place + 1 < w.place_count()) // right
        ++st.place;
    if (key(in, kVkEscape)) {
        // Shuts the list of seeds if it is open; if not, the settings
        // window; and if neither is, the viewer.
        if (st.seeds_open)
            st.seeds_open = false;
        else if (st.settings_open)
            st.settings_open = false;
        else
            st.quit = true;
    }
    if (st.place >= w.place_count())
        st.place = 0;
    // Gate 3's, with a map: the map or the memory, the map's colours, and
    // the second row of charts.
    const bool mapped = w.map().placed();
    if (mapped && key(in, 'V'))
        st.map = !st.map;
    if (mapped && key(in, 'C'))
        st.map_colour =
            static_cast<MapColour>((static_cast<u32>(st.map_colour) + 1) % static_cast<u32>(MapColour::count));
    // (In a world with gate 4's sources or heat, G goes round the three
    // rows: traffic, power, heat.)
    // (And in one with gate 5's strengths, on to its row, wear, after them.)
    const bool wear_row = st.wear_row && gate5_world(w);
    const bool heat_row = st.heat_row && gate4_world(w) && !wear_row;
    if (mapped && key(in, 'G')) {
        if (wear_row) {
            st.wear_row = st.heat_row = st.power_row = false;
        } else if (heat_row) {
            st.wear_row = gate5_world(w);
            st.heat_row = st.power_row = st.wear_row;
        } else if (st.power_row && gate4_world(w)) {
            st.heat_row = true;
        } else if (st.power_row && gate5_world(w)) {
            st.wear_row = true;
        } else {
            st.power_row = !st.power_row;
        }
    }

    const i32 W = c.width(), H = c.height(), g = c.px(6);
    double at = g_times ? clock_seconds() : 0;
    PanelTimes none;
    PanelTimes& pt = g_times ? *g_times : none;
    c.fill(Rect{0, 0, W, H}, kBg);
    const i32 top_h = c.line_h(kFontBold) + c.px(12);
    draw_top(c, Rect{0, 0, W, top_h}, st, w, in);
    lap(&pt.top, &at);
    // The inspector down the right, its full height (gate 2's devices and
    // states make it long); two rows of charts (gate 2's under gate 1's)
    // under the places and memory, taking 42% of the height between bounds.
    // While the list of a run's seeds is open over them, the views under
    // the bar are not given the pointer.
    const UiInput held = no_pointer(in);
    const UiInput& under = st.seeds_open ? held : in;
    const i32 left_w = c.px(250), right_w = c.px(360), y0 = top_h + g;
    if (st.settings_open) {
        // The settings window covers everything under the bar.
        draw_settings(c, Rect{g, y0, W - 2 * g, H - y0 - g}, st, mapped, under);
    } else {
        i32 chart_h = static_cast<i32>((H - y0) * 0.42);
        chart_h = chart_h < c.px(240) ? c.px(240) : (chart_h > c.px(360) ? c.px(360) : chart_h);
        const i32 mid_h = H - y0 - chart_h - 2 * g, left_all = W - right_w - 3 * g;
        draw_places(c, Rect{g, y0, left_w, mid_h}, st, w, under);
        lap(&pt.places, &at);
        Place& p = w.place(st.place);
        const Rect middle{g + left_w + g, y0, left_all - left_w - g, mid_h};
        if (mapped && st.map)
            draw_map(c, middle, st, w, obs, under);
        else
            draw_memory(c, middle, st, p, obs, under, mapped);
        lap(&pt.middle, &at);
        draw_inspector(c, Rect{W - right_w - g, y0, right_w, H - y0 - g}, st, w, p, obs, under);
        lap(&pt.inspector, &at);
        draw_charts(c, Rect{g, y0 + mid_h + g, left_all, chart_h}, obs, p, mapped && !st.power_row,
                    mapped && st.heat_row && gate4_world(w), mapped && st.wear_row && gate5_world(w));
        lap(&pt.charts, &at);
    }
    draw_seed_list(c, st, in, top_h);
}

void draw_waiting(Canvas& c, ViewState& st, const UiInput& in, const char* message) {
    const i32 W = c.width(), H = c.height(), g = c.px(6);
    c.fill(Rect{0, 0, W, H}, kBg);
    const i32 top_h = c.line_h(kFontBold) + c.px(12);
    c.fill(Rect{0, 0, W, top_h}, kHead);
    char buf[256];
    snprintf(buf, sizeof buf, "Nova 2   %s", st.title);
    const i32 bh = button_h(c);
    ButtonRow row{c, in, W - c.px(10), (top_h - bh) / 2, bh};
    seed_button(c, row, st);
    c.text(c.px(10), c.px(6), buf, kFontBold, kText, kAlignLeft, row.x - c.px(20));
    c.text(W / 2, top_h + g + (H - top_h) / 3, message, kFontUi, kDim, kAlignCenter);
    draw_seed_list(c, st, in, top_h);
}

const char* mem_colour_label(MemColour c) { return kColourNames[static_cast<u32>(c) % 4]; }
const char* map_colour_label(MapColour c) { return kMapShort[static_cast<u32>(c) % 4]; }

} // namespace nova2
