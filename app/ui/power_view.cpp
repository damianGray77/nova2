#include "power_view.h"

#include <cmath>   // log, sqrt
#include <cstdio>  // snprintf
#include <cstring> // memset

#include "nova2/world.h"
#include "theme.h"

namespace nova2 {

namespace {

// The proximity links' colours by what they carry, the sources' by type
// (steady, daylight, gusty), and a place under every core's threshold.
const u32 kLandPower = 0xD9A441, kSeaPower = 0x4FA3E0, kBorderPower = 0xD06CC8;
const u32 kSourceColor[kSourceTypes] = {0xF2F2F2, 0xFFD23F, 0x5FE0FF};
const u32 kSwellColor = 0xA080FF; // theme.h's kNowColor for a swell
const u32 kUnderVolt = 0x7A3030, kOverVolt = 0xFFFFFF, kNoPower = 0x30343C;

u32 mix(u32 a, u32 b, double t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    u32 out = 0;
    for (u32 sh = 0; sh < 24; sh += 8) {
        const double x = ((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t;
        out |= static_cast<u32>(x + 0.5) << sh;
    }
    return out;
}

// Dark blue through green and yellow to red, for a share from 0 to 1.
u32 ramp(double t) {
    static const u32 kStops[5] = {0x23395B, 0x2E8F8A, 0x6CC24A, 0xF2C84B, 0xE4572E};
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const double at = t * 4;
    const u32 k = at >= 4 ? 3 : static_cast<u32>(at);
    return mix(kStops[k], kStops[k + 1], at - k);
}

double degrees(i64 t) { return static_cast<double>(t) / kDegree; }
double watts(u64 uw) { return static_cast<double>(uw) / 1e6; }

i32 px_x(const Map& m, const MapAt& at, u32 i) { return at.ox + static_cast<i32>(m.x(i) * at.s + 0.5); }
i32 px_y(const Map& m, const MapAt& at, u32 i) { return at.oy + static_cast<i32>(m.y(i) * at.s + 0.5); }

void diamond(Canvas& c, i32 x, i32 y, i32 r, u32 colour, float width) {
    c.line(x - r, y, x, y - r, colour, width);
    c.line(x, y - r, x + r, y, colour, width);
    c.line(x + r, y, x, y + r, colour, width);
    c.line(x, y + r, x - r, y, colour, width);
}

i32 line(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, const char* s, u32 font, u32 colour) {
    c.text(x, y, s, font, colour, kAlignLeft, mw);
    return y + lh;
}

} // namespace

const char* power_colour_label(PowerColour c) {
    static const char* const kNames[] = {"", "voltage", "temperature", "draw", "cooling"};
    return kNames[static_cast<u32>(c) % 5];
}

bool gate4_world(const World& w) { return w.powered() || w.heated(); }

void power_scale(World& w, PowerScale* s) {
    *s = PowerScale{};
    const u32 n = w.place_count();
    for (u32 i = 0; i < n; ++i) {
        const Place& p = w.place(i);
        const double draw = watts(p.draw.total()), t = degrees(p.temp), cool = watts(w.cooling_rate(i));
        s->most_w = draw > s->most_w ? draw : s->most_w;
        s->temp_low = i == 0 || t < s->temp_low ? t : s->temp_low;
        s->temp_high = i == 0 || t > s->temp_high ? t : s->temp_high;
        s->cool_low = i == 0 || cool < s->cool_low ? cool : s->cool_low;
        s->cool_high = i == 0 || cool > s->cool_high ? cool : s->cool_high;
    }
    if (n) {
        const HeatLaw& h = w.place(0).heat;
        s->outside = degrees(h.outside);
        s->limit = degrees(h.limit[0]);
        for (u32 k = 1; k < kHeatKinds; ++k)
            s->limit = degrees(h.limit[k]) < s->limit ? degrees(h.limit[k]) : s->limit;
    }
}

u32 power_colour(World& w, u32 i, PowerColour mode, const PowerScale& s) {
    const Place& p = w.place(i);
    if (mode == PowerColour::voltage) {
        // Nothing: dark. Under every core's threshold: dull red, brighter
        // as it nears it. In the cores' range: green at the threshold to
        // yellow at the P cores' maximum. Above that: towards white.
        const PowerSettings& ps = p.power();
        const i32 mv = p.env.voltage_mv, low = ps.p.threshold < ps.e.threshold ? ps.p.threshold : ps.e.threshold;
        if (mv <= 0)
            return kNoPower;
        if (mv < low)
            return mix(kNoPower, kUnderVolt, 0.3 + 0.7 * mv / low);
        if (mv <= ps.p.maximum)
            return mix(0x3E9B4F, 0xF2D34B, static_cast<double>(mv - low) / (ps.p.maximum - low));
        return mix(0xF2D34B, kOverVolt, static_cast<double>(mv - ps.p.maximum) / 300);
    }
    if (mode == PowerColour::temperature) {
        const double span = s.limit - s.outside;
        return ramp(span > 0 ? (degrees(p.temp) - s.outside) / span : 0);
    }
    if (mode == PowerColour::draw) {
        const double draw = watts(p.draw.total());
        return draw > 0 && s.most_w > 0 ? ramp(log(1 + 100 * draw) / log(1 + 100 * s.most_w)) : kNoPower;
    }
    const double cool = watts(w.cooling_rate(i));
    return mix(0x3A3F4A, 0x5FD0E8, s.cool_high > s.cool_low ? (cool - s.cool_low) / (s.cool_high - s.cool_low) : 1);
}

void power_legend(World& w, PowerColour mode, const PowerScale& s, char* out, usize size) {
    const PowerSettings& ps = w.place(0).power();
    const i32 low = ps.p.threshold < ps.e.threshold ? ps.p.threshold : ps.e.threshold;
    if (mode == PowerColour::voltage)
        snprintf(out, size,
                 "voltage: dark none, red under every core's threshold (%d mV), green to yellow up to the P cores' "
                 "maximum (%d), whiter above",
                 low, ps.p.maximum);
    else if (mode == PowerColour::temperature)
        snprintf(out, size,
                 "temperature: blue at the outside's %.0f degrees to red at the lowest heat limit, %.0f; the places "
                 "now %.1f to %.1f%s",
                 s.outside, s.limit, s.temp_low, s.temp_high, w.heated() ? "" : " (heat is off)");
    else if (mode == PowerColour::draw)
        snprintf(out, size, "draw: what each place's devices draw (log scale), the most now %.2f W; dark: nothing",
                 s.most_w);
    else
        snprintf(out, size, "cooling: %.2f (grey) to %.2f (blue) W a degree; a white corner: a cooling device%s",
                 s.cool_low, s.cool_high, w.heated() ? "" : " (heat is off)");
}

void draw_power_links(Canvas& c, World& w, const MapAt& at, float scale) {
    const Map& m = w.map();
    const u32 links = m.power_link_count();
    const bool solved = w.powered();
    const Grid& g = w.grid();
    double most = 0;
    for (u32 k = 0; solved && k < links; ++k) {
        if (!g.link_g(k))
            continue;
        const i64 f = g.link_flow(k);
        const double a = static_cast<double>(f < 0 ? -f : f);
        most = a > most ? a : most;
    }
    for (u32 k = 0; k < links; ++k) {
        const PowerLink& l = m.power_link(k);
        if (solved && !g.link_g(k))
            continue; // too weak to keep: the solve leaves it out
        const u32 colour = l.border ? kBorderPower : (l.water > 0 ? kSeaPower : kLandPower);
        const i32 xa = px_x(m, at, l.a), ya = px_y(m, at, l.a), xb = px_x(m, at, l.b), yb = px_y(m, at, l.b);
        const i64 flow = solved ? g.link_flow(k) : 0;
        const double share = most > 0 ? sqrt(static_cast<double>(flow < 0 ? -flow : flow) / most) : 0;
        // A link carrying nothing to speak of is faint and thin; the most
        // loaded is four times as wide.
        c.line(xa, ya, xb, yb, mix(kLand, colour, 0.35 + 0.65 * share), scale * static_cast<float>(1 + 3 * share));
        if (share >= 0.05) {
            // A mark three quarters of the way to the end the power flows to.
            const double t = flow > 0 ? 0.75 : 0.25;
            const i32 mx = xa + static_cast<i32>((xb - xa) * t), my = ya + static_cast<i32>((yb - ya) * t);
            const i32 r = static_cast<i32>(scale * static_cast<float>(1.5 + 2 * share) + 0.5f);
            c.fill(Rect{mx - r, my - r, 2 * r + 1, 2 * r + 1}, colour);
        }
    }
}

void draw_coolers(Canvas& c, World& w, const MapAt& at, i32 d) {
    const Map& m = w.map();
    const i32 k = d / 3 > 2 ? d / 3 : 2;
    for (u32 i = 0; i < w.place_count(); ++i)
        if (w.place(i).has_cooler)
            c.fill(Rect{px_x(m, at, i) + d / 2 - k, px_y(m, at, i) - d / 2, k, k}, 0xFFFFFF);
}

void draw_sources(Canvas& c, World& w, const MapAt& at, i32 d, float scale) {
    if (!w.powered())
        return;
    const Map& m = w.map();
    const Sources& s = w.sources();
    static Array<u8> at_place; // the sources drawn at each place so far
    at_place.resize_unfilled(w.place_count());
    memset(at_place.data(), 0, at_place.size());
    for (u32 k = 0; k < s.count(); ++k) {
        const Source& src = s.source(k);
        const SourceState& state = s.state(k);
        if (src.place >= w.place_count())
            continue;
        // Its size: from 1 W to 10 kW rated, on a log scale; each further
        // source at a place a ring outside the last.
        double size = log(1 + watts(src.rated_uw)) / log(1 + 10000.0);
        size = size > 1 ? 1 : size;
        const u32 nth = at_place[src.place] < 250 ? at_place[src.place]++ : 250;
        const i32 r = d / 2 + static_cast<i32>(scale * static_cast<float>(4 + 8 * size + 4 * nth) + 0.5f);
        const i32 x = px_x(m, at, src.place), y = px_y(m, at, src.place);
        const u32 colour = kSourceColor[src.type % kSourceTypes];
        const bool out = state.capacity == 0 || s.dropout(src, s.tick()) < kCapacityOne;
        diamond(c, x, y, r, out ? mix(kLand, colour, 0.55) : colour, out ? scale : 2 * scale);
        if (state.swell > kCapacityOne)
            diamond(c, x, y, r + static_cast<i32>(3 * scale + 0.5f), kSwellColor, 2 * scale);
    }
}

i32 draw_power_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p) {
    // (The inspector is narrow: a line holds some 45 characters.)
    char buf[160];
    const PowerSettings& ps = p.power();
    snprintf(buf, sizeof buf, "%d mV, solved from the sources", p.env.voltage_mv);
    y = line(c, x, y, mw, lh, buf, kFontUi, p.env.voltage_mv < ps.e.threshold ? kWarn : kText);
    snprintf(buf, sizeof buf, "draws %.2f W: cores %.2f, memory %.2f,", watts(p.draw.total()), watts(p.draw.cores),
             watts(p.draw.memory));
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    snprintf(buf, sizeof buf, "   disk %.2f, network %.2f, cooling %.2f", watts(p.draw.disk), watts(p.draw.network),
             watts(p.draw.cooling));
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    // The sources at the place: the first two, and how many more.
    const Sources& s = w.sources();
    u32 here = 0;
    for (u32 k = 0; k < s.count(); ++k) {
        const Source& src = s.source(k);
        if (src.place != p.id() || ++here > 2)
            continue;
        const SourceState& state = s.state(k);
        const bool dropped = s.dropout(src, s.tick()) < kCapacityOne;
        const u32 colour = state.swell > kCapacityOne ? kSwellColor : (dropped || !state.capacity ? kWarn : kText);
        snprintf(buf, sizeof buf, "%s source%s, rated %.0f W, sag %d mV", kSourceTypeNames[src.type % kSourceTypes],
                 src.fixed ? " (by hand)" : "", watts(src.rated_uw), src.sag);
        y = line(c, x, y, mw, lh, buf, kFontUi, colour);
        char how[48] = "";
        if (state.swell > kCapacityOne)
            snprintf(how, sizeof how, ", swelling x%.2f", static_cast<double>(state.swell) / kCapacityOne);
        else if (dropped)
            snprintf(how, sizeof how, ", in a drop-out");
        snprintf(buf, sizeof buf, "   %d mV, at %.2f of full%s", static_cast<i32>((state.uv + 500) / 1000),
                 static_cast<double>(state.capacity) / kCapacityOne, how);
        y = line(c, x, y, mw, lh, buf, kFontUi, colour);
    }
    if (here > 2) {
        snprintf(buf, sizeof buf, "and %u more source%s here", here - 2, here == 3 ? "" : "s");
        y = line(c, x, y, mw, lh, buf, kFontUi, kDim);
    } else if (!here) {
        y = line(c, x, y, mw, lh, "no source at this place", kFontUi, kDim);
    }
    return y;
}

namespace {

// A row of temperatures under a name, six to a line, a tripped device's
// followed by "off"; the line of one that is tripped in the heat trip's
// colour.
i32 temp_rows(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, const char* name, const i64* temp, const bool* tripped,
              u32 count) {
    char buf[160];
    for (u32 from = 0; from < count; from += 6) {
        bool any = false;
        usize at = static_cast<usize>(snprintf(buf, sizeof buf, "%-6s", from ? "" : name));
        for (u32 k = from; k < count && k < from + 6; ++k) {
            at += static_cast<usize>(
                snprintf(buf + at, sizeof buf - at, tripped[k] ? " %5.1f off" : " %5.1f", degrees(temp[k])));
            any = any || tripped[k];
        }
        y = line(c, x, y, mw, lh, buf, kFontMono, any ? kNowColor[3] : kText);
    }
    return y;
}

} // namespace

i32 draw_heat_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p) {
    char buf[160];
    y = line(c, x, y, mw, lh, "Heat", kFontBold, kDim);
    // The place, and its cooling: its own rate, and its cooling device.
    snprintf(buf, sizeof buf, "place %.1f degrees, the outside %.1f", degrees(p.temp), degrees(p.heat.outside));
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    const double coast = w.map().placed() ? w.map().water_share(p.id()) : 0;
    snprintf(buf, sizeof buf, "cooling %.2f W a degree%s", watts(w.cooling_rate(p.id())),
             coast > 0 ? ", on the coast" : "");
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    if (!p.has_cooler) {
        y = line(c, x, y, mw, lh, "no cooling device", kFontUi, kDim);
    } else {
        const bool off = p.cooler_tripped || !p.cooler_state.on;
        snprintf(buf, sizeof buf, "cooling device %s, at %.2f of its rate, %.1f",
                 p.cooler_tripped ? "tripped" : (p.cooler_state.on ? "on" : "cut out"),
                 static_cast<double>(p.cooler_work) / kCapacityOne, degrees(p.cooler_temp));
        y = line(c, x, y, mw, lh, buf, kFontUi, off ? kCutOut : kText);
    }
    // Each device's temperature, and whether it is tripped.
    y = temp_rows(c, x, y, mw, lh, "cores", p.core_temp, p.core_tripped, p.core_count());
    y = temp_rows(c, x, y, mw, lh, "memory", p.memory_temp, p.memory_tripped, p.mem.device_count());
    const i64 others[2] = {p.disk_temp, p.network_temp};
    const bool others_off[2] = {p.disk_tripped, p.network_tripped};
    y = temp_rows(c, x, y, mw, lh, "disk, network", others, others_off, 2);
    snprintf(buf, sizeof buf, "trips: P %llu, E %llu, memory %llu, disk %llu,",
             static_cast<unsigned long long>(p.trips[kHeatP]), static_cast<unsigned long long>(p.trips[kHeatE]),
             static_cast<unsigned long long>(p.trips[kHeatMemory]),
             static_cast<unsigned long long>(p.trips[kHeatDisk]));
    y = line(c, x, y, mw, lh, buf, kFontUi, kText);
    snprintf(buf, sizeof buf, "   network %llu, cooler %llu; threads ended %llu",
             static_cast<unsigned long long>(p.trips[kHeatNetwork]),
             static_cast<unsigned long long>(p.trips[kHeatCooler]), static_cast<unsigned long long>(p.heat_threads));
    return line(c, x, y, mw, lh, buf, kFontUi, kText);
}

} // namespace nova2
