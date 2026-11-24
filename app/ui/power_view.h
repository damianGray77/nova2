#pragma once

// Gate 4's views (design.md 13.4; build-plan-gate4.md step 9), for a world
// whose voltage is solved from sources or that has heat on. The viewer
// (viewer.h) calls these from its map and its inspector; the charts of
// power, heat, trips and cut-outs are in viewer.cpp with the other charts.
//   - The map's places coloured by voltage, by temperature, by what each
//     draws, or by its cooling (a place with a cooling device marked).
//   - Over the map: the proximity links by what they carry (power and heat
//     over land, power only across water, and those that cross a border of
//     the resistance field), each as wide as the power flowing in it, with
//     a mark towards the end the power flows to; and the sources, each a
//     diamond round its place in its type's colour, larger for a larger
//     one, doubled while it swells and thin while it is dropped out or
//     giving nothing.
//   - In the inspector: the place's voltage and what it draws by device,
//     the sources at it with their states; its temperature and cooling,
//     each device's temperature and whether it is tripped, and its trips.
// Nothing here changes the world.
#include "canvas.h"
#include "nova2/types.h"

namespace nova2 {

class World;
class Place;

// A gate 4 colouring of the map's places (none: the map's own colouring).
enum class PowerColour : u8 { none, voltage, temperature, draw, cooling, count };
const char* power_colour_label(PowerColour c);

// Whether a world has anything of gate 4's to show: sources, or heat.
bool gate4_world(const World& w);

// What a frame's colours are drawn against: the most a place draws, the
// places' temperatures and cooling rates from lowest to highest, and the
// lowest heat limit.
struct PowerScale {
    double most_w = 0;
    double temp_low = 0, temp_high = 0, limit = 100, outside = 25;
    double cool_low = 0, cool_high = 0;
};
void power_scale(World& w, PowerScale* s);
// Place i's colour under a colouring, and what the colours say.
u32 power_colour(World& w, u32 i, PowerColour mode, const PowerScale& s);
void power_legend(World& w, PowerColour mode, const PowerScale& s, char* out, usize size);

// Where the map is drawn: the pixel of its top left, and the pixels its
// height takes.
struct MapAt {
    i32 ox = 0, oy = 0;
    double s = 1;
};

// The proximity links, over the data links. `scale`: the canvas's.
void draw_power_links(Canvas& c, World& w, const MapAt& at, float scale);
// A place's cooling device, as a mark on its square of d pixels.
void draw_coolers(Canvas& c, World& w, const MapAt& at, i32 d);
// The sources, round their places' squares of d pixels.
void draw_sources(Canvas& c, World& w, const MapAt& at, i32 d, float scale);

// The inspector's lines, from (x, y), each lh high and at most mw wide; the
// y under them. Power: for a world with sources, in place of the lines
// about a place's own supply. Heat: for a world with heat on.
i32 draw_power_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p);
i32 draw_heat_lines(Canvas& c, i32 x, i32 y, i32 mw, i32 lh, World& w, const Place& p);

} // namespace nova2
