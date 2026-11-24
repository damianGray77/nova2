// nova2 view: the viewer (design.md 13.4; build-plan-gate1.md step 12).
//
//   nova2 view [config | snapshot] [--set key=value]... [--ticks N] [--png path] [--size WxH] [--time N] [--fill]
//
// Opens a world from a config file (or the defaults) or a saved snapshot,
// runs N ticks, and either renders one frame headless to a PNG (--png), or
// opens a window and runs the world live: Space pauses, N steps a tick, +
// and - change the ticks run each frame, M changes the memory's colours, the
// arrows and a click choose the place, Esc quits. With a map (gate 3), C
// changes the map's colours, V switches the middle between the map and the
// memory, and G the second row of charts between gate 3's and gate 2's.
// --time N draws each of the views N times off screen and says how long a
// frame took, for the busiest place (the most threads): what makes a big
// place's views slow to draw. --fill puts A.5's replicator in every lax
// place's reset region first (a test world, R7, as nova2 bench's), so a few
// thousand ticks crowd the places for --time.
// Colonies settle by the origin test's criteria (origin.span and the rest).
//
// The window's other use is on a run (run_view.h; build-plan-viewer.md steps
// 4, 7 and 8): `nova2 origin` opens it on a thread of its own, at the start
// (origin.view) or when `nova2 watch [pid]` asks, showing one seed at a
// time, read while that seed holds between two ticks. `nova2 save [pid]`
// asks a run's seeds to save their worlds.
#include <windows.h>

#include <cstdio>
#include <cstdlib> // strtoull
#include <cstring>

#include "commands.h"
#include "run_view.h"
#include "ui/settings.h"
#include "nova2/io.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "ui/canvas.h"
#include "ui/programs.h"
#include "ui/viewer.h"

namespace nova2 {

namespace {

struct ViewArgs {
    const char* file = nullptr;
    const char* png = nullptr;
    u64 ticks = 0;
    u32 time = 0;      // frames to time each view over (0: none)
    bool fill = false; // A.5's replicator in each lax place's reset region
    i32 w = 1280, h = 800;
    const char* sets[64];
    u32 nsets = 0;
};

bool parse_view_args(int argc, char** argv, ViewArgs* a) {
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--set") == 0 && i + 1 < argc && a->nsets < 64) {
            a->sets[a->nsets++] = argv[++i];
        } else if (strcmp(argv[i], "--png") == 0 && i + 1 < argc) {
            a->png = argv[++i];
        } else if (strcmp(argv[i], "--fill") == 0) {
            a->fill = true;
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            a->time = static_cast<u32>(strtoul(argv[++i], nullptr, 10));
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            a->ticks = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &a->w, &a->h) != 2 || a->w < 320 || a->h < 240) {
                fprintf(stderr, "nova2: --size wants WxH, at least 320x240\n");
                return false;
            }
        } else if (argv[i][0] != '-' && !a->file) {
            a->file = argv[i];
        } else {
            fprintf(stderr, "nova2: don't understand '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

bool apply(Config& cfg, const ViewArgs& a, bool snapshot) {
    for (u32 i = 0; i < a.nsets; ++i) {
        const char* eq = strchr(a.sets[i], '=');
        if (!eq || eq == a.sets[i]) {
            fprintf(stderr, "nova2: --set wants key=value, not '%s'\n", a.sets[i]);
            return false;
        }
        const Str key(a.sets[i], static_cast<usize>(eq - a.sets[i]));
        const bool free_key = (key.n > 4 && strncmp(key.p, "run.", 4) == 0) ||
                              (key.n > 8 && strncmp(key.p, "observe.", 8) == 0);
        if (snapshot && !free_key) {
            fprintf(stderr, "nova2: a saved world keeps its settings; only run.* and observe.* can change\n");
            return false;
        }
        cfg.set(key, Str(eq + 1));
    }
    return true;
}

// The world, from a snapshot (told by its magic), a config file, or the defaults.
bool open_world(const ViewArgs& a, Config& cfg, World& w) {
    Array<u8> bytes;
    SnapshotHeader h;
    TextBuf err;
    bool snapshot = false;
    if (a.file) {
        if (!read_file(a.file, &bytes)) {
            fprintf(stderr, "nova2: cannot read %s\n", a.file);
            return false;
        }
        snapshot = bytes.size() >= 8 && memcmp(bytes.data(), "NOVA2SNP", 8) == 0;
    }
    Config::ParseError pe;
    if (snapshot) {
        if (!snapshot_parse(bytes.data(), bytes.size(), &h, &err)) {
            fprintf(stderr, "nova2: %s: %s\n", a.file, err.c_str());
            return false;
        }
        if (h.flags == kSnapshotPlace) {
            fprintf(stderr, "nova2: %s holds one place, not a world: read it with nova2 trace --snapshot\n", a.file);
            return false;
        }
        if (!snapshot_config(h, &cfg, &pe)) {
            fprintf(stderr, "nova2: the snapshot's settings: line %u: %s\n", pe.line, pe.msg);
            return false;
        }
    } else if (a.file && !cfg.parse(Str(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &pe)) {
        fprintf(stderr, "nova2: %s: line %u: %s\n", a.file, pe.line, pe.msg);
        return false;
    }
    if (!apply(cfg, a, snapshot))
        return false;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return false;
    }
    if (snapshot) {
        ByteReader in(bytes.data() + h.world_offset, h.world_size);
        if (!w.load(in, h.version) || w.tick_count() != h.tick) {
            fprintf(stderr, "nova2: %s: the world section doesn't match its settings\n", a.file);
            return false;
        }
    }
    return true;
}

UiInput g_in;
bool g_closed = false;

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        g_closed = true;
        return 0;
    case WM_MOUSEMOVE:
        g_in.mx = static_cast<short>(LOWORD(lp));
        g_in.my = static_cast<short>(HIWORD(lp));
        return 0;
    case WM_LBUTTONDOWN:
        g_in.mx = static_cast<short>(LOWORD(lp));
        g_in.my = static_cast<short>(HIWORD(lp));
        g_in.down = g_in.pressed = true;
        SetCapture(hwnd); // a drag keeps reporting outside the window
        return 0;
    case WM_MOUSEWHEEL:
        g_in.wheel += GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        return 0;
    case WM_LBUTTONUP:
        g_in.down = false;
        g_in.released = true;
        ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (g_in.nkeys < 16)
            g_in.keys[g_in.nkeys++] = static_cast<u32>(wp);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// The viewer's window, w by h; `shown` false keeps it hidden (the tests').
HWND open_window(i32 w, i32 h, bool shown) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = L"Nova2View";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"Nova2View", L"Nova 2", WS_OVERLAPPEDWINDOW | (shown ? WS_VISIBLE : 0), CW_USEDEFAULT,
                           CW_USEDEFAULT, w, h, nullptr, nullptr, wc.hInstance, nullptr);
}

// Text onto the clipboard, for the code pane's copy button (programs.h):
// ASCII, each line ending in CR LF, as Windows' own programs paste it.
void put_on_clipboard(HWND hwnd, const char* s) {
    const usize n = strlen(s);
    usize lines = 0;
    for (usize i = 0; i < n; ++i)
        lines += s[i] == '\n' ? 1 : 0;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (n + lines + 1) * sizeof(wchar_t));
    if (!mem)
        return;
    wchar_t* out = static_cast<wchar_t*>(GlobalLock(mem));
    usize k = 0;
    for (usize i = 0; i < n; ++i) {
        if (s[i] == '\n')
            out[k++] = L'\r';
        out[k++] = static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
    }
    out[k] = 0;
    GlobalUnlock(mem);
    if (!OpenClipboard(hwnd)) {
        GlobalFree(mem);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem); // the clipboard owns it only once it has taken it
    CloseClipboard();
}

void input_done() {
    g_in.pressed = false;
    g_in.released = false;
    g_in.wheel = 0;
    g_in.nkeys = 0;
}

int run_window(World& w, Observer& obs, ViewState& st, const ViewArgs& a) {
    HWND hwnd = open_window(a.w, a.h, true);
    if (!hwnd) {
        fprintf(stderr, "nova2: cannot open a window\n");
        return 1;
    }
    Canvas c;
    TextBuf why;
    if (!c.init(&why)) {
        fprintf(stderr, "nova2: %s\n", why.c_str());
        return 1;
    }
    double last = clock_seconds();
    u64 last_tick = w.tick_count();
    while (!g_closed && !st.quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!st.paused || st.step) {
            const u32 n = st.step ? 1 : st.ticks_per_frame;
            for (u32 k = 0; k < n; ++k)
                w.tick();
            st.step = false;
        }
        const double now = clock_seconds();
        if (now - last >= 0.5) {
            st.tps = static_cast<double>(w.tick_count() - last_tick) / (now - last);
            if (st.tps <= 0)
                st.tps = 0.001; // shown as paused-ish rather than hidden
            last = now;
            last_tick = w.tick_count();
        }
        RECT rc;
        GetClientRect(hwnd, &rc);
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        if (c.begin_window(hwnd, rc.right - rc.left, rc.bottom - rc.top, scale)) {
            draw_viewer(c, st, w, &obs, g_in);
            c.end();
        }
        if (st.copy_asked)
            put_on_clipboard(hwnd, copied_text());
        st.copy_asked = false;
        input_done();
        if (st.paused)
            Sleep(15);
    }
    DestroyWindow(hwnd);
    return 0;
}

// ---------------------------------------------------------------------------
// The window on a run (run_view.h)
// ---------------------------------------------------------------------------

// Asks a seed to hold between two ticks. The window never waits for it: a
// seed in a long tick, or saving its world, would stop the window answering
// for as long (Damian, 2026-11-19: the window locked up for seconds at each
// save late in a run). It asks, goes on answering, and looks again.
void ask_hold(SeedSlot& s) { InterlockedCompareExchange(&s.hold, kHoldAsked, kHoldNone); }

// Whether the seed holds now, waiting at most `ms` for it.
bool seed_holds(SeedSlot& s, DWORD ms) {
    if (s.hold == kHoldHeld)
        return true;
    WaitForSingleObject(static_cast<HANDLE>(s.held), ms);
    return s.hold == kHoldHeld;
}

// Lets a seed go: one that holds carries on, and one only asked is asked no
// more. The seed's side goes by `hold` alone (seed_asked), so an event set
// with nobody waiting for it does no harm.
void let_go(SeedSlot& s) {
    if (InterlockedCompareExchange(&s.hold, kHoldNone, kHoldAsked) == kHoldAsked)
        return;
    InterlockedExchange(&s.hold, kHoldNone);
    SetEvent(static_cast<HANDLE>(s.go));
}

// `nova2 save`: every seed running now is asked to save its world, which
// each does between two ticks, on its own thread.
void ask_saves(RunView& rv) {
    for (u32 k = 0; k < rv.count; ++k)
        if (rv.slots[k].state == kSeedRunning)
            InterlockedOr(&rv.slots[k].want, kWantSave);
}

// Whether one of a pair of signals has been set since it was last looked at.
bool signalled(void* const pair[2]) {
    bool any = false;
    for (u32 i = 0; i < 2; ++i)
        if (pair[i] && WaitForSingleObject(static_cast<HANDLE>(pair[i]), 0) == WAIT_OBJECT_0)
            any = true;
    return any;
}

// The tests' hand on the window (NOVA2_VIEW_TEST): a different page, place
// and pane every frame, and a program picked, so every view reads the held
// seed's world.
void test_views(ViewState& st, World& w, const Observer* obs, u32 frame) {
    static Array<ProgramRow> rows;
    st.place = frame % w.place_count();
    st.inspect = static_cast<Inspect>(frame % 3);
    st.map = frame % 2 == 0;
    st.colour = static_cast<MemColour>(frame % static_cast<u32>(MemColour::count));
    st.map_colour = static_cast<MapColour>(frame % static_cast<u32>(MapColour::count));
    st.power_row = frame % 4 < 2;
    st.family = frame % 8 < 4;
    st.world_figures = frame % 6 < 3;
    st.settings_open = frame % 7 == 6;
    st.seeds_open = frame % 5 == 3;
    st.settings_scroll.first = frame;
    list_programs(w.place(st.place), obs, ProgramSort::age, false, &rows);
    if (!rows.empty()) {
        st.program = rows[frame % rows.size()].uid;
        st.program_place = st.place;
    }
}

// The list's figures, as each seed last published them. True if any seed
// has yet to start.
bool read_tabs(const RunView& rv, const ViewState& st, SeedTab* tabs) {
    bool any_waiting = false;
    for (u32 k = 0; k < st.seed_count; ++k) {
        const SeedSlot& s = rv.slots[k];
        SeedTab& t = tabs[k];
        t.seed = s.seed;
        t.state = s.state;
        t.stage = s.stage;
        t.permille = s.permille;
        t.tick = s.tick;
        t.births = s.births;
        t.saved = s.saved_tick;
        t.saving = (s.want & kWantSave) != 0 || s.saving != 0;
        for (u32 i = 0; i < 6; ++i)
            t.stage_tick[i] = s.stage_tick[i];
        any_waiting = any_waiting || t.state == kTabWaiting;
    }
    return any_waiting;
}

// One window's life, from its opening to its closing, on the run's window
// thread with COM up around it (the canvas must be gone before COM is).
// False: it could not open.
bool watch_run(RunView& rv) {
    g_closed = false;
    g_in = UiInput();
    HWND hwnd = open_window(1280, 800, rv.test == 0);
    Canvas c;
    TextBuf why;
    if (!hwnd || !c.init(&why)) {
        if (hwnd)
            DestroyWindow(hwnd);
        return false;
    }
    SetEvent(static_cast<HANDLE>(rv.ready)); // seeds that wait for a first window start once it is there
    static SeedTab tabs[kMostSeedTabs];
    ViewState st;
    st.can_save = true;
    st.title = rv.title;
    st.settings = rv.settings;
    st.watching = true;
    st.seeds = tabs;
    st.seed_count = rv.count < kMostSeedTabs ? rv.count : kMostSeedTabs;
    double next = 0, rate_at = clock_seconds();
    u64 rate_tick = 0;
    u32 rate_seed = ~0u;
    u32 frames = 0, held_frames = 0; // the tests' counts
    u64 watched = 0;                 // the seeds shown, a bit each
    double held_secs = 0;            // the time seeds were held, summed
    // The seed asked to hold for the next frame (null: none), which of the
    // tabs it is, and when it was asked.
    SeedSlot* asked = nullptr;
    u32 asked_tab = 0;
    double asked_at = 0;
    bool any_waiting = false;
    double picture_at = clock_seconds() + 20; // the first picture, once the views have filled
    while (!g_closed && !st.quit && !rv.done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Signals that come while the window is open: a save goes to the
        // seeds; a second `nova2 watch` brings the window to the front.
        if (signalled(rv.save))
            ask_saves(rv);
        if (signalled(rv.watch) && !rv.test) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        if (!asked) {
            if (!rv.test && clock_seconds() < next) {
                Sleep(5);
                continue;
            }
            any_waiting = read_tabs(rv, st, tabs);
            // The seed shown: the one chosen, while it runs; then the first
            // that does. (The tests' window moves to the next running seed a
            // frame.) It is asked to hold for this frame.
            u32 from = rv.test ? st.seed_shown + 1 : st.seed_shown;
            for (u32 k = 0; k < st.seed_count; ++k) {
                const u32 at = (from + k) % st.seed_count;
                if (tabs[at].state == kTabRunning) {
                    st.seed_shown = at;
                    break;
                }
            }
            if (st.seed_shown < st.seed_count && tabs[st.seed_shown].state == kTabRunning) {
                asked = &rv.slots[st.seed_shown];
                asked_tab = st.seed_shown;
                asked_at = clock_seconds();
                ask_hold(*asked);
            }
        }
        // The frame waits for the seed to come between two ticks; the window
        // doesn't. A wait of half a second or more is drawn: a seed saving
        // its world, or a tick that long.
        bool held = false;
        if (asked) {
            held = seed_holds(*asked, 5);
            if (!held && asked->state != kSeedRunning) { // it ended first
                let_go(*asked);
                asked = nullptr;
                continue;
            }
            if (!held && (clock_seconds() - asked_at < 0.5 || clock_seconds() < next))
                continue; // the frame before stays up
            if (!held)
                any_waiting = read_tabs(rv, st, tabs);
        }
        // A picture of the window to a file (origin.picture), drawn off
        // screen from the seed as it is held: the screen itself can't be
        // read with the display asleep, or the window covered or shrunk.
        if (held && rv.picture[0] && rv.picture_minutes > 0 && clock_seconds() >= picture_at) {
            RECT prc;
            GetClientRect(hwnd, &prc);
            const i32 pw = prc.right > prc.left ? prc.right - prc.left : 1280;
            const i32 ph = prc.bottom > prc.top ? prc.bottom - prc.top : 800;
            char part[300];
            snprintf(part, sizeof part, "%s.part", rv.picture);
            UiInput none;
            if (c.begin_bitmap(pw, ph, static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f)) {
                draw_viewer(c, st, *asked->w, asked->obs, none);
                c.end();
                if (c.write_png(part))
                    MoveFileExA(part, rv.picture, MOVEFILE_REPLACE_EXISTING);
            }
            picture_at = clock_seconds() + rv.picture_minutes * 60;
        }
        RECT rc;
        GetClientRect(hwnd, &rc);
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        const bool drawing = c.begin_window(hwnd, rc.right - rc.left, rc.bottom - rc.top, scale);
        const double t0 = clock_seconds();
        double took = 0;
        if (drawing && held) {
            // The seed is between two ticks, and stays there while its world
            // is read; the frame is put on screen after it is let go.
            SeedSlot* s = asked;
            const u64 tick = s->w->tick_count();
            if (rate_seed != st.seed_shown || tick < rate_tick) {
                rate_seed = st.seed_shown;
                rate_tick = tick;
                rate_at = t0;
                st.tps = 0;
            } else if (t0 - rate_at >= 0.5) {
                st.tps = static_cast<double>(tick - rate_tick) / (t0 - rate_at);
                rate_tick = tick;
                rate_at = t0;
            }
            if (rv.test)
                test_views(st, *s->w, s->obs, frames);
            draw_viewer(c, st, *s->w, s->obs, g_in);
            if (st.save_asked) // the window's own button: this seed saves its world as it is let go
                InterlockedOr(&s->want, kWantSave);
            st.save_asked = false;
            let_go(*s);
            asked = nullptr;
            took = clock_seconds() - t0;
            if (st.copy_asked) // the code pane's copy button: its text is made, and the seed let go
                put_on_clipboard(hwnd, copied_text());
            st.copy_asked = false;
            held_secs += took;
            ++held_frames;
            watched |= 1ull << (st.seed_shown % 64);
        } else if (drawing && asked) {
            draw_waiting(c, st, g_in,
                         asked->saving ? "this seed is saving its world" : "waiting for this seed's tick to end");
        } else if (drawing) {
            draw_waiting(c, st, g_in, any_waiting ? "waiting for a seed to start" : "every seed has ended");
        }
        if (held && asked) { // nothing could be drawn: the seed carries on all the same
            let_go(*asked);
            asked = nullptr;
        }
        if (drawing)
            c.end();
        input_done();
        ++frames;
        // Another seed chosen while this one was waited for: it is asked no more.
        if (asked && st.seed_shown != asked_tab) {
            let_go(*asked);
            asked = nullptr;
        }
        // Twenty frames a second at most, and fewer if reading a frame takes
        // the watched seed more than a tenth of its time (build-plan-viewer.md 7).
        next = clock_seconds() + (took * 10 > 0.05 ? took * 10 : 0.05);
        if (rv.test >= 2 && frames == 5)
            g_closed = true; // as a person closing the window mid-run
    }
    if (asked) // the window closes with a seed asked to hold, or holding
        let_go(*asked);
    DestroyWindow(hwnd);
    if (rv.test) {
        u32 seeds = 0;
        for (u32 k = 0; k < 64; ++k)
            seeds += (watched >> k) & 1;
        fprintf(stderr,
                "nova2: view test: %u frames, %u of a held seed, %u seeds watched, the window %s; a seed was held "
                "%.2f ms a frame\n",
                frames, held_frames, seeds, g_closed ? "closed before the run ended" : "open to the run's end",
                held_frames ? 1000 * held_secs / held_frames : 0.0);
    }
    return true;
}

// The run's window thread, from the run's start to its end: it opens the
// window at the start if the run asks (origin.view), and whenever `nova2
// watch` signals; between windows it waits, and passes `nova2 save` on.
DWORD WINAPI run_view_thread(void* arg) {
    RunView& rv = *static_cast<RunView*>(arg);
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    bool open_now = rv.at_start;
    for (;;) {
        if (open_now) {
            if (!com || !watch_run(rv))
                fprintf(stderr, "nova2: cannot open the window; the run goes on without it\n");
            SetEvent(static_cast<HANDLE>(rv.ready)); // open or not, no seed waits longer
            (void)signalled(rv.watch);               // asked for again while it was open: it was there
            open_now = false;
        }
        if (rv.done)
            break;
        HANDLE hs[5];
        u32 kind[5], n = 0;
        hs[n] = static_cast<HANDLE>(rv.quit);
        kind[n++] = 0;
        for (u32 i = 0; i < 2; ++i) {
            if (rv.watch[i]) {
                hs[n] = static_cast<HANDLE>(rv.watch[i]);
                kind[n++] = 1;
            }
            if (rv.save[i]) {
                hs[n] = static_cast<HANDLE>(rv.save[i]);
                kind[n++] = 2;
            }
        }
        const DWORD r = WaitForMultipleObjects(n, hs, FALSE, INFINITE);
        if (r >= n || kind[r] == 0)
            break;
        if (kind[r] == 1)
            open_now = true;
        else
            ask_saves(rv);
    }
    if (com)
        CoUninitialize();
    return 0;
}

// The name of a run's signal: "watch" or "save", of the run whose process
// is `pid`, or (pid 0) the one any run answers to.
void signal_name(char* out, usize size, const char* what, unsigned long pid) {
    if (pid)
        snprintf(out, size, "Local\\nova2-%s-%lu", what, pid);
    else
        snprintf(out, size, "Local\\nova2-%s", what);
}

} // namespace

bool run_view_open(RunView* rv, u32 seeds, u64 first_seed, const char* title, bool at_start) {
    rv->slots = static_cast<SeedSlot*>(calloc(seeds ? seeds : 1, sizeof(SeedSlot)));
    if (!rv->slots)
        return false;
    rv->count = seeds;
    for (u32 k = 0; k < seeds; ++k) {
        SeedSlot& s = rv->slots[k];
        s.seed = first_seed + k;
        s.saved_tick = kNoSave;
        for (u32 i = 0; i < 6; ++i)
            s.stage_tick[i] = ~0ull;
        s.held = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s.go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    snprintf(rv->title, sizeof rv->title, "%s", title);
    char test[16];
    const DWORD n = GetEnvironmentVariableA("NOVA2_VIEW_TEST", test, sizeof test);
    rv->test = n == 0 || n >= sizeof test ? 0 : strcmp(test, "close") == 0 ? 2 : strcmp(test, "attach") == 0 ? 3 : 1;
    rv->at_start = at_start;
    rv->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    rv->quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // The signals: this run's own, by its process's number, and (not under
    // the tests) the ones any run answers to.
    const unsigned long pid = GetCurrentProcessId();
    char name[64];
    for (u32 i = 0; i < (rv->test ? 1u : 2u); ++i) {
        signal_name(name, sizeof name, "watch", i == 0 ? pid : 0);
        rv->watch[i] = CreateEventA(nullptr, FALSE, FALSE, name);
        signal_name(name, sizeof name, "save", i == 0 ? pid : 0);
        rv->save[i] = CreateEventA(nullptr, FALSE, FALSE, name);
    }
    rv->thread = CreateThread(nullptr, 0, run_view_thread, rv, 0, nullptr);
    if (!rv->thread) {
        run_view_close(rv);
        return false;
    }
    fprintf(stderr, "nova2: this run is process %lu: `nova2 watch %lu` opens its window, `nova2 save %lu` saves its "
                    "seeds' worlds\n", pid, pid, pid);
    if (at_start || rv->test == 3)
        WaitForSingleObject(static_cast<HANDLE>(rv->ready), INFINITE);
    return true;
}

void run_view_close(RunView* rv) {
    InterlockedExchange(&rv->done, 1);
    if (rv->quit)
        SetEvent(static_cast<HANDLE>(rv->quit));
    if (rv->thread) {
        WaitForSingleObject(static_cast<HANDLE>(rv->thread), INFINITE);
        CloseHandle(static_cast<HANDLE>(rv->thread));
        rv->thread = nullptr;
    }
    void** const events[6] = {&rv->ready, &rv->quit, &rv->watch[0], &rv->watch[1], &rv->save[0], &rv->save[1]};
    for (u32 i = 0; i < 6; ++i) {
        if (*events[i])
            CloseHandle(static_cast<HANDLE>(*events[i]));
        *events[i] = nullptr;
    }
    for (u32 k = 0; k < rv->count; ++k) {
        CloseHandle(static_cast<HANDLE>(rv->slots[k].held));
        CloseHandle(static_cast<HANDLE>(rv->slots[k].go));
    }
    free(rv->slots);
    rv->slots = nullptr;
    rv->count = 0;
}

void seed_begin(SeedSlot* s, World* w, Observer* obs) {
    s->w = w;
    s->obs = obs;
    s->state = kSeedRunning;
}

void seed_publish(SeedSlot* s, u64 tick, u64 births, u32 stage, const u64* stage_tick, u32 permille) {
    s->tick = tick;
    s->births = births;
    s->stage = stage;
    s->permille = permille;
    for (u32 i = 1; i < 6; ++i)
        s->stage_tick[i] = stage_tick[i];
}

bool seed_asked(SeedSlot* s) {
    // Asked to hold: it holds until the window lets it go, which is `hold`
    // leaving kHoldHeld; the event only wakes it sooner.
    if (s->hold && InterlockedCompareExchange(&s->hold, kHoldHeld, kHoldAsked) == kHoldAsked) {
        SetEvent(static_cast<HANDLE>(s->held));
        while (s->hold == kHoldHeld)
            WaitForSingleObject(static_cast<HANDLE>(s->go), 20);
    }
    if (s->want & kWantSave) {
        InterlockedAnd(&s->want, ~kWantSave);
        return true;
    }
    return false;
}

void seed_saved(SeedSlot* s, u64 tick) { s->saved_tick = tick; }

void seed_saving(SeedSlot* s, bool now) { s->saving = now ? 1 : 0; }

void seed_end(SeedSlot* s) { s->state = kSeedDone; }

int cmd_signal(int argc, char** argv) {
    const bool save = strcmp(argv[1], "save") == 0;
    const char* const what = save ? "save" : "watch";
    unsigned long pid = 0;
    if (argc > 3 || (argc == 3 && (argv[2][0] < '1' || argv[2][0] > '9'))) {
        fprintf(stderr, "nova2: %s takes a run's process number, or nothing for the run that is going\n", what);
        return 2;
    }
    if (argc == 3)
        pid = strtoul(argv[2], nullptr, 10);
    char name[64];
    signal_name(name, sizeof name, what, pid);
    // A run that has only just started may not be listening yet.
    HANDLE e = nullptr;
    for (u32 tries = 0; tries < 20 && !e; ++tries) {
        e = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
        if (!e)
            Sleep(100);
    }
    if (!e) {
        if (pid)
            fprintf(stderr, "nova2: no run with process number %lu is listening\n", pid);
        else
            fprintf(stderr, "nova2: no run is listening (only `nova2 origin` and `nova2 busy` runs do)\n");
        return 1;
    }
    SetEvent(e);
    CloseHandle(e);
    if (pid)
        printf("asked run %lu to %s\n", pid, save ? "save its seeds' worlds" : "open its window");
    else
        printf("asked the run to %s\n", save ? "save its seeds' worlds" : "open its window");
    return 0;
}

// A run's save of a seed's world (origin.cpp: seed-S-tick-T.world.snap) has
// the test's save beside it, with what the world's leaves out: the counters
// kept for reports and the observers' memory. Opened with them, the views
// know the lineages, the colonies with their clocks, and the samples the
// run had gathered, not only what happens while they watch (gate 3's step 6
// left that open: a colony showed as settled only after origin.span ticks
// watched). True if `path` is such a save and its test's was read into the
// world's counters and `obs`, which must be new and built under the run's
// settings. False, saying why if there is a test's save that can't be used:
// `obs` may then hold half of one, and another must be used.
static bool load_test_beside(const char* path, World& w, Observer& obs) {
    static const char kWorld[] = ".world.snap";
    const usize n = strlen(path), k = sizeof kWorld - 1;
    char test[1024];
    if (n < k || strcmp(path + n - k, kWorld) != 0 || n + 1 >= sizeof test)
        return false;
    memcpy(test, path, n - k);
    memcpy(test + n - k, ".test.snap", 11);
    FileIn f;
    if (!f.open(test))
        return false; // none beside it: a world saved alone
    u64 tag = 0, seed = 0, tick = 0, size = 0, bytes = 0;
    if (!f.get_u64(&tag) || !f.get_u64(&seed) || !f.get_u64(&tick) || !f.get_u64(&size) || tag != kTestSaveTag ||
        seed != w.seed() || tick != w.tick_count() || !f.get_u64(&bytes) || bytes > f.left()) {
        fprintf(stderr, "nova2: %s is not this world's test save, or is another build's: the views start their "
                        "records afresh\n", test);
        return false;
    }
    Array<u8> counters;
    counters.resize(static_cast<usize>(bytes));
    f.get(counters.data(), counters.size());
    ByteReader in(counters.data(), counters.size());
    if (!f.ok() || !w.load_counters(in)) {
        fprintf(stderr, "nova2: %s: its counters don't match the world: the views start their records afresh\n", test);
        return false;
    }
    if (!obs.load(f)) {
        // Gathered under other observer settings (--set), or its rows are
        // in the run's record files (origin.records), which belong to the
        // run and are not read here.
        fprintf(stderr, "nova2: %s: the observers' memory can't be read here (its rows are in the run's record "
                        "files, or the observers' settings differ): the views start their records afresh\n", test);
        return false;
    }
    return true;
}

// --time: each view drawn off screen `a.time` times on the busiest place,
// and the mean time a frame took to draw (what the window on a run holds
// its seed for) and to finish (the pixels drawn as well, off screen here).
static int time_views(World& w, Observer& obs, ViewState& base, const ViewArgs& a) {
    u32 busy = 0;
    for (u32 i = 1; i < w.place_count(); ++i)
        busy = w.place(i).os.live_threads() > w.place(busy).os.live_threads() ? i : busy;
    const Place& p = w.place(busy);
    u64 programs = 0, threads = 0;
    u32 strict = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        programs += w.place(i).os.live_programs();
        threads += w.place(i).os.live_threads();
        strict += w.place(i).mem.strict() ? 1 : 0;
    }
    printf("tick %llu, %u places (%u strict): %llu programs, %llu threads\n",
           static_cast<unsigned long long>(w.tick_count()), w.place_count(), strict,
           static_cast<unsigned long long>(programs), static_cast<unsigned long long>(threads));
    printf("place %u: %u cores, %u memory devices, %u pages of %u bytes, %u programs (room for %u), %u threads\n", busy,
           p.core_count(), p.mem.device_count(), p.mem.page_count(), p.mem.page_size(), p.os.live_programs(),
           p.os.program_capacity(), p.os.live_threads());
    // The program with the most threads, for the views of one program.
    Array<ProgramRow> rows;
    list_programs(p, &obs, ProgramSort::threads, true, &rows);
    // The parts the views lean on, alone: the list unsorted by a dear key,
    // the whole list, and the observer's record of each program.
    {
        Array<ProgramRow> some;
        const u32 n = a.time;
        double t0 = clock_seconds();
        for (u32 k = 0; k < n; ++k)
            list_programs(p, &obs, ProgramSort::age, false, &some, false);
        double t1 = clock_seconds();
        for (u32 k = 0; k < n; ++k)
            list_programs(p, &obs, ProgramSort::age, false, &some, true);
        double t2 = clock_seconds();
        u32 known = 0;
        for (u32 k = 0; k < n; ++k)
            for (u32 i = 0; i < some.size(); ++i)
                known += obs.program(some[i].uid) ? 1 : 0;
        double t3 = clock_seconds();
        u32 mixed = 0;
        for (u32 k = 0; k < n; ++k)
            for (u32 i = 0; i < some.size(); ++i)
                mixed ^= lineage_colour(some[i].uid);
        double t4 = clock_seconds();
        printf("  parts: the list by age %.2f ms, whole %.2f ms; %u records looked up %.2f ms (%u known), "
               "coloured %.2f ms\n",
               1000 * (t1 - t0) / n, 1000 * (t2 - t1) / n, static_cast<u32>(some.size()), 1000 * (t3 - t2) / n,
               known / n, 1000 * (t4 - t3) / n);
        (void)mixed;
    }
    struct View {
        const char* name;
        bool map, settings, family, picked;
        MapColour map_colour;
        MemColour colour;
        Inspect inspect;
        // Gate 4's (power_view.h), timed only in a world with sources or
        // heat: the colouring, with the sources, the proximity links and
        // gate 4's row of charts. The views above are timed without them.
        PowerColour power = PowerColour::none;
    };
    const View views[] = {
        {"settings window (the bar and charts' baseline)", false, true, false, false, MapColour::population,
         MemColour::owner, Inspect::place},
        {"map: population; inspector: place", true, false, false, false, MapColour::population, MemColour::owner,
         Inspect::place},
        {"map: lineage; inspector: place", true, false, false, false, MapColour::lineage, MemColour::owner,
         Inspect::place},
        {"map: queues; inspector: place", true, false, false, false, MapColour::queues, MemColour::owner,
         Inspect::place},
        {"memory: owner; inspector: place", false, false, false, false, MapColour::population, MemColour::owner,
         Inspect::place},
        {"memory: lineage; inspector: place", false, false, false, false, MapColour::population, MemColour::lineage,
         Inspect::place},
        {"memory: owner; inspector: figures", false, false, false, false, MapColour::population, MemColour::owner,
         Inspect::figures},
        {"memory: owner; inspector: programs", false, false, false, false, MapColour::population, MemColour::owner,
         Inspect::programs},
        {"memory: owner; programs, the busiest's code", false, false, false, true, MapColour::population,
         MemColour::owner, Inspect::programs},
        {"memory: owner; programs, the busiest's family", false, false, true, true, MapColour::population,
         MemColour::owner, Inspect::programs},
        {"map: population; programs, the busiest's code", true, false, false, true, MapColour::population,
         MemColour::owner, Inspect::programs},
        {"map: voltage, sources, grid; inspector: place", true, false, false, false, MapColour::population,
         MemColour::owner, Inspect::place, PowerColour::voltage},
        {"map: temperature, sources, grid; inspector: place", true, false, false, false, MapColour::population,
         MemColour::owner, Inspect::place, PowerColour::temperature},
    };
    Canvas c;
    TextBuf why;
    if (!c.init(&why)) {
        fprintf(stderr, "nova2: %s\n", why.c_str());
        return 1;
    }
    UiInput none;
    for (const View& v : views) {
        ViewState st = base;
        st.place = busy;
        st.map = v.map && w.map().placed();
        st.settings_open = v.settings;
        st.map_colour = v.map_colour;
        st.colour = v.colour;
        st.inspect = v.inspect;
        st.family = v.family;
        const bool gate4 = v.power != PowerColour::none;
        if (gate4 && !gate4_world(w))
            continue;
        st.power_colour = v.power;
        st.show_sources = st.show_grid = st.heat_row = gate4;
        if (v.picked && rows.size()) {
            st.program = rows[0].uid;
            st.program_place = busy;
            st.picked = rows[0];
            st.picked_alive = true;
        }
        // The least of the frames, the run beside this one (a watched run
        // takes every core) making the rest noisy; and the parts' means.
        double least = 1e30, ended = 0;
        PanelTimes parts;
        for (u32 k = 0; k < a.time + 1; ++k) { // the first frame makes the fonts and is not counted
            if (!c.begin_bitmap(a.w, a.h, 1.0f)) {
                fprintf(stderr, "nova2: cannot render off screen\n");
                return 1;
            }
            PanelTimes one;
            time_panels(&one);
            const double t0 = clock_seconds();
            draw_viewer(c, st, w, &obs, none);
            const double t1 = clock_seconds();
            time_panels(nullptr);
            c.end();
            const double t2 = clock_seconds();
            if (k) {
                least = t1 - t0 < least ? t1 - t0 : least;
                ended += t2 - t0;
                parts.top += one.top;
                parts.places += one.places;
                parts.middle += one.middle;
                parts.inspector += one.inspector;
                parts.charts += one.charts;
            }
        }
        const double ms = 1000.0 / a.time;
        printf("  %-48s least %6.2f ms, mean %6.2f with the pixels; top %5.2f places %5.2f middle %5.2f "
               "inspector %5.2f charts %5.2f\n",
               v.name, 1000 * least, ended * ms, parts.top * ms, parts.places * ms, parts.middle * ms,
               parts.inspector * ms, parts.charts * ms);
    }
    return 0;
}

int cmd_view(int argc, char** argv) {
    ViewArgs a;
    if (!parse_view_args(argc, argv, &a))
        return 2;
    Config cfg;
    World w;
    // The settings the world and its observers read, for the settings window.
    Settings book;
    settings_begin(&book);
    if (!open_world(a, cfg, w)) {
        settings_end(&book, cfg);
        return 2;
    }
    ObserverSettings os;
    os.sample_every = key_u32(cfg, "observe.sample_every");
    os.write_every = key_u32(cfg, "observe.write_every");
    os.genome_key_every = key_u32(cfg, "observe.genome_key_every");
    os.group_threshold = key_f64(cfg, "observe.group_threshold");
    os.min_genome = key_u32(cfg, "observe.min_genome");
    os.keep_generations = key_u32(cfg, "observe.keep_generations");
    os.lineage_generations = os.keep_generations; // a lineage with a row is one whose genomes could be kept
    os.day = key_u32(cfg, "supply.day");
    os.turnover.window = key_u64(cfg, "observe.turnover_window");
    os.turnover.members = key_u32(cfg, "observe.turnover_members");
    os.forget = key_flag(cfg, "observe.forget"); // as the run that saved it, so its test save reads back
    os.keep_facts = false;
    StageCriteria stages;
    read_stages(cfg, &stages);
    const u32 threads = key_u32(cfg, "run.threads");
    settings_end(&book, cfg);
    key_accept_known(cfg);
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    // The observer: a run's, read from the test's save beside the world's
    // if there is one, or else a new one.
    Observer kept, fresh;
    kept.init(os);
    kept.settle_by(stages);
    w.watch(&kept);
    const bool carried = a.file && load_test_beside(a.file, w, kept);
    if (!carried) {
        fresh.init(os);
        fresh.settle_by(stages);
        w.watch(&fresh);
    }
    Observer& obs = carried ? kept : fresh;
    if (carried)
        printf("read the run's test save beside it: %llu samples, %u lineages and %u colonies known\n",
               static_cast<unsigned long long>(obs.samples.size()), obs.lineage_count(), obs.colony_count());
    if (a.fill) {
        // A.5's 14-byte replicator on a lax place (as nova2 bench's).
        static const u8 kLax[14] = {encode(kMov, kImm), 14, encode(kSto, kB), encode(kSys, kImm), kAlloc,
                                    encode(kMov, kC), encode(kSto, kB), encode(kCpy, kImm), 14, encode(kMov, kD),
                                    encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
        for (u32 i = 0; i < w.place_count(); ++i)
            if (!w.place(i).mem.strict())
                memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
    }
    w.start_threads(threads);
    for (u64 t = 0; t < a.ticks; ++t)
        w.tick();

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        fprintf(stderr, "nova2: COM failed to start\n");
        return 1;
    }
    ViewState st;
    st.title = a.file ? a.file : "a new world";
    st.settings = &book;
    int rc = 0;
    if (a.time) {
        rc = time_views(w, obs, st, a);
    } else if (a.png) {
        Canvas c;
        TextBuf why;
        UiInput none;
        if (!c.init(&why) || !c.begin_bitmap(a.w, a.h, 1.0f)) {
            fprintf(stderr, "nova2: %s\n", why.size() ? why.c_str() : "cannot render off screen");
            rc = 1;
        } else {
            draw_viewer(c, st, w, &obs, none);
            c.end();
            if (!c.write_png(a.png)) {
                fprintf(stderr, "nova2: cannot write %s\n", a.png);
                rc = 1;
            } else {
                printf("wrote %s (%dx%d, tick %llu)\n", a.png, a.w, a.h, static_cast<unsigned long long>(w.tick_count()));
            }
        }
    } else {
        rc = run_window(w, obs, st, a);
    }
    CoUninitialize();
    return rc;
}

} // namespace nova2
