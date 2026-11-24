#pragma once

// Gate 5's views (design.md 13.4; build-plan-gate5.md step 13), for a world
// with any of gate 5's strengths on (gate5_world). The viewer (viewer.h)
// calls these from its map, its inspector and its memory view; the charts of
// wear, failures, visits and disasters are in viewer.cpp with the others.
//   - The map's places coloured by wear (each place's most worn device, as a
//     share of its life), with a corner marked where a device has failed and
//     waits for its visit and where a device is flaky; or by the time since
//     the place's last visit, against its mean gap.
//   - Over the map: the patches there now, each a ring of its type's colour,
//     faint while it rises or eases (gusts among them); the places struck
//     this tick; failed links drawn broken, and the bridges of a parted
//     strait broken in the water's colour; and the current, each data link
//     it pushes along with an arrow the way it flows, and its heading and
//     strength in the map's corner.
//   - In the inspector: the visits, and each device's wear, worn threshold,
//     failed state and flaky factor; the memory's weak cells; the disk's
//     blocks.
//   - Under the memory: a strip of the disk's blocks by wear.
//   - The inspector's fourth page, events: the facts and the labels of the
//     run together, newest first, each kind shown or hidden by its button,
//     a click on one picking its place.
// Nothing here changes the world.
#include "canvas.h"
#include "nova2/types.h"
#include "power_view.h" // MapAt
#include "widgets.h"

namespace nova2 {

class World;
class Place;
class Observer;
struct UiInput; // viewer.h

// Whether a world has anything of gate 5's to show: wear, links that fail
// or straits that part, patches, visits that tear places down, the
// current, the killer, or disks whose blocks wear.
bool gate5_world(World& w);

// A gate 5 colouring of the map's places (none: another colouring).
enum class WearColour : u8 { none, wear, visits, count };
const char* wear_colour_label(WearColour c);
u32 wear_colour(World& w, u32 i, WearColour mode);
void wear_legend(World& w, WearColour mode, char* out, usize size);

// Data link k's state: 0 carrying, 1 failed, 2 parted with its strait.
u32 link_down(World& w, u32 k);
// A link drawn broken: its two ends, with a gap at its middle.
void broken_line(Canvas& c, i32 xa, i32 ya, i32 xb, i32 yb, u32 colour, float width);

// Marks on the places' squares of d pixels: a failed device waiting for
// its visit (a red corner, bottom left) and a flaky device (violet, top
// left).
void draw_wear_marks(Canvas& c, World& w, const MapAt& at, i32 d);
// The patches there now and the places struck this tick.
void draw_patches(Canvas& c, World& w, const MapAt& at, i32 d, float scale);
// The current: arrows along the data links it pushes along, and its heading
// in the bottom left corner of `area`.
void draw_current(Canvas& c, World& w, const MapAt& at, const Rect& area, float scale);

// The inspector's lines about wear and visits, from (x, y); the y under
// them.
i32 draw_wear_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p);
// The disk's blocks by wear, in r, with a caption: the height it takes for
// a strip r.w wide (0: the disk's blocks don't wear).
i32 disk_strip_h(Canvas& c, const Place& p, i32 w);
void draw_disk_strip(Canvas& c, const Rect& r, const Place& p);

// The events page: which kinds are hidden, and where its list stands.
enum : u32 { kEvFailure = 0, kEvRespawn, kEvCascade, kEvStrait, kEvKill, kEvWatchdog, kEvKinds };
struct EventsView {
    bool hide[kEvKinds] = {};
    Scroll scroll;
};
// Draws it in r; a click on an event at a place sets *place to it.
void draw_events(Canvas& c, const Rect& r, World& w, const Observer* obs, const UiInput& in, EventsView& ev,
                 u32* place);

} // namespace nova2
