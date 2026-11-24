#pragma once

// The viewer's views at gate 1 (design.md 13.4; build-plan-gate1.md step 12):
//   - the places, as tiles coloured by how many programs each holds (the
//     map comes at gate 3);
//   - the place inspector: its devices and what's on each core, its OS
//     values, its counts and its reset region's measures;
//   - its memory, page by page, coloured by owner, lineage, permission or
//     size class, with the reset region marked;
//   - charts from the observers' samples: programs and threads, births and
//     deaths, distinct paths tried.
// Gate 2 adds power (build-plan-gate2.md step 8): the tiles dim as a place's
// voltage falls; the inspector shows its voltage, capacity and drop-out, and
// each device's state (on, cut out with its off-time left, or waiting for
// the voltage to restart it) and its speed this tick, and how much of a
// memory device that's cut out has faded. The deaths chart is by cause, and
// a second row of charts shows the inspected place's voltage, cold starts
// and boots, and turnover per window.
// Gate 3 adds the map (build-plan-gate3.md step 6), for a world that has
// one: the places at their positions on the land and water, the data links
// and bridges, coloured by population, by each place's largest lineage, by
// whether its network device is on, or by its links' queues and the
// messages dropped there since the world was built or opened (C). It takes
// the memory's place in the middle, the tiles staying (V switches the two);
// the inspector shows the place's queues, inboxes and drops; and the second
// row of charts shows traffic, drops, and colonists and settlements (G
// switches back to gate 2's). With no map the views are gate 2's.
// The viewer's own build (build-plan-viewer.md) starts with its controls,
// step 1: everything a key does a button on screen does too (the bar across
// the top: run, pause, one tick, slower, faster, and which row of charts;
// the middle panel's head: map or memory, and their colourings), and the
// map zooms and pans (the wheel about the pointer, a drag, and the buttons
// on it; Home shows the whole map again).
// Step 2: the inspector has a second page, the place's programs
// (programs.h): a list that sorts and scrolls, and for the program picked
// its code, an instruction a line with its threads marked on it, and its
// threads. The pages of memory it holds are framed on the memory view.
// Step 3 (figures.h): the program picked shows its family in place of its
// code (its parent and children a click away, its lineage, its genome),
// and the inspector has a third page, the figures of the place or of the
// world: births and deaths by cause, the commonest lineages and genomes
// with their shares, and the place's turnover.
// Step 4: the window on a run (app/run_view.h) draws the seed it is shown
// with a dropdown in the top bar that lists the run's seeds and chooses
// among them (Damian, 2026-11-17: a dropdown, not a tab a seed), and
// without the buttons that run or pause a world: it only watches.
// Step 5: the top bar's "settings" button opens the settings window
// (settings.h) over everything under the bar: the view's switches as
// buttons, and the world's settings to read.
// Gate 4 adds power and heat (build-plan-gate4.md step 9; power_view.h),
// for a world with sources or with heat on: the map's places coloured by
// voltage, temperature, draw or cooling, the sources and the proximity
// links over it (the buttons in the map's top left corner); the inspector's
// lines about the sources at the place and about its heat; and a third row
// of charts, "heat": power given, drawn and lost, the places' temperatures,
// and heat trips and cut-outs. A world with neither shows none of it.
// Gate 5 adds wear, visits and its events (build-plan-gate5.md step 13;
// wear_view.h), for a world with any of its strengths on: the map's places
// coloured by wear or by the time since their last visit, the patches,
// strikes and the current over it, and failed links and parted straits
// drawn broken (a second row of buttons in the map's top left corner); the
// inspector's lines about wear and visits, and a fourth page, its events;
// the disk's blocks by wear under the memory; and a fourth row of charts,
// "wear": failures, replacements, visits and respawns, the disasters with
// the cascades labelled, and first leads by era.
// draw_viewer draws one frame from the world as it is and the input since
// the last; it changes nothing in the world. The window and headless
// renders both call it.
#include "canvas.h"
#include "nova2/types.h"
#include "power_view.h"
#include "programs.h"
#include "wear_view.h"
#include "widgets.h"

namespace nova2 {

class World;
class Observer;

// What the window saw since the last frame.
struct UiInput {
    i32 mx = -1, my = -1; // the mouse, in pixels (-1: outside)
    bool down = false;    // the left button held
    bool pressed = false; // ... and went down since the last frame
    bool released = false; // ... or came up since the last frame
    i32 wheel = 0;        // the wheel's notches since the last frame (away from the user: positive)
    ButtonLog* log = nullptr; // if set, where each button is drawn this frame (widgets.h; the tests ask)
    u32 keys[16] = {};    // virtual keys pressed (Win32 VK_ codes)
    u32 nkeys = 0;
};

enum class MemColour : u8 { owner, lineage, permission, size_class, count };
enum class MapColour : u8 { population, lineage, network, queues, count };
enum class Inspect : u8 { place, programs, figures, events };

struct Settings; // settings.h

// One seed of a run, for the list of seeds (build-plan-viewer.md step 4):
// whether it waits, runs or has ended, how far through it is, the best
// stage it has reached and when it reached each (~0: not yet), and its
// births.
enum : u32 { kTabWaiting = 0, kTabRunning = 1, kTabDone = 2 };
inline constexpr u32 kMostSeedTabs = 64; // the most seeds the list holds

struct SeedTab {
    u64 seed = 0;
    u32 state = kTabWaiting;
    u32 stage = 0;
    u32 permille = 0;
    u64 tick = 0, births = 0;
    u64 stage_tick[6] = {};
    u64 saved = ~0ull;   // the tick of its last save (build-plan-viewer.md step 8; ~0: none)
    bool saving = false; // a save has been asked of it and not yet taken
};

struct ViewState {
    // The view's number, given at its first frame: what the views keep
    // between frames (the map's lineages) is kept for one view at a time.
    u32 id = 0;
    u32 place = 0;
    MemColour colour = MemColour::owner;
    // Where the memory map was last drawn: its top left, the pages in a row,
    // and the pixels a page takes each way (0: not drawn yet).
    i32 mem_x = 0, mem_y = 0, mem_cols = 0, mem_cell = 0;
    // With a map (gate 3): the map in the middle, or the memory; how it's
    // coloured; and the second row of charts gate 2's, not gate 3's.
    bool map = true;
    MapColour map_colour = MapColour::population;
    bool power_row = false;
    // Gate 4's (power_view.h), in a world with sources or heat: the places
    // coloured by one of its colourings in place of the map's own; the
    // sources and the proximity links drawn over the map; and the second
    // row of charts gate 4's.
    PowerColour power_colour = PowerColour::none;
    bool show_sources = true, show_grid = true;
    bool heat_row = false;
    // Gate 5's (wear_view.h), in a world with any of its strengths on: the
    // places coloured by wear or by the time since their last visit; the
    // patches and the current drawn over the map; the second row of charts
    // gate 5's; and the events page's filters and list.
    WearColour wear_colour = WearColour::none;
    bool show_patches = true, show_current = true;
    bool wear_row = false;
    EventsView events;
    // Where the inspector's place page stands, when it is taller than its
    // panel.
    Scroll place_scroll;
    // The map's zoom (build-plan-viewer.md step 1): 1 shows the whole map
    // (and below 1 asks for that again); zoomed in, the point of the map at
    // the view's middle, in the map's units. And a press on the map being
    // held: where the pointer was last, and whether it has become a drag.
    double map_zoom = 1, map_cx = -1, map_cy = -1;
    bool map_held = false, map_dragged = false;
    i32 held_x = 0, held_y = 0;
    // The inspector's page (build-plan-viewer.md step 2): the place, or its
    // programs. A program picked there (its uid; 0: none) and the place it
    // was picked in; the program as it was last seen alive, and its genome's
    // hash, so its code outlives it; the thread of its that is followed (its
    // record in the place's OS; ~0u: none); and where its lists stand.
    Inspect inspect = Inspect::place;
    ProgramSort sort = ProgramSort::age;
    bool sort_down = false;
    u64 program = 0;
    u32 program_place = 0;
    ProgramRow picked;
    bool picked_alive = false;
    u64 picked_genome = 0;
    u32 thread = ~0u;
    Scroll programs_scroll, code_scroll, threads_scroll;
    // Step 3: under the program picked, its family in place of its code and
    // threads; and on the figures page, the world's in place of the place's.
    bool family = false;
    bool world_figures = false;
    // Step 4, the window on a run: its seeds, for the dropdown in the top
    // bar (none: no dropdown); the one shown, which a click on a running
    // seed's row in the open list changes; whether the list is open, and
    // where it stands if it scrolls. `watching`: the window only watches,
    // so the buttons and keys that run, pause and step the world are not
    // there.
    const SeedTab* seeds = nullptr;
    u32 seed_count = 0;
    u32 seed_shown = 0;
    bool seeds_open = false;
    Scroll seeds_scroll;
    bool watching = false;
    // Step 5, the settings window (settings.h): whether it is open over the
    // view, the world's settings for it to list (none: it says so), and
    // where its list stands.
    // The window on a run can ask the seed shown to save its world (step 8):
    // the button is there (`can_save`), and has been pressed this frame.
    bool can_save = false, save_asked = false;
    // The code pane's copy button (programs.h): pressed this frame, for the
    // window to put copied_text() on the clipboard; and the program whose
    // code it copied last, so the button says it has.
    bool copy_asked = false;
    u64 copied = 0;
    bool settings_open = false;
    const Settings* settings = nullptr;
    Scroll settings_scroll;
    bool paused = false;
    bool step = false;        // one tick, then pause again
    u32 ticks_per_frame = 1;
    bool quit = false;
    double tps = 0;           // measured ticks a second (0: not shown, as in headless renders)
    const char* title = "";   // what's open
};

void draw_viewer(Canvas& c, ViewState& st, World& w, const Observer* obs, const UiInput& in);

// Seconds each part of the frames drawn took, summed, while timed (nova2
// view --time): null, not timed.
struct PanelTimes {
    double top = 0, places = 0, middle = 0, inspector = 0, charts = 0;
};
void time_panels(PanelTimes* t);

// A frame with no world to show (a run's window while no seed runs): the
// top bar with the seeds' dropdown, and a message.
void draw_waiting(Canvas& c, ViewState& st, const UiInput& in, const char* message);

// The steady colour a lineage is drawn in everywhere (by its origin).
u32 lineage_colour(u64 origin);

// A colouring's button.
const char* mem_colour_label(MemColour c);
const char* map_colour_label(MapColour c);

} // namespace nova2
