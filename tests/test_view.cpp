// The viewer (app/ui; design.md 13.4; build-plan-gate1.md step 12,
// build-plan-gate2.md step 8, build-plan-gate3.md step 6): a headless render
// through WIC matches a stored image pixel for pixel; the same frame renders
// the same twice; and a world opened from a snapshot renders exactly as the
// world it was saved from.
//
// At gate 2 the stored image is tests/golden/view_gate2.png: a busy world
// under a varying supply, with no map, drawn at a moment its inspected place
// is part way through a drop-out. (Gate 1's, view_gate1.png, drew the gate 1
// layout.) Gate 3 adds tests/golden/view_gate3.png: a bigger world on a
// landscape map with gate 3's traffic, its map coloured by queues and drops,
// drawn with messages part way across links and a network device off; and
// view_gate3_zoom.png, the same moment with the map zoomed in four times on
// the inspected place (the viewer's own build, build-plan-viewer.md), and
// view_programs.png, the same moment with the inspector on the programs of
// its busiest lax place, a program picked and a thread of its followed;
// view_family.png, that program's family; and view_figures.png and
// view_figures_world.png, that place's figures and the world's. If
// one is missing, this writes it and fails, so a person looks at it before
// it counts. Renders depend on this machine's fonts (Segoe UI, Consolas):
// another Windows version may draw text a pixel differently.
//
// Gate 2's snapshot is taken mid-drop-out, with devices cut out: the views
// open on it, and draw every place in every memory colouring as they drew
// the world it came from. Gate 3's is taken mid-hop, with a network device
// off: the views draw its map in every colouring, its memory in every
// colouring and gate 2's row of charts as they drew its world.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "traffic_world.h"

#include "check.h"
#include "nova2/io.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "ui/canvas.h"
#include "ui/figures.h"
#include "ui/settings.h"
#include "ui/theme.h"
#include "ui/viewer.h"
#include "ui/widgets.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 6\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0.3\n"
                           "region.retry = 3\n"
                           "observe.sample_every = 5\n";

// Gate 2's: the same small world under a varying supply, with a short day,
// deep nights, swings, and drop-outs often, half of them blackouts; with no
// map, as gate 2 had.
const char* const kGate2 = "world.places = 6\n"
                           "world.map = none\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0.3\n"
                           "region.retry = 3\n"
                           "observe.sample_every = 5\n"
                           "supply.sag = 50\n"
                           "supply.day = 600\n"
                           "supply.night = 0.2\n"
                           "supply.swing = 0.25\n"
                           "supply.swing_period = 200\n"
                           "supply.dropouts = 1000\n"
                           "supply.dropout.min = 5\n"
                           "supply.dropout.max = 120\n"
                           "supply.blackout_share = 0.5\n";

// Gate 3's: gate 2's world with more places, on a landscape map (the
// default), and gate 3's traffic in every place (traffic_world.h).
const char* const kGate3 = "world.places = 30\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0.3\n"
                           "region.retry = 3\n"
                           "observe.sample_every = 5\n"
                           "supply.sag = 50\n"
                           "supply.day = 600\n"
                           "supply.night = 0.2\n"
                           "supply.swing = 0.25\n"
                           "supply.swing_period = 200\n"
                           "supply.dropouts = 1000\n"
                           "supply.dropout.min = 5\n"
                           "supply.dropout.max = 120\n"
                           "supply.blackout_share = 0.5\n";

const i32 kW = 1280, kH = 800; // the window's default size

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// test_boot.cpp's booter: as a thread it DWRITEs itself to offset 0 and
// waits; booted, it copies itself and SPAWNs the copy.
const u8 kBooter[34] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                        kDwrite,            encode(kJnz, kImm), 19,                 encode(kMov, kImm),
                        2,                  encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSys, kImm), kAlloc,             encode(kMov, kIp),  encode(kSub, kImm),
                        18,                 encode(kSto, kB),   encode(kCpy, kImm), 34,
                        encode(kMov, kD),   encode(kSys, kImm), kSpawn,             encode(kPush, kA),
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4)};

// A world from `text`: a replicator in every lax region, with `booters` a
// booter placed by hand in every place, and with `traffic` gate 3's traffic
// (R7: a test world).
bool build(World& w, Config& cfg, const char* text, bool booters, bool traffic = false) {
    Config::ParseError pe;
    if (!cfg.parse(text, &pe))
        return false;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  view: %s %s\n", why.c_str(), cfg.value_error());
        return false;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        if (booters) {
            const u32 at = p.mem.alloc(1, 64, 2, 0);
            if (at) {
                memcpy(p.mem.bytes() + at, kBooter, sizeof kBooter);
                p.os.place_by_hand(1, at, at, 0);
            }
        }
        if (traffic && place_traffic(p, w.place_count(), i) != 3)
            return false;
    }
    return true;
}

bool render(Canvas& c, World& w, const Observer* obs, ViewState& st, Array<u32>* px) {
    UiInput none;
    if (!c.begin_bitmap(kW, kH, 1.0f))
        return false;
    draw_viewer(c, st, w, obs, none);
    c.end();
    return c.read_pixels(px);
}

// Whether place i is part way through a drop-out: one started at least
// `into` ticks before the next tick and goes on through it.
bool mid_dropout(World& w, u32 i, u64 into) {
    Dropout d;
    d.length = 0;
    const u64 t = w.tick_count();
    return w.place(i).supply.dropout(t, &d) < kCapacityOne && d.length && d.start + into <= t;
}

// Whether some device of place i is cut out.
bool cut_out(World& w, u32 i) {
    Place& p = w.place(i);
    for (u32 k = 0; k < p.core_count(); ++k)
        if (!p.cores[k].working)
            return true;
    for (u32 d = 0; d < p.mem.device_count(); ++d)
        if (!p.memory_state[d].on)
            return true;
    return !p.disk_state.on;
}

// The counters a snapshot doesn't keep (reports, not state), cleared.
void forget_counters(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        p.births = p.deaths = 0;
        p.sched.instructions = p.sched.switches = 0;
        p.region.started = p.region.faulted = p.region.capped = p.region.preempted = p.region.instructions = 0;
        p.region.paths_new = p.region.paths_ended = 0;
        // Gate 2's.
        p.cutouts[0] = p.cutouts[1] = p.off_ticks[0] = p.off_ticks[1] = 0;
        p.cut_threads = p.paused_threads = p.disk_cutouts = 0;
        p.memory_cutouts = p.memory_off_ticks = p.faded_threads = 0;
        p.cold_starts = p.cold_written = 0;
    }
    w.net().clear_counts(); // gate 3's: the network's counts and drops
}

// World a saved to a snapshot (in `out`, its settings read back into cfg2)
// and opened again as b.
bool reopen(World& a, Config& cfg, ByteWriter& out, Config& cfg2, World& b) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(a.seed(), a.tick_count(), text.view(), out);
    a.save(out);
    snapshot_finish(out);
    SnapshotHeader h;
    TextBuf err;
    if (!snapshot_parse(out.data(), out.size(), &h, &err))
        return false;
    Config::ParseError pe;
    if (!cfg2.parse(h.config_text, &pe))
        return false;
    TextBuf why;
    if (!b.init(cfg2, &why))
        return false;
    ByteReader in(out.data() + h.world_offset, h.world_size);
    return b.load(in);
}

// A render, twice the same, compared with tests/golden/<name>.png (and
// written as <name>_actual.png in the build tree, for a person to look at).
// With no stored image, writes it and fails. The number of pixels that
// differ, or ~0 if it couldn't compare.
u32 against_golden(Canvas& c, World& w, const Observer* obs, ViewState& st, const char* name) {
    Array<u32> a, b;
    if (!render(c, w, obs, st, &a) || !render(c, w, obs, st, &b))
        return ~0u;
    CHECK(a.size() == static_cast<usize>(kW) * kH && memcmp(a.data(), b.data(), a.size() * 4) == 0); // the same twice
    char path[512];
    snprintf(path, sizeof path, "%s_actual.png", name);
    CHECK(c.write_png(path));
    snprintf(path, sizeof path, "%s/tests/golden/%s.png", NOVA2_SOURCE_DIR, name);
    Array<u32> g;
    i32 gw = 0, gh = 0;
    if (!c.load_png(path, &g, &gw, &gh)) {
        CHECK(c.write_png(path));
        fprintf(stderr, "  view: no stored image yet; wrote %s. Look at it, then run again.\n", path);
        return ~0u;
    }
    if (gw != kW || gh != kH) {
        fprintf(stderr, "  view: %s is %dx%d, not %dx%d\n", path, gw, gh, kW, kH);
        return ~0u;
    }
    u32 differ = 0;
    for (usize i = 0; i < a.size(); ++i)
        differ += a[i] != g[i] ? 1 : 0;
    if (differ)
        fprintf(stderr, "  view: %u pixels differ from %s (see %s_actual.png)\n", differ, path, name);
    return differ;
}

// The golden render: a busy gate 2 world, watched, drawn at the first
// moment after tick 300 that a lax place with programs alive is 15 ticks or
// more into a drop-out, with a device cut out, inspecting that place.
void test_golden() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate2, true));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    os.turnover.window = 100; // short windows, so several end in the run
    os.turnover.members = 2;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    ViewState st;
    st.title = "the golden test world";
    st.colour = MemColour::lineage;
    bool found = false;
    while (!found && w.tick_count() < 3000) {
        w.tick();
        if (w.tick_count() < 300)
            continue;
        for (u32 i = 0; i < w.place_count() && !found; ++i) {
            Place& p = w.place(i);
            if (!p.mem.strict() && p.os.live_programs() && mid_dropout(w, i, 15) && cut_out(w, i)) {
                st.place = i;
                found = true;
            }
        }
    }
    CHECK(found);
    CHECK(obs.windows.size() > 0 && obs.cold_starts > 0);
    CHECK(!w.map().placed());
    if (against_golden(c, w, &obs, st, "view_gate2") != 0) {
        CHECK(false);
        return;
    }
    printf("  view: the golden render, place %u at tick %llu, part way through a drop-out, matched pixel for pixel\n",
           st.place, static_cast<unsigned long long>(w.tick_count()));
}

// Whether some place's network device is off.
bool network_off(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i)
        if (!w.place(i).network_state.on)
            return true;
    return false;
}

// Gate 3's golden render (build-plan-gate3.md step 6): the traffic world on
// its landscape, watched, its map coloured by queues and drops, drawn at the
// first moment after tick 400 that messages are part way across links, a
// network device is off and a place has dropped messages, inspecting the
// first place with drops, its device on and programs alive.
void test_golden_gate3() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate3, true, true));
    CHECK(w.map().placed() && w.map().raster_w() > 0 && w.net().active());
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    ViewState st;
    st.title = "the gate 3 golden test world";
    st.map_colour = MapColour::queues;
    bool found = false;
    while (!found && w.tick_count() < 4000) {
        w.tick();
        if (w.tick_count() < 400 || w.net().on_links() == 0 || !network_off(w))
            continue;
        for (u32 i = 0; i < w.place_count() && !found; ++i) {
            Place& p = w.place(i);
            if (w.net().dropped_at(i) && p.network_state.on && p.os.live_programs()) {
                st.place = i;
                found = true;
            }
        }
    }
    CHECK(found);
    const Sample& last = obs.samples.back();
    CHECK(obs.samples.size() > 1 && last.net_sent > 0 && last.net_delivered > 0 && last.colonists > 0);
    if (against_golden(c, w, &obs, st, "view_gate3") != 0) {
        CHECK(false);
        return;
    }
    // The map zoomed in four times on the inspected place (build-plan-viewer.md
    // step 1), against its own stored image.
    ViewState zoomed = st;
    zoomed.map_zoom = 4;
    zoomed.map_cx = w.map().x(st.place);
    zoomed.map_cy = w.map().y(st.place);
    if (against_golden(c, w, &obs, zoomed, "view_gate3_zoom") != 0) {
        CHECK(false);
        return;
    }
    // The programs page (build-plan-viewer.md step 2) on the lax place with
    // the most programs, the memory beside it: a program from the middle of
    // the list picked, by age, and its first thread followed.
    u32 busy = ~0u;
    for (u32 i = 0; i < w.place_count(); ++i)
        if (!w.place(i).mem.strict() && !cut_out(w, i) &&
            (busy == ~0u || w.place(i).os.live_programs() > w.place(busy).os.live_programs()))
            busy = i;
    CHECK(busy != ~0u);
    if (busy != ~0u) {
        Array<ProgramRow> rows;
        list_programs(w.place(busy), &obs, ProgramSort::age, false, &rows);
        CHECK(rows.size() > 4);
        u32 k = static_cast<u32>(rows.size()) / 2;
        while (k + 1 < rows.size() && rows[k].threads == 0)
            ++k;
        Array<ThreadRow> ths;
        list_threads(w.place(busy), rows[k], &ths);
        ViewState pg = st;
        pg.place = busy;
        pg.map = false;
        pg.colour = MemColour::lineage;
        pg.inspect = Inspect::programs;
        pg.program = rows[k].uid;
        pg.program_place = busy;
        pg.programs_scroll.first = k > 3 ? k - 3 : 0;
        pg.thread = ths.size() ? ths[0].record : ~0u;
        if (against_golden(c, w, &obs, pg, "view_programs") != 0) {
            CHECK(false);
            return;
        }
        // Step 3: the same program's family, and the place's figures and
        // the world's.
        pg.family = true;
        if (against_golden(c, w, &obs, pg, "view_family") != 0) {
            CHECK(false);
            return;
        }
        pg.inspect = Inspect::figures;
        if (against_golden(c, w, &obs, pg, "view_figures") != 0) {
            CHECK(false);
            return;
        }
        pg.world_figures = true;
        if (against_golden(c, w, &obs, pg, "view_figures_world") != 0) {
            CHECK(false);
            return;
        }
    }
    // The other colourings, in the build tree, for a person to look at.
    const char* const names[3] = {"population", "lineage", "network"};
    for (u32 k = 0; k < 3; ++k) {
        st.map_colour = static_cast<MapColour>(k);
        Array<u32> px;
        CHECK(render(c, w, &obs, st, &px));
        char path[64];
        snprintf(path, sizeof path, "view_gate3_%s_actual.png", names[k]);
        CHECK(c.write_png(path));
    }
    printf("  view: gate 3's golden render, place %u at tick %llu, %u messages mid-hop, matched pixel for pixel\n",
           st.place, static_cast<unsigned long long>(w.tick_count()), w.net().on_links());
}

// A snapshot taken mid-drop-out, with a device cut out, opens in the views
// and draws every place, in every memory colouring, as the world it came
// from did; the speeds and capacity a snapshot doesn't keep come back.
void test_snapshot_opens() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World a;
    Config cfg;
    CHECK(build(a, cfg, kGate2, true));
    a.start_threads(2);
    u32 at = ~0u;
    while (at == ~0u && a.tick_count() < 3000) {
        a.tick();
        if (a.tick_count() < 50)
            continue;
        for (u32 i = 0; i < a.place_count() && at == ~0u; ++i)
            if (mid_dropout(a, i, 1) && cut_out(a, i))
                at = i;
    }
    CHECK(at != ~0u);
    ByteWriter out;
    Config cfg2;
    World b;
    CHECK(reopen(a, cfg, out, cfg2, b));
    // What the views read that a snapshot doesn't keep, restored on loading.
    bool slower = false;
    for (u32 i = 0; i < a.place_count(); ++i) {
        CHECK(a.place(i).capacity == b.place(i).capacity);
        for (u32 k = 0; k < a.place(i).core_count(); ++k) {
            CHECK(a.place(i).cores[k].speed == b.place(i).cores[k].speed);
            slower = slower || a.place(i).cores[k].speed < a.place(i).cores[k].stock_speed;
        }
    }
    CHECK(slower); // so the speeds had something to restore

    forget_counters(a);
    ViewState st;
    st.title = "a snapshot";
    u32 renders = 0;
    for (u32 mode = 0; mode < static_cast<u32>(MemColour::count); ++mode) {
        st.colour = static_cast<MemColour>(mode);
        for (u32 place = 0; place < a.place_count(); ++place) {
            st.place = place;
            Array<u32> pa, pb;
            CHECK(render(c, a, nullptr, st, &pa));
            CHECK(render(c, b, nullptr, st, &pb));
            CHECK(pa.size() == pb.size() && memcmp(pa.data(), pb.data(), pa.size() * 4) == 0);
            ++renders;
        }
    }
    printf("  view: a snapshot taken at tick %llu, place %u part way through a drop-out with a device cut out, drew "
           "the same as its world in %u renders\n",
           static_cast<unsigned long long>(a.tick_count()), at, renders);
}

// Gate 3's (build-plan-gate3.md step 6): a snapshot taken with messages part
// way across links and a network device off opens in the views and draws as
// the world it came from: the map in every colouring, the memory in every
// colouring, and gate 2's row of charts, inspecting a place whose device is
// off, a place with bytes queued on a link, and place 0.
void test_snapshot_mid_hop() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World a;
    Config cfg;
    CHECK(build(a, cfg, kGate3, true, true));
    a.start_threads(2);
    u32 dark = ~0u, queued = ~0u;
    while (a.tick_count() < 4000) {
        a.tick();
        if (a.tick_count() < 100 || a.net().on_links() == 0)
            continue;
        dark = queued = ~0u;
        const Map& m = a.map();
        for (u32 i = 0; i < a.place_count(); ++i) {
            if (!a.place(i).network_state.on && dark == ~0u)
                dark = i;
            for (u32 j = 0; j < m.degree(i) && queued == ~0u; ++j)
                if (a.net().queued(m.link_at(i, j), i))
                    queued = i;
        }
        if (dark != ~0u && queued != ~0u)
            break;
    }
    CHECK(dark != ~0u && queued != ~0u);
    ByteWriter out;
    Config cfg2;
    World b;
    CHECK(reopen(a, cfg, out, cfg2, b));
    CHECK(b.net().on_links() == a.net().on_links() && b.net().in_flight() == a.net().in_flight());

    forget_counters(a);
    ViewState st;
    st.title = "a snapshot mid-hop";
    const u32 places[3] = {dark, queued, 0};
    u32 renders = 0;
    for (u32 k = 0; k < 3; ++k) {
        st.place = places[k];
        // The map in every colouring, then the memory in every colouring,
        // then gate 2's charts.
        for (u32 view = 0; view < static_cast<u32>(MapColour::count) + static_cast<u32>(MemColour::count) + 1;
             ++view) {
            st.map = view < static_cast<u32>(MapColour::count);
            st.power_row = view == static_cast<u32>(MapColour::count) + static_cast<u32>(MemColour::count);
            if (st.map)
                st.map_colour = static_cast<MapColour>(view);
            else if (!st.power_row)
                st.colour = static_cast<MemColour>(view - static_cast<u32>(MapColour::count));
            Array<u32> pa, pb;
            CHECK(render(c, a, nullptr, st, &pa));
            CHECK(render(c, b, nullptr, st, &pb));
            CHECK(pa.size() == pb.size() && memcmp(pa.data(), pb.data(), pa.size() * 4) == 0);
            ++renders;
        }
    }
    printf("  view: a snapshot taken at tick %llu with %u messages mid-hop and place %u's network device off drew "
           "the same as its world in %u renders\n",
           static_cast<unsigned long long>(a.tick_count()), a.net().on_links(), dark, renders);
}

// Keys and clicks change the view, not the world.
void test_input() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kSmall, false));
    const u64 before = w.hash();
    ViewState st;
    UiInput in;
    in.keys[in.nkeys++] = 'M';
    in.keys[in.nkeys++] = 0x27; // right
    in.keys[in.nkeys++] = 0x20; // space
    CHECK(c.begin_bitmap(960, 600, 1.0f));
    draw_viewer(c, st, w, nullptr, in);
    c.end();
    CHECK(st.colour == MemColour::lineage && st.place == 1 && st.paused);
    // Gate 3's, with a map (kSmall has the default landscape): V, C and G.
    UiInput in3;
    in3.keys[in3.nkeys++] = 'V';
    in3.keys[in3.nkeys++] = 'C';
    in3.keys[in3.nkeys++] = 'G';
    CHECK(w.map().placed());
    CHECK(c.begin_bitmap(960, 600, 1.0f));
    draw_viewer(c, st, w, nullptr, in3);
    c.end();
    CHECK(!st.map && st.map_colour == MapColour::lineage && st.power_row);
    CHECK(w.hash() == before);
    // With no map they change nothing.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, kGate2, false));
    ViewState st2;
    CHECK(c.begin_bitmap(960, 600, 1.0f));
    draw_viewer(c, st2, w2, nullptr, in3);
    c.end();
    CHECK(st2.map && st2.map_colour == MapColour::population && !st2.power_row);
}

// The controls themselves (build-plan-viewer.md step 1), with made-up
// input: a button answers a press on it and nothing else; a row lays its
// buttons side by side; a list scrolls by the wheel and by its bar, and
// stops at its ends.
void test_widgets() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    CHECK(c.begin_bitmap(400, 300, 1.0f));
    const Rect r{10, 10, 80, 24};
    UiInput in;
    CHECK(!button(c, in, r, "go")); // the pointer is elsewhere
    in.mx = 50;
    in.my = 20;
    CHECK(!button(c, in, r, "go")); // over it, not pressed
    in.down = in.pressed = true;
    CHECK(button(c, in, r, "go"));
    in.pressed = false;
    CHECK(!button(c, in, r, "go")); // still held: one press, one answer
    in.pressed = true;
    in.mx = 91;
    CHECK(!button(c, in, r, "go")); // a press beside it
    // A row, rightwards and leftwards, and the log of where they went.
    ButtonLog log;
    UiInput quiet;
    quiet.log = &log;
    ButtonRow right{c, quiet, 20, 60, button_h(c)};
    right.right("one");
    right.right("two", true);
    ButtonRow left{c, quiet, 380, 100, button_h(c)};
    left.left("three");
    left.left("four");
    const Rect *one = log.find("one"), *two = log.find("two"), *three = log.find("three"), *four = log.find("four");
    CHECK(log.count == 4 && one && two && three && four && !log.find("five"));
    if (one && two && three && four) {
        CHECK(one->x == 20 && one->w == button_w(c, "one") && two->x == one->right() + 3);
        CHECK(right.x == two->right() + 3);
        CHECK(three->right() == 380 && four->right() == three->x - 3 && left.x == four->x - 3);
        CHECK(one->h == button_h(c) && one->y == 60 && four->y == 100);
    }
    // A list of 100 rows where 10 fit.
    const Rect list{20, 20, 200, 200};
    Scroll s;
    UiInput over;
    over.mx = 100;
    over.my = 100;
    ListRows rows = scroll_list(c, over, list, 20, 100, s);
    CHECK(rows.first == 0 && rows.count == 10 && rows.rows.w < list.w && rows.row(0).y == 20 && rows.row(1).y == 40);
    over.wheel = -1; // towards the user: down the list, three rows
    rows = scroll_list(c, over, list, 20, 100, s);
    CHECK(rows.first == 3 && s.first == 3 && rows.row(3).y == 20);
    over.wheel = 5;
    rows = scroll_list(c, over, list, 20, 100, s);
    CHECK(rows.first == 0); // stops at the top
    over.wheel = -100;
    rows = scroll_list(c, over, list, 20, 100, s);
    CHECK(rows.first == 90 && rows.count == 10); // and at the bottom
    over.wheel = 1;
    over.mx = 300; // the wheel elsewhere moves nothing
    rows = scroll_list(c, over, list, 20, 100, s);
    CHECK(rows.first == 90);
    // The bar: a press on the track above the grip jumps there, a drag
    // follows the pointer, and letting go lets go.
    UiInput bar;
    bar.mx = list.right() - 4;
    bar.my = list.y + 100;
    bar.down = bar.pressed = true;
    rows = scroll_list(c, bar, list, 20, 100, s);
    CHECK(s.held && rows.first > 30 && rows.first < 60);
    const u32 mid = rows.first;
    bar.pressed = false;
    bar.my = list.y + 140;
    rows = scroll_list(c, bar, list, 20, 100, s);
    CHECK(s.held && rows.first > mid);
    bar.my = list.y - 500; // dragged far past the top
    bar.mx = 0;            // and off the bar: it still follows
    rows = scroll_list(c, bar, list, 20, 100, s);
    CHECK(rows.first == 0);
    bar.down = false;
    bar.my = list.y + 190;
    rows = scroll_list(c, bar, list, 20, 100, s);
    CHECK(!s.held && rows.first == 0);
    // A list that fits has no bar and doesn't scroll.
    Scroll s2;
    s2.first = 7;
    over.mx = 100;
    over.wheel = -3;
    rows = scroll_list(c, over, list, 20, 8, s2);
    CHECK(rows.first == 0 && rows.count == 8 && rows.rows.w == list.w && s2.first == 0);
    c.end();
}

// One frame of the viewer under `in`.
void frame(Canvas& c, ViewState& st, World& w, const UiInput& in, const Observer* obs = nullptr) {
    CHECK(c.begin_bitmap(kW, kH, 1.0f));
    draw_viewer(c, st, w, obs, in);
    c.end();
}

bool click_nth(Canvas& c, ViewState& st, World& w, const char* label, u32 nth, const Observer* obs = nullptr);

// A click on the viewer's button labelled `label`: a frame to find where it
// is, then one with a press on its middle. False if there is no such button.
bool click(Canvas& c, ViewState& st, World& w, const char* label, const Observer* obs = nullptr) {
    ButtonLog log;
    UiInput look;
    look.log = &log;
    frame(c, st, w, look, obs);
    const Rect* r = log.find(label);
    if (!r)
        return false;
    UiInput in;
    in.mx = r->x + r->w / 2;
    in.my = r->y + r->h / 2;
    in.down = in.pressed = true;
    frame(c, st, w, in, obs);
    return true;
}

// Everything a key does a button does (build-plan-viewer.md 2, choice 6),
// and none of it changes the world.
void test_buttons() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kSmall, false));
    CHECK(w.map().placed());
    const u64 before = w.hash();
    ViewState st;
    CHECK(click(c, st, w, "pause") && st.paused && !st.step);
    CHECK(click(c, st, w, "run") && !st.paused);
    CHECK(click(c, st, w, "one tick") && st.paused && st.step);
    CHECK(click(c, st, w, "faster") && st.ticks_per_frame == 2);
    CHECK(click(c, st, w, "faster") && st.ticks_per_frame == 4);
    CHECK(click(c, st, w, "slower") && st.ticks_per_frame == 2);
    CHECK(click(c, st, w, "power") && st.power_row);
    CHECK(click(c, st, w, "traffic") && !st.power_row);
    // The middle panel: the map's colourings, then the memory's.
    CHECK(st.map);
    CHECK(click(c, st, w, "queues") && st.map_colour == MapColour::queues);
    CHECK(click(c, st, w, "network") && st.map_colour == MapColour::network);
    CHECK(click(c, st, w, "lineage") && st.map_colour == MapColour::lineage && st.colour == MemColour::owner);
    CHECK(click(c, st, w, "population") && st.map_colour == MapColour::population);
    CHECK(!click(c, st, w, "permission")); // the memory's, not shown with the map
    CHECK(click(c, st, w, "memory") && !st.map);
    CHECK(click(c, st, w, "permission") && st.colour == MemColour::permission);
    CHECK(click(c, st, w, "lineage") && st.colour == MemColour::lineage && st.map_colour == MapColour::population);
    CHECK(click(c, st, w, "size class") && st.colour == MemColour::size_class);
    CHECK(click(c, st, w, "owner") && st.colour == MemColour::owner);
    CHECK(!click(c, st, w, "queues") && !click(c, st, w, "whole map"));
    CHECK(click(c, st, w, "map") && st.map);
    CHECK(w.hash() == before && w.tick_count() == 0);
    // With no map there is nothing to choose between: no tabs, no charts'
    // buttons, and the memory's colourings still there.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, kGate2, false));
    ViewState st2;
    CHECK(!click(c, st2, w2, "map") && !click(c, st2, w2, "memory") && !click(c, st2, w2, "power"));
    CHECK(click(c, st2, w2, "permission") && st2.colour == MemColour::permission);
    CHECK(click(c, st2, w2, "pause") && st2.paused);
}

// A press at (x, y) on the viewer and, with a (dx, dy), a drag from there by
// (dx, dy) before it is let go.
void press(Canvas& c, ViewState& st, World& w, i32 x, i32 y, i32 dx = 0, i32 dy = 0) {
    UiInput in;
    in.mx = x;
    in.my = y;
    in.down = in.pressed = true;
    frame(c, st, w, in);
    in.pressed = false;
    if (dx || dy) {
        in.mx += dx;
        in.my += dy;
        frame(c, st, w, in);
    }
    in.down = false;
    in.released = true;
    frame(c, st, w, in);
}

// The map's zoom and pan (build-plan-viewer.md step 1). A click picks the
// place nearest the pointer; the wheel zooms about the pointer, so the same
// click picks the same place after it; a drag moves the map with the
// pointer, picking nothing; the buttons on the map and Home do as the wheel
// does. None of it changes the world.
void test_map_zoom() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate3, false));
    CHECK(w.map().placed());
    const u64 before = w.hash();
    ViewState st;
    const i32 x = 640, y = 300; // on the map, right of and above its middle
    press(c, st, w, x, y);
    const u32 here = st.place;
    press(c, st, w, x - 200, y + 100);
    const u32 there = st.place;
    CHECK(here != there && st.map_zoom == 1);
    // A drag at zoom 1 moves nothing (the whole map stays in view) and
    // picks nothing.
    const double cx1 = st.map_cx, cy1 = st.map_cy;
    press(c, st, w, x, y, 60, 40);
    CHECK(st.place == there && st.map_cx == cx1 && st.map_cy == cy1 && !st.map_held);
    // The wheel, six notches in at the pointer.
    UiInput wheel;
    wheel.mx = x;
    wheel.my = y;
    wheel.wheel = 6;
    frame(c, st, w, wheel);
    CHECK(st.map_zoom > 3.8 && st.map_zoom < 3.82);
    press(c, st, w, x, y);
    CHECK(st.place == here); // the place under the pointer stayed under it
    press(c, st, w, x - 200, y + 100);
    CHECK(st.place != there); // and the far one is further off now
    // A drag moves the map with the pointer: the same place is under it at
    // the drag's end, and the drag itself picked nothing.
    press(c, st, w, x, y);
    const double cx2 = st.map_cx, cy2 = st.map_cy;
    press(c, st, w, x, y, 50, -30);
    CHECK(st.place == here && st.map_cx < cx2 && st.map_cy > cy2);
    st.place = there;
    press(c, st, w, x + 50, y - 30);
    CHECK(st.place == here);
    // The wheel elsewhere (over the places' list) zooms nothing.
    wheel.mx = 100;
    wheel.wheel = -2;
    frame(c, st, w, wheel);
    CHECK(st.map_zoom > 3.8);
    // The buttons on the map: closer, further, and the whole map again; a
    // press on one isn't a press on the map.
    const double z = st.map_zoom;
    st.place = there;
    CHECK(click(c, st, w, "+") && st.map_zoom > z * 1.49 && st.map_zoom < z * 1.51 && !st.map_held);
    CHECK(click(c, st, w, "-") && st.map_zoom > z * 0.99 && st.map_zoom < z * 1.01);
    CHECK(click(c, st, w, "whole map"));
    UiInput none;
    frame(c, st, w, none);
    CHECK(st.map_zoom == 1 && st.map_cx == cx1 && st.map_cy == cy1 && st.place == there);
    // As far in as it goes, and all the way out again by the wheel; then
    // Home from a zoom.
    wheel.mx = x;
    wheel.wheel = 40;
    frame(c, st, w, wheel);
    CHECK(st.map_zoom == 32);
    wheel.wheel = -40;
    frame(c, st, w, wheel);
    CHECK(st.map_zoom == 1 && st.map_cx == cx1 && st.map_cy == cy1);
    wheel.wheel = 3;
    frame(c, st, w, wheel);
    CHECK(st.map_zoom > 1);
    UiInput home;
    home.keys[home.nkeys++] = 0x24;
    frame(c, st, w, home);
    CHECK(st.map_zoom == 1);
    CHECK(w.hash() == before && w.tick_count() == 0);
}

// The programs page (build-plan-viewer.md step 2), on gate 2's world with
// the hand-placed replicator in every lax region (R7): the list holds every
// living program once, in the order asked for; the replicator's programs
// are among them; a program's code is its genome, its lines the ones nova2
// trace's disassembler writes, with a mark on each line a thread of its
// stands at; clicks pick a program, sort the list, and follow a thread; a
// program that dies while picked keeps its code; and none of it changes
// the world.
void test_programs() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate2, false));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    u32 at = ~0u;
    while (at == ~0u && w.tick_count() < 3000) {
        w.tick();
        for (u32 i = 0; i < w.place_count() && at == ~0u; ++i)
            if (!w.place(i).mem.strict() && w.place(i).os.live_programs() >= 8 && !cut_out(w, i))
                at = i;
    }
    CHECK(at != ~0u);
    if (at == ~0u)
        return;
    Place& p = w.place(at);
    const u64 before = w.hash();

    // The list.
    Array<ProgramRow> rows;
    list_programs(p, &obs, ProgramSort::age, false, &rows);
    CHECK(rows.size() == p.os.live_programs());
    u32 threads = 0;
    for (u32 i = 0; i < rows.size(); ++i) {
        const ProgramRow& g = rows[i];
        CHECK(g.known && g.uid && g.bytes > 0 && p.os.program(g.holder).alive && p.os.program(g.holder).uid == g.uid);
        CHECK(i == 0 || rows[i - 1].born_tick < g.born_tick ||
              (rows[i - 1].born_tick == g.born_tick && rows[i - 1].uid < g.uid));
        ProgramRow same;
        CHECK(find_program(p, &obs, g.uid, &same) && same.holder == g.holder && same.threads == g.threads);
        threads += g.threads;
    }
    CHECK(threads == p.os.live_threads());
    ProgramRow none;
    CHECK(!find_program(p, &obs, 0, &none) && !find_program(p, &obs, ~0ull, &none));
    Array<ProgramRow> by;
    list_programs(p, &obs, ProgramSort::threads, true, &by);
    CHECK(by.size() == rows.size());
    for (u32 i = 1; i < by.size(); ++i)
        CHECK(by[i - 1].threads >= by[i].threads);
    list_programs(p, &obs, ProgramSort::bytes, false, &by);
    for (u32 i = 1; i < by.size(); ++i)
        CHECK(by[i - 1].bytes <= by[i].bytes);
    list_programs(p, &obs, ProgramSort::lineage, false, &by);
    for (u32 i = 1; i < by.size(); ++i)
        CHECK(by[i - 1].origin <= by[i].origin);

    // Every program's code: its genome, line for line what the disassembler
    // writes for the whole of it, with the threads' marks where their
    // instruction pointers are.
    u32 replicators = 0, marked = 0;
    Array<CodeLine> lines;
    Array<ThreadRow> ths;
    TextBuf whole, joined, one;
    for (u32 i = 0; i < rows.size(); ++i) {
        ProgramCode code;
        CHECK(program_code(p, &obs, rows[i].uid, 0, &rows[i], &code, &lines));
        const ProgramRec* rec = obs.program(rows[i].uid);
        CHECK(rec && code.birth && code.genome == rec->genome && code.len == obs.genome(rec->genome)->len);
        if (code.len == sizeof kLax && memcmp(code.bytes, kLax, sizeof kLax) == 0)
            ++replicators;
        whole.clear();
        joined.clear();
        disassemble(code.bytes, code.len, &whole);
        u32 bytes = 0, here = 0;
        for (u32 k = 0; k < lines.size(); ++k) {
            CHECK(lines[k].at == bytes && line_at(lines, lines[k].at) == k);
            CHECK(line_at(lines, lines[k].at + lines[k].len - 1) == k);
            bytes += lines[k].len;
            here += lines[k].threads;
            line_text(code, lines[k], &one);
            if (k)
                joined.append("; ");
            joined.append(one.view());
        }
        CHECK(bytes == code.len && line_at(lines, code.len) == ~0u);
        CHECK(joined.size() == whole.size() && memcmp(joined.c_str(), whole.c_str(), whole.size()) == 0);
        list_threads(p, rows[i], &ths);
        CHECK(ths.size() == rows[i].threads);
        u32 inside = 0;
        for (u32 t = 0; t < ths.size(); ++t) {
            const Thread& th = p.os.thread(ths[t].record);
            CHECK(th.id == ths[t].id && th.program == rows[i].holder && th.state == ths[t].state);
            const u32 k = line_at(lines, ths[t].r.ip - code.addr);
            if (k != ~0u) {
                CHECK(code.addr != kNowhere && lines[k].threads > 0);
                ++inside;
            }
        }
        CHECK(here == inside);
        marked += inside;
    }
    CHECK(replicators >= 2 && marked > 0);
    printf("  view: place %u at tick %llu: %u programs listed, %u of them the replicator's, %u threads marked on "
           "their code\n",
           at, static_cast<unsigned long long>(w.tick_count()), static_cast<u32>(rows.size()), replicators, marked);

    // The page itself: its tab, a click on a program, the sorts, a thread
    // followed and let go, and the program let go.
    ViewState st;
    st.place = at;
    CHECK(click(c, st, w, "programs", &obs) && st.inspect == Inspect::programs);
    CHECK(!click(c, st, w, "thread row", &obs) && !click(c, st, w, "let go", &obs)); // none picked yet
    CHECK(click(c, st, w, "program row", &obs));
    CHECK(st.program == rows[0].uid && st.program_place == at && st.picked.uid == rows[0].uid && st.picked_alive);
    UiInput quiet;
    frame(c, st, w, quiet, &obs);
    CHECK(st.picked_genome == obs.program(rows[0].uid)->genome);
    CHECK(click(c, st, w, "threads", &obs) && st.sort == ProgramSort::threads && !st.sort_down);
    CHECK(click(c, st, w, "threads", &obs) && st.sort == ProgramSort::threads && st.sort_down);
    CHECK(click(c, st, w, "bytes", &obs) && st.sort == ProgramSort::bytes && !st.sort_down);
    CHECK(click(c, st, w, "age", &obs) && st.sort == ProgramSort::age && st.program == rows[0].uid);
    // The copy button: the code as text for the clipboard (the window puts
    // it there; a frame only asks), and then it says it has.
    CHECK(!st.copy_asked && click(c, st, w, "copy", &obs) && st.copy_asked && st.copied == rows[0].uid);
    {
        ProgramCode code;
        CHECK(program_code(p, &obs, rows[0].uid, 0, &rows[0], &code, &lines));
        const char* text = copied_text();
        char hex[8], id[40];
        uid_text(rows[0].uid, id, sizeof id);
        CHECK(strncmp(text, "Program ", 8) == 0 && strstr(text, id) != nullptr);
        const char* hx = strstr(text, "Hex (nova2 trace reads it):\n");
        CHECK(hx != nullptr);
        hx = hx ? hx + strlen("Hex (nova2 trace reads it):\n") : text;
        for (u32 i = 0; i < code.len; ++i) {
            snprintf(hex, sizeof hex, "%02x", code.bytes[i]);
            CHECK(hx[2 * i] == hex[0] && hx[2 * i + 1] == hex[1]);
        }
        CHECK(hx[2 * code.len] == '\n');
        u32 lines_out = 0;
        for (const char* s = hx; *s; ++s)
            lines_out += *s == '\n' ? 1 : 0;
        CHECK(lines_out == 2 + lines.size()); // the hex's, "Code:" and a line an instruction
        line_text(code, lines.back(), &one);
        CHECK(strstr(text, one.c_str()) != nullptr);
    }
    st.copy_asked = false;
    CHECK(!click(c, st, w, "copy", &obs) && click(c, st, w, "copied", &obs) && st.copy_asked);
    st.copy_asked = false;
    if (rows[0].threads) {
        list_threads(p, rows[0], &ths);
        CHECK(click(c, st, w, "thread row", &obs) && st.thread == ths[0].record);
        frame(c, st, w, quiet, &obs);
        CHECK(st.thread == ths[0].record); // still followed a frame on
        CHECK(click(c, st, w, "thread row", &obs) && st.thread == ~0u);
    }
    // Another place: the list is that place's, and the program stays picked
    // where it is, a click away.
    st.place = at == 0 ? 1 : 0;
    frame(c, st, w, quiet, &obs);
    CHECK(st.program == rows[0].uid && st.program_place == at);
    CHECK(click(c, st, w, "go to it", &obs) && st.place == at);
    CHECK(click(c, st, w, "let go", &obs) && st.program == 0);
    CHECK(click(c, st, w, "place", &obs) && st.inspect == Inspect::place);
    CHECK(w.hash() == before);

    // A program that dies while picked: its code stays, by its genome.
    const ProgramRow young = rows.back();
    st.inspect = Inspect::programs;
    st.program = young.uid;
    st.program_place = at;
    frame(c, st, w, quiet, &obs);
    CHECK(st.picked_alive && st.picked.uid == young.uid && st.picked_genome != 0);
    ProgramCode code;
    CHECK(program_code(p, &obs, young.uid, 0, &young, &code, &lines));
    Array<u8> born;
    born.append(code.bytes, code.len);
    ProgramRow gone;
    while (find_program(p, &obs, young.uid, &gone) && w.tick_count() < 20000)
        w.tick();
    CHECK(!find_program(p, &obs, young.uid, &gone));
    frame(c, st, w, quiet, &obs);
    CHECK(!st.picked_alive && st.program == young.uid && st.picked.uid == young.uid && st.thread == ~0u);
    CHECK(program_code(p, &obs, young.uid, st.picked_genome, nullptr, &code, &lines));
    CHECK(code.birth && code.addr == kNowhere && code.len == born.size() && lines.size() > 0);
    CHECK(memcmp(code.bytes, born.data(), born.size()) == 0);
    for (u32 k = 0; k < lines.size(); ++k)
        CHECK(lines[k].threads == 0 && !lines[k].changed);
    printf("  view: program %llu died while picked, at about tick %llu, and kept its %u bytes of code\n",
           static_cast<unsigned long long>(young.uid), static_cast<unsigned long long>(w.tick_count()), code.len);
}

// The family pane and the figures (build-plan-viewer.md step 3), on the
// same world as test_programs, later on: each figure shown is the
// observers' own number or the place's own count, the commonest lineages
// and genomes are the ones a plain count finds, a program's parent and
// children are a click away, and none of it changes the world.
void test_figures() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate2, false));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    os.turnover.window = 100;
    os.turnover.members = 2;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    // A lax place with programs, one of them a living program's child, after
    // a few turnover windows.
    u32 at = ~0u;
    u64 child = 0;
    while (at == ~0u && w.tick_count() < 5000) {
        w.tick();
        if (w.tick_count() < 450)
            continue;
        for (u32 i = 0; i < w.place_count() && at == ~0u; ++i) {
            Place& q = w.place(i);
            if (q.mem.strict() || q.os.live_programs() < 8)
                continue;
            for (u32 k = 0; k < q.os.program_capacity() && at == ~0u; ++k) {
                const Program& g = q.os.program(kFirstProgramHolder + k);
                ProgramRow parent;
                if (g.alive && g.parent_uid && find_program(q, &obs, g.parent_uid, &parent)) {
                    at = i;
                    child = g.uid;
                }
            }
        }
    }
    CHECK(at != ~0u);
    if (at == ~0u)
        return;
    Place& p = w.place(at);
    const u64 before = w.hash();
    Array<ProgramRow> rows;
    list_programs(p, &obs, ProgramSort::age, false, &rows);

    // The place's figures.
    PlaceFigures f;
    place_figures(p, &obs, &f);
    CHECK(f.programs == p.os.live_programs() && f.threads == p.os.live_threads());
    CHECK(f.births == p.births && f.deaths == p.deaths && f.known == rows.size() && f.watched);
    CHECK(f.window == 100 && memcmp(&f.turnover, &obs.turnover[at], sizeof f.turnover) == 0);
    CHECK(f.turnover.windows > 0);
    u64 died = 0;
    for (u32 k = 0; k < kDeathCauses; ++k) {
        CHECK(f.deaths_by_cause[k] == obs.turnover[at].deaths_by_cause[k]);
        died += f.deaths_by_cause[k];
    }
    CHECK(died == p.deaths);
    u32 faithful = 0, copies = 0, lineages = 0, genomes = 0;
    for (u32 i = 0; i < rows.size(); ++i) {
        const ProgramRec* r = obs.program(rows[i].uid);
        faithful += r->faithful ? 1 : 0;
        copies += r->copy ? 1 : 0;
        bool first_l = true, first_g = true;
        for (u32 j = 0; j < i; ++j) {
            first_l = first_l && rows[j].origin != rows[i].origin;
            first_g = first_g && obs.program(rows[j].uid)->genome != r->genome;
        }
        lineages += first_l ? 1 : 0;
        genomes += first_g ? 1 : 0;
    }
    CHECK(f.faithful == faithful && f.copies == copies && f.lineage_count == lineages && f.genome_count == genomes);
    CHECK(f.lineages_shown == (lineages < kTop ? lineages : kTop) && f.lineages_shown > 0);
    CHECK(f.genomes_shown == (genomes < kTop ? genomes : kTop) && f.genomes_shown > 0);
    // Each of the commonest has the count a plain count gives it, they come
    // the commonest first, and none left out has more than the last shown.
    for (u32 k = 0; k < f.lineages_shown; ++k) {
        u32 n = 0;
        for (u32 i = 0; i < rows.size(); ++i)
            n += rows[i].origin == f.lineages[k].key ? 1 : 0;
        CHECK(n == f.lineages[k].count && n > 0);
        CHECK(k == 0 || f.lineages[k - 1].count >= f.lineages[k].count);
    }
    for (u32 i = 0; i < rows.size(); ++i) {
        u32 n = 0;
        bool shown = false;
        for (u32 j = 0; j < rows.size(); ++j)
            n += rows[j].origin == rows[i].origin ? 1 : 0;
        for (u32 k = 0; k < f.lineages_shown; ++k)
            shown = shown || f.lineages[k].key == rows[i].origin;
        CHECK(shown || n <= f.lineages[f.lineages_shown - 1].count);
    }
    for (u32 k = 0; k < f.genomes_shown; ++k) {
        u32 n = 0;
        for (u32 i = 0; i < rows.size(); ++i)
            n += obs.program(rows[i].uid)->genome == f.genomes[k].key ? 1 : 0;
        CHECK(n == f.genomes[k].count && f.genomes[k].len == obs.genome(f.genomes[k].key)->len);
        CHECK(k == 0 || f.genomes[k - 1].count >= f.genomes[k].count);
    }

    // The world's.
    WorldFigures g;
    world_figures(w, &obs, &g);
    u64 programs = 0, threads = 0, alive = 0, most_alive = 0, most_born = 0, colonies_alive = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        programs += w.place(i).os.live_programs();
        threads += w.place(i).os.live_threads();
    }
    for (u32 i = 0; i < obs.lineage_count(); ++i) {
        alive += obs.lineage_at(i).alive ? 1 : 0;
        most_alive = obs.lineage_at(i).alive > most_alive ? obs.lineage_at(i).alive : most_alive;
    }
    for (u32 i = 0; i < obs.genome_count(); ++i)
        most_born = obs.genome_at(i).count > most_born ? obs.genome_at(i).count : most_born;
    for (u32 i = 0; i < obs.colony_count(); ++i)
        colonies_alive += obs.colony_at(i).alive ? 1 : 0;
    CHECK(g.programs == programs && g.threads == threads);
    CHECK(g.births == obs.total_births && g.faithful == obs.total_faithful && g.deaths == obs.total_deaths);
    CHECK(g.births > 0 && g.deaths > 0 && g.cold_starts == obs.cold_starts);
    CHECK(memcmp(g.deaths_by_cause, obs.deaths_by_cause, sizeof g.deaths_by_cause) == 0);
    CHECK(memcmp(g.births_by_cause, obs.births_by_cause, sizeof g.births_by_cause) == 0);
    CHECK(g.genomes == obs.genome_count() && g.groups == obs.group_count() && g.lineages == obs.lineage_count());
    CHECK(g.lineages_alive == alive && g.colonies == obs.colony_count() && g.colonies_alive == colonies_alive);
    CHECK(g.lineages_shown > 0 && g.top_lineages[0].count == most_alive);
    CHECK(g.genomes_shown > 0 && g.top_genomes[0].count == most_born);
    for (u32 k = 0; k < g.lineages_shown; ++k) {
        CHECK(obs.lineage(g.top_lineages[k].key)->alive == g.top_lineages[k].count);
        CHECK(k == 0 || g.top_lineages[k - 1].count >= g.top_lineages[k].count);
    }
    for (u32 k = 0; k < g.genomes_shown; ++k) {
        const GenomeRec* gr = obs.genome(g.top_genomes[k].key);
        CHECK(gr && gr->count == g.top_genomes[k].count && gr->len == g.top_genomes[k].len);
        CHECK(k == 0 || g.top_genomes[k - 1].count >= g.top_genomes[k].count);
    }

    // Every program's family.
    u32 with_parent = 0, with_children = 0;
    for (u32 i = 0; i < rows.size(); ++i) {
        Family fam;
        family_of(w, p, &obs, rows[i], &fam);
        const ProgramRec* r = obs.program(rows[i].uid);
        const LineageRec* L = obs.lineage(r->origin);
        CHECK(fam.known && fam.parent == p.os.program(rows[i].holder).parent_uid && fam.origin == r->origin);
        ProgramRow parent;
        const bool lives = fam.parent && uid_place(fam.parent) < w.place_count() &&
                           find_program(w.place(uid_place(fam.parent)), &obs, fam.parent, &parent);
        CHECK(fam.parent_alive == lives && (!lives || fam.parent_place == uid_place(fam.parent)));
        with_parent += lives ? 1 : 0;
        u32 children = 0, here = 0;
        for (u32 j = 0; j < rows.size(); ++j) {
            children += p.os.program(rows[j].holder).parent_uid == rows[i].uid ? 1 : 0;
            here += rows[j].origin == r->origin ? 1 : 0;
        }
        CHECK(fam.children == children && fam.lineage_here == here && here > 0);
        with_children += children ? 1 : 0;
        for (u32 k = 0; k < children && k < Family::kShown; ++k) {
            ProgramRow kid;
            CHECK(find_program(p, &obs, fam.child[k], &kid) && p.os.program(kid.holder).parent_uid == rows[i].uid);
            CHECK(k == 0 || fam.child[k - 1] < fam.child[k]);
        }
        CHECK(fam.lineage_alive == L->alive && fam.lineage_births == L->births && fam.lineage_places >= 1);
        CHECK(fam.depth == r->depth && fam.faithful == r->faithful && fam.group == r->group);
        CHECK(fam.genome_len == obs.genome(r->genome)->len && fam.genome_births == obs.genome(r->genome)->count);
    }
    CHECK(with_parent > 0 && with_children > 0);

    // The pages: the figures' tab and whose they are; then the family of
    // the child found above, its parent a click away, and from the parent
    // the child again.
    ViewState st;
    st.place = at;
    CHECK(click(c, st, w, "figures", &obs) && st.inspect == Inspect::figures && !st.world_figures);
    CHECK(click(c, st, w, "the world", &obs) && st.world_figures);
    CHECK(click(c, st, w, "this place", &obs) && !st.world_figures);
    CHECK(click(c, st, w, "programs", &obs) && st.inspect == Inspect::programs);
    CHECK(!click(c, st, w, "family", &obs)); // none picked
    st.program = child;
    st.program_place = at;
    CHECK(click(c, st, w, "family", &obs) && st.family);
    ProgramRow kid;
    CHECK(find_program(p, &obs, child, &kid));
    const u64 parent = p.os.program(kid.holder).parent_uid;
    char id[40], label[64];
    uid_text(parent, id, sizeof id);
    snprintf(label, sizeof label, "parent %s", id);
    CHECK(click(c, st, w, label, &obs) && st.program == parent && st.program_place == uid_place(parent));
    CHECK(st.place == uid_place(parent) && st.picked_alive && st.family);
    Family fam;
    ProgramRow par;
    CHECK(find_program(w.place(st.place), &obs, parent, &par));
    family_of(w, w.place(st.place), &obs, par, &fam);
    CHECK(fam.children > 0);
    uid_text(fam.child[0], id, sizeof id);
    CHECK(click(c, st, w, id, &obs) && st.program == fam.child[0]);
    CHECK(click(c, st, w, "code", &obs) && !st.family);
    CHECK(w.hash() == before);
    printf("  view: place %u at tick %llu: the figures of %u programs in %u lineages and %u genomes, and the world's "
           "%llu births, matched the observers'; %u programs had a living parent, %u had children\n",
           at, static_cast<unsigned long long>(w.tick_count()), f.known, f.lineage_count, f.genome_count,
           static_cast<unsigned long long>(g.births), with_parent, with_children);
}

// ---------------------------------------------------------------------------
// Step 4: the window on a run
// ---------------------------------------------------------------------------

// A run's seeds are chosen from a dropdown in the top bar: its button says
// which seed is shown and opens the list; a running seed's row shows that
// seed and shuts the list, one that waits or has ended does nothing; while
// the list is open nothing under it answers the pointer, and a click
// elsewhere or Escape shuts it. And a window that watches has no buttons or
// keys to run, pause or step the world.
void test_seed_list() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kSmall, false));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    obs.init(os);
    w.watch(&obs);
    const u64 before = w.hash();
    static SeedTab tabs[26];
    for (u32 k = 0; k < 26; ++k) {
        SeedTab& t = tabs[k];
        t.seed = k + 1;
        t.state = k < 3 ? kTabDone : (k < 21 ? kTabRunning : kTabWaiting);
        t.stage = k < 21 ? 1 + k % 5 : 0;
        t.permille = k < 3 ? 1000 : (k < 21 ? 40 * k : 0);
        t.births = k < 21 ? 123456ull * (k + 1) : 0;
        for (u32 s = 0; s < 6; ++s)
            t.stage_tick[s] = s >= 1 && s <= t.stage ? 1000 * s + k : ~0ull;
    }
    ViewState st;
    st.title = "a run's window";
    st.watching = true;
    st.seeds = tabs;
    st.seed_count = 26;
    st.seed_shown = 3;
    st.tps = 4321;
    // Shut, the dropdown is its button, in the top bar, saying the seed
    // shown; the seeds' rows are not there.
    ButtonLog log;
    UiInput look;
    look.log = &log;
    frame(c, st, w, look, &obs);
    const Rect* button = log.find("seed 4  12%  stage 4");
    CHECK(button && button->y < 30 && !log.find("seed 1  done  stage 1") && !log.find("seed 26  waiting"));
    CHECK(!click(c, st, w, "seed 6  20%  stage 1", &obs));
    // Open, every seed has a row under it, inside the window.
    CHECK(!st.seeds_open && click(c, st, w, "seed 4  12%  stage 4", &obs) && st.seeds_open);
    ButtonLog open;
    look.log = &open;
    frame(c, st, w, look, &obs);
    const Rect* first = open.find("seed 1  done  stage 1");
    const Rect* last = open.find("seed 26  waiting");
    CHECK(first && last && button);
    if (first && last && button) {
        CHECK(first->y >= button->y + button->h && last->y > first->y);
        CHECK(first->x >= 0 && last->x + last->w <= kW && last->y + last->h <= kH);
    }
    // A running seed's row shows it and shuts the list; one that has ended
    // or waits does nothing.
    CHECK(click(c, st, w, "seed 6  20%  stage 1", &obs) && st.seed_shown == 5 && !st.seeds_open);
    CHECK(click(c, st, w, "seed 6  20%  stage 1", &obs) && st.seeds_open); // the button now says seed 6
    CHECK(click(c, st, w, "seed 1  done  stage 1", &obs) && st.seed_shown == 5 && st.seeds_open);
    CHECK(click(c, st, w, "seed 26  waiting", &obs) && st.seed_shown == 5 && st.seeds_open);
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && st.seed_shown == 20 && !st.seeds_open);
    // The row of the seed already shown shuts the list too.
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && st.seeds_open);
    CHECK(click_nth(c, st, w, "seed 21  80%  stage 1", 1, &obs) && st.seed_shown == 20 && !st.seeds_open);
    // While the list is open a click elsewhere shuts it and does no more;
    // so does Escape, which shuts nothing else then; and its button.
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && st.seeds_open);
    CHECK(st.map && click(c, st, w, "memory", &obs) && st.map && !st.seeds_open);
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && st.seeds_open);
    UiInput esc;
    esc.keys[0] = kVkEscape;
    esc.nkeys = 1;
    frame(c, st, w, esc, &obs);
    CHECK(!st.seeds_open && !st.quit);
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && st.seeds_open);
    CHECK(click(c, st, w, "seed 21  80%  stage 1", &obs) && !st.seeds_open);
    // It only watches.
    CHECK(!click(c, st, w, "pause", &obs) && !click(c, st, w, "run", &obs) && !click(c, st, w, "one tick", &obs));
    CHECK(!click(c, st, w, "faster", &obs) && !click(c, st, w, "slower", &obs));
    UiInput keys;
    keys.keys[0] = kVkSpace;
    keys.keys[1] = 'N';
    keys.keys[2] = kVkPlus;
    keys.nkeys = 3;
    frame(c, st, w, keys, &obs);
    CHECK(!st.paused && !st.step && st.ticks_per_frame == 1);
    // A window on a run has a button that asks the seed shown to save its
    // world (step 8), lit while that is asked and not yet done; a window on
    // a world of its own has none.
    CHECK(!click(c, st, w, "save world", &obs));
    st.can_save = true;
    CHECK(!st.save_asked && click(c, st, w, "save world", &obs) && st.save_asked);
    st.save_asked = false;
    tabs[5].saved = 46800;
    // The rest of the window is as ever.
    CHECK(click(c, st, w, "memory", &obs) && !st.map);
    CHECK(click(c, st, w, "map", &obs) && st.map);
    CHECK(click(c, st, w, "figures", &obs) && st.inspect == Inspect::figures);
    CHECK(click(c, st, w, "the world", &obs) && st.world_figures);
    CHECK(w.hash() == before && w.tick_count() == 0);
    st.seed_shown = 5;
    st.seeds_open = true;
    if (against_golden(c, w, &obs, st, "view_run") != 0)
        CHECK(false);
    // With no seed running there is no world to show: the dropdown, with
    // its list if open, and a line.
    for (u32 k = 0; k < 26; ++k)
        tabs[k].state = k < 20 ? kTabDone : kTabWaiting;
    ButtonLog log2;
    UiInput look2;
    look2.log = &log2;
    CHECK(c.begin_bitmap(kW, kH, 1.0f));
    draw_waiting(c, st, look2, "waiting for a seed to start");
    c.end();
    CHECK(log2.find("seed 26  waiting") && log2.find("seed 20  done  stage 5") && !log2.find("map"));
    st.seeds_open = false;
    ButtonLog log3;
    look2.log = &log3;
    CHECK(c.begin_bitmap(kW, kH, 1.0f));
    draw_waiting(c, st, look2, "waiting for a seed to start");
    c.end();
    CHECK(log3.find("seed 6  done  stage 1") && !log3.find("seed 26  waiting"));
    printf("  view: a run's dropdown of 26 seeds: a running seed's row shows it and shuts the list, the others' do "
           "nothing, nothing under the open list answers, and the window has no way to run or pause the world\n");
}

// `nova2 busy` on a small test world of three seeds at once, its files
// under view_run/<name>, with `threads` host threads a seed; `view` "" for
// no window, or NOVA2_VIEW_TEST's value for the window on the run, hidden
// and moving between the seeds and the pages by itself.
// `more`: settings beyond those.
void busy_command(char* out, usize size, const char* name, u32 threads, const char* more,
                  const char* verb = "busy") {
    snprintf(out, size,
             "\"%s\" %s --set world.places=6 --set memory.size=32768 --set disk.size=65536 --set hw.cores.max=4 "
             "--set origin.seeds=3 --set origin.parallel=3 --set origin.ticks=12000 --set observe.sample_every=5 "
             "--set observe.turnover_window=100 --set observe.turnover_members=2 --set run.threads=%u "
             "--set origin.samples=view_run/%s/samples --set origin.genomes=view_run/%s/genomes "
             "--set origin.windows=view_run/%s/windows%s",
             NOVA2_EXE, verb, threads, name, name, name, more);
}

void busy_folder(const char* name, char* dir, usize size) {
    CreateDirectoryA("view_run", nullptr);
    snprintf(dir, size, "view_run\\%s", name);
    CreateDirectoryA(dir, nullptr);
}

bool run_busy(const char* name, u32 threads, const char* view, const char* more = "") {
    char dir[64], run[3200], all[1600], cmd[3400];
    busy_folder(name, dir, sizeof dir);
    snprintf(all, sizeof all, "%s%s", view[0] ? " --set origin.view=true" : "", more);
    busy_command(run, sizeof run, name, threads, all);
    SetEnvironmentVariableA("NOVA2_VIEW_TEST", view[0] ? view : nullptr);
    snprintf(cmd, sizeof cmd, "\"%s > %s\\report.txt 2> %s\\err.txt\"", run, dir, dir);
    const int code = system(cmd);
    SetEnvironmentVariableA("NOVA2_VIEW_TEST", nullptr);
    return code == 0;
}

// `nova2 samples` under the settings of the run `name`: the run's samples
// as CSVs, made from its record files (`more` names them, as it did for the
// run).
bool make_samples(const char* name, const char* more) {
    char dir[64], run[2400], cmd[2600];
    busy_folder(name, dir, sizeof dir);
    busy_command(run, sizeof run, name, 1, more, "samples");
    snprintf(cmd, sizeof cmd, "\"%s > %s\\samples.txt 2> %s\\samples-err.txt\"", run, dir, dir);
    return system(cmd) == 0;
}

// The same run started and left going, with NOVA2_VIEW_TEST as `view` and
// no window asked for: its process, to be signalled and waited for.
bool start_busy(const char* name, u32 threads, const char* view, const char* more, PROCESS_INFORMATION* pi) {
    char dir[64], path[128], cmd[2400];
    busy_folder(name, dir, sizeof dir);
    busy_command(cmd, sizeof cmd, name, threads, more);
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    snprintf(path, sizeof path, "%s\\report.txt", dir);
    HANDLE out = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    snprintf(path, sizeof path, "%s\\err.txt", dir);
    HANDLE err = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = err;
    SetEnvironmentVariableA("NOVA2_VIEW_TEST", view);
    const bool ok = out != INVALID_HANDLE_VALUE && err != INVALID_HANDLE_VALUE &&
                    CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, pi);
    SetEnvironmentVariableA("NOVA2_VIEW_TEST", nullptr);
    CloseHandle(out);
    CloseHandle(err);
    return ok;
}

bool run_file(const char* name, const char* file, Array<u8>* out) {
    char path[256];
    snprintf(path, sizeof path, "view_run/%s/%s", name, file);
    return read_file(path, out) && !out->empty();
}

// The next line of a text from *at, without its end, into `line`; false at
// the text's end.
bool next_line(const Array<u8>& text, usize* at, char* line, usize cap) {
    if (*at >= text.size())
        return false;
    usize n = 0;
    while (*at < text.size() && text[*at] != '\n') {
        if (n + 1 < cap && text[*at] != '\r')
            line[n++] = static_cast<char>(text[*at]);
        ++*at;
    }
    ++*at;
    line[n] = 0;
    return true;
}

// The next line of a report, as next_line; but a run carried on from saves
// says so in a line of its own with an empty one after it, which a run
// never stopped doesn't have: those two are passed over.
bool next_report_line(const Array<u8>& text, usize* at, char* line, usize cap) {
    if (!next_line(text, at, line, cap))
        return false;
    if (strncmp(line, "carried on from saves in ", 25) != 0)
        return true;
    return next_line(text, at, line, cap) && next_line(text, at, line, cap);
}

// Whether two origin reports say the same, their timings aside: the seeds'
// seconds, speed and paths a second in the table, the routes' microseconds
// a tick, and the wall time. The first line that differs is printed.
bool same_report(const Array<u8>& a, const Array<u8>& b) {
    static char la[4096], lb[4096];
    usize ia = 0, ib = 0;
    bool table = false;
    for (u32 n = 1;; ++n) {
        const bool more_a = next_report_line(a, &ia, la, sizeof la);
        const bool more_b = next_report_line(b, &ib, lb, sizeof lb);
        if (!more_a || !more_b)
            return more_a == more_b;
        bool same = strcmp(la, lb) == 0;
        if (table && !la[0])
            table = false;
        const char* ra = strstr(la, "; routes ");
        if (table) {
            // seed, ticks and instructions, then the three timings, then the rest.
            same = strlen(la) == strlen(lb) && strlen(la) >= 60 && memcmp(la, lb, 32) == 0 &&
                   strcmp(la + 60, lb + 60) == 0;
        } else if (strncmp(la, "wall time ", 10) == 0) {
            same = strncmp(lb, "wall time ", 10) == 0;
        } else if (ra) {
            // The columns of routes worked out are a count of work done, and a
            // run carried on works its routes out again: they go with the timings.
            const char* const kColumns = " columns worked out";
            const char *ka = strstr(la, kColumns), *kb = strstr(lb, kColumns);
            while (ka && ka > la && ka[-1] >= '0' && ka[-1] <= '9')
                --ka;
            while (kb && kb > lb && kb[-1] >= '0' && kb[-1] <= '9')
                --kb;
            if (ka && kb)
                ra = ka;
            const char* rb = ka && kb ? kb : strstr(lb, "; routes ");
            const char *ea = strstr(la, "; network devices"), *eb = strstr(lb, "; network devices");
            same = rb && ea && eb && ra - la == rb - lb && memcmp(la, lb, static_cast<usize>(ra - la)) == 0 &&
                   strcmp(ea, eb) == 0;
        }
        if (strstr(la, " secs ") && strstr(la, " paths/s "))
            table = true;
        if (!same) {
            fprintf(stderr, "  view: the reports differ at line %u:\n    %s\n    %s\n", n, la, lb);
            return false;
        }
    }
}

// Whether two runs wrote the same samples, turnover windows, genomes and
// report (timings aside), for each of the three seeds.
bool same_run(const char* a, const char* b) {
    static const char* const kFiles[3] = {"samples/seed-%u.csv", "windows/seed-%u.csv",
                                          "genomes/genomes-seed-%u.txt"};
    Array<u8> fa, fb;
    for (u32 seed = 1; seed <= 3; ++seed)
        for (u32 i = 0; i < 3; ++i) {
            char file[64];
            snprintf(file, sizeof file, kFiles[i], seed);
            if (!run_file(a, file, &fa) || !run_file(b, file, &fb) || fa.size() != fb.size() ||
                memcmp(fa.data(), fb.data(), fa.size()) != 0) {
                fprintf(stderr, "  view: %s differs between the runs %s and %s\n", file, a, b);
                return false;
            }
        }
    return run_file(a, "report.txt", &fa) && run_file(b, "report.txt", &fb) && same_report(fa, fb);
}

// What the tests' window said of itself at the run's end (stderr): its
// frames, those that read a held seed, the seeds it showed, and whether it
// was closed before the run ended. False: it said nothing.
bool window_said(const char* name, u32* frames, u32* held, u32* seeds, bool* closed) {
    Array<u8> err;
    static char line[4096];
    usize at = 0;
    if (!run_file(name, "err.txt", &err))
        return false;
    while (next_line(err, &at, line, sizeof line)) {
        const char* form = "nova2: view test: %u frames, %u of a held seed, %u seeds watched";
        if (sscanf(line, form, frames, held, seeds) == 3) {
            *closed = strstr(line, "closed before the run ended") != nullptr;
            return true;
        }
    }
    return false;
}

// The window on a run only watches (build-plan-viewer.md step 4): a run
// with it open writes what a run with it shut writes, on 1 and on 24 host
// threads a seed, while the window moves between the seeds, places and
// pages every frame; and closing the window part way leaves the run to end
// as it would have.
void test_run_window() {
    static const u32 kThreads[2] = {1, 24};
    u32 frames = 0, held = 0, seeds = 0, most = 0;
    bool closed = false;
    for (u32 t = 0; t < 2; ++t) {
        char shut[32], open[32];
        snprintf(shut, sizeof shut, "shut-%u", kThreads[t]);
        snprintf(open, sizeof open, "open-%u", kThreads[t]);
        CHECK(run_busy(shut, kThreads[t], ""));
        CHECK(run_busy(open, kThreads[t], "1"));
        CHECK(same_run(shut, open));
        CHECK(!window_said(shut, &frames, &held, &seeds, &closed));
        CHECK(window_said(open, &frames, &held, &seeds, &closed));
        CHECK(held >= 3 && seeds >= 2 && !closed);
        most = held > most ? held : most;
    }
    CHECK(run_busy("closed-1", 1, "close"));
    CHECK(same_run("shut-1", "closed-1"));
    CHECK(window_said("closed-1", &frames, &held, &seeds, &closed));
    CHECK(closed && frames == 5 && held >= 1);
    printf("  view: a run of three seeds wrote the same samples, windows, genomes and report with the window open as "
           "with it shut, on 1 and on 24 host threads (up to %u frames, each of a seed held between two ticks, moving "
           "between the three), and with the window closed after 5 frames\n",
           most);
}

// How many lines of a run's progress stream (stderr) hold `words`; and the
// last of them, if `last` is given.
u32 said_lines(const char* name, const char* words, char* last = nullptr, usize size = 0) {
    Array<u8> err;
    static char line[4096];
    usize at = 0;
    u32 n = 0;
    if (!run_file(name, "err.txt", &err))
        return 0;
    while (next_line(err, &at, line, sizeof line))
        if (strstr(line, words)) {
            ++n;
            if (last)
                snprintf(last, size, "%s", line);
        }
    return n;
}

// A window on a run already going (build-plan-viewer.md step 7), and a
// save asked of it (step 8): a run started with no window opens one when
// `nova2 watch` asks, and again each time it is asked after the window has
// closed; `nova2 save` has its seeds save their worlds; and its files are
// those of a run never watched. (The run's seeds wait for the first window,
// the tests' way of making sure there is one; each window closes itself
// after 5 frames.)
void test_run_attach() {
    PROCESS_INFORMATION pi = {};
    CHECK(start_busy("attach-1", 1, "attach", " --set origin.saves=view_run/attach-1/saves", &pi));
    if (!pi.hProcess)
        return;
    const unsigned long pid = pi.dwProcessId;
    char cmd[600], name[64];
    snprintf(cmd, sizeof cmd, "\"\"%s\" watch %lu > view_run\\attach-1\\watch.txt 2>&1\"", NOVA2_EXE, pid);
    CHECK(system(cmd) == 0);
    // From here the signal is set as the command sets it, every 20 ms to the
    // run's end, and once on the way the seeds are asked to save.
    snprintf(name, sizeof name, "Local\\nova2-watch-%lu", pid);
    HANDLE watch = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    CHECK(watch != nullptr);
    u32 asked = 0;
    bool save_asked = false;
    while (WaitForSingleObject(pi.hProcess, 20) == WAIT_TIMEOUT) {
        if (watch)
            SetEvent(watch);
        if (++asked == 8) {
            snprintf(cmd, sizeof cmd, "\"\"%s\" save %lu > view_run\\attach-1\\save.txt 2>&1\"", NOVA2_EXE, pid);
            save_asked = system(cmd) == 0;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (watch)
        CloseHandle(watch);
    CHECK(code == 0 && save_asked);
    CHECK(same_run("shut-1", "attach-1"));
    const u32 windows = said_lines("attach-1", "nova2: view test:");
    CHECK(windows >= 2);
    // The saves: one a seed still running when asked, each a whole world
    // that reads back as what it says it is.
    static char line[4096];
    const u32 saves = said_lines("attach-1", "saved its world at tick", line, sizeof line);
    CHECK(saves >= 1 && saves <= 3);
    unsigned long long seed = 0, tick = 0;
    char path[300] = "";
    if (saves && sscanf(line, " seed %llu: saved its world at tick %llu as %299s", &seed, &tick, path) == 3) {
        Array<u8> bytes;
        SnapshotHeader h;
        TextBuf why;
        CHECK(read_file(path, &bytes) && snapshot_parse(bytes.data(), bytes.size(), &h, &why));
        CHECK(h.flags == 0 && h.seed == seed && h.tick == tick && tick > 0 && tick < 12000);
    } else {
        CHECK(false);
    }
    // And no run is left listening: the command says so.
    snprintf(cmd, sizeof cmd, "\"\"%s\" watch %lu > view_run\\attach-1\\watch.txt 2>&1\"", NOVA2_EXE, pid);
    CHECK(system(cmd) == 1);
    printf("  view: a run started with no window opened one %u times when asked from outside, saved %u seeds' worlds "
           "when asked, and wrote the files of a run never watched\n",
           windows, saves);
}

// `nova2 resume` on a save of the run `name`, for `ticks` ticks: the tick
// it reached and its world's hash.
bool resume_hash(const char* name, const char* save, u32 ticks, u32 threads, u64* tick, u64* hash) {
    char cmd[800];
    snprintf(cmd, sizeof cmd,
             "\"\"%s\" resume view_run/%s/saves/%s.world.snap --set run.ticks=%u --set run.threads=%u > "
             "view_run\\%s\\resume.txt 2>&1\"",
             NOVA2_EXE, name, save, ticks, threads, name);
    if (system(cmd) != 0)
        return false;
    Array<u8> out;
    static char line[4096];
    usize at = 0;
    bool got_tick = false, got_hash = false;
    if (!run_file(name, "resume.txt", &out))
        return false;
    while (next_line(out, &at, line, sizeof line)) {
        unsigned long long v = 0;
        if (sscanf(line, "tick %llu", &v) == 1) {
            *tick = v;
            got_tick = true;
        } else if (sscanf(line, "hash %llx", &v) == 1) {
            *hash = v;
            got_hash = true;
        }
    }
    return got_tick && got_hash;
}

// A seed's world saved mid-run (build-plan-viewer.md step 8): a run that
// saves every 4,000 ticks writes the files of one that doesn't; and a save
// carried on by `nova2 resume` goes as the run went: from tick 4,000 to
// tick 8,000 it reaches the world the run saved at tick 8,000, on 1 host
// thread and on 24.
void test_run_saves() {
    CHECK(run_busy("saves-1", 1, "",
                   " --set origin.save_every=4000 --set origin.saves_kept=0"
                   " --set origin.saves=view_run/saves-1/saves"));
    CHECK(same_run("shut-1", "saves-1"));
    CHECK(said_lines("saves-1", "saved its world at tick") == 9);
    for (u32 seed = 1; seed <= 3; ++seed) {
        char early[48], late[48];
        snprintf(early, sizeof early, "seed-%u-tick-4000", seed);
        snprintf(late, sizeof late, "seed-%u-tick-8000", seed);
        u64 t0 = 0, h0 = 0, t1 = 0, h1 = 1, t24 = 0, h24 = 2, t4 = 0, h4 = 0;
        CHECK(resume_hash("saves-1", late, 0, 1, &t0, &h0) && t0 == 8000);
        CHECK(resume_hash("saves-1", early, 4000, 1, &t1, &h1) && t1 == 8000 && h1 == h0);
        CHECK(resume_hash("saves-1", early, 4000, 24, &t24, &h24) && t24 == 8000 && h24 == h0);
        CHECK(resume_hash("saves-1", early, 0, 1, &t4, &h4) && t4 == 4000 && h4 != h0);
    }
    printf("  view: a run saving its three seeds' worlds every 4,000 ticks wrote the files of one that doesn't, and "
           "each save of tick 4,000 carried on 4,000 ticks reached the world saved at tick 8,000, on 1 host thread "
           "and on 24\n");
}

// Whether a file is there.
bool there(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

// Removes the saves an earlier run of the tests left in view_run/<name>/saves:
// a run carried on takes the newest it finds, whoever wrote it.
void forget_saves(const char* name) {
    char pattern[160], path[400];
    snprintf(pattern, sizeof pattern, "view_run/%s/saves/seed-*.snap*", name);
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        snprintf(path, sizeof path, "view_run/%s/saves/%s", name, found.cFileName);
        DeleteFileA(path);
    } while (FindNextFileA(h, &found));
    FindClose(h);
}

// The origin test carried on from saves (build-plan-viewer.md step 9): a
// run stopped and carried on writes the samples, turnover windows, genomes
// and report of a run never stopped, whichever way it stopped:
//  - it reached its ticks (5,000), and is carried on with more (12,000), on
//    1 host thread a seed and on 24;
//  - it was killed part way, as by a power cut, and each seed is carried on
//    from its newest save, or from the start if it had none.
// And a run under other settings than its saves' is refused.
void test_run_resume() {
    forget_saves("first-1");
    forget_saves("cut-1");
    // Stopped at its ticks. Saving every 2,000, each seed took saves at
    // ticks 2,000, 4,000 and 5,000 (where it stopped); finished, it keeps
    // only the last (Damian, 2026-11-20).
    CHECK(run_busy("first-1", 1, "",
                   " --set origin.ticks=5000 --set origin.save_every=2000 --set origin.saves=view_run/first-1/saves"));
    for (u32 seed = 1; seed <= 3; ++seed) {
        char path[128];
        static const u32 kTicks[3] = {2000, 4000, 5000};
        for (u32 i = 0; i < 3; ++i)
            for (u32 k = 0; k < 2; ++k) {
                snprintf(path, sizeof path, "view_run/first-1/saves/seed-%u-tick-%u.%s.snap", seed, kTicks[i],
                         k ? "test" : "world");
                CHECK(there(path) == (i == 2)); // the earlier ones went as the seed finished
            }
    }
    static const u32 kThreads[2] = {1, 24};
    for (u32 t = 0; t < 2; ++t) {
        char name[32], whole[32];
        snprintf(name, sizeof name, "second-%u", kThreads[t]);
        snprintf(whole, sizeof whole, "shut-%u", kThreads[t]);
        CHECK(run_busy(name, kThreads[t], "", " --set origin.resume=view_run/first-1/saves"));
        CHECK(said_lines(name, "carried on from tick 5000") == 3);
        CHECK(same_run(whole, name));
    }
    // Killed part way: once a seed has saved, the run is ended where it
    // stands. (If it ends by itself first, its seeds are carried on from
    // where they stopped, which must come to the same.)
    PROCESS_INFORMATION pi = {};
    CHECK(start_busy("cut-1", 1, nullptr, " --set origin.save_every=1500 --set origin.saves=view_run/cut-1/saves",
                     &pi));
    if (!pi.hProcess)
        return;
    u32 waited = 0;
    while (WaitForSingleObject(pi.hProcess, 10) == WAIT_TIMEOUT && waited < 3000) {
        ++waited;
        if (there("view_run/cut-1/saves/seed-2-tick-3000.test.snap")) {
            TerminateProcess(pi.hProcess, 9);
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    const bool killed = code == 9;
    CHECK(run_busy("mended-1", 1, "", " --set origin.resume=view_run/cut-1/saves"));
    const u32 carried = said_lines("mended-1", "carried on from tick");
    CHECK(carried >= 1 && carried <= 3 && carried + said_lines("mended-1", "it starts at tick 0") == 3);
    CHECK(same_run("shut-1", "mended-1"));
    // Under other settings a seed is not carried on, and the run says why.
    CHECK(run_busy("refused-1", 1, "", " --set origin.resume=view_run/first-1/saves --set observe.sample_every=7"));
    CHECK(said_lines("refused-1", "can't be carried on from tick 5000") == 3);
    CHECK(said_lines("refused-1", "it was saved with observe.sample_every = 5, and this run has 7") == 3);
    printf("  view: a run of three seeds stopped at tick 5,000 and carried on to 12,000 from its saves wrote the "
           "samples, windows, genomes and report of a run never stopped, on 1 and on 24 host threads; so did one "
           "%s (%u of its seeds carried on from a save); and a run under other settings was refused\n",
           killed ? "killed part way" : "that ended before it could be killed", carried);
}

// Whether two runs' record files (origin.records) are the same, byte for
// byte, for all three seeds.
bool same_records(const char* a, const char* b) {
    static const char* const kKinds[7] = {"samples",      "volts",    "windows",      "genomes",
                                          "genome_bytes", "lineages", "lineage_tally"};
    Array<u8> fa, fb;
    for (u32 seed = 1; seed <= 3; ++seed)
        for (u32 i = 0; i < 7; ++i) {
            char pa[160], pb[160];
            snprintf(pa, sizeof pa, "%s/seed-%u.%s", a, seed, kKinds[i]);
            snprintf(pb, sizeof pb, "%s/seed-%u.%s", b, seed, kKinds[i]);
            // The finished lineages' file may be empty: in a short run each is one
            // program that never bred, counted in the tally beside it.
            if (!read_file(pa, &fa) || !read_file(pb, &fb) || (fa.size() == 0 && i != 5) || fa.size() != fb.size() ||
                memcmp(fa.data(), fb.data(), fa.size()) != 0) {
                fprintf(stderr, "  view: %s differs from %s\n", pa, pb);
                return false;
            }
        }
    return true;
}

// The observers' records written to files as the run goes (origin.records;
// design.md 13.1, build-plan-observe.md step 3): a run with them writes the
// same samples, windows, genomes and report as one that keeps them in
// memory; and a run carried on from a save older than where it stopped
// cuts its record files back to the save and comes to the same files as a
// run never stopped.
void test_run_records() {
    DeleteFileA("view_run/rec-whole/samples/seed-1.csv"); // a run before this one's
    CHECK(run_busy("rec-whole", 1, "", " --set origin.records=view_run/rec-whole/records"));
    // Its samples are stored once, in the record files: the run wrote no CSV,
    // and `nova2 samples` makes from the records the CSV of the run that kept
    // them in memory, byte for byte.
    Array<u8> none;
    CHECK(!run_file("rec-whole", "samples/seed-1.csv", &none));
    CHECK(make_samples("rec-whole", " --set origin.records=view_run/rec-whole/records"));
    CHECK(same_run("shut-1", "rec-whole"));
    // Stopped at 5,000 with every save kept (origin.saves_kept = 0), the newest taken away:
    // carried on from 4,000, the files hold 1,000 ticks of rows past the
    // save, which must go.
    forget_saves("rec-first");
    CHECK(run_busy("rec-first", 1, "",
                   " --set origin.ticks=5000 --set origin.save_every=2000 --set origin.saves=view_run/rec-first/saves"
                   " --set origin.saves_kept=0 --set origin.records=view_run/rec-first/records"));
    for (u32 seed = 1; seed <= 3; ++seed)
        for (u32 k = 0; k < 2; ++k) {
            char path[128];
            snprintf(path, sizeof path, "view_run/rec-first/saves/seed-%u-tick-5000.%s.snap", seed,
                     k ? "test" : "world");
            CHECK(DeleteFileA(path));
        }
    CHECK(run_busy("rec-second", 1, "",
                   " --set origin.resume=view_run/rec-first/saves --set origin.records=view_run/rec-first/records"));
    CHECK(said_lines("rec-second", "carried on from tick 4000") == 3);
    CHECK(make_samples("rec-second", " --set origin.records=view_run/rec-first/records"));
    CHECK(same_run("shut-1", "rec-second"));
    CHECK(same_records("view_run/rec-whole/records", "view_run/rec-first/records"));
    printf("  view: a run writing its samples, voltages, windows, kept genomes and finished lineages to files as it "
           "went wrote the files of one keeping them in memory; stopped at tick 5,000 and carried on from its save "
           "of tick 4,000, it cut its record files back and came to the same files as a run never stopped\n");
}

// The storage changes of 2026-11-20 on a run (build-plan-gate5.md 11).
// - The kept genomes' record, written as differences: nova2 genomes builds
//   every genome of each seed from the files, each on its own, and each
//   matches the hash in its row.
// - Writing every second sample (observe.write_every = 2): the samples'
//   CSV is the usual run's rows at every second sample's tick with its last
//   row, the voltages' record is half as long, and the windows, the genomes
//   and the report are the usual run's.
// - Such a run stopped at tick 4,005, between two written rows, and carried
//   on comes to the record files of one never stopped. (A carry-on reads a
//   save with no groups' index in it and works it out again: this and the
//   carry-ons above are that check on a run.)
void test_run_storage() {
    char cmd[700];
    for (u32 seed = 1; seed <= 3; ++seed) {
        snprintf(cmd, sizeof cmd,
                 "\"\"%s\" genomes view_run/rec-whole/records %u --each > view_run\\rec-whole\\genomes-%u.txt 2>&1\"",
                 NOVA2_EXE, seed, seed);
        CHECK(system(cmd) == 0);
        char file[64];
        snprintf(file, sizeof file, "genomes-%u.txt", seed);
        Array<u8> said;
        CHECK(run_file("rec-whole", file, &said));
        said.push(0);
        const char* text = reinterpret_cast<const char*>(said.data());
        CHECK(strstr(text, "as differences") && strstr(text, ": 0 wrong") && !strstr(text, " 0 kept genomes"));
    }
    const char* half = " --set observe.write_every=2 --set origin.records=view_run/half-whole/records";
    CHECK(run_busy("half-whole", 1, "", half));
    CHECK(make_samples("half-whole", half));
    static const char* const kFiles[2] = {"windows/seed-%u.csv", "genomes/genomes-seed-%u.txt"};
    Array<u8> fa, fb;
    static char line[8192];
    for (u32 seed = 1; seed <= 3; ++seed) {
        char file[64];
        for (u32 i = 0; i < 2; ++i) {
            snprintf(file, sizeof file, kFiles[i], seed);
            CHECK(run_file("shut-1", file, &fa) && run_file("half-whole", file, &fb) && fa.size() == fb.size() &&
                  memcmp(fa.data(), fb.data(), fa.size()) == 0);
        }
        // The usual run's rows at every second sample's tick (samples come
        // every 5 ticks here), its header and its last row with them.
        snprintf(file, sizeof file, "samples/seed-%u.csv", seed);
        CHECK(run_file("shut-1", file, &fa) && run_file("half-whole", file, &fb));
        Array<u8> want;
        usize at = 0;
        u32 rows = 0, kept = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            const bool head = rows++ == 0;
            if (!head && strtoull(line, nullptr, 10) % 10 != 0)
                continue;
            kept += head ? 0 : 1;
            want.append(reinterpret_cast<const u8*>(line), strlen(line));
            want.push('\n');
        }
        Array<u8> got; // without the line ends' carriage returns, as next_line gives the other
        at = 0;
        while (next_line(fb, &at, line, sizeof line)) {
            got.append(reinterpret_cast<const u8*>(line), strlen(line));
            got.push('\n');
        }
        CHECK(rows == 2402 && kept == 1201 && got.size() == want.size() &&
              memcmp(got.data(), want.data(), got.size()) == 0);
        char pa[160], pb[160];
        snprintf(pa, sizeof pa, "view_run/rec-whole/records/seed-%u.volts", seed);
        snprintf(pb, sizeof pb, "view_run/half-whole/records/seed-%u.volts", seed);
        CHECK(read_file(pa, &fa) && read_file(pb, &fb) && fa.size() == 2401u * 6 * 4 && fb.size() == 1201u * 6 * 4);
    }
    CHECK(run_file("shut-1", "report.txt", &fa) && run_file("half-whole", "report.txt", &fb) && same_report(fa, fb));
    forget_saves("half-first");
    CHECK(run_busy("half-first", 1, "",
                   " --set observe.write_every=2 --set origin.ticks=4005 --set origin.save_every=1335"
                   " --set origin.saves=view_run/half-first/saves --set origin.records=view_run/half-first/records"));
    CHECK(run_busy("half-second", 1, "",
                   " --set observe.write_every=2 --set origin.resume=view_run/half-first/saves"
                   " --set origin.records=view_run/half-first/records"));
    CHECK(said_lines("half-second", "carried on from tick 4005") == 3);
    CHECK(same_records("view_run/half-whole/records", "view_run/half-first/records"));
    CHECK(run_file("half-second", "report.txt", &fb) && same_report(fa, fb));
    printf("  view: every kept genome of a run's three seeds built from its record of differences and matched its "
           "hash; a run writing every second sample wrote the usual run's rows at those ticks, half the voltages, "
           "and the same windows, genomes and report; stopped at tick 4,005, between two written rows, and carried "
           "on, it came to the same record files\n");
}

// Field `k` of a CSV line (from 0), up to its comma; "" past the last.
const char* csv_field(const char* line, u32 k) {
    for (; k && *line; ++line)
        k -= *line == ',' ? 1 : 0;
    return line;
}

// The classes files an earlier test run left in view_run/<run>/<folder>: a
// classing keeps the rows of a file it finds, and refuses one of other
// columns.
void forget_classes(const char* run, const char* folder) {
    char pattern[160], path[400];
    snprintf(pattern, sizeof pattern, "view_run/%s/%s/seed-*-classes.csv", run, folder);
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        snprintf(path, sizeof path, "view_run/%s/%s/%s", run, folder, found.cFileName);
        DeleteFileA(path);
    } while (FindNextFileA(h, &found));
    FindClose(h);
}

// `nova2 worlds` with worlds.test = classes on the world of test_run_classes:
// seed `seed`, classings from `from` every 3,000 ticks to `to`, the places
// kept full 300 ticks, into view_run/<run>/<folder>, with `more` set too.
bool run_classes(const char* world, u32 seed, u32 threads, u64 from, u64 to, const char* folder,
                 const char* run = "classes", const char* more = "") {
    char cmd[2400];
    snprintf(cmd, sizeof cmd,
             "\"\"%s\" worlds --set memory.size=32768 --set disk.size=65536 --set hw.cores.max=4%s%s "
             "--set worlds.test=classes --set world.seed=%u --set worlds.ticks=300 --set worlds.class_every=3000 "
             "--set worlds.from=%llu --set worlds.class_to=%llu --set worlds.classes=view_run/%s/%s "
             "--set run.threads=%u > view_run\\%s\\classes-%s-%u.txt 2>&1\"",
             NOVA2_EXE, world, more, seed, static_cast<unsigned long long>(from), static_cast<unsigned long long>(to),
             run, folder, threads, run, folder, seed);
    return system(cmd) == 0;
}

// Gate 4's mark as changed (build-plan-gate4.md 11; build-plan-gate5.md 11):
// ground named by what a full place gets there, from the classing tool.
// - A run of three seeds on a small map under sources that come and go. Each
//   seed is classed at five ticks (nova2 worlds, classes) into the run's
//   windows folder, and the finished run carried on for its report.
// - The classing's rows are the same on 1 host thread and on 3, and a
//   classing from a tick is the same whether or not the earlier ticks were
//   classed in the same call.
// - The report's place-windows by class are those worked here from the two
//   files: each row of a seed's ground record under the class its place had
//   at the latest classing at or before the window's start.
void test_run_classes() {
    const char* world = " --set world.places=12 --set world.map=landscape --set supply.model=sources "
                        "--set heat.on=true --set sources.start=40 --set sources.life=3000 "
                        "--set sources.chance=20000 --set supply.day=600";
    char more[700];
    forget_saves("classes");
    const char* const kFolders[4] = {"windows", "one", "split", "alone"};
    for (u32 k = 0; k < 4; ++k)
        forget_classes("classes", kFolders[k]);
    snprintf(more, sizeof more, "%s --set origin.saves=view_run/classes/saves --set origin.save_every=6000", world);
    CHECK(run_busy("classes", 1, "", more));
    for (u32 seed = 1; seed <= 3; ++seed)
        CHECK(run_classes(world, seed, 3, 0, 12000, "windows"));
    Array<u8> fa, fb;
    CHECK(run_classes(world, 1, 1, 0, 12000, "one"));
    CHECK(run_file("classes", "windows/seed-1-classes.csv", &fa) && run_file("classes", "one/seed-1-classes.csv", &fb));
    CHECK(fa.size() == fb.size() && memcmp(fa.data(), fb.data(), fa.size()) == 0);
    // In two calls, the second beginning at tick 6,000: the same file. And
    // tick 9,000 alone is the whole call's rows of that tick.
    CHECK(run_classes(world, 1, 3, 0, 3000, "split") && run_classes(world, 1, 3, 6000, 12000, "split"));
    CHECK(run_file("classes", "split/seed-1-classes.csv", &fb));
    CHECK(fa.size() == fb.size() && memcmp(fa.data(), fb.data(), fa.size()) == 0);
    CHECK(run_classes(world, 1, 3, 9000, 0, "alone") && run_file("classes", "alone/seed-1-classes.csv", &fb));
    static char line[4096];
    {
        Array<u8> want;
        usize at = 0;
        u32 rows = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ != 0 && strtoull(line, nullptr, 10) != 9000)
                continue;
            want.append(reinterpret_cast<const u8*>(line), strlen(line));
            want.push('\n');
        }
        Array<u8> got;
        at = 0;
        while (next_line(fb, &at, line, sizeof line)) {
            got.append(reinterpret_cast<const u8*>(line), strlen(line));
            got.push('\n');
        }
        CHECK(rows == 1 + 5 * 12 && got.size() == want.size() && memcmp(got.data(), want.data(), got.size()) == 0);
    }
    // The report, made again by carrying the finished run on.
    snprintf(more, sizeof more,
             "%s --set origin.resume=view_run/classes/saves --set origin.windows=view_run/classes/windows", world);
    CHECK(run_busy("classes-again", 1, "", more));
    CHECK(said_lines("classes-again", "carried on from tick 12000") == 3);
    Array<u8> report;
    CHECK(run_file("classes-again", "report.txt", &report));
    u64 all = 0, not_fair = 0, measured = 0;
    for (u32 seed = 1; seed <= 3; ++seed) {
        char file[64];
        // The seed's classings: [classing][place], 0 fair, 1 hot, 2 dim.
        u64 ticks[8] = {};
        u8 cls[8][12] = {};
        u32 classings = 0;
        snprintf(file, sizeof file, "windows/seed-%u-classes.csv", seed);
        CHECK(run_file("classes", file, &fa));
        usize at = 0;
        u32 rows = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ == 0)
                continue;
            const u64 tick = strtoull(line, nullptr, 10);
            const u32 place = static_cast<u32>(strtoul(csv_field(line, 1), nullptr, 10));
            const char* name = csv_field(line, 6);
            if (!classings || ticks[classings - 1] != tick)
                ticks[classings < 8 ? classings++ : 7] = tick;
            CHECK(place < 12);
            if (place < 12)
                cls[classings - 1][place] =
                    strncmp(name, "hot,", 4) == 0 ? 1 : strncmp(name, "dim,", 4) == 0 ? 2 : 0;
        }
        CHECK(rows == 1 + 5 * 12 && classings == 5 && ticks[4] == 12000);
        // Its lax place-windows under them.
        u64 windows[3] = {}, starters[3] = {};
        snprintf(file, sizeof file, "windows/seed-%u-ground.csv", seed);
        CHECK(run_file("classes", file, &fa));
        at = 0;
        rows = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ == 0)
                continue;
            {
                // The reviewer's measures at the row's end stand inside the
                // row's own counts: core time worked within what the cores
                // had left, copies among the births and the starters, and
                // the largest program's threads among the place's.
                const u64 cores = strtoull(csv_field(line, 4), nullptr, 10);
                const u64 lost = strtoull(csv_field(line, 5), nullptr, 10) + strtoull(csv_field(line, 6), nullptr, 10);
                const u64 row_starters = strtoull(csv_field(line, 10), nullptr, 10);
                const u64 births = strtoull(csv_field(line, 11), nullptr, 10);
                const u64 lead_copies = strtoull(csv_field(line, 20), nullptr, 10);
                const u64 worked = strtoull(csv_field(line, 22), nullptr, 10);
                const u64 births_copies = strtoull(csv_field(line, 23), nullptr, 10);
                const u64 starters_copies = strtoull(csv_field(line, 24), nullptr, 10);
                const u64 threads = strtoull(csv_field(line, 25), nullptr, 10);
                const u64 top = strtoull(csv_field(line, 26), nullptr, 10);
                CHECK(csv_field(line, 26)[0] != 0 && worked + lost <= cores * 100);
                CHECK(births_copies <= births && starters_copies <= row_starters && starters_copies >= lead_copies);
                CHECK(top <= threads && (row_starters == 0 || threads >= row_starters));
                measured += worked ? 1 : 0;
            }
            if (csv_field(line, 1)[0] != '0')
                continue;
            const u32 place = static_cast<u32>(strtoul(line, nullptr, 10));
            const u64 start = strtoull(csv_field(line, 3), nullptr, 10);
            u32 k = classings;
            while (k > 1 && ticks[k - 1] > start)
                --k;
            const u8 c = cls[k - 1][place < 12 ? place : 0];
            ++windows[c];
            starters[c] += strtoull(csv_field(line, 10), nullptr, 10);
        }
        // The report's line for the seed, under "ground by class".
        char want[200], label[32], mean[3][16];
        for (u32 c = 0; c < 3; ++c) {
            if (windows[c])
                snprintf(mean[c], sizeof mean[c], "%.2f", static_cast<double>(starters[c]) / windows[c]);
            else
                snprintf(mean[c], sizeof mean[c], "-");
        }
        snprintf(label, sizeof label, "seed %u, lax", seed);
        snprintf(want, sizeof want, "  %-20s %10llu %9s %10llu %9s %10llu %9s", label,
                 static_cast<unsigned long long>(windows[0]), mean[0], static_cast<unsigned long long>(windows[1]),
                 mean[1], static_cast<unsigned long long>(windows[2]), mean[2]);
        at = 0;
        bool under = false, found = false;
        while (next_line(report, &at, line, sizeof line)) {
            under = under || strncmp(line, "ground by class:", 16) == 0;
            found = found || (under && strcmp(line, want) == 0);
        }
        CHECK(found);
        all += windows[0] + windows[1] + windows[2];
        not_fair += windows[1] + windows[2];
    }
    CHECK(all > 1000 && not_fair > 100 && measured > 1000);
    printf("  view: three seeds on a map under sources that come and go, each classed at five ticks: the rows the "
           "same on 1 host thread and 3, in one call and in two, and a tick's alone; the finished run carried on "
           "reported its %llu lax place-windows by class (%llu of them hot or dim) as worked here from the two "
           "files\n",
           static_cast<unsigned long long>(all), static_cast<unsigned long long>(not_fair));
}

// The classing worked again with the run's respawns applied, and its
// straits as their spells stand (build-plan-gate5.md 11, gate 4's mark on
// the combined run). A run of three seeds whose places wear out and respawn,
// with straits that part and join every few thousand ticks:
// - each seed classed with worlds.respawns, so its classings' worlds replay
//   the run's respawns (the tool stops if one isn't the run's); every row's
//   incarnation is its place's respawns at or before the row's tick;
// - the report's place-windows by class are those worked here from the
//   three files: a window of incarnation k takes its place's latest row at
//   or before its start if that row is of k, lax or strict as the row has
//   it; one with a respawn of its place inside it is left out and counted.
void test_run_classes_respawns() {
    const char* world = " --set world.places=12 --set world.map=landscape --set supply.model=sources "
                        "--set heat.on=true --set sources.start=40 --set sources.life=3000 "
                        "--set sources.chance=20000 --set supply.day=600 --set wear.rate=1 "
                        "--set wear.p.endurance=500 --set wear.e.endurance=500 --set wear.memory.endurance=500 "
                        "--set wear.disk.endurance=400 --set wear.network.endurance=400 "
                        "--set wear.cooler.endurance=400 --set visit.mean=250 --set visit.regularity=0.3 "
                        "--set visit.replace_at=0.8 --set visit.respawn_share=0.3 --set strait.share=1 "
                        "--set strait.spell.min=500 --set strait.spell.max=3000";
    char more[1400];
    forget_saves("classes-r");
    forget_classes("classes-r", "windows");
    snprintf(more, sizeof more, "%s --set origin.saves=view_run/classes-r/saves --set origin.save_every=6000", world);
    CHECK(run_busy("classes-r", 1, "", more));
    for (u32 seed = 1; seed <= 3; ++seed)
        CHECK(run_classes(world, seed, 3, 0, 12000, "windows", "classes-r",
                          " --set worlds.respawns=view_run/classes-r/windows"));
    snprintf(more, sizeof more,
             "%s --set origin.resume=view_run/classes-r/saves --set origin.windows=view_run/classes-r/windows", world);
    CHECK(run_busy("classes-r-again", 1, "", more));
    CHECK(said_lines("classes-r-again", "carried on from tick 12000") == 3);
    Array<u8> report, fa;
    CHECK(run_file("classes-r-again", "report.txt", &report));
    static char line[4096];
    const u64 kWindow = 100; // observe.turnover_window, as busy_command sets it
    u64 respawns = 0, later = 0, straddled = 0, all = 0;
    for (u32 seed = 1; seed <= 3; ++seed) {
        char file[64];
        // The run's respawns, by place.
        u64 when[12][64] = {};
        u32 count[12] = {};
        snprintf(file, sizeof file, "windows/seed-%u-respawns.csv", seed);
        CHECK(run_file("classes-r", file, &fa));
        usize at = 0;
        u32 rows = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ == 0)
                continue;
            const u32 place = static_cast<u32>(strtoul(csv_field(line, 1), nullptr, 10));
            CHECK(place < 12);
            if (place < 12 && count[place] < 64)
                when[place][count[place]++] = strtoull(line, nullptr, 10);
            ++respawns;
        }
        // The seed's rows, by place in tick order: the tick, the class (0
        // fair, 1 hot, 2 dim), strictness and incarnation. Five classings of
        // every place, and a place that respawned off a classing's tick
        // classed alone at its respawn's.
        u64 row_tick[12][80] = {};
        u8 cls[12][80] = {}, strict[12][80] = {};
        u32 inc[12][80] = {}, kept[12] = {};
        u32 full = 0, alone = 0;
        snprintf(file, sizeof file, "windows/seed-%u-classes.csv", seed);
        CHECK(run_file("classes-r", file, &fa));
        at = 0;
        rows = 0;
        u64 last = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ == 0) {
                CHECK(strstr(line, ",incarnation") != nullptr);
                continue;
            }
            const u64 tick = strtoull(line, nullptr, 10);
            const u32 place = static_cast<u32>(strtoul(csv_field(line, 1), nullptr, 10));
            CHECK(place < 12 && tick >= last);
            last = tick;
            if (place >= 12 || kept[place] >= 80)
                continue;
            const u32 r = kept[place]++;
            row_tick[place][r] = tick;
            const char* name = csv_field(line, 6);
            cls[place][r] = strncmp(name, "hot,", 4) == 0 ? 1 : strncmp(name, "dim,", 4) == 0 ? 2 : 0;
            strict[place][r] = csv_field(line, 2)[0] == '1' ? 1 : 0;
            inc[place][r] = static_cast<u32>(strtoul(csv_field(line, 15), nullptr, 10));
            u32 k = 0;
            while (k < count[place] && when[place][k] <= tick)
                ++k;
            CHECK_EQ_U64(inc[place][r], k);
            if (tick % 3000 == 0)
                ++full;
            else
                ++alone;
        }
        // Each respawn off a classing's tick has its place's row at its tick.
        u32 off_grid = 0;
        for (u32 p = 0; p < 12; ++p)
            for (u32 j = 0; j < count[p]; ++j) {
                if (when[p][j] % 3000 == 0)
                    continue;
                ++off_grid;
                bool has = false;
                for (u32 r = 0; r < kept[p]; ++r)
                    has = has || row_tick[p][r] == when[p][j];
                CHECK(has);
            }
        CHECK(full == 5 * 12 && alone == off_grid);
        // The lax place-windows under them.
        u64 windows[3] = {}, starters[3] = {};
        snprintf(file, sizeof file, "windows/seed-%u-ground.csv", seed);
        CHECK(run_file("classes-r", file, &fa));
        at = 0;
        rows = 0;
        while (next_line(fa, &at, line, sizeof line)) {
            if (rows++ == 0)
                continue;
            const u32 place = static_cast<u32>(strtoul(line, nullptr, 10));
            const u64 start = strtoull(csv_field(line, 3), nullptr, 10);
            if (place >= 12)
                continue;
            ++all;
            u32 k = 0;
            while (k < count[place] && when[place][k] <= start)
                ++k;
            if (k < count[place] && when[place][k] < start + kWindow) {
                ++straddled;
                continue;
            }
            // Its place's latest row at or before its start: of its
            // incarnation, since a respawn's tick has its own row.
            u32 r = kept[place];
            while (r > 0 && row_tick[place][r - 1] > start)
                --r;
            CHECK(r > 0 && inc[place][r - 1] == k);
            if (r == 0 || inc[place][r - 1] != k || strict[place][r - 1])
                continue;
            later += k ? 1 : 0;
            ++windows[cls[place][r - 1]];
            starters[cls[place][r - 1]] += strtoull(csv_field(line, 10), nullptr, 10);
        }
        // The report's line for the seed, under "ground by class".
        char want[200], label[32], mean[3][16];
        for (u32 c = 0; c < 3; ++c) {
            if (windows[c])
                snprintf(mean[c], sizeof mean[c], "%.2f", static_cast<double>(starters[c]) / windows[c]);
            else
                snprintf(mean[c], sizeof mean[c], "-");
        }
        snprintf(label, sizeof label, "seed %u, lax", seed);
        snprintf(want, sizeof want, "  %-20s %10llu %9s %10llu %9s %10llu %9s", label,
                 static_cast<unsigned long long>(windows[0]), mean[0], static_cast<unsigned long long>(windows[1]),
                 mean[1], static_cast<unsigned long long>(windows[2]), mean[2]);
        at = 0;
        bool under = false, found = false;
        while (next_line(report, &at, line, sizeof line)) {
            under = under || strncmp(line, "ground by class:", 16) == 0;
            found = found || (under && strcmp(line, want) == 0);
        }
        CHECK(found);
    }
    // The report's count of those left out with a respawn inside them.
    char want[160];
    snprintf(want, sizeof want,
             "  with the respawns applied: place-windows with a respawn of their place inside them, left out: %llu",
             static_cast<unsigned long long>(straddled));
    usize at = 0;
    bool found = false;
    while (next_line(report, &at, line, sizeof line))
        found = found || strcmp(line, want) == 0;
    CHECK(found);
    CHECK(respawns > 10 && later > 100 && straddled > 0);
    printf("  view: three seeds whose places respawn (%llu respawns), with straits that part, each classed with the "
           "run's respawns replayed: every row's incarnation the run's at its tick; the finished run carried on "
           "reported its place-windows by class as worked here, %llu lax ones after a respawn among them, and %llu "
           "of its %llu left out with a respawn inside\n",
           static_cast<unsigned long long>(respawns), static_cast<unsigned long long>(later),
           static_cast<unsigned long long>(straddled), static_cast<unsigned long long>(all));
}

// ---------------------------------------------------------------------------
// Step 5: the settings window
// ---------------------------------------------------------------------------

// A click on the nth button of a frame labelled `label` (0: the first).
bool click_nth(Canvas& c, ViewState& st, World& w, const char* label, u32 nth, const Observer* obs) {
    ButtonLog log;
    UiInput look;
    look.log = &log;
    frame(c, st, w, look, obs);
    for (u32 i = 0; i < log.count; ++i) {
        if (strcmp(log.label[i], label) != 0 || nth-- != 0)
            continue;
        UiInput in;
        in.mx = log.at[i].x + log.at[i].w / 2;
        in.my = log.at[i].y + log.at[i].h / 2;
        in.down = in.pressed = true;
        frame(c, st, w, in, obs);
        return true;
    }
    return false;
}

void press_key(Canvas& c, ViewState& st, World& w, u32 vk) {
    UiInput in;
    in.keys[0] = vk;
    in.nkeys = 1;
    frame(c, st, w, in);
}

// The settings window (build-plan-viewer.md step 5): every setting of the
// registry is on the page once, with the value the world read, its label
// and its help; the view's switches on the page do what their keys do; and
// the page changes nothing in the world.
void test_settings() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    // The settings are noted while the world is built.
    Settings book;
    World w;
    Config cfg;
    settings_begin(&book);
    CHECK(build(w, cfg, kGate3, false));
    settings_end(&book, cfg);
    const u64 before = w.hash();
    // Every entry but the test-only ones, once each, group by group in the
    // registry's order.
    u32 shown = 0, hidden = 0;
    for (usize i = 0; i < key_count(); ++i) {
        const Key& k = key_at(i);
        u32 found = 0;
        for (usize r = 0; r < book.rows.size(); ++r)
            found += book.rows[r].key == &k ? 1 : 0;
        CHECK(found == ((k.flags & kKeyHidden) ? 0u : 1u));
        shown += found;
        hidden += (k.flags & kKeyHidden) ? 1 : 0;
    }
    CHECK(shown == book.rows.size() && shown > 150 && shown + hidden == key_count());
    u32 group = 0, groups = 0;
    for (usize r = 0; r < book.rows.size(); ++r) {
        const Key& k = *book.rows[r].key;
        CHECK(k.label[0] && k.help[0]);
        groups += r == 0 || strcmp(book.rows[r - 1].key->group, k.group) != 0 ? 1 : 0;
        while (group < kKeyGroupCount && strcmp(kKeyGroups[group], k.group) != 0)
            ++group;
        CHECK(group < kKeyGroupCount); // a group never comes back once left
    }
    // The values: what the world read, given or not; and for what nothing
    // read, the config's own text, or the default.
    const SettingRow* places = book.find("world.places");
    CHECK(places && places->read && places->given && strcmp(places->value, "30") == 0 && setting_changed(*places));
    const SettingRow* map = book.find("world.map");
    CHECK(map && map->read && !map->given && strcmp(map->value, "landscape") == 0 && !setting_changed(*map));
    const SettingRow* night = book.find("supply.night");
    CHECK(night && night->read && strcmp(night->value, "0.2") == 0 && setting_changed(*night));
    const SettingRow* every = book.find("observe.sample_every"); // the observers', which this test doesn't build
    CHECK(every && every->given && !every->read && strcmp(every->value, "5") == 0 && setting_changed(*every));
    const SettingRow* seeds = book.find("origin.seeds");
    CHECK(seeds && !seeds->given && !seeds->read && !setting_changed(*seeds));
    CHECK(seeds && strcmp(seeds->value, seeds->key->def_text) == 0);
    CHECK(!book.find("test.switch") && !book.find("no.such.setting"));
    // A setting that depends on a switch: wanted with the landscape map,
    // not with none.
    const SettingRow* aspect = book.find("map.land.aspect");
    CHECK(aspect && aspect->read && setting_wanted(book, *aspect));
    Settings book2;
    World w2;
    Config cfg2;
    settings_begin(&book2);
    CHECK(build(w2, cfg2, kGate2, false));
    settings_end(&book2, cfg2);
    const SettingRow* aspect2 = book2.find("map.land.aspect");
    CHECK(aspect2 && !setting_wanted(book2, *aspect2));
    CHECK(book2.find("world.map") && strcmp(book2.find("world.map")->value, "none") == 0);
    settings_say(&book2, "world.seed", "1 to 20, one a seed");
    CHECK(strcmp(book2.find("world.seed")->value, "1 to 20, one a seed") == 0);
    // Nothing is noted once the noting has ended.
    Config cfg3;
    Config::ParseError pe;
    CHECK(cfg3.parse("world.places = 7\n", &pe) && key_u32(cfg3, "world.places") == 7);
    CHECK(strcmp(book.find("world.places")->value, "30") == 0);

    // The list's lines: each setting has one line of its own, in order,
    // under its group's, and its help whole, cut between words.
    Array<SettingLine> lines;
    setting_lines(book, 60, &lines);
    static char help[2048];
    u32 heads = 0, heads_of_groups = 0, helps = 0;
    usize at = 0;
    for (usize i = 0; i <= lines.size(); ++i) {
        const bool end = i == lines.size();
        if ((end || lines[i].kind == SettingLineKind::head) && heads) {
            help[at] = 0;
            CHECK(strcmp(help, book.rows[heads - 1].key->help) == 0);
        }
        if (end)
            break;
        const SettingLine& l = lines[i];
        if (l.kind == SettingLineKind::group) {
            ++heads_of_groups;
            CHECK(l.row == heads);
        } else if (l.kind == SettingLineKind::head) {
            CHECK(l.row == heads);
            ++heads;
            at = 0;
        } else if (l.kind == SettingLineKind::help) {
            CHECK(heads && l.row == heads - 1 && l.len > 0 && l.len <= 60);
            if (at && at + 1 < sizeof help)
                help[at++] = ' ';
            if (at + l.len < sizeof help) {
                memcpy(help + at, book.rows[l.row].key->help + l.from, l.len);
                at += l.len;
            }
            ++helps;
        }
    }
    CHECK(heads == book.rows.size() && heads_of_groups == groups && helps >= heads);

    // The page: the top bar's button opens it, and scrolled from its top to
    // its end every setting's line is drawn.
    ViewState st;
    st.title = "the settings' test world";
    st.settings = &book;
    CHECK(!st.settings_open && click(c, st, w, "settings") && st.settings_open);
    static bool seen[1024];
    memset(seen, 0, sizeof seen);
    u32 last = ~0u, frames = 0;
    for (; frames < 400; ++frames) {
        ButtonLog log;
        UiInput look;
        look.log = &log;
        frame(c, st, w, look);
        for (u32 i = 0; i < log.count; ++i) {
            const SettingRow* r = book.find(log.label[i]);
            if (r && static_cast<usize>(r - book.rows.data()) < sizeof seen)
                seen[r - book.rows.data()] = true;
        }
        if (st.settings_scroll.first == last)
            break; // the end: it went no further
        last = st.settings_scroll.first;
        st.settings_scroll.first += 5;
    }
    u32 drawn = 0;
    for (usize r = 0; r < book.rows.size(); ++r)
        drawn += seen[r] ? 1 : 0;
    CHECK(drawn == book.rows.size() && frames > 10 && frames < 400);
    st.settings_scroll.first = 0;
    if (against_golden(c, w, nullptr, st, "view_settings") != 0)
        CHECK(false);

    // The view's switches: a button on the page does what its key does.
    CHECK(st.map && click(c, st, w, "memory") && !st.map);
    press_key(c, st, w, 'V');
    CHECK(st.map);
    CHECK(click(c, st, w, "queues") && st.map_colour == MapColour::queues);
    press_key(c, st, w, 'C');
    CHECK(st.map_colour == MapColour::population);
    CHECK(click(c, st, w, "permission") && st.colour == MemColour::permission);
    press_key(c, st, w, 'M');
    CHECK(st.colour == MemColour::size_class);
    // "power" is the top bar's first and the page's second, and "lineage"
    // the map's, the memory's and the programs' order's, in that order.
    CHECK(!st.power_row && click_nth(c, st, w, "power", 1) && st.power_row);
    press_key(c, st, w, 'G');
    CHECK(!st.power_row);
    CHECK(click_nth(c, st, w, "lineage", 0) && st.map_colour == MapColour::lineage);
    CHECK(click_nth(c, st, w, "lineage", 1) && st.colour == MemColour::lineage);
    CHECK(click_nth(c, st, w, "lineage", 2) && st.sort == ProgramSort::lineage && !st.sort_down);
    CHECK(click_nth(c, st, w, "lineage", 2) && st.sort == ProgramSort::lineage && st.sort_down);
    CHECK(!click_nth(c, st, w, "lineage", 3));
    CHECK(click(c, st, w, "born") && st.sort == ProgramSort::born && !st.sort_down);
    CHECK(click(c, st, w, "programs") && st.inspect == Inspect::programs);
    CHECK(click(c, st, w, "figures") && st.inspect == Inspect::figures);
    CHECK(click(c, st, w, "place") && st.inspect == Inspect::place);
    CHECK(click(c, st, w, "the world") && st.world_figures);
    CHECK(click(c, st, w, "this place") && !st.world_figures);
    CHECK(click(c, st, w, "family") && st.family);
    CHECK(click(c, st, w, "code") && !st.family);
    // Shut by its own button, by the top bar's, and by Escape, which shuts
    // the viewer only when the page is not open.
    CHECK(st.settings_open && click(c, st, w, "close") && !st.settings_open);
    CHECK(click(c, st, w, "settings") && st.settings_open);
    CHECK(click(c, st, w, "settings") && !st.settings_open);
    CHECK(click(c, st, w, "settings") && st.settings_open);
    press_key(c, st, w, kVkEscape);
    CHECK(!st.settings_open && !st.quit);
    press_key(c, st, w, kVkEscape);
    CHECK(st.quit);
    CHECK(w.hash() == before && w.tick_count() == 0);
    // With no map the page has no switches for one; and a window given no
    // settings says so and draws.
    ViewState st2;
    st2.settings = &book2;
    CHECK(click(c, st2, w2, "settings") && st2.settings_open);
    CHECK(!click(c, st2, w2, "queues") && !click(c, st2, w2, "map") && !click(c, st2, w2, "power"));
    CHECK(click(c, st2, w2, "permission") && st2.colour == MemColour::permission);
    ViewState st3;
    CHECK(click(c, st3, w2, "settings") && st3.settings_open);
    CHECK(click(c, st3, w2, "close") && !st3.settings_open);
    printf("  view: the settings window listed all %u settings in %u groups once each (%u test-only ones left off), "
           "with the values the world read, and its switches did what their keys do\n",
           shown, groups, hidden);
}

} // namespace

// The memory map of a big place (build-plan-viewer.md step 11): its pages
// are drawn as one picture, a pixel a page, enlarged to the page's cell.
// Every page's cell must still be that page's colour from corner to corner,
// with the gap between cells, where there is one, the panel's; and nothing
// drawn past the last page. Places of 1 MB: 16,384 pages of 64 bytes take
// 3 pixels each with no gap, and the larger pages take more with one.
void test_big_map() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg,
                "world.places = 6\nworld.map = none\nmemory.size = 1048576\ndisk.size = 65536\nhw.cores.max = 4\n"
                "os.strict_share = 0.3\nregion.retry = 3\nobserve.sample_every = 5\n",
                true));
    w.start_threads(2);
    for (u32 t = 0; t < 60; ++t)
        w.tick();
    ViewState st;
    st.title = "the big map's test world";
    st.colour = MemColour::permission;
    u32 close = 0, gapped = 0, pages = 0, kinds = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        st.place = i;
        Array<u32> px;
        CHECK(render(c, w, nullptr, st, &px));
        const Memory& m = w.place(i).mem;
        const i32 cell = st.mem_cell, cols = st.mem_cols, gap = cell >= 5 ? 1 : 0;
        CHECK(cell >= 2 && cols > 0);
        if (cell < 2 || cols <= 0)
            return;
        (gap ? gapped : close) += 1;
        u32 seen = 0, wrong = 0;
        for (u32 pg = 0; pg < m.page_count(); ++pg) {
            const Page& page = m.page(pg);
            u32 want = page.perm == Perm::data ? kPermData : (page.perm == Perm::exec ? kPermExec : kPermBoth);
            if (page.state == PageState::free)
                want = kPageFree;
            else if (page.state == PageState::reserved)
                want = kPageReserved;
            else if (page.state == PageState::region)
                want = kPageRegion;
            seen |= want == kPageFree ? 1 : (want == kPageRegion ? 2 : (want == kPageReserved ? 4 : 8));
            const i32 x = st.mem_x + static_cast<i32>(pg % static_cast<u32>(cols)) * cell;
            const i32 y = st.mem_y + static_cast<i32>(pg / static_cast<u32>(cols)) * cell;
            bool first = false; // a memory device's first page carries a mark at its left
            for (u32 d = 1; d < m.device_count(); ++d)
                first = first || m.device(d).first_page == pg;
            const bool framed = page.state == PageState::region && cell >= 4; // the region's pages are outlined
            // Inside the cell: the middle always, and the corners where nothing is drawn over them.
            const i32 last = cell - gap - 1;
            wrong += px[static_cast<usize>(y + last / 2) * kW + x + last] != want && !framed;
            wrong += px[static_cast<usize>(y + last) * kW + x + last] != want && !framed;
            wrong += px[static_cast<usize>(y) * kW + x] != want && !framed && !first;
            // The gap at its right and under it.
            if (gap) {
                wrong += px[static_cast<usize>(y) * kW + x + cell - 1] != kPanel;
                wrong += px[static_cast<usize>(y + cell - 1) * kW + x] != kPanel && !first; // the mark runs through it
            }
            ++pages;
        }
        // Past the last page, on its row and under it: the panel.
        const u32 n = m.page_count();
        if (n % static_cast<u32>(cols)) {
            const i32 x = st.mem_x + static_cast<i32>(n % static_cast<u32>(cols)) * cell;
            const i32 y = st.mem_y + static_cast<i32>(n / static_cast<u32>(cols)) * cell;
            wrong += px[static_cast<usize>(y) * kW + x] != kPanel;
        }
        const i32 under = st.mem_y + static_cast<i32>((n + static_cast<u32>(cols) - 1) / static_cast<u32>(cols)) * cell;
        wrong += px[static_cast<usize>(under) * kW + st.mem_x] != kPanel;
        if (wrong)
            printf("  view: place %u, cell %d, %u pixels wrong\n", i, cell, wrong);
        CHECK(wrong == 0);
        kinds |= seen;
    }
    CHECK(close > 0 && gapped > 0 && kinds == 15);
    printf("  view: the memory maps of 6 places of 1 MB, %u with pages too small for a gap, matched their %u pages "
           "cell by cell\n",
           close, pages);
}

// Gate 4's (build-plan-gate4.md step 9): gate 3's traffic world with its
// voltage solved from sources that come and go, swell and drop out often,
// and heat on in places that cool badly, half of them with a cooling
// device.
const char* const kGate4 = "world.places = 30\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0.3\n"
                           "region.retry = 3\n"
                           "observe.sample_every = 5\n"
                           "supply.model = sources\n"
                           "heat.on = true\n"
                           "sources.start = 40\n"
                           "sources.life = 500\n"
                           "sources.size.min = 20\n"
                           "sources.size.max = 120\n"
                           "source.steady.dropouts = 1000\n"
                           "source.daylight.dropouts = 1000\n"
                           "source.gusty.dropouts = 1000\n"
                           "source.steady.swells = 1000\n"
                           "source.daylight.swells = 1000\n"
                           "source.gusty.swells = 1000\n"
                           "supply.dropout.min = 5\n"
                           "supply.dropout.max = 60\n"
                           "sources.swell.longest = 60\n"
                           "supply.day = 600\n"
                           "heat.place.cooling = 0.3\n"
                           "heat.place.capacity = 60\n"
                           "cooler.share = 0.5\n"
                           // The picture was drawn at the cores' first heat numbers and the first
                           // link resistance: written out, so it stays the same picture now the
                           // defaults are the numbers agreed on 2026-11-20.
                           "heat.p.sink = 0.55\n"
                           "heat.p.capacity = 11\n"
                           "heat.e.sink = 0.07\n"
                           "heat.e.capacity = 1.4\n"
                           "grid.link_resistance = 5\n";

// A source swelling at tick w.tick_count(), at a place with programs alive:
// its place, or ~0.
u32 swelling_at(World& w) {
    const Sources& s = w.sources();
    for (u32 k = 0; k < s.count(); ++k)
        if (s.state(k).swell > kCapacityOne && w.place(s.source(k).place).os.live_programs())
            return s.source(k).place;
    return ~0u;
}

bool tripped_somewhere(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i)
        for (u32 k = 0; k < w.place(i).core_count(); ++k)
            if (w.place(i).core_tripped[k])
                return true;
    return false;
}

// Heat's counters, which a snapshot doesn't keep either, cleared.
void forget_heat(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k)
            p.trips[k] = p.tripped_ticks[k] = 0;
        p.heat_threads = p.cooler_cutouts = p.region_heat_ticks = 0;
        p.lv_slept = p.lv_outlived = 0;
    }
}

// Gate 4's world run to the first moment after tick `from` that a source
// is swelling at a place with programs alive and a core somewhere is
// tripped by heat; the place, or ~0.
u32 run_to_swell(World& w, u64 from) {
    while (w.tick_count() < 6000) {
        w.tick();
        if (w.tick_count() < from || !tripped_somewhere(w))
            continue;
        const u32 at = swelling_at(w);
        if (at != ~0u)
            return at;
    }
    return ~0u;
}

// Gate 4's golden render (build-plan-gate4.md step 9): the map coloured by
// voltage with the sources and the proximity links over it, the inspector
// on a place whose source is swelling, and gate 4's row of charts, drawn at
// the first moment after tick 300 that a source swells at a place with
// programs alive while a core somewhere is off by its heat. The other
// colourings are written to the build tree for a person to look at.
void test_golden_gate4() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate4, true, true));
    CHECK(w.map().placed() && w.powered() && w.heated());
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    ViewState st;
    st.title = "the gate 4 golden test world";
    st.power_colour = PowerColour::voltage;
    st.heat_row = true;
    st.place = run_to_swell(w, 300);
    CHECK(st.place != ~0u);
    if (st.place == ~0u)
        return;
    const Sample& last = obs.samples.back();
    CHECK(obs.samples.size() > 1 && last.sources > 0 && last.power_given > 0 && last.power_drawn > 0);
    CHECK(last.temp_high > w.place(0).heat.outside && last.trips[kHeatP] + last.trips[kHeatE] > 0);
    if (against_golden(c, w, &obs, st, "view_gate4") != 0) {
        CHECK(false);
        return;
    }
    // The colourings differ from one another and from the map's own, and
    // the two overlays each draw something.
    Array<u32> base, px;
    CHECK(render(c, w, &obs, st, &base));
    u32 differ = 0;
    for (u32 k = 0; k < static_cast<u32>(PowerColour::count); ++k) {
        st.power_colour = static_cast<PowerColour>(k);
        CHECK(render(c, w, &obs, st, &px));
        if (k != static_cast<u32>(PowerColour::voltage)) {
            CHECK(memcmp(px.data(), base.data(), px.size() * 4) != 0);
            ++differ;
        }
        char path[64];
        snprintf(path, sizeof path, "view_gate4_%s_actual.png", k ? power_colour_label(st.power_colour) : "own");
        CHECK(c.write_png(path));
    }
    st.power_colour = PowerColour::voltage;
    st.show_sources = false;
    CHECK(render(c, w, &obs, st, &px) && memcmp(px.data(), base.data(), px.size() * 4) != 0);
    st.show_sources = true;
    st.show_grid = false;
    CHECK(render(c, w, &obs, st, &px) && memcmp(px.data(), base.data(), px.size() * 4) != 0);
    st.show_grid = true;
    // Its buttons do what they say: the colourings (again to turn one off),
    // the overlays, and the charts' rows; a map colouring turns gate 4's off.
    CHECK(click(c, st, w, "temperature", &obs) && st.power_colour == PowerColour::temperature);
    CHECK(click(c, st, w, "draw", &obs) && st.power_colour == PowerColour::draw);
    CHECK(click(c, st, w, "draw", &obs) && st.power_colour == PowerColour::none);
    CHECK(click(c, st, w, "cooling", &obs) && st.power_colour == PowerColour::cooling);
    CHECK(click(c, st, w, "network", &obs) && st.power_colour == PowerColour::none &&
          st.map_colour == MapColour::network);
    CHECK(click(c, st, w, "sources", &obs) && !st.show_sources && click(c, st, w, "sources", &obs) && st.show_sources);
    CHECK(click(c, st, w, "grid", &obs) && !st.show_grid && click(c, st, w, "grid", &obs) && st.show_grid);
    CHECK(click(c, st, w, "traffic", &obs) && !st.heat_row && !st.power_row);
    CHECK(click(c, st, w, "heat", &obs) && st.heat_row);
    CHECK(click(c, st, w, "power", &obs) && !st.heat_row && st.power_row);
    printf("  view: gate 4's golden render, place %u at tick %llu with a source swelling there, %u sources, matched "
           "pixel for pixel; %u other colourings each drew differently, and its buttons did what they say\n",
           st.place, static_cast<unsigned long long>(w.tick_count()), w.sources().count(), differ);
}

// A snapshot taken mid-swell, with a core off by its heat, opens in the
// views and draws as the world it came from: the map in each of gate 4's
// colourings with the sources and the proximity links over it, and the
// memory with gate 4's inspector lines, inspecting the place of the
// swelling source, a place with a core tripped, and place 0.
void test_snapshot_mid_swell() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World a;
    Config cfg;
    CHECK(build(a, cfg, kGate4, true, true));
    a.start_threads(2);
    const u32 swell = run_to_swell(a, 300);
    CHECK(swell != ~0u);
    if (swell == ~0u)
        return;
    u32 hot = 0;
    for (u32 i = 0; i < a.place_count(); ++i)
        for (u32 k = 0; k < a.place(i).core_count(); ++k)
            if (a.place(i).core_tripped[k])
                hot = i;
    ByteWriter out;
    Config cfg2;
    World b;
    CHECK(reopen(a, cfg, out, cfg2, b));
    CHECK(b.powered() && b.heated() && b.sources().count() == a.sources().count() && swelling_at(b) == swell);
    forget_counters(a);
    forget_heat(a);
    ViewState st;
    st.title = "a snapshot mid-swell";
    st.heat_row = true;
    const u32 places[3] = {swell, hot, 0};
    u32 renders = 0;
    for (u32 k = 0; k < 3; ++k) {
        st.place = places[k];
        for (u32 view = 0; view <= static_cast<u32>(PowerColour::count); ++view) {
            st.map = view < static_cast<u32>(PowerColour::count);
            if (st.map)
                st.power_colour = static_cast<PowerColour>(view);
            Array<u32> pa, pb;
            CHECK(render(c, a, nullptr, st, &pa));
            CHECK(render(c, b, nullptr, st, &pb));
            const bool same = pa.size() == pb.size() && memcmp(pa.data(), pb.data(), pa.size() * 4) == 0;
            CHECK(same);
            if (!same) {
                char path[64];
                CHECK(render(c, a, nullptr, st, &pa));
                snprintf(path, sizeof path, "view_swell_%u_%u_world.png", k, view);
                CHECK(c.write_png(path));
                CHECK(render(c, b, nullptr, st, &pb));
                snprintf(path, sizeof path, "view_swell_%u_%u_snapshot.png", k, view);
                CHECK(c.write_png(path));
            }
            ++renders;
        }
    }
    printf("  view: a snapshot taken at tick %llu with a source swelling at place %u and a core of place %u off by its "
           "heat drew the same as its world in %u renders\n",
           static_cast<unsigned long long>(a.tick_count()), swell, hot, renders);
}

// Gate 5's (build-plan-gate5.md step 13): the gate 4 world's sources and
// heat, with every strength of gate 5's on and fast enough to see in a few
// hundred ticks: devices wearing out and replaced at visits, places built
// again, links cut, the patches of every type, flaky parts, straits that
// part, the current with its gusts, rowhammer, the killer and the watchdog,
// and the disk's blocks wearing. (determinism5's world, on a map of 30.)
const char* const kGate5 = "world.places = 30\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0.3\n"
                           "region.retry = 3\n"
                           "observe.sample_every = 5\n"
                           "supply.model = sources\n"
                           "heat.on = true\n"
                           "supply.day = 400\n"
                           "wear.rate = 1\n"
                           "wear.p.endurance = 500\n"
                           "wear.e.endurance = 500\n"
                           "wear.memory.endurance = 500\n"
                           "wear.disk.endurance = 400\n"
                           "wear.network.endurance = 400\n"
                           "wear.cooler.endurance = 400\n"
                           "wear.weak.share = 0.01\n"
                           "wear.weak.chance = 0.01\n"
                           "disk.block = 8192\n"
                           "disk.block_endurance = 2\n"
                           "disk.worn_flip = 0.05\n"
                           "os.levelling = true\n"
                           "os.levelling_gap = 1\n"
                           "visit.mean = 250\n"
                           "visit.regularity = 0.3\n"
                           "visit.replace_at = 0.8\n"
                           "visit.respawn_share = 0.3\n"
                           "link.cuts = 0.003\n"
                           "net.lifetime = 40\n"
                           "ramp.lead.min = 10\n"
                           "ramp.lead.max = 100\n"
                           "patch.life.min = 100\n"
                           "patch.life.max = 500\n"
                           "patch.storm.rate = 30000\n"
                           "patch.storm.strength.min = 0.005\n"
                           "patch.storm.strength.max = 0.02\n"
                           "patch.swell.rate = 30000\n"
                           "patch.brownout.rate = 30000\n"
                           "patch.heatwave.rate = 30000\n"
                           "patch.outage.rate = 30000\n"
                           "hammer.strength = 0.01\n"
                           "hammer.window = 500\n"
                           "hammer.threshold.fast = 200\n"
                           "hammer.threshold.slow = 600\n"
                           "flaky.share = 0.3\n"
                           "flaky.worn = 10\n"
                           "strait.share = 1\n"
                           "strait.spell.min = 50\n"
                           "strait.spell.max = 400\n"
                           "current.strength = 60\n"
                           "current.season = 700\n"
                           "patch.gust.rate = 30000\n"
                           "os.killer = 10\n"
                           "os.watchdog = 5000\n";

// A storm at its peak through the next tick (determinism5's mid-storm).
bool mid_storm(World& w) {
    const Events& ev = w.events();
    const u64 et = ev.tick();
    for (u32 k = 0; k < ev.count(); ++k) {
        const Patch& p = ev.patch(k);
        if (p.type == static_cast<u8>(PatchType::storm) && !p.ramp.false_alarm && p.start + p.ramp.lead <= et &&
            et + 1 < p.start + p.ramp.lead + p.life)
            return true;
    }
    return false;
}

// A place with a failed device waiting for its visit, or ~0.
u32 failed_at(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i)
        if (w.place(i).failed_devices() > 0 && w.place(i).os.live_programs())
            return i;
    return ~0u;
}

// Gate 5's world run to the first moment after tick `from` that a storm is
// at its peak while a place with programs alive has a failed device
// waiting for its visit; that place, or ~0.
u32 run_to_storm(World& w, u64 from) {
    while (w.tick_count() < 6000) {
        w.tick();
        if (w.tick_count() < from || !mid_storm(w))
            continue;
        const u32 at = failed_at(w);
        if (at != ~0u)
            return at;
    }
    return ~0u;
}

// Gate 5's golden render: the map coloured by wear, a failed device's
// corner marked, with the patches, the strikes and the current over it and
// the failed links and parted straits drawn broken; the inspector on a
// place with a failed device, its wear lines under gate 4's; and gate 5's
// row of charts, drawn at the first moment after tick 400 that a storm is
// at its peak while such a place waits for its visit. The other colouring,
// the events page and the memory with its disk's strip are written to the
// build tree for a person to look at, and stored as well.
void test_golden_gate5() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    World w;
    Config cfg;
    CHECK(build(w, cfg, kGate5, true, true));
    CHECK(w.map().placed() && gate5_world(w) && gate4_world(w));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 5;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    ViewState st;
    st.title = "the gate 5 golden test world";
    st.wear_colour = WearColour::wear;
    st.wear_row = true;
    st.place = run_to_storm(w, 400);
    CHECK(st.place != ~0u);
    if (st.place == ~0u)
        return;
    const Sample& last = obs.samples.back();
    CHECK(obs.samples.size() > 1 && last.failures > 0 && last.visits > 0 && last.link_cuts > 0);
    u32 down = 0, parted = 0;
    for (u32 k = 0; k < w.map().link_count(); ++k) {
        down += link_down(w, k) == 1 ? 1 : 0;
        parted += link_down(w, k) == 2 ? 1 : 0;
    }
    CHECK(against_golden(c, w, &obs, st, "view_gate5") == 0); // and on, so each picture is compared
    // The other pages, stored too: the visits colouring, the events, and
    // the memory with its disk's blocks.
    ViewState vs = st;
    vs.wear_colour = WearColour::visits;
    CHECK(against_golden(c, w, &obs, vs, "view_gate5_visits") == 0);
    ViewState ev = st;
    ev.inspect = Inspect::events;
    CHECK(against_golden(c, w, &obs, ev, "view_gate5_events") == 0);
    ViewState mem = st;
    mem.map = false;
    CHECK(against_golden(c, w, &obs, mem, "view_gate5_memory") == 0);
    // Each colouring and overlay draws something of its own.
    Array<u32> base, px;
    CHECK(render(c, w, &obs, st, &base));
    st.wear_colour = WearColour::none;
    CHECK(render(c, w, &obs, st, &px) && memcmp(px.data(), base.data(), px.size() * 4) != 0);
    st.wear_colour = WearColour::wear;
    st.show_patches = false;
    CHECK(render(c, w, &obs, st, &px) && memcmp(px.data(), base.data(), px.size() * 4) != 0);
    st.show_patches = true;
    st.show_current = false;
    CHECK(render(c, w, &obs, st, &px) && memcmp(px.data(), base.data(), px.size() * 4) != 0);
    st.show_current = true;
    // Its buttons do what they say: the colourings (again to turn one off),
    // a gate 4 colouring and a map colouring turning them off, the
    // overlays, the charts' rows, and the inspector's events page.
    CHECK(click(c, st, w, "visits", &obs) && st.wear_colour == WearColour::visits);
    CHECK(click(c, st, w, "visits", &obs) && st.wear_colour == WearColour::none);
    CHECK(click(c, st, w, "wear", &obs) && st.wear_colour == WearColour::wear);
    CHECK(click(c, st, w, "temperature", &obs) && st.power_colour == PowerColour::temperature &&
          st.wear_colour == WearColour::none);
    CHECK(click(c, st, w, "wear", &obs) && st.wear_colour == WearColour::wear && st.power_colour == PowerColour::none);
    CHECK(click(c, st, w, "network", &obs) && st.wear_colour == WearColour::none &&
          st.map_colour == MapColour::network);
    CHECK(click(c, st, w, "patches", &obs) && !st.show_patches && click(c, st, w, "patches", &obs) && st.show_patches);
    CHECK(click(c, st, w, "current", &obs) && !st.show_current && click(c, st, w, "current", &obs) && st.show_current);
    CHECK(click(c, st, w, "traffic", &obs) && !st.wear_row && !st.heat_row && !st.power_row);
    CHECK(click(c, st, w, "failures", &obs) && st.wear_row);
    CHECK(click(c, st, w, "heat", &obs) && st.heat_row && !st.wear_row);
    CHECK(click(c, st, w, "failures", &obs) && st.wear_row);
    CHECK(click(c, st, w, "events", &obs) && st.inspect == Inspect::events);
    // The events page's buttons hide and show their kinds.
    CHECK(click(c, st, w, "devices", &obs) && st.events.hide[kEvFailure]);
    CHECK(click(c, st, w, "devices", &obs) && !st.events.hide[kEvFailure]);
    CHECK(click(c, st, w, "place", &obs) && st.inspect == Inspect::place);
    // The place page, taller than its panel here, scrolls: a notch of the
    // wheel over it moves it three lines, and back.
    UiInput wheel;
    wheel.mx = kW - 120;
    wheel.my = kH / 2;
    wheel.wheel = -1;
    frame(c, st, w, wheel, &obs);
    const u32 scrolled = st.place_scroll.first;
    wheel.wheel = 1;
    frame(c, st, w, wheel, &obs);
    CHECK(scrolled == 3 && st.place_scroll.first == 0);
    // G goes round the rows: traffic, power, heat, failures, and back.
    st.wear_row = st.heat_row = st.power_row = false;
    u32 rows[4] = {};
    for (u32 k = 0; k < 4; ++k) {
        press_key(c, st, w, 'G');
        rows[k] = st.wear_row ? 3 : st.heat_row ? 2 : st.power_row ? 1 : 0;
    }
    CHECK(rows[0] == 1 && rows[1] == 2 && rows[2] == 3 && rows[3] == 0);
    printf("  view: gate 5's golden render, place %u at tick %llu mid-storm with a failed device waiting for its "
           "visit, %u links failed and %u parted, matched pixel for pixel, with its visits colouring, events and "
           "memory; its colouring and overlays each drew something, and its buttons and G did what they say\n",
           st.place, static_cast<unsigned long long>(w.tick_count()), down, parted);
}

// Gate 5's counters a snapshot doesn't keep (reports, not state), cleared.
void forget_wear(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        p.noise.weak = 0;
    }
}

// The views on a snapshot taken mid-storm and one taken just after a
// respawn draw as the world it came from: the map in each of gate 5's
// colourings and in its own, with the patches, the strikes, the current
// and the broken links over it; the memory with its disk's strip; and the
// inspector's place page with its wear lines and its events page (no
// observer: the snapshot's world alone), at the place the moment is about
// and at place 0.
u32 snapshot_views(Canvas& c, World& a, Config& cfg, u32 at, const char* moment) {
    ByteWriter out;
    Config cfg2;
    World b;
    CHECK(reopen(a, cfg, out, cfg2, b));
    CHECK(gate5_world(b));
    CHECK(b.map().respawned() == a.map().respawned());
    CHECK(b.links().failed_count() == a.links().failed_count());
    forget_counters(a);
    forget_heat(a);
    forget_wear(a);
    ViewState st;
    st.title = "a gate 5 snapshot";
    st.wear_row = true;
    const u32 places[2] = {at, 0};
    u32 renders = 0;
    for (u32 k = 0; k < 2; ++k) {
        st.place = places[k];
        for (u32 view = 0; view < 5; ++view) {
            st.map = view < 3;
            st.inspect = view == 4 ? Inspect::events : Inspect::place;
            if (st.map)
                st.wear_colour = static_cast<WearColour>(view);
            Array<u32> pa, pb;
            CHECK(render(c, a, nullptr, st, &pa));
            CHECK(render(c, b, nullptr, st, &pb));
            const bool same = pa.size() == pb.size() && memcmp(pa.data(), pb.data(), pa.size() * 4) == 0;
            CHECK(same);
            if (!same) {
                char path[96];
                CHECK(render(c, a, nullptr, st, &pa));
                snprintf(path, sizeof path, "view_%s_%u_%u_world.png", moment, k, view);
                CHECK(c.write_png(path));
                CHECK(render(c, b, nullptr, st, &pb));
                snprintf(path, sizeof path, "view_%s_%u_%u_snapshot.png", moment, k, view);
                CHECK(c.write_png(path));
            }
            ++renders;
        }
    }
    return renders;
}

void test_snapshot_gate5() {
    Canvas c;
    TextBuf why;
    CHECK(c.init(&why));
    // Mid-storm, with a failed device waiting for its visit.
    World a;
    Config cfg;
    CHECK(build(a, cfg, kGate5, true, true));
    a.start_threads(2);
    const u32 failed = run_to_storm(a, 300);
    CHECK(failed != ~0u);
    if (failed == ~0u)
        return;
    const u64 storm_tick = a.tick_count();
    const u32 storm_renders = snapshot_views(c, a, cfg, failed, "storm");
    // Just after a respawn: the place built again.
    World r;
    Config rcfg;
    CHECK(build(r, rcfg, kGate5, true, true));
    r.start_threads(2);
    u32 rebuilt = ~0u;
    while (rebuilt == ~0u && r.tick_count() < 6000) {
        const u64 before = r.respawns;
        r.tick();
        if (r.respawns == before)
            continue;
        for (u32 i = 0; i < r.place_count(); ++i)
            if (r.place(i).last_visit + 1 == r.tick_count() && r.place(i).incarnation > 0)
                rebuilt = i;
        if (rebuilt == ~0u)
            rebuilt = 0;
    }
    CHECK(rebuilt != ~0u && r.respawns > 0);
    if (rebuilt == ~0u)
        return;
    const u64 respawn_tick = r.tick_count();
    const u32 respawn_renders = snapshot_views(c, r, rcfg, rebuilt, "respawn");
    printf("  view: a snapshot taken mid-storm at tick %llu, place %u with a failed device waiting, drew the same as "
           "its world in %u renders; one taken just after a respawn at tick %llu, place %u built again, in %u\n",
           static_cast<unsigned long long>(storm_tick), failed, storm_renders,
           static_cast<unsigned long long>(respawn_tick), rebuilt, respawn_renders);
}

void suite_view() {
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    test_golden();
    test_golden_gate3();
    test_golden_gate4();
    test_snapshot_mid_swell();
    test_golden_gate5();
    test_snapshot_gate5();
    test_snapshot_opens();
    test_snapshot_mid_hop();
    test_big_map();
    test_input();
    test_widgets();
    test_buttons();
    test_map_zoom();
    test_programs();
    test_figures();
    test_seed_list();
    test_run_window();
    test_run_attach();
    test_run_saves();
    test_run_resume();
    test_run_records();
    test_run_storage();
    test_run_classes();
    test_run_classes_respawns();
    test_settings();
    CoUninitialize();
}
