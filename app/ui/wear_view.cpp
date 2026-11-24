#include "wear_view.h"

#include <cmath>  // cos, sin, sqrt
#include <cstdio> // snprintf

#include "nova2/observe.h"
#include "nova2/world.h"
#include "theme.h"
#include "viewer.h" // UiInput

namespace nova2 {

namespace {

// The patches' colours by type (storm, swell, brownout, heat wave, outage,
// gust), the current's, a strike's, and the marks on a place's corners.
const u32 kPatchColor[kPatchTypes] = {0xFFE14D, 0xA080FF, 0xB07A48, 0xFF6A3D, 0x9AA6B8, 0x5FE0FF};
const u32 kCurrentColor = 0x5FE0FF, kStrikeColor = 0xFFF4A0, kFailedMark = 0xFF4040, kFlakyMark = 0xC070FF;
const u32 kWornBlock = 0xFF4040;

u32 mix(u32 a, u32 b, double t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    u32 out = 0;
    for (u32 sh = 0; sh < 24; sh += 8) {
        const double x = ((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t;
        out |= static_cast<u32>(x + 0.5) << sh;
    }
    return out;
}

// Dark blue through green and yellow to red, for a share from 0 to 1 (gate
// 4's, power_view.cpp).
u32 ramp(double t) {
    static const u32 kStops[5] = {0x23395B, 0x2E8F8A, 0x6CC24A, 0xF2C84B, 0xE4572E};
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const double at = t * 4;
    const u32 k = at >= 4 ? 3 : static_cast<u32>(at);
    return mix(kStops[k], kStops[k + 1], at - k);
}

i32 px_x(const Map& m, const MapAt& at, u32 i) { return at.ox + static_cast<i32>(m.x(i) * at.s + 0.5); }
i32 px_y(const Map& m, const MapAt& at, u32 i) { return at.oy + static_cast<i32>(m.y(i) * at.s + 0.5); }

// A device's wear as a share of its life (1: at its end, or failed).
double share(const Worn& d) {
    if (d.failed)
        return 1;
    if (!d.life)
        return 0;
    const double s = static_cast<double>(d.wear) / static_cast<double>(d.life);
    return s > 1 ? 1 : s;
}

bool flaky_here(const Place& p) {
    for (u32 c = 0; c < p.core_count(); ++c)
        if (p.noise.core_flaky(c) > kFlakyOne)
            return true;
    for (u32 d = 0; d < p.mem.device_count(); ++d)
        if (p.noise.memory_flaky(d) > kFlakyOne)
            return true;
    return false;
}

void ring(Canvas& c, double x, double y, double r, u32 colour, float width) {
    float xy[2 * 49];
    for (u32 k = 0; k <= 48; ++k) {
        const double a = 6.283185307179586 * k / 48;
        xy[2 * k] = static_cast<float>(x + r * cos(a));
        xy[2 * k + 1] = static_cast<float>(y + r * sin(a));
    }
    c.polyline(xy, 49, colour, width);
}

// An arrow from (x0, y0) to (x1, y1), its head at the second.
void arrow(Canvas& c, double x0, double y0, double x1, double y1, double head, u32 colour, float width) {
    c.line(static_cast<i32>(x0), static_cast<i32>(y0), static_cast<i32>(x1), static_cast<i32>(y1), colour, width);
    const double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
    if (!(len > 0))
        return;
    const double ux = dx / len, uy = dy / len;
    for (int side = -1; side <= 1; side += 2) {
        const double hx = x1 - head * (0.9 * ux - 0.45 * side * uy), hy = y1 - head * (0.9 * uy + 0.45 * side * ux);
        c.line(static_cast<i32>(x1), static_cast<i32>(y1), static_cast<i32>(hx), static_cast<i32>(hy), colour, width);
    }
}

i32 line(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, const char* s, u32 font, u32 colour) {
    c.text(x, y, s, font, colour, kAlignLeft, mw);
    return y + lh;
}

// A row of devices' wear and worn thresholds under a name, three to a line:
// each "41%/615", with x after one that has failed and f after a flaky one.
i32 worn_rows(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, const char* name, const char* const* tags, const Worn* d,
              const i32* threshold, const bool* flaky, u32 count) {
    char buf[160];
    for (u32 from = 0; from < count; from += 3) {
        bool failed = false;
        usize at = static_cast<usize>(snprintf(buf, sizeof buf, "%-7s", from ? "" : name));
        for (u32 k = from; k < count && k < from + 3; ++k) {
            at += static_cast<usize>(snprintf(buf + at, sizeof buf - at, " %s%3.0f%%/%d%c", tags ? tags[k] : "",
                                              100 * share(d[k]), threshold[k],
                                              d[k].failed ? 'x' : (flaky && flaky[k] ? 'f' : ' ')));
            failed = failed || d[k].failed;
        }
        y = line(c, x, y, mw, lh, buf, kFontMono, failed ? kCutOut : kText);
    }
    return y;
}

} // namespace

bool gate5_world(World& w) {
    if (!w.place_count())
        return false;
    const Place& p = w.place(0);
    return p.wear.on || w.links().on() || w.events().on() || w.respawning() || w.current().on() || w.killing() ||
           p.os.blocks_on();
}

const char* wear_colour_label(WearColour c) {
    static const char* const kNames[] = {"", "wear", "visits"};
    return kNames[static_cast<u32>(c) % 3];
}

u32 wear_colour(World& w, u32 i, WearColour mode) {
    const Place& p = w.place(i);
    if (mode == WearColour::wear) {
        double most = 0;
        for (u32 c = 0; c < p.core_count(); ++c)
            most = share(p.core_worn[c]) > most ? share(p.core_worn[c]) : most;
        for (u32 d = 0; d < p.mem.device_count(); ++d)
            most = share(p.memory_worn[d]) > most ? share(p.memory_worn[d]) : most;
        const double rest[3] = {share(p.disk_worn), share(p.network_worn), p.has_cooler ? share(p.cooler_worn) : 0};
        for (u32 k = 0; k < 3; ++k)
            most = rest[k] > most ? rest[k] : most;
        return ramp(most);
    }
    // Since the last visit, against the place's own mean gap: green just
    // visited to red at twice it.
    if (!p.visit_mean())
        return kEmptyPlace;
    const u64 now = w.tick_count(), since = now > p.last_visit ? now - p.last_visit : 0;
    const double t = static_cast<double>(since) / (2.0 * static_cast<double>(p.visit_mean()));
    return t < 0.5 ? mix(0x3E9B4F, 0xF2D34B, 2 * t) : mix(0xF2D34B, 0xE4572E, 2 * t - 1);
}

void wear_legend(World& w, WearColour mode, char* out, usize size) {
    if (mode == WearColour::wear) {
        snprintf(out, size,
                 "wear: each place's most worn device, blue new to red at its end; corners: red a failed device "
                 "waiting for its visit, violet a flaky one");
        return;
    }
    const u64 mean = w.place_count() ? w.place(0).visit_mean() : 0;
    if (mean)
        snprintf(out, size,
                 "since the last visit: green just visited, yellow at the place's mean gap (place 0's %llu ticks), "
                 "red at twice it",
                 static_cast<unsigned long long>(mean));
    else
        snprintf(out, size, "since the last visit: no place is visited (visit.mean is 0)");
}

u32 link_down(World& w, u32 k) {
    Links& l = w.links();
    if (!l.on() || !l.down_count() || k >= w.map().link_count())
        return 0;
    return l.data_down()[k];
}

void broken_line(Canvas& c, i32 xa, i32 ya, i32 xb, i32 yb, u32 colour, float width) {
    const i32 dx = xb - xa, dy = yb - ya;
    c.line(xa, ya, xa + dx * 2 / 5, ya + dy * 2 / 5, colour, width);
    c.line(xa + dx * 3 / 5, ya + dy * 3 / 5, xb, yb, colour, width);
}

void draw_wear_marks(Canvas& c, World& w, const MapAt& at, i32 d) {
    const Map& m = w.map();
    const i32 k = d / 3 > 2 ? d / 3 : 2;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        const i32 x = px_x(m, at, i), y = px_y(m, at, i);
        if (p.failed_devices())
            c.fill(Rect{x - d / 2, y + d / 2 - k, k, k}, kFailedMark);
        if (flaky_here(p))
            c.fill(Rect{x - d / 2, y - d / 2, k, k}, kFlakyMark);
    }
}

void draw_patches(Canvas& c, World& w, const MapAt& at, i32 d, float scale) {
    Events& e = w.events();
    for (u32 k = 0; e.on() && k < e.count(); ++k) {
        const PatchNow& q = e.now(k);
        const u32 colour = kPatchColor[q.type % kPatchTypes];
        const double x = at.ox + q.x * at.s, y = at.oy + q.y * at.s, r = q.radius * at.s;
        // Faint while it rises or eases; a dotted centre where it is.
        ring(c, x, y, r, mix(kLand, colour, 0.3 + 0.7 * q.level), scale * static_cast<float>(1 + q.level));
        const i32 dot = static_cast<i32>(2 * scale + 0.5f);
        c.fill(Rect{static_cast<i32>(x) - dot, static_cast<i32>(y) - dot, 2 * dot + 1, 2 * dot + 1}, colour);
    }
    // The places struck this tick: a star over each.
    const Map& m = w.map();
    const i32 r = d / 2 + static_cast<i32>(5 * scale + 0.5f), s = r * 7 / 10;
    for (u32 i = 0; i < w.place_count(); ++i) {
        if (!w.struck(i))
            continue;
        const i32 x = px_x(m, at, i), y = px_y(m, at, i);
        c.line(x - r, y, x + r, y, kStrikeColor, 2 * scale);
        c.line(x, y - r, x, y + r, kStrikeColor, 2 * scale);
        c.line(x - s, y - s, x + s, y + s, kStrikeColor, scale);
        c.line(x - s, y + s, x + s, y - s, kStrikeColor, scale);
    }
}

void draw_current(Canvas& c, World& w, const MapAt& at, const Rect& area, float scale) {
    Current& cur = w.current();
    if (!cur.on())
        return;
    const Map& m = w.map();
    const double strength = cur.strength() < 1 ? cur.strength() : 1;
    const double head = 5.0 * scale;
    for (u32 k = 0; k < m.link_count(); ++k) {
        if (link_down(w, k))
            continue;
        const MapLink& l = m.link(k);
        const double xa = m.x(l.a), ya = m.y(l.a), xb = m.x(l.b), yb = m.y(l.b);
        const double ab = cur.weight(xa, ya, xb, yb), ba = cur.weight(xb, yb, xa, ya);
        const double wgt = ab > ba ? ab : ba;
        if (wgt < 0.3)
            continue; // across the current: it pushes little along it
        const u32 from = ab > ba ? l.a : l.b, to = ab > ba ? l.b : l.a;
        const double x0 = px_x(m, at, from), y0 = px_y(m, at, from), x1 = px_x(m, at, to), y1 = px_y(m, at, to);
        const u32 colour = mix(kLand, kCurrentColor, 0.25 + 0.75 * wgt * (0.4 + 0.6 * strength));
        arrow(c, x0 + 0.35 * (x1 - x0), y0 + 0.35 * (y1 - y0), x0 + 0.65 * (x1 - x0), y0 + 0.65 * (y1 - y0), head,
              colour, scale);
    }
    // Its heading and the season's strength, in the corner.
    const double r = 16.0 * scale, cx = area.x + 10.0 * scale + r, cy = area.bottom() - 10.0 * scale - r;
    ring(c, cx, cy, r, kFaint, scale);
    arrow(c, cx - r * 0.8 * cur.dx(), cy - r * 0.8 * cur.dy(), cx + r * 0.8 * cur.dx(), cy + r * 0.8 * cur.dy(),
          6.0 * scale, kCurrentColor, 2 * scale);
    char buf[48];
    snprintf(buf, sizeof buf, "current x%.2f", cur.strength());
    c.text(static_cast<i32>(cx + r + 6 * scale), static_cast<i32>(cy) - c.line_h(kFontSmall) / 2, buf, kFontSmall,
           kCurrentColor);
}

i32 draw_wear_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p) {
    char buf[200];
    y = line(c, x, y, mw, lh, "Wear and visits", kFontBold, kDim);
    if (!p.visit_mean())
        snprintf(buf, sizeof buf, "never visited");
    else if (p.next_visit == kNoVisit)
        snprintf(buf, sizeof buf, "visits %llu, the last at %llu, no more", static_cast<unsigned long long>(p.visits),
                 static_cast<unsigned long long>(p.last_visit));
    else
        snprintf(buf, sizeof buf, "visits %llu, the last at %llu, the next at %llu",
                 static_cast<unsigned long long>(p.visits), static_cast<unsigned long long>(p.last_visit),
                 static_cast<unsigned long long>(p.next_visit));
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    snprintf(buf, sizeof buf, "a mean gap of %llu ticks; built %u time%s again",
             static_cast<unsigned long long>(p.visit_mean()), p.incarnation, p.incarnation == 1 ? "" : "s");
    y = line(c, x, y, mw, lh, buf, kFontUi, p.incarnation ? kWarn : kText);
    if (!p.wear.on)
        return line(c, x, y, mw, lh, "nothing wears (wear.rate is 0)", kFontUi, kDim);
    y = line(c, x, y, mw, lh, "of its life / worn threshold (mV); x failed, f flaky", kFontSmall, kDim);
    // Each core, P or E; each memory device by number; then the others.
    static const char* const kKind[2] = {"P", "E"};
    const char* tags[kMaxCores];
    i32 th[kMaxCores > kMaxMemDevices ? kMaxCores : kMaxMemDevices];
    bool flaky[kMaxCores > kMaxMemDevices ? kMaxCores : kMaxMemDevices];
    for (u32 k = 0; k < p.core_count(); ++k) {
        tags[k] = kKind[p.cores[k].kind == DeviceKind::e_core ? 1 : 0];
        th[k] = p.core_threshold(k);
        flaky[k] = p.noise.core_flaky(k) > kFlakyOne;
    }
    y = worn_rows(c, x, y, mw, lh, "cores", tags, p.core_worn, th, flaky, p.core_count());
    static const char* const kNum[16] = {"0 ", "1 ", "2 ", "3 ", "4 ", "5 ", "6 ", "7 ",
                                         "8 ", "9 ", "10 ", "11 ", "12 ", "13 ", "14 ", "15 "};
    u64 weak = 0;
    for (u32 d = 0; d < p.mem.device_count(); ++d) {
        th[d] = p.memory_threshold(d);
        flaky[d] = p.noise.memory_flaky(d) > kFlakyOne;
        weak += p.noise.weak_cells(d);
    }
    y = worn_rows(c, x, y, mw, lh, "memory", p.mem.device_count() <= 16 ? kNum : nullptr, p.memory_worn, th, flaky,
                  p.mem.device_count());
    Worn others[3] = {p.disk_worn, p.network_worn, p.cooler_worn};
    const i32 other_th[3] = {p.disk_threshold(), p.network_threshold(), p.cooler_threshold()};
    static const char* const kOther[3] = {"disk ", "net ", "cool "};
    y = worn_rows(c, x, y, mw, lh, "others", kOther, others, other_th, nullptr, p.has_cooler ? 3 : 2);
    snprintf(buf, sizeof buf, "weak cells %llu; bits they lost %llu", static_cast<unsigned long long>(weak),
             static_cast<unsigned long long>(p.noise.weak));
    y = line(c, x, y, mw, lh, buf, kFontUi, weak ? kWarn : kText);
    if (p.os.blocks_on()) {
        u32 past = 0, most = 0, most_life = 0;
        for (u32 b = 0; b < p.os.block_count(); ++b) {
            past += p.os.block_writes(b) > p.os.block_life(b) ? 1 : 0;
            if (p.os.block_writes(b) > most) {
                most = p.os.block_writes(b);
                most_life = p.os.block_life(b);
            }
        }
        snprintf(buf, sizeof buf, "disk blocks %u, %u past their endurance; the most written %u of %u",
                 p.os.block_count(), past, most, most_life);
        y = line(c, x, y, mw, lh, buf, kFontUi, past ? kWarn : kText);
    }
    (void)w;
    return y;
}

i32 disk_strip_h(Canvas& c, const Place& p, i32 w) {
    if (!p.os.blocks_on() || w < 8)
        return 0;
    const i32 cell = c.px(8) > 3 ? c.px(8) : 3, cols = w / cell > 0 ? w / cell : 1;
    const i32 rows = (static_cast<i32>(p.os.block_count()) + cols - 1) / cols;
    return c.line_h(kFontSmall) + c.px(4) + (rows < 8 ? rows : 8) * cell;
}

void draw_disk_strip(Canvas& c, const Rect& r, const Place& p) {
    if (!p.os.blocks_on())
        return;
    const u32 n = p.os.block_count();
    i32 cell = c.px(8) > 3 ? c.px(8) : 3;
    i32 cols = r.w / cell > 0 ? r.w / cell : 1;
    // More blocks than eight rows hold: smaller cells.
    while (cell > 2 && (static_cast<i32>(n) + cols - 1) / cols > 8) {
        --cell;
        cols = r.w / cell > 0 ? r.w / cell : 1;
    }
    u32 past = 0;
    for (u32 b = 0; b < n; ++b)
        past += p.os.block_writes(b) > p.os.block_life(b) ? 1 : 0;
    char buf[160];
    snprintf(buf, sizeof buf, "disk: %u blocks of %u bytes by wear, blue new to red at its endurance; %u past it%s", n,
             p.os.block_size(), past, p.os.disk_failed() ? "; the disk has failed" : "");
    c.text(r.x, r.y, buf, kFontSmall, past || p.os.disk_failed() ? kWarn : kDim, kAlignLeft, r.w);
    const i32 top = r.y + c.line_h(kFontSmall) + c.px(4), gap = cell >= 5 ? 1 : 0;
    for (u32 b = 0; b < n; ++b) {
        const i32 row = static_cast<i32>(b) / cols, col = static_cast<i32>(b) % cols;
        if (top + (row + 1) * cell > r.bottom())
            break;
        const u32 writes = p.os.block_writes(b), life = p.os.block_life(b);
        const u32 colour = writes > life ? kWornBlock : ramp(life ? static_cast<double>(writes) / life : 0);
        c.fill(Rect{r.x + col * cell, top + row * cell, cell - gap, cell - gap}, colour);
    }
}

namespace {

// One event on the page: its tick, kind (kEv*), the place it picks (~0u:
// none), and what to say.
struct EventRow {
    u64 tick;
    u32 kind, place;
    char text[72];
};

const char* const kEvNames[kEvKinds] = {"devices", "respawns", "cascades", "straits", "kills", "watchdog"};
const u32 kEvColour[kEvKinds] = {0xFF6060, 0x40E0F0, 0xFFE14D, 0x7FB0E0, 0xE0A060, 0x60C0C0};

// A strait's place to pick: the lower end of its first bridge.
u32 strait_place(World& w, u32 strait) {
    Links& l = w.links();
    for (u32 i = 0; i < l.count(); ++i)
        if (l.line(i).strait == strait)
            return l.line(i).a;
    return ~0u;
}

// The observers' tables merged, newest first, each kind shown: at most
// `most` from each table.
void gather(World& w, const Observer& o, const EventsView& ev, Array<EventRow>* out) {
    static const char* const kDevice[kHeatKinds] = {"a P core",           "an E core",        "a memory device",
                                                    "the disk",           "the network device", "the cooling device"};
    static const char* const kCause[kCascadeCauses] = {"load", "strike", "swell"};
    const usize most = 2000;
    usize at[4] = {o.event_log.size(), o.respawn_log.size(), o.cascade_watch.cascades.size(), o.joinings.size()};
    usize floor[4];
    for (u32 k = 0; k < 4; ++k)
        floor[k] = at[k] > most ? at[k] - most : 0;
    out->clear();
    for (;;) {
        // The table whose latest left is the latest.
        u32 pick = 4;
        u64 best = 0;
        const u64 ticks[4] = {at[0] > floor[0] ? o.event_log[at[0] - 1].tick : 0,
                              at[1] > floor[1] ? o.respawn_log[at[1] - 1].tick : 0,
                              at[2] > floor[2] ? o.cascade_watch.cascades[at[2] - 1].start : 0,
                              at[3] > floor[3] ? o.joinings[at[3] - 1].tick : 0};
        for (u32 k = 0; k < 4; ++k)
            if (at[k] > floor[k] && (pick == 4 || ticks[k] > best)) {
                pick = k;
                best = ticks[k];
            }
        if (pick == 4)
            break;
        EventRow r;
        r.tick = best;
        r.place = ~0u;
        r.text[0] = 0;
        const usize i = --at[pick];
        if (pick == 0) {
            const EventRec& e = o.event_log[i];
            const EventKind kind = static_cast<EventKind>(e.kind);
            if (kind == EventKind::failure) {
                r.kind = kEvFailure;
                r.place = e.place;
                snprintf(r.text, sizeof r.text, "%s failed", kDevice[e.detail % kHeatKinds]);
            } else if (kind == EventKind::parted) {
                r.kind = kEvStrait;
                r.place = strait_place(w, e.place);
                snprintf(r.text, sizeof r.text, "strait %u parted", e.place);
            } else {
                r.kind = kind == EventKind::kill ? kEvKill : kEvWatchdog;
                r.place = e.place;
                snprintf(r.text, sizeof r.text, "a program ended %s",
                         kind == EventKind::kill ? "out of memory" : "by the watchdog");
            }
        } else if (pick == 1) {
            const RespawnRec& e = o.respawn_log[i];
            r.kind = kEvRespawn;
            r.place = e.place;
            snprintf(r.text, sizeof r.text, "torn down and built again%s", e.moved ? ", at a new point" : "");
        } else if (pick == 2) {
            const CascadeRec& e = o.cascade_watch.cascades[i];
            r.kind = kEvCascade;
            r.place = e.root;
            snprintf(r.text, sizeof r.text, "a cascade from here (%s), %u places",
                     kCause[static_cast<u32>(e.cause) % 3], e.size);
        } else {
            const JoinRec& e = o.joinings[i];
            r.kind = kEvStrait;
            r.place = strait_place(w, e.strait);
            snprintf(r.text, sizeof r.text, "strait %u joined again", e.strait);
        }
        if (!ev.hide[r.kind])
            out->push(r);
    }
}

} // namespace

void draw_events(Canvas& c, const Rect& r, World& w, const Observer* obs, const UiInput& in, EventsView& ev,
                 u32* place) {
    const i32 bh = button_h(c);
    ButtonRow row{c, in, r.x, r.y, bh};
    for (u32 k = 0; k < kEvKinds; ++k)
        if (row.right(kEvNames[k], !ev.hide[k]))
            ev.hide[k] = !ev.hide[k];
    i32 y = r.y + bh + c.px(6);
    if (!obs) {
        c.text(r.x, y, "no observers: the events are theirs", kFontUi, kDim, kAlignLeft, r.w);
        return;
    }
    static Array<EventRow> rows;
    gather(w, *obs, ev, &rows);
    char buf[120];
    snprintf(buf, sizeof buf, "%u events, newest first (up to 2,000 a log); a click picks its place",
             static_cast<u32>(rows.size()));
    c.text(r.x, y, buf, kFontSmall, kDim, kAlignLeft, r.w);
    y += c.line_h(kFontSmall) + c.px(4);
    const i32 rh = c.line_h(kFontSmall) + c.px(3);
    const ListRows lr = scroll_list(c, in, Rect{r.x, y, r.w, r.bottom() - y}, rh, static_cast<u32>(rows.size()),
                                    ev.scroll);
    for (u32 i = lr.first; i < lr.first + lr.count; ++i) {
        const EventRow& e = rows[i];
        const Rect at = lr.row(i);
        if (at.contains(in.mx, in.my)) {
            c.fill(at, kRowHot);
            if (in.pressed && e.place != ~0u && e.place < w.place_count())
                *place = e.place;
        }
        if (e.place == *place)
            c.fill(at, kRowSel);
        if (e.place != ~0u)
            snprintf(buf, sizeof buf, "%10llu  %5u  %s", static_cast<unsigned long long>(e.tick), e.place, e.text);
        else
            snprintf(buf, sizeof buf, "%10llu         %s", static_cast<unsigned long long>(e.tick), e.text);
        c.fill(Rect{at.x, at.y + c.px(3), c.px(6), c.px(6)}, kEvColour[e.kind]);
        c.text(at.x + c.px(10), at.y, buf, kFontSmall, kText, kAlignLeft, at.w - c.px(10));
    }
}

} // namespace nova2
