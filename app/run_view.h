#pragma once

// The window on a run (build-plan-viewer.md steps 4, 7 and 8; 2, choices 1
// and 2): `nova2 origin` opens the viewer's window in the run's own process,
// on a thread of its own: at the start with origin.view on, and whenever
// `nova2 watch` asks, any number of times. It only watches. The same thread
// hears `nova2 save`, which has every running seed save its whole world.
//
// Each seed has a slot. Its thread publishes a few figures there at every
// sample (its tick, births and stages), which the window reads whenever,
// for the list of seeds. To draw a seed's world the window asks for it
// (`want`); the seed's thread sees that between two ticks, says it is held
// and waits; the window reads the world and the observer for one frame and
// lets it go. So a frame shows a whole tick's state, the seed loses the
// frame's reading time and no more, and a seed nobody watches pays one
// read of `want` a tick. Nothing the window does reaches the world: the
// run's files are the same with it open or shut (tests/test_view.cpp).
//
// The window never waits for the seed (Damian, 2026-11-19): it asks, goes on
// answering, and draws the frame once the seed holds. `hold` is the whole of
// that: none, asked (by the window), held (by the seed), and back to none
// when the window lets it go; the two events only wake the other side.
//
// A save is asked the same way: a bit of `want`, which the seed's thread
// takes between two ticks and answers by saving its world itself.
#include "nova2/types.h"

namespace nova2 {

class Observer;
class World;
struct Settings;

enum : u32 { kSeedWaiting = 0, kSeedRunning = 1, kSeedDone = 2 };
// What a seed is asked between two ticks: to save its world (SeedSlot::want),
// and to hold while the window reads it (SeedSlot::hold).
enum : long { kWantSave = 2 };
enum : long { kHoldNone = 0, kHoldAsked = 1, kHoldHeld = 2 };
inline constexpr u64 kNoSave = ~0ull;

struct SeedSlot {
    u64 seed = 0;
    // Published by the seed's thread, each a single aligned word.
    volatile u64 tick = 0, births = 0;
    volatile u64 stage_tick[6] = {}; // when it reached each stage (~0: not yet); [0] unused
    volatile u32 state = kSeedWaiting;
    volatile u32 stage = 0;    // the best stage reached
    volatile u32 permille = 0; // how far through its ticks or its budget
    // The world and its observer, while the seed runs: read by the window
    // only while the seed is held.
    World* w = nullptr;
    Observer* obs = nullptr;
    volatile long want = 0; // kWantSave: asked of the seed between ticks
    volatile long hold = 0; // kHoldNone, kHoldAsked, kHoldHeld
    volatile u32 saving = 0; // it is saving its world now
    void* held = nullptr;   // an event: the seed holds (a wake for the window)
    void* go = nullptr;     // an event: the window has done (a wake for the seed)
    volatile u64 saved_tick = kNoSave; // the tick of its last save (kNoSave: none yet)
};

struct RunView {
    SeedSlot* slots = nullptr;
    u32 count = 0;
    char title[200] = "";
    const Settings* settings = nullptr; // the run's settings, for the settings window (ui/settings.h)
    volatile long done = 0; // every seed has ended: the window closes
    void* thread = nullptr; // the window's, which also waits for the signals
    void* ready = nullptr;  // an event: the first window is open, or can't be
    void* quit = nullptr;   // an event: the run has ended
    // Named events, set from outside by `nova2 watch` and `nova2 save`: this
    // run's own (by its process's number) and the ones any run answers to.
    void* watch[2] = {nullptr, nullptr};
    void* save[2] = {nullptr, nullptr};
    bool at_start = false; // origin.view: the window opens before the seeds start
    // origin.picture: a file the window writes a picture of itself to, drawn
    // off screen, every picture_minutes (empty, or 0: none).
    char picture[260] = "";
    double picture_minutes = 0;
    // NOVA2_VIEW_TEST in the environment (the tests'): the window stays
    // hidden, draws as fast as it can and moves between the seeds and the
    // pages by itself; 2 ("close"): and closes itself after a few frames, as
    // a person might; 3 ("attach"): as 2, and the seeds don't start until a
    // first window has been asked for from outside and has opened.
    u32 test = 0;
};

// The run's side: sets up the seeds' slots and the thread that waits for
// `nova2 watch` and `nova2 save` (false: it can't; the run goes on without),
// with the window open from the start if `at_start`; and at the run's end
// closes the window, if open, and waits for the thread.
bool run_view_open(RunView* rv, u32 seeds, u64 first_seed, const char* title, bool at_start);
void run_view_close(RunView* rv);
// nova2 watch [pid], nova2 save [pid]: asks a run going on this machine to
// open its window, or to save its seeds' worlds.
int cmd_signal(int argc, char** argv);

// A seed's side, on its own thread: its world is there to be watched; the
// figures at a sample; the hold between ticks; and its world is going.
void seed_begin(SeedSlot* s, World* w, Observer* obs);
void seed_publish(SeedSlot* s, u64 tick, u64 births, u32 stage, const u64* stage_tick, u32 permille);
// Between two ticks: true if the seed is asked to save its world now.
bool seed_asked(SeedSlot* s);
inline bool seed_between_ticks(SeedSlot* s) { return (s->want | s->hold) ? seed_asked(s) : false; }
void seed_saved(SeedSlot* s, u64 tick);
// Its world is being saved, or the save is over: the window says so while it waits.
void seed_saving(SeedSlot* s, bool now);
void seed_end(SeedSlot* s);

} // namespace nova2
