#pragma once

// The settings window (build-plan-viewer.md step 5; 2, choice 3): a page
// over the view, opened by the top bar's "settings" button. At the top are
// the view's own switches, each a row of buttons that says which is chosen
// (what the keys V, C, M and G and the buttons about the view choose).
// Below is every setting the registry holds (nova2/keys.h), group by group
// in the registry's order, each with its label, its value, its name and its
// help. They are there to read: a world keeps the settings it was built
// with, and the setup page that changes them before a world is built is
// step 6.
//
// The values come from the registry's own readers. Between settings_begin
// and settings_end every setting a reader gives back is noted with the
// value it gave, so the page shows what the world uses, defaults worked
// out; a setting nothing read (its feature is off, or it is another
// command's) shows the config's text for it, or its default, and says it
// was not read.
#include "canvas.h"
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/keys.h"
#include "nova2/types.h"

namespace nova2 {

struct UiInput;
struct ViewState;

// One setting on the page.
struct SettingRow {
    const Key* key = nullptr;
    char value[160] = ""; // as config text
    bool read = false;    // a reader gave it back while the world was built
    bool given = false;   // the config names it
};

// The page's settings: every entry of the registry but the test-only ones,
// once each, in the order of the registry's groups.
struct Settings {
    Array<SettingRow> rows;
    const SettingRow* find(const char* name) const;
};

// Notes what the readers give back from here to settings_end. One at a
// time, and on one thread: no other thread may read a setting meanwhile.
void settings_begin(Settings* s);
// Ends the noting, and fills in what nothing read from the config's own
// text.
void settings_end(Settings* s, const Config& cfg);
// Words in place of a setting's value (a run's seeds: "1 to 20, one a seed").
void settings_say(Settings* s, const char* name, const char* value);

// Whether a setting's value is not its default.
bool setting_changed(const SettingRow& r);
// Whether the switch a setting depends on (its entry's `needs`) is on. The
// page greys it out otherwise, as the registry says the setup page shall.
bool setting_wanted(const Settings& s, const SettingRow& r);

// The page's list, line by line: a group's name, a setting's line (label,
// value, name), and its help, cut into lines of `chars` characters at most
// between words. The tests read it.
enum class SettingLineKind : u8 { gap, group, head, help };
struct SettingLine {
    SettingLineKind kind = SettingLineKind::gap;
    u32 row = 0;           // the setting (group: the group's first)
    u32 from = 0, len = 0; // help: its part of the help's text
};
void setting_lines(const Settings& s, u32 chars, Array<SettingLine>* out);

// The page, in r. `mapped`: the world has a map, so the switches that
// choose between map and memory, the map's colours and the rows of charts
// are there.
void draw_settings(Canvas& c, const Rect& r, ViewState& st, bool mapped, const UiInput& in);

} // namespace nova2
