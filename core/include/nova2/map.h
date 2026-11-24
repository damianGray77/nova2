#pragma once

// The map (design.md 6.1, 6.2; build-plan-gate3.md step 2): Nova 1's
// landscape, rebuilt, with the parts gate 3's data needs. It's worked out
// once from the seed and the settings, so it needs no snapshot; its hash
// checks it.
//
// - Land and water: a raster of warped fractal terrain that sinks towards
//   the map's edges, cut at the land share, and flood-filled (by 4
//   neighbours) into landmasses. The map is `width` across and 1 high, and
//   every length on it is in its height.
// - Places, scattered on land by darts with a spacing, as tight as fits: the
//   spacing starts loose and shrinks until every place fits. The density
//   field sets where darts land and how close they may come, so places are
//   map.density times as dense where it's highest as where it's lowest
//   (towns and countryside). Setup refuses a map whose places would sit
//   closer than a quarter of a raster cell. Their ids are then shuffled.
// - Data links: on each landmass by itself, the Gabriel graph of its places
//   (two are linked if no other place of the landmass lies in the circle on
//   them as a diameter), from its Delaunay triangulation: an edge is kept if
//   both angles facing it are acute. It's connected, so a landmass is one
//   piece (a degenerate one is joined by its shortest gaps, counted).
// - Bridges, data links across water: from the triangulation of all the
//   places, the shortest crossings between each pair of landmasses, up to
//   map.bridges a pair, none longer than map.bridge.longest, no place at the
//   end of two. Then every landmass is joined to the rest, through the
//   shortest crossings whatever their length (counted as joins), except
//   those left unbridged (a chance each, map.unbridged).
// - Each link has its length; a delay in whole ticks, the per-hop cost plus
//   length x the per-length cost, rounded up (11.3), with lengths in average
//   link lengths (6.1: the average of the links on land); and a bandwidth,
//   from the bandwidth field at its middle, ranked among the links on land
//   and spread evenly on a log scale from the least to the most. Bridges
//   have their own.
//
// The power map (6.1, 6.2; build-plan-gate4.md step 2) is worked out beside
// all that, from the same places, and changes none of it:
// - Proximity links, which carry power, and heat over land: the Gabriel
//   graph over all the places, whatever their landmass, from their
//   triangulation. On land they're nearly the data links; a data link on
//   land with a place of another landmass in its circle has none, and a
//   proximity link between landmasses is power only. A test map's are its
//   data links.
// - A link's water width, the part of it that lies over water cells, walked
//   through the raster exactly.
// - Its resistance, in average links of the usual resistance: its length x
//   a value a length from the resistance field (read at its middle, ranked
//   among the proximity links, and spread evenly on a log scale from
//   map.resistance.min to .max) x e^(its water width / map.water.reach),
//   and map.border.strength more if it crosses a grid border: if its ends
//   lie on opposite sides of a step of the resistance field. What a link of
//   the usual resistance is in ohms is the power solve's setting.
// - Catchments: each land cell belongs to the place nearest its middle
//   (6.1: a source lands on land and joins the nearest place), whatever the
//   landmass. A place's water share is the share of water among the cells
//   bordering its catchment (cells beside it and not in it; past the map's
//   edge is water). A place so close to another that no cell's middle is
//   nearest it has no catchment, and its share is read round its own cell.
//   A test map has no raster: every place's catchment is 1 and no water.
// - Each place's cooling, over the usual: the cooling field at the place,
//   ranked, and spread evenly on a log scale from map.cooling.min to .max.
//   The field for cooling devices is only drawn here (Field::coolers).
//
// From gate 5 a place can respawn (7.8; build-plan-gate5.md step 6): torn
// down at a visit and built again at a new point, with the same id and an
// incarnation one higher. The map then changes:
// - The new point is drawn from the seed, the id and the incarnation: a land
//   cell by the density field, a point in it, kept if it is no closer to
//   any other place than the spacing the map settled on allows (as the
//   darts were). Up to kRespawnTries draws; if none fits, or the map is a
//   test map, the place stays where it was.
// - Both link sets are worked out again from the places' points, by the
//   rules above. A bridge is anchored to the two points on land its places
//   stood on at setup: each end goes to the place of its landmass nearest
//   its anchor (a landmass with nobody on it has no bridges meanwhile). A
//   landmass with places and no way to the rest then gets its shortest
//   crossing, whatever its length, as setup's joins; that bridge stays.
// - A link between two places that have never respawned, where setup had
//   one, keeps the values setup gave it. Any other link is new: its
//   bandwidth, or its resistance a length, is read from its field at its
//   middle, with a scatter of its own, and is the value setup dealt to the
//   field value nearest that (7.3). The average link length, the unit of
//   delays and resistances, stays setup's.
// - Catchments and water shares are worked out again, and the place's
//   cooling is the one setup dealt to the cooling field value nearest its
//   new point's.
// So the map is a function of the seed, the settings, each place's point
// and incarnation, and the bridges' anchors; those are what a snapshot
// saves once any place has respawned.
//
// The test maps: `grid`, places in rows (as square as fits) linked to the
// places beside them, and `ring`, places round a circle each linked to the
// next. Both are one landmass with their ids in order. `none` is gates 1
// and 2's world: no positions and no links.
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/field.h"
#include "nova2/hashmap.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h

enum class MapKind : u8 { none, landscape, grid, ring };

struct MapSettings {
    MapKind kind = MapKind::landscape;
    // Land (6.1): Nova 1's six settings.
    double aspect = 1.6, land = 0.3, land_scale = 3, warp = 0.3, border = 0.3;
    u32 resolution = 256;
    double density = 3; // towns' density over the countryside's
    // Bridges.
    u32 bridges = 2;
    double bridge_longest = 0.25; // in the map's height
    double unbridged = 0;         // each landmass's chance of none
    // Links (11.3).
    u32 bandwidth_min = 16, bandwidth_max = 256, bridge_bandwidth = 256; // bytes a tick
    u32 delay_hop = 1;                                                   // ticks a hop
    double delay_length = 1;                                             // ticks per average link length
    // The fields (6.2, 7.3).
    bool fields = true; // the hardware and OS values from them
    double field_scale = 3, scatter = 0.05;
    FieldShape shape[kFieldCount];
    // The power map (6.1, 6.2), with lengths in average link lengths.
    double resistance_min = 0.5, resistance_max = 2; // a link's resistance a length, over the usual
    double reach = 1;                                // water's reach
    double border_strength = 10;                     // what crossing a grid border adds, in links; 0 is none
    double cooling_min = 0.5, cooling_max = 2;       // a place's cooling, over the usual
};

void read_map_settings(Config& cfg, MapSettings* ms);

struct MapLink {
    u32 a = 0, b = 0;   // its places, a < b
    double length = 0;  // in the map's height
    u32 bandwidth = 0;  // bytes a tick
    u32 delay = 0;      // whole ticks: the per-hop cost plus length x the per-length cost, rounded up
    bool bridge = false;
};

inline constexpr u32 kNoLink = 0xFFFFFFFFu;
inline constexpr u32 kNoPlace = 0xFFFFFFFFu;

// A proximity link (6.1): it carries power, and heat where it's over land.
struct PowerLink {
    u32 a = 0, b = 0;      // its places, a < b
    u32 data = kNoLink;    // the data link between the same two places, or kNoLink: power only
    double length = 0;     // in the map's height
    double water = 0;      // the part of it over water cells, in the map's height; 0 is wholly over land
    double per_length = 1; // its resistance a length, over the usual: map.resistance.min to .max
    bool border = false;   // its ends lie on opposite sides of a step of the resistance field
    // In average links of the usual resistance: its length in links x per_length x e^(water / reach), and the
    // border strength more if it crosses one. Infinite across water too wide to matter.
    double resistance = 0;
};

// The draws a respawned place's new point may take (7.8).
inline constexpr u32 kRespawnTries = 1000;

// A field's values as setup found them, lowest first, each with who had it
// (7.3: a respawned place takes "the value the world dealt to the field
// value nearest its own").
struct NearTable {
    Array<double> v; // ascending
    Array<u32> who;
    double span = 0; // the scatter's reach either way: map.field.scatter x the values' range
    // From each one's value (who: its index).
    void build(const Array<double>& values, double scatter_span);
    // Who had the value nearest x, the lower of two as near; with `only`,
    // among those with only[who] != 0. kNoPlace if there is none.
    u32 nearest(double x, const u8* only = nullptr) const;
};

// A bridge's anchors (7.8): the points on land its ends stood on, and their
// landmasses.
struct BridgeAnchor {
    double ax = 0, ay = 0, bx = 0, by = 0;
    u32 ma = 0, mb = 0;
};

// The Delaunay triangulation of n points (Bowyer-Watson, from Nova 1): three
// indices a triangle, counter-clockwise. Fewer than 3 points, or all on a
// line, give none.
void delaunay(const double* x, const double* y, u32 n, Array<u32>* tris);

class Map {
  public:
    // Works it out. False, with the reason, if the places don't fit.
    bool build(u64 seed, u32 places, const MapSettings& s, TextBuf* why);

    MapKind kind() const { return kind_; }
    // Whether the places have positions (every kind but none).
    bool placed() const { return kind_ != MapKind::none; }
    u32 place_count() const { return n_; }
    double width() const { return width_; }
    double x(u32 i) const { return x_[i]; }
    double y(u32 i) const { return y_[i]; }
    u32 landmass(u32 i) const { return mass_[i]; }
    u32 landmasses() const { return masses_; }

    u32 link_count() const { return static_cast<u32>(links_.size()); }
    const MapLink& link(u32 k) const { return links_[k]; }
    // The links at place i, in order of the place at their other end.
    u32 degree(u32 i) const { return begin_[i + 1] - begin_[i]; }
    u32 link_at(u32 i, u32 j) const { return adj_[begin_[i] + j]; }
    u32 other(u32 k, u32 i) const { return links_[k].a == i ? links_[k].b : links_[k].a; }

    // The proximity links, in order of their places, and those at place i in
    // order of the place at their other end.
    u32 power_link_count() const { return static_cast<u32>(plinks_.size()); }
    const PowerLink& power_link(u32 k) const { return plinks_[k]; }
    u32 power_degree(u32 i) const { return pbegin_[i + 1] - pbegin_[i]; }
    u32 power_link_at(u32 i, u32 j) const { return padj_[pbegin_[i] + j]; }
    u32 power_other(u32 k, u32 i) const { return plinks_[k].a == i ? plinks_[k].b : plinks_[k].a; }
    // A place's catchment, in raster cells (1 on a map with no raster); the
    // share of water among the cells bordering it, 0 to 1; and its cooling,
    // over the usual.
    u32 catchment(u32 i) const { return catchment_[i]; }
    double water_share(u32 i) const { return water_share_[i]; }
    double cooling(u32 i) const { return cooling_[i]; }
    // The place whose catchment a raster cell is in (a landscape's only), or
    // kNoPlace for water.
    u32 cell_place(u32 cx, u32 cy) const { return cell_place_[static_cast<usize>(cy) * raster_w_ + cx]; }
    // How the power map came out.
    u32 power_wet() const { return power_wet_; }         // proximity links with any water under them
    u32 power_only() const { return power_only_; }       // proximity links that are no data link
    u32 data_only_land() const { return data_only_; }    // data links on land that are no proximity link
    u32 borders_crossed() const { return borders_; }     // proximity links that cross a grid border
    double border_strength() const { return border_strength_; }

    // How it came out, for reports.
    double spacing() const { return spacing_; }         // the darts' widest spacing
    double mean_length() const { return mean_length_; } // the average link length (the links on land)
    // A landscape's land cells, and the density field over them summed in
    // that order: what a dart is drawn from (for events.h's patches).
    void land_table(Array<u32>* cells, Array<double>* cum) const;
    u32 bridges() const { return bridges_; }            // bridges, joins among them
    u32 joins() const { return joins_; }                // bridges longer than map.bridge.longest
    u32 patches() const { return patches_; }            // links joining a degenerate landmass's pieces
    u32 peopled() const { return peopled_; }            // landmasses with places on them
    u32 unbridged() const { return unbridged_; }        // peopled landmasses left with no bridges

    // The raster (a landscape's only): land or water, and each land cell's
    // landmass.
    u32 raster_w() const { return raster_w_; }
    u32 raster_h() const { return raster_h_; }
    bool land(u32 cx, u32 cy) const { return land_[static_cast<usize>(cy) * raster_w_ + cx] != 0; }

    // A field's value at each place, with its scatter (7.3): each place's
    // value nudged by up to `scatter` of the values' range, either way.
    void field_values(u64 seed, Field f, const MapSettings& s, Array<double>* out) const;

    // --- Respawning (7.8; the head of this file) -----------------------------
    // Keeps what a respawn needs of the map as setup left it: the density
    // over the land, each place's spacing, the bridges' anchors, the links'
    // values, and each field's values over the places. Called before the
    // first place moves (respawn and a snapshot's load call it; it does
    // nothing a second time).
    void prepare_respawns();
    bool prepared() const { return ready_; }
    // Place i is torn down and built again. True if it has a new point;
    // false if it stays where it was. Either way its incarnation is one
    // higher, every link at it is a new link, and the links, the bridges'
    // ends, the catchments and its cooling are worked out again.
    bool respawn(u32 i);
    // Tests: everything a respawn works out again, with nobody moved.
    void relink_again() {
        prepare_respawns();
        relink(kNoPlace);
    }
    u32 incarnation(u32 i) const { return i < inc_.size() ? inc_[i] : 0; }
    u32 respawned() const { return respawned_; } // places with an incarnation above 0
    u32 respawns() const { return respawns_; }   // respawns in all: the incarnations summed
    // After a respawn: where each data link and each proximity link the map
    // had before it is now, or kNoLink: gone (every link at the respawned
    // place is gone, and those it has now are new).
    const Array<u32>& data_moved() const { return data_moved_; }
    const Array<u32>& power_moved() const { return power_moved_; }
    // A field's value at place i's point now, with the scatter its
    // incarnation draws; and the place setup dealt the value nearest v (with
    // `only`, among the places with only[place] != 0), or kNoPlace.
    double field_now(Field f, u32 i) const;
    u32 nearest_dealt(Field f, double v, const u8* only = nullptr) const {
        return field_[static_cast<u32>(f)].nearest(v, only);
    }
    // The respawned places' points and incarnations and the bridges'
    // anchors, for a snapshot; loading them works the links out again.
    void serialize_respawns(Archive& ar);
    u64 respawn_hash() const;
    const Array<BridgeAnchor>& anchors() const { return anchors_; }
    // The map's invariants (build-plan-gate5.md 6): the proximity links are
    // the Gabriel graph over all the places and the links on land each
    // landmass's (but its patches), against every pair with `brute` and
    // each link against every place without; no two links on land cross,
    // nor two proximity links (with `brute`); every landmass is one piece;
    // and every landmass with places that isn't left alone is joined to
    // the rest. False, with the first broken in why.
    bool check_links(bool brute, TextBuf* why) const;

    // The data map's: positions, ids, landmasses, and the data links and
    // bridges with their numbers. The power map isn't in it, so it's gate
    // 3's hash still.
    u64 hash() const;
    // The power map's: the proximity links with their numbers, and each
    // place's catchment, water share and cooling.
    u64 power_hash() const;
    // One line on how it came out: places, landmasses, links per place,
    // bridges.
    void describe(TextBuf* t) const;
    // And one on the power map: proximity links a place, those over water,
    // those that differ from the data links, the borders crossed.
    void describe_power(TextBuf* t) const;

  private:
    bool landscape(u64 seed, const MapSettings& s, TextBuf* why);
    void grid();
    void ring();
    void finish(u64 seed, const MapSettings& s);
    void add_link(u32 a, u32 b, bool bridge);
    void power(u64 seed, const MapSettings& s);
    double water_under(u32 a, u32 b) const;
    void catchments();
    // The parts of setup a respawn runs again.
    void density_over_land(const Array<u32>& land_list, Array<double>* d, Array<double>* cum) const;
    void land_links();
    void sort_links();
    void set_delays();
    void index_links();
    void gabriel_all(Array<u64>* kept) const;
    void set_resistance(PowerLink& l) const;
    void index_power();
    // And a respawn's own.
    void bridge_links();
    u32 nearest_on(u32 mass, double x, double y) const;
    void relink(u32 moved);
    void place_table(Field f, NearTable* t) const;
    double link_scatter(u32 a, u32 b, u32 which) const;

    MapKind kind_ = MapKind::none;
    u32 n_ = 0;
    double width_ = 1;
    Array<double> x_, y_;
    Array<u32> mass_;
    u32 masses_ = 0;
    Array<MapLink> links_;
    Array<u32> begin_, adj_;
    double spacing_ = 0, mean_length_ = 1;
    u32 bridges_ = 0, joins_ = 0, patches_ = 0, peopled_ = 0, unbridged_ = 0;
    u32 raster_w_ = 0, raster_h_ = 0;
    Array<u8> land_;
    Array<u32> cell_mass_;
    // The power map.
    Array<PowerLink> plinks_;
    Array<u32> pbegin_, padj_;
    Array<u32> catchment_, cell_place_;
    Array<double> water_share_, cooling_;
    u32 power_wet_ = 0, power_only_ = 0, data_only_ = 0, borders_ = 0;
    double border_strength_ = 0;
    // Respawning. From setup: the seed and the settings, which landmasses
    // are left alone, and the fields' values over the links with what each
    // was dealt (a bandwidth; a proximity link's resistance a length).
    u64 seed_ = 0;
    MapSettings s_;
    Array<u8> lonely_;
    NearTable bandwidth_table_, resistance_table_;
    Array<double> resistance_dealt_;
    // Kept by prepare_respawns: the land cells in order, each one's density
    // and their running total; each place's spacing; setup's links by their
    // places (a bandwidth; a proximity link's resistance a length, + 1 in
    // resistance_dealt_); each field's values over the places; and each
    // place's cooling as dealt.
    bool ready_ = false;
    Array<u32> land_list_;
    Array<double> dens_, cum_;
    Array<double> radius_;
    HashMap<u32> setup_data_, setup_power_;
    NearTable field_[kFieldCount];
    Array<double> cooling_dealt_;
    // The map's state beyond setup: each place's incarnation, and the
    // bridges' anchors (setup's, then each crossing a respawn added).
    Array<u32> inc_;
    u32 respawned_ = 0, respawns_ = 0;
    Array<BridgeAnchor> anchors_;
    Array<u32> data_moved_, power_moved_;
};

} // namespace nova2
