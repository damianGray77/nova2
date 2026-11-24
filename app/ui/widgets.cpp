#include "widgets.h"

#include <cstring>

#include "theme.h"
#include "viewer.h"

namespace nova2 {

const Rect* ButtonLog::find(const char* name) const {
    for (u32 i = 0; i < count; ++i)
        if (strcmp(label[i], name) == 0)
            return &at[i];
    return nullptr;
}

void note(const UiInput& in, const Rect& r, const char* label) {
    if (in.log && in.log->count < ButtonLog::kMost) {
        in.log->at[in.log->count] = r;
        in.log->label[in.log->count++] = label;
    }
}

i32 button_w(Canvas& c, const char* label, u32 font) { return c.text_w(label, font) + c.px(14); }

i32 button_h(Canvas& c, u32 font) { return c.line_h(font) + c.px(6); }

bool button(Canvas& c, const UiInput& in, const Rect& r, const char* label, bool chosen, u32 font) {
    note(in, r, label);
    const bool hot = r.contains(in.mx, in.my);
    c.fill(r, chosen ? kRowSel : (hot ? kButtonHot : kButton));
    c.frame(r, chosen ? kAccent : kEdge);
    c.text(r.x + r.w / 2, r.y + (r.h - c.line_h(font)) / 2, label, font, chosen || hot ? kText : kDim, kAlignCenter);
    return hot && in.pressed;
}

bool ButtonRow::right(const char* label, bool chosen) {
    const Rect r{x, y, button_w(c, label, font), h};
    x += r.w + c.px(3);
    return button(c, in, r, label, chosen, font);
}

i32 drop_button_w(Canvas& c, const char* label, u32 font) { return button_w(c, label, font) + c.px(14); }

bool drop_button(Canvas& c, const UiInput& in, const Rect& r, const char* label, bool open, u32 font) {
    note(in, r, label);
    const bool hot = r.contains(in.mx, in.my);
    const u32 ink = open || hot ? kText : kDim;
    c.fill(r, open ? kRowSel : (hot ? kButtonHot : kButton));
    c.frame(r, open ? kAccent : kEdge);
    const i32 pad = (button_w(c, label, font) - c.text_w(label, font)) / 2;
    c.text(r.x + pad, r.y + (r.h - c.line_h(font)) / 2, label, font, ink, kAlignLeft, r.w - pad - c.px(16));
    // The arrow: down while shut, up while open.
    const i32 cx = r.right() - c.px(10), cy = r.y + r.h / 2, a = c.px(4), b = c.px(2);
    const i32 tip = open ? cy - b : cy + b, base = open ? cy + b : cy - b;
    c.line(cx - a, base, cx, tip, ink, 1.5f);
    c.line(cx, tip, cx + a, base, ink, 1.5f);
    return hot && in.pressed;
}

bool ButtonRow::left(const char* label, bool chosen) {
    const i32 w = button_w(c, label, font);
    x -= w;
    const Rect r{x, y, w, h};
    x -= c.px(3);
    return button(c, in, r, label, chosen, font);
}

void ButtonRow::words(const char* s, u32 colour) {
    c.text(x, y + (h - c.line_h(font)) / 2, s, font, colour);
    x += c.text_w(s, font) + c.px(6);
}

void ButtonRow::words_left(const char* s, u32 colour, i32 width) {
    const i32 w = width ? width : c.text_w(s, font);
    x -= w;
    c.text(x + w / 2, y + (h - c.line_h(font)) / 2, s, font, colour, kAlignCenter);
    x -= c.px(6);
}

void ButtonRow::gap(i32 px) { x += px; }

ListRows scroll_list(Canvas& c, const UiInput& in, const Rect& r, i32 row_h, u32 total, Scroll& s) {
    ListRows out;
    out.row_h = row_h > 0 ? row_h : 1;
    const u32 fit = r.h > 0 ? static_cast<u32>(r.h / out.row_h) : 0;
    const u32 last_first = total > fit ? total - fit : 0; // the furthest it scrolls
    const i32 bar_w = c.px(10);
    const bool bar = total > fit && fit > 0 && r.w > bar_w * 2;
    out.rows = Rect{r.x, r.y, bar ? r.w - bar_w - c.px(2) : r.w, r.h};
    if (!bar) {
        s.first = 0;
        s.held = false;
        out.count = total < fit ? total : fit;
        return out;
    }
    // The wheel: three rows a notch, towards the top when it turns away.
    if (in.wheel != 0 && r.contains(in.mx, in.my)) {
        const i64 to = static_cast<i64>(s.first) - static_cast<i64>(in.wheel) * 3;
        s.first = to < 0 ? 0 : (to > last_first ? last_first : static_cast<u32>(to));
    }
    // The bar: a grip as long as the share of the rows that fit, where the
    // first row stands; a drag moves it, and a click on the track jumps.
    const Rect track{r.right() - bar_w, r.y, bar_w, r.h};
    i32 grip_h = static_cast<i32>(static_cast<i64>(track.h) * fit / total);
    grip_h = grip_h < c.px(16) ? c.px(16) : grip_h;
    grip_h = grip_h > track.h ? track.h : grip_h;
    const i32 travel = track.h - grip_h;
    if (!in.down)
        s.held = false;
    if (in.pressed && track.contains(in.mx, in.my)) {
        const i32 grip_y = track.y + static_cast<i32>(static_cast<i64>(travel) * s.first / last_first);
        s.held = true;
        s.grab = in.my >= grip_y && in.my < grip_y + grip_h ? in.my - grip_y : grip_h / 2;
    }
    if (s.held && travel > 0) {
        i64 at = in.my - s.grab - track.y;
        at = at < 0 ? 0 : (at > travel ? travel : at);
        s.first = static_cast<u32>((at * last_first + travel / 2) / travel);
    }
    s.first = s.first > last_first ? last_first : s.first;
    const i32 grip_y = track.y + static_cast<i32>(static_cast<i64>(travel) * s.first / last_first);
    c.fill(track, kRowHot);
    c.fill(Rect{track.x, grip_y, track.w, grip_h}, s.held || track.contains(in.mx, in.my) ? kButtonHot : kButton);
    out.first = s.first;
    out.count = fit;
    return out;
}

} // namespace nova2
