// The settings window (settings.h; build-plan-viewer.md step 5).
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "theme.h"
#include "viewer.h"
#include "widgets.h"

namespace nova2 {

namespace {

Settings* g_noting = nullptr; // between settings_begin and settings_end

SettingRow* row_of(Settings* s, const Key* key) {
    for (usize i = 0; i < s->rows.size(); ++i)
        if (s->rows[i].key == key)
            return &s->rows[i];
    return nullptr;
}

void put(SettingRow* r, Str value) {
    const usize n = value.n < sizeof r->value - 1 ? value.n : sizeof r->value - 1;
    memcpy(r->value, value.p, n);
    r->value[n] = 0;
}

void on_value(const Key& key, Str value) {
    SettingRow* r = g_noting ? row_of(g_noting, &key) : nullptr;
    if (!r)
        return; // a test-only setting
    put(r, value);
    r->read = true;
}

bool same(Str a, const char* b) { return a.n == strlen(b) && memcmp(a.p, b, a.n) == 0; }

} // namespace

const SettingRow* Settings::find(const char* name) const {
    for (usize i = 0; i < rows.size(); ++i)
        if (strcmp(rows[i].key->name, name) == 0)
            return &rows[i];
    return nullptr;
}

void settings_begin(Settings* s) {
    s->rows.clear();
    for (u32 g = 0; g < kKeyGroupCount; ++g)
        for (usize i = 0; i < key_count(); ++i) {
            const Key& k = key_at(i);
            if ((k.flags & kKeyHidden) || strcmp(k.group, kKeyGroups[g]) != 0)
                continue;
            SettingRow r;
            r.key = &k;
            put(&r, k.def_text);
            s->rows.push(r);
        }
    g_noting = s;
    key_set_value_hook(on_value);
}

void settings_end(Settings* s, const Config& cfg) {
    key_set_value_hook(nullptr);
    g_noting = nullptr;
    for (usize i = 0; i < cfg.count(); ++i) {
        const Str name = cfg.key(i);
        for (usize k = 0; k < s->rows.size(); ++k) {
            SettingRow& r = s->rows[k];
            if (!same(name, r.key->name))
                continue;
            r.given = true;
            if (!r.read)
                put(&r, cfg.value(i));
            break;
        }
    }
}

void settings_say(Settings* s, const char* name, const char* value) {
    for (usize i = 0; i < s->rows.size(); ++i)
        if (strcmp(s->rows[i].key->name, name) == 0)
            put(&s->rows[i], value);
}

bool setting_changed(const SettingRow& r) {
    const Key& k = *r.key;
    if (k.kind == KeyKind::whole || k.kind == KeyKind::number) {
        char* end = nullptr;
        const double v = strtod(r.value, &end);
        if (end != r.value && *end == 0)
            return v != k.def;
    }
    return strcmp(r.value, k.def_text) != 0;
}

bool setting_wanted(const Settings& s, const SettingRow& r) {
    const char* needs = r.key->needs;
    if (!needs || !needs[0])
        return true;
    char name[96];
    const char* eq = strchr(needs, '=');
    const usize n = eq ? static_cast<usize>(eq - needs) : strlen(needs);
    if (n >= sizeof name)
        return true;
    memcpy(name, needs, n);
    name[n] = 0;
    const SettingRow* on = s.find(name);
    if (!on)
        return true;
    if (!eq) {
        // That switch on, or that number or text not zero and not empty.
        if (on->key->kind == KeyKind::flag)
            return strcmp(on->value, "true") == 0;
        if (on->key->kind == KeyKind::whole || on->key->kind == KeyKind::number)
            return strtod(on->value, nullptr) != 0;
        return on->value[0] != 0;
    }
    // That choice is one of a|b.
    const usize vn = strlen(on->value);
    for (const char* a = eq + 1; *a;) {
        const char* bar = strchr(a, '|');
        const usize an = bar ? static_cast<usize>(bar - a) : strlen(a);
        if (an == vn && memcmp(a, on->value, an) == 0)
            return true;
        a += an + (bar ? 1 : 0);
    }
    return false;
}

void setting_lines(const Settings& s, u32 chars, Array<SettingLine>* out) {
    out->clear();
    if (chars < 20)
        chars = 20;
    const char* group = nullptr;
    for (usize i = 0; i < s.rows.size(); ++i) {
        const Key& k = *s.rows[i].key;
        const u32 row = static_cast<u32>(i);
        if (!group || strcmp(group, k.group) != 0) {
            if (group)
                out->push(SettingLine{SettingLineKind::gap, row, 0, 0});
            out->push(SettingLine{SettingLineKind::group, row, 0, 0});
            group = k.group;
        }
        out->push(SettingLine{SettingLineKind::head, row, 0, 0});
        // The help, cut between words.
        const u32 len = static_cast<u32>(strlen(k.help));
        for (u32 from = 0; from < len;) {
            u32 n = len - from;
            if (n > chars) {
                n = chars;
                while (n > 0 && k.help[from + n] != ' ')
                    --n;
                if (n == 0)
                    n = chars; // one long word: cut it
            }
            out->push(SettingLine{SettingLineKind::help, row, from, n});
            from += n;
            while (from < len && k.help[from] == ' ')
                ++from;
        }
    }
}

namespace {

// Captions and their buttons, laid along lines from the left and onto the
// next line when one won't fit.
struct Flow {
    Canvas& c;
    const UiInput& in;
    i32 x0, x1, x, y, bh;
};

// A caption and a row of buttons, one of them chosen. The one clicked this
// frame, or -1.
i32 choice(Flow& f, const char* caption, const char* const* labels, u32 n, u32 chosen) {
    const i32 gap = f.c.px(3);
    i32 need = f.c.text_w(caption, kFontSmall) + f.c.px(6);
    for (u32 i = 0; i < n; ++i)
        need += button_w(f.c, labels[i]) + gap;
    if (f.x + need > f.x1 && f.x > f.x0) {
        f.x = f.x0;
        f.y += f.bh + f.c.px(5);
    }
    f.c.text(f.x, f.y + (f.bh - f.c.line_h(kFontSmall)) / 2, caption, kFontSmall, kDim);
    f.x += f.c.text_w(caption, kFontSmall) + f.c.px(6);
    i32 clicked = -1;
    for (u32 i = 0; i < n; ++i) {
        const i32 w = button_w(f.c, labels[i]);
        if (button(f.c, f.in, Rect{f.x, f.y, w, f.bh}, labels[i], i == chosen))
            clicked = static_cast<i32>(i);
        f.x += w + gap;
    }
    f.x += f.c.px(22);
    return clicked;
}

// The view's switches: what the buttons about the view choose, in one
// place. Where the lines end.
i32 draw_switches(Canvas& c, const Rect& r, ViewState& st, bool mapped, const UiInput& in) {
    static const char* const kMiddle[2] = {"map", "memory"};
    static const char* const kCharts[2] = {"traffic", "power"};
    static const char* const kPage[3] = {"place", "programs", "figures"};
    static const char* const kFigures[2] = {"this place", "the world"};
    static const char* const kPane[2] = {"code", "family"};
    // The programs' orders, as their list's head has them.
    static const ProgramSort kBy[5] = {ProgramSort::lineage, ProgramSort::threads, ProgramSort::bytes,
                                       ProgramSort::age, ProgramSort::born};
    const char* map_names[static_cast<u32>(MapColour::count)];
    const char* mem_names[static_cast<u32>(MemColour::count)];
    const char* sort_names[5];
    for (u32 i = 0; i < static_cast<u32>(MapColour::count); ++i)
        map_names[i] = map_colour_label(static_cast<MapColour>(i));
    for (u32 i = 0; i < static_cast<u32>(MemColour::count); ++i)
        mem_names[i] = mem_colour_label(static_cast<MemColour>(i));
    u32 sorted = 0;
    for (u32 i = 0; i < 5; ++i) {
        sort_names[i] = sort_label(kBy[i]);
        sorted = st.sort == kBy[i] ? i : sorted;
    }
    Flow f{c, in, r.x, r.right(), r.x, r.y, button_h(c)};
    i32 k;
    if (mapped) {
        if ((k = choice(f, "the middle shows", kMiddle, 2, st.map ? 0 : 1)) >= 0)
            st.map = k == 0;
        if ((k = choice(f, "the map's colours", map_names, static_cast<u32>(MapColour::count),
                        static_cast<u32>(st.map_colour))) >= 0)
            st.map_colour = static_cast<MapColour>(k);
    }
    if ((k = choice(f, "the memory's colours", mem_names, static_cast<u32>(MemColour::count),
                    static_cast<u32>(st.colour))) >= 0)
        st.colour = static_cast<MemColour>(k);
    if (mapped && (k = choice(f, "charts", kCharts, 2, st.power_row ? 1 : 0)) >= 0) {
        st.power_row = k == 1;
        st.heat_row = false; // gate 4's row is the top bar's to choose
    }
    if ((k = choice(f, "the panel on the right", kPage, 3, static_cast<u32>(st.inspect))) >= 0)
        st.inspect = static_cast<Inspect>(k);
    if ((k = choice(f, "figures of", kFigures, 2, st.world_figures ? 1 : 0)) >= 0)
        st.world_figures = k == 1;
    if ((k = choice(f, "programs in order of", sort_names, 5, sorted)) >= 0) {
        // As the list's own head: the one chosen again turns the order round.
        st.sort_down = st.sort == kBy[k] ? !st.sort_down : false;
        st.sort = kBy[k];
    }
    if ((k = choice(f, "a program's pane", kPane, 2, st.family ? 1 : 0)) >= 0)
        st.family = k == 1;
    return f.y + f.bh;
}

Array<SettingLine> g_lines; // this frame's

} // namespace

void draw_settings(Canvas& c, const Rect& r, ViewState& st, bool mapped, const UiInput& in) {
    c.fill(r, kPanel);
    c.frame(r, kEdge);
    const i32 pad = c.px(12), bh = button_h(c);
    const i32 head_h = c.line_h(kFontBold) + c.px(10);
    c.fill(Rect{r.x + 1, r.y + 1, r.w - 2, head_h}, kHead);
    c.text(r.x + pad, r.y + c.px(6), "Settings", kFontBold, kText);
    const i32 close_w = button_w(c, "close");
    if (button(c, in, Rect{r.right() - pad - close_w, r.y + (head_h - bh) / 2 + 1, close_w, bh}, "close"))
        st.settings_open = false;
    i32 y = r.y + head_h + c.px(10);
    c.text(r.x + pad, y, "The view", kFontBold, kDim);
    y += c.line_h(kFontBold) + c.px(6);
    y = draw_switches(c, Rect{r.x + pad, y, r.w - 2 * pad, 0}, st, mapped, in) + c.px(14);
    c.text(r.x + pad, y, "The world's settings", kFontBold, kDim);
    c.text(r.x + pad + c.text_w("The world's settings", kFontBold) + c.px(12),
           y + c.line_h(kFontBold) - c.line_h(kFontSmall),
           "to read: a world keeps the settings it was built with. A value in blue is not the default.", kFontSmall,
           kDim, kAlignLeft, r.w - 2 * pad - c.text_w("The world's settings", kFontBold) - c.px(12));
    y += c.line_h(kFontBold) + c.px(6);
    const Rect list{r.x + pad, y, r.w - 2 * pad, r.bottom() - y - pad};
    if (list.h <= 0)
        return;
    if (!st.settings || st.settings->rows.empty()) {
        c.text(list.x, list.y, "this window was not given the world's settings", kFontSmall, kDim);
        return;
    }
    const Settings& s = *st.settings;
    // The columns of a setting's line, and the help under it, set in.
    const i32 indent = c.px(14), label_w = c.px(250), value_w = c.px(230), name_w = c.px(270), col = c.px(10);
    const i32 text_w = list.w - c.px(14) - indent - c.px(8); // less the bar's room
    const char* sample = "the quick brown fox jumps over the lazy dog, 0123456789 times a tick.";
    const i32 sample_w = c.text_w(sample, kFontSmall);
    const u32 chars = sample_w > 0 ? static_cast<u32>(0.92 * text_w * static_cast<i32>(strlen(sample)) / sample_w) : 80;
    setting_lines(s, chars, &g_lines);
    const i32 lh = c.line_h(kFontUi) + c.px(2);
    const ListRows rows = scroll_list(c, in, list, lh, static_cast<u32>(g_lines.size()), st.settings_scroll);
    c.clip(rows.rows);
    char buf[512];
    for (u32 i = rows.first; i < rows.first + rows.count; ++i) {
        const SettingLine& l = g_lines[i];
        const SettingRow& row = s.rows[l.row];
        const Key& k = *row.key;
        const Rect at = rows.row(i);
        const bool wanted = setting_wanted(s, row);
        if (l.kind == SettingLineKind::group) {
            c.text(at.x, at.y + (lh - c.line_h(kFontBold)) / 2, k.group, kFontBold, kText, kAlignLeft, at.w);
            c.line(at.x, at.bottom() - 1, at.right(), at.bottom() - 1, kEdge);
        } else if (l.kind == SettingLineKind::head) {
            note(in, at, k.name);
            const bool changed = setting_changed(row);
            const i32 ty = at.y + (lh - c.line_h(kFontUi)) / 2;
            i32 x = at.x + indent;
            c.text(x, ty, k.label, kFontUi, wanted ? kText : kFaint, kAlignLeft, label_w);
            x += label_w + col;
            c.text(x, ty, row.value[0] ? row.value : "(empty)", kFontUi,
                   !wanted || !row.value[0] ? kFaint : (changed ? kAccent : kText), kAlignLeft, value_w);
            x += value_w + col;
            const i32 sy = at.y + (lh - c.line_h(kFontSmall)) / 2;
            c.text(x, sy, k.name, kFontSmall, wanted ? kDim : kFaint, kAlignLeft, name_w);
            x += name_w + col;
            // What else there is to say of it.
            usize n = 0;
            buf[0] = 0;
            if (changed && k.kind != KeyKind::text)
                n += static_cast<usize>(snprintf(buf + n, sizeof buf - n, "default %s", k.def_text));
            if (!wanted)
                n += static_cast<usize>(snprintf(buf + n, sizeof buf - n, "%sneeds %s", n ? "; " : "", k.needs));
            else if (!row.read)
                n += static_cast<usize>(snprintf(buf + n, sizeof buf - n, "%snot read by this world", n ? "; " : ""));
            if (n && at.right() - x > c.px(40))
                c.text(x, sy, buf, kFontSmall, kDim, kAlignLeft, at.right() - x);
        } else if (l.kind == SettingLineKind::help) {
            const u32 n = l.len < sizeof buf - 1 ? l.len : static_cast<u32>(sizeof buf - 1);
            memcpy(buf, k.help + l.from, n);
            buf[n] = 0;
            c.text(at.x + indent, at.y + (lh - c.line_h(kFontSmall)) / 2, buf, kFontSmall, wanted ? kDim : kFaint,
                   kAlignLeft, at.w - indent);
        }
    }
    c.unclip();
}

} // namespace nova2
