#pragma once

// The world: its places, run in parallel on host threads (build-plan-gate1.md
// step 9). At gate 1 places share nothing, so a tick is every place's tick in
// any order, and the result is the same on any number of threads.
//
// From gate 3 the places sit on a map (map.h), worked out first. Each
// place's hardware and OS values are drawn as at gate 2, then, on a map with
// fields (map.fields), dealt out to the places by each value's field (7.3):
// the place with the k-th lowest field value takes the k-th lowest value
// drawn, so the world's mix of each value is gate 2's and only where they
// sit changes. A value that comes with another is dealt among the places
// that have the same other: the E cores' pattern among places with the same
// core count, in order of E cores; the memory latencies, a place's whole
// set, among places with the same memory device count, in order of their
// sum. So the mix of cores and their kinds, and of devices and their
// latencies, stays gate 2's too.
//
// The network (net.h) moves messages at the barrier, after every place has
// run its tick, in one fixed order whatever the thread count: first each
// place's SENDs and PINGs of the tick, in place order; then the network's
// step; then what it delivered, in its order. Messages to a place's port 0
// join its reset region's queue with their sender (10.4); those to a comms
// port go to a waiting RECV or the port's inbox (11.2); a PING's echo ends
// its wait. Then the neighbour SENSEs landing at this barrier are read
// (A.4), and each place's network device draws for the bytes it sent and
// received (11.4).
//
// From gate 4 (build-plan-gate4.md steps 4 and 5; appendix C) a tick starts
// with the sources' states for the tick (sources.h) and one solve for every
// place's voltage (solve.h), from what each place drew in its last tick;
// each place then runs at the voltage it was given. After the barrier comes
// heat's second half (heat.h): each place's temperature takes a step, up by
// what its devices passed it and what its links and its sources lost, down
// by what leaves to the outside and to cooler neighbours over land. Both are
// worked on one host thread in place order, so the thread count changes
// nothing. With supply.model = place there are no sources and no solve, and
// with heat.on off no heat: gate 3's world.
//
// From gate 5 (build-plan-gate5.md step 5) links can fail (links.h). A
// tick's inputs come first: a visit due at a place this tick mends its
// failed links, then the cuts due land. After the solve the links at their
// endurance fail and the rest take the tick's wear. A failed link is out
// of the solve, carries no heat between its places, and is failed for the
// network at the barrier, where a neighbour SENSE over it reads 0 as over
// a dark device's.
//
// And outside events arrive (events.h; build-plan-gate5.md step 7). The
// patches there at the tick are worked first among its inputs. A swell
// patch raises the voltage of the sources inside it and a brownout lowers
// their capacity, as the solve takes them; a storm's strikes join the solve
// as sources of one tick; an outage patch's cuts land with the tick's
// others; a heat wave raises the temperature its places cool toward.
//
// And a place can respawn (7.8; build-plan-gate5.md step 6). First among a
// tick's inputs, in place order: a place whose visit comes this tick, with
// visit.respawn_share of its devices failed or more, is torn down instead
// of repaired. What lives there ends; the map gives it a new point and
// works its links out again (map.h); a new place is built there, with the
// values the world dealt to the field values nearest its own (7.3) and
// draws of its own for everything else; and the network, the solve, the
// link table, the sources and heat follow the new map without being built
// again from nothing. The id keeps its visit schedule, its programs'
// numbering and its counters; its incarnation is one higher.
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/current.h"
#include "nova2/events.h"
#include "nova2/io.h"
#include "nova2/links.h"
#include "nova2/map.h"
#include "nova2/net.h"
#include "nova2/place.h"
#include "nova2/pool.h"
#include "nova2/solve.h"
#include "nova2/sources.h"
#include "nova2/types.h"

namespace nova2 {

class Observer; // observe.h: watches without changing anything (13.1)

// Every place's values as World::init has them at the start, before its
// visits and cooling devices (7.3, 9.6): each place's draws, dealt out by
// the fields when the map has them. A built map is enough, so `nova2
// places` lists them without building the places.
void draw_places(const WorldSettings& ws, const MapSettings& ms, const Map& map, Array<PlaceHw>* hw);

class World {
  public:
    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Reads the settings and builds every place. Bad settings fail through
    // cfg (cfg.ok()); a world the settings can't make fails with a reason.
    bool init(Config& cfg, TextBuf* why);
    // Moves a world just built, not yet run, to a tick: its sources are as
    // they stand then (sources.h: a function of the seed and the tick), its
    // straits too (links.h: the same), and every place is as it was built,
    // empty and at the outside's temperature. For gate 4's classing
    // (build-plan-gate4.md 11; nova2 worlds, classes), which asks what a
    // full place gets under the sources of a later tick. A run never calls
    // it, nor respawn_at.
    void begin_at(u64 tick);
    // The classing's too (build-plan-gate5.md 11, the classing worked again
    // with the respawns applied): before begin_at, place i torn down and
    // built again as a run's respawn at `tick` did it. A run's respawns
    // given in its order give its map, each new place's values and its
    // links: the map's draws are the seed's, the place's and its
    // incarnation's, from the points the places stand on (map.h).
    void respawn_at(u32 i, u64 tick);
    // Host threads, including the caller; 0 means every hardware thread.
    // Changes nothing in the world.
    void start_threads(u32 threads) { pool_.start(threads); }
    ThreadPool& pool() { return pool_; }

    void tick();
    // A hash of the whole world's state, place by place, then the network's
    // (principle 20).
    u64 hash();

    // The world section of a snapshot (snapshot.h), taken at the barrier:
    // the places, then the network. `version`: the format it is written in or
    // was saved in (the snapshot header's; this build's unless told).
    void save(ByteWriter& out, u32 version = kArchiveVersion);
    bool load(ByteReader& in, u32 version = kArchiveVersion);
    // The counters kept for reports, every place's and the network's, which
    // a snapshot leaves out (counters.cpp): saved beside one for a test carried
    // on, and loaded into a world that has loaded that snapshot and has its
    // observer watching.
    void save_counters(ByteWriter& out);
    bool load_counters(ByteReader& in);
    void grid_counters(Archive& ar);
    void event_counters(Archive& ar);
    void respawn_counters(Archive& ar);
    // One place's section instead (a place snapshot: snapshot.h), for
    // tracing a lineage where it lived. Loading it into a world built from
    // the same settings sets that place alone, and not the world's tick.
    void save_place(u32 i, ByteWriter& out);
    bool load_place(u32 i, ByteReader& in, u32 version = kArchiveVersion);

    u64 seed() const { return ws_.seed; }
    u64 tick_count() const { return tick_; }
    u32 place_count() const { return static_cast<u32>(places_.size()); }
    Place& place(u32 i) { return *places_[i]; }
    const WorldSettings& settings() const { return ws_; }
    const Map& map() const { return map_; }
    const MapSettings& map_settings() const { return ms_; }
    Network& net() { return net_; }
    // The time-stamp counter's cycles spent in the barrier's network step,
    // for reports (the benchmark's gate 3 part): never saved or hashed.
    u64 net_cycles = 0;

    // Gate 4: the sources on the map and the voltage solve, with
    // supply.model = sources (powered()).
    bool powered() const { return ss_.on; }
    Sources& sources() { return sources_; }
    Grid& grid() { return grid_; }
    const SolveSettings& solve_settings() const { return gs_; }
    // Gate 5: which links have failed, and how worn they are (links.h).
    Links& links() { return links_; }
    const LinkSettings& link_settings() const { return ls_; }
    // Gate 5's events (events.h): the patches there now, after a tick's
    // inputs; and the strikes that have landed, for reports.
    Events& events() { return events_; }
    u64 strikes = 0;
    // Whether a strike landed at place i in the tick being run, for the
    // observers' cascades: not state, set again by every tick.
    bool struck(u32 i) const { return i < struck_.size() && struck_[i]; }
    // The replication current (current.h; 12.6): whether it runs here (a
    // strength, on a map with positions), its heading and strength as of
    // the last tick run, and what it did, for reports.
    bool drifting() const { return current_.on() && net_.active(); }
    Current& current() { return current_; }
    CurrentCounts drift;
    void current_counters(Archive& ar);
    // The out-of-memory killer (9.1; build-plan-gate5.md step 10): whether
    // it acts here (os.killer above 0).
    bool killing() const { return ws_.os.killer != 0; }
    // Respawning (7.8): whether a visit can tear a place down here (visits
    // on, and the share at 1 or under); and place i torn down and built
    // again now, between ticks, whatever its devices' state (what a visit
    // does; tests call it by hand).
    bool respawning() const { return respawns_on_; }
    void respawn(u32 i);
    // The values setup dealt place i (7.3): what it had until its first
    // respawn. And the tick of that (~0: it never has), for whoever reads
    // its records by the map as it was set up.
    const PlaceHw& dealt(u32 i) const { return hw0_[i]; }
    u64 respawned_at(u32 i) const { return first_respawn_[i]; }
    // For reports (not state): respawns, those that found a new point, and
    // the programs alive in a place when it was torn down.
    u64 respawns = 0, respawns_moved = 0, torn_programs = 0;
    // Heat (heat.h): a place's own cooling rate, in microwatts a degree,
    // without its cooling device's; and the heat that left the world to the
    // outside in the last tick's step, in microwatts.
    bool heated() const { return ws_.heat.on; }
    u64 cooling_rate(u32 i) const { return cool_rate_[i]; }
    i64 heat_out_uw = 0;
    // The cycles spent in the solve (with the sources' states) and in the
    // places' heat step, for reports: never saved or hashed.
    u64 solve_cycles = 0, heat_cycles = 0;
    // The same for gate 5's inputs at the start of a tick (respawns, visits
    // mending links, cuts, straits and the patches) and the links' wear
    // (build-plan-gate5.md step 12).
    u64 input_cycles = 0, link_wear_cycles = 0;
    // A big fade shared out over the host threads (optimization-plan.md
    // pass 10): a place's tick stops before its fade's cells when it has
    // `defer` of them or more (~0: never), and they go out in chunks of
    // `chunk` cells (a multiple of 64; 4,096 and twice that unless a test
    // says, pass 11). Neither changes the world. Counted, never saved or
    // hashed: the fades shared out, their chunks and their cells.
    void set_fade_spread(u64 chunk, u64 defer);
    u64 spread_fades = 0, spread_chunks = 0, spread_cells = 0;

    // An observer, or none. Each tick's facts go to it at the barrier,
    // place by place, and it samples the world every so often. Watching
    // changes nothing: the hash is the same with or without (13.1).
    void watch(Observer* o);
    Observer* observer() const { return observer_; }

  private:
    static void tick_one(void* ctx, u32 k);
    void order_heaviest();
    static void chunk_one(void* ctx, u32 c);
    static void end_one(void* ctx, u32 k);
    void fade_spread();
    static void hash_one(void* ctx, u32 i);
    static void route_one(void* ctx, u32 task);
    void clear();
    void network_step();
    void current_step();
    void killer_step();
    u32 neighbour_reading(u32 from, u32 nb, u32 which) const;
    void power_step(u32 cap);
    void inputs();
    void heat_places();
    void build_heat();
    bool on_map();
    void respawn_hw(u32 i, PlaceHw* hw) const;

    WorldSettings ws_;
    MapSettings ms_;
    Map map_;
    NetSettings ns_;
    Network net_;
    Array<u8> dark_; // each place's network device off, at the barrier
    Array<Place*> places_;
    Array<u64> hashes_;
    SourceSettings ss_;
    SolveSettings gs_;
    Sources sources_;
    Grid grid_;
    LinkSettings ls_;
    Links links_;
    EventSettings es_;
    Events events_;
    CurrentSettings cs_;
    Current current_;
    // Heat between places: each place's own cooling rate, and its links
    // over land, by the place at the other end (where they start, then the
    // other place and the link's rate), all in microwatts a degree.
    Array<u64> cool_rate_;
    Array<u32> heat_begin_, heat_other_, heat_link_; // heat_link_: each one's proximity link
    Array<u64> heat_rate_;
    Array<i64> temp_next_;
    // Respawning: on or not; each place's values as setup dealt them; and
    // the tick of each place's first respawn.
    bool respawns_on_ = false;
    Array<PlaceHw> hw0_;
    Array<u64> first_respawn_;
    Array<u8> struck_;
    // The fades shared out (set_fade_spread): whether each place's tick
    // stopped before its fade's cells; the chunks of the places that did,
    // place by place and each one's jobs in order; those places, and where
    // each one's chunks start; and a list of hits for each chunk.
    struct FadeChunk {
        u32 place, job;
        u64 k0, k1;
        u8 shared, region;
    };
    u64 chunk_cells_ = 4096, defer_cells_ = 2 * 4096;
    Array<u8> deferred_;
    // The places' run handed out heaviest first: each place's tick last
    // time, by the time-stamp counter, and the places in that order. Only
    // the order the host threads take them in; nothing saved, and no
    // result depends on it (pool.h).
    Array<u64> place_cycles_;
    Array<u32> heaviest_;
    Array<FadeChunk> chunks_;
    Array<u32> spread_, spread_from_;
    Array<u32>* chunk_hits_ = nullptr;
    u32 hit_lists_ = 0;
    ThreadPool pool_;
    u64 tick_ = 0;
    Observer* observer_ = nullptr;
};

} // namespace nova2
