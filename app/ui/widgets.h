#pragma once

// The viewer's controls (build-plan-viewer.md step 1): buttons, rows of
// them, and a list that scrolls. Immediate-mode, as the views are: each is
// drawn and answered in the one call, from the input since the last frame,
// and keeps no state but what its caller holds. Everything the viewer can
// be told to do has a button (build-plan-viewer.md 2, choice 6).
#include "canvas.h"
#include "nova2/types.h"

namespace nova2 {

struct UiInput;

// Where each button of a frame was drawn, for a caller that asks (UiInput's
// `log`): the tests find a button by its label and click where it is.
struct ButtonLog {
    static constexpr u32 kMost = 192;
    Rect at[kMost];
    const char* label[kMost];
    u32 count = 0;
    const Rect* find(const char* name) const; // the first button so labelled, or null
};

// Notes a place a click means something that isn't a button (a list's row),
// under a name, for a caller that asks where things are.
void note(const UiInput& in, const Rect& r, const char* label);

// A button's size around its label.
i32 button_w(Canvas& c, const char* label, u32 font = kFontSmall);
i32 button_h(Canvas& c, u32 font = kFontSmall);

// Draws a button and says whether it was clicked this frame. `chosen` marks
// the one of several that is in force (a tab, a colouring).
bool button(Canvas& c, const UiInput& in, const Rect& r, const char* label, bool chosen = false,
            u32 font = kFontSmall);

// A button that opens a list under it (a dropdown): its label at the left,
// an arrow at the right, lit while the list is `open`. Whether it was
// clicked this frame. Its width is drop_button_w.
i32 drop_button_w(Canvas& c, const char* label, u32 font = kFontSmall);
bool drop_button(Canvas& c, const UiInput& in, const Rect& r, const char* label, bool open, u32 font = kFontSmall);

// Buttons and words laid along a line from a point, rightwards (`right`
// and `words`) or leftwards (`left` and `words_left`), each as wide as its
// label: x is where the next one goes.
struct ButtonRow {
    Canvas& c;
    const UiInput& in;
    i32 x, y, h;
    u32 font = kFontSmall;

    bool right(const char* label, bool chosen = false);
    bool left(const char* label, bool chosen = false);
    void words(const char* s, u32 colour);
    void words_left(const char* s, u32 colour, i32 width = 0); // width: a fixed room for it (0: its own)
    void gap(i32 px);
};

// Where a list that scrolls stands: its first row shown, and the bar's
// grip while it is held.
struct Scroll {
    u32 first = 0;
    bool held = false;
    i32 grab = 0; // where on the grip the pointer took it
};

// The rows of a list to draw this frame: rows [first, first + count), in
// `rows` (the list's rectangle less its bar), each row_h high.
struct ListRows {
    u32 first = 0, count = 0;
    Rect rows{};
    i32 row_h = 0;
    Rect row(u32 i) const { return Rect{rows.x, rows.y + static_cast<i32>(i - first) * row_h, rows.w, row_h}; }
};

// A list of `total` rows in r: the wheel over it and a drag of its bar
// move it, and the bar is drawn when not every row fits. The caller draws
// the rows it is given.
ListRows scroll_list(Canvas& c, const UiInput& in, const Rect& r, i32 row_h, u32 total, Scroll& s);

} // namespace nova2
