#pragma once

// A place: one machine of the world, with its hardware, its memory and disk,
// its OS, scheduler, idle runs and noise (build-plan-gate1.md step 9).
//
// At gate 1 a place's hardware and OS values are drawn from the settings'
// ranges, with no map fields yet (build-plan-gate1.md 3), and its memory and
// disk start as static (10.1). Places share nothing, so they run in parallel.
//
// A tick, gate 1's part of appendix C:
//   execution in rounds of rotating quanta (the scheduler and idle runs),
//   the barrier (disk I/O),
//   the cosmic-ray floor,
//   facts handed over.
// Gate 2 adds the power fields (power.h): each core's stock speed follows
// the voltage, and each tick ends by working out what the place drew. And
// it adds the supply (supply.h): each tick starts with the source's capacity
// for the tick and the place's voltage from what it drew in the last one
// (appendix C 1-3), then each device's state at that voltage: cut-outs and
// restarts, and each core's speed for the tick (appendix C 4).
// Gate 4 (build-plan-gate4.md steps 4 to 7): with the sources on the map
// (supply.model = sources) the place has no source of its own, and the
// world's solve (solve.h) hands it each tick's voltage. With heat on
// (heat.h) each device has a temperature, which its draw raises and its
// heatsink lowers toward the place's; a device at its heat limit trips, and
// is off until it has cooled; hot devices draw more, and hot memory fades
// faster. A place may have a cooling device (7.7). The place's own
// temperature is moved by the world, after the barrier, from what its
// devices passed it and what its links and sources lost.
// Gate 5 (build-plan-gate5.md step 2; wear.h): every device wears, by its
// temperature and by the voltage over its maximum, and its threshold rises
// with its wear; a worn memory device's leakiest cells lose bits; and a
// device at the end of its life fails, and stays off until a visit replaces
// it (7.6). A tick ends with the wear it added (appendix C 8), and the next
// tick's device states end with the failures (appendix C 4), the cores in
// order, then the memory devices, the disk, the network device and the
// cooling device. With wear.rate 0 nothing wears and the place is gate 4's.
#include "nova2/archive.h"
#include "nova2/config.h"
#include "nova2/cpu.h"
#include "nova2/heat.h"
#include "nova2/memory.h"
#include "nova2/noise.h"
#include "nova2/os.h"
#include "nova2/power.h"
#include "nova2/region.h"
#include "nova2/sched.h"
#include "nova2/supply.h"
#include "nova2/types.h"
#include "nova2/visit.h"
#include "nova2/wear.h"

namespace nova2 {

inline constexpr u32 kMaxCores = 64;
inline constexpr u32 kMaxPageSizes = 8;

// Everything the world's settings say, read once (design.md 2: fixed for the run).
struct WorldSettings {
    u64 seed = 1;
    u32 places = 48;
    // Hardware
    u32 cores_min = 2, cores_max = 8;
    double e_share = 0.5;
    PowerSettings power;   // each kind's range, speed slope and draw (7.1)
    SupplySettings supply; // the source each place has (4.2, 4.3)
    bool sources = true;   // supply.model = sources: no place has a source of its own (gate 4)
    HeatSettings heat;     // heat.h
    WearSettings wear;     // wear.h
    VisitSettings visit;   // visit.h
    HammerSettings hammer; // noise.h: rowhammer (9.8)
    FlakySettings flaky;   // noise.h: flaky parts (7.5)
    i32 temperature = 250;
    // Memory and disk
    u32 memory_size = 163840, mem_devices_min = 2, mem_devices_max = 4;
    u32 latency_min = 4, latency_max = 12;
    i32 threshold_fast = 700, threshold_slow = 600;
    u32 class_steps = 2, max_alloc = 4096;
    u32 disk_size = 1048576;
    u32 region_size = 1024;
    // OS values
    u32 page_sizes[kMaxPageSizes] = {64, 256, 1024};
    u32 page_size_count = 3;
    double strict_share = 0.5;
    OsSettings os;
    SchedSettings sched;
    RegionSettings region;
    NoiseSettings noise;
    CostTable p_costs = default_p_costs(), e_costs = default_e_costs();
};

// Reads the settings through the registry. Bad values and broken
// constraints are reported through cfg.fail (cfg.ok() then says false).
void read_world_settings(Config& cfg, WorldSettings* ws);

// A place's hardware and OS values (7.3, 9.6): drawn for each place from the
// ranges, then, on a map with fields, dealt out to the places by the fields
// (world.cpp).
struct PlaceHw {
    u32 cores = 0;
    u8 e[kMaxCores] = {}; // core c is an E core
    u32 devices = 0;      // memory devices
    u32 latency[kMaxMemDevices] = {};
    u32 page_size = 0;
    bool strict = false;
    bool cooler = false; // it has a cooling device (7.7; the world deals them by the coolers' field)
    double visits = 1;   // its mean visit interval over the world's (7.8's density tie; the world works it out)
};

// Place `id`'s draws, as gates 1 and 2 make them: the cores' count and kinds,
// the memory devices' count and latencies (kPurposeHardware), then the page
// size and strict or lax (kPurposeOs).
void draw_hardware(const WorldSettings& ws, u32 id, PlaceHw* hw);
// ... from a seed of its own (a respawned place's, with no fields: 7.8).
void draw_hardware(const WorldSettings& ws, u32 id, PlaceHw* hw, u64 seed);
// The seed a place's own draws take: the world's, or for a place built again
// at a respawn (7.8), one made from the world's, its id and its incarnation,
// so its static, its devices' lives and its noise are all new.
u64 place_seed(u64 seed, u32 id, u32 incarnation);

// The failed bytes' blocks, for Place::end_failed_marked
// (optimization-plan.md pass 12): a bit for each block of
// 2^kFailedBlockShift bytes, set for each failed byte's block and for the
// block a saved state's length less one before it. A saved state (24
// bytes) or a handler's frame (20) holding the byte starts between the
// two, so in one of those two blocks, whether it lies within a block or
// across two. A state starting in a clear block holds no failed byte.
//   failed_mark_words: the words for a memory of `bytes`, with a block past
//     its end that is never marked, where kNowhere looks.
//   failed_marks: sets the marks for addrs (each in the memory); unmark
//     clears their words again.
//   failed_marked: whether a state starting at `at` starts in a marked
//     block; `past` is the block past memory's end (failed_block_past).
inline constexpr u32 kFailedBlockShift = 6;
static_assert(kHandlerFrameBytes <= kSavedRegsBytes && kSavedRegsBytes <= (1u << kFailedBlockShift),
              "a state starting in a block ends in it or the next");
static_assert(kFailedBlockShift == Os::kStackBlockShift, "the marks' blocks are the OS's lists' (pass 13)");
inline u32 failed_block_past(u32 bytes) { return (bytes + (1u << kFailedBlockShift) - 1) >> kFailedBlockShift; }
inline u32 failed_mark_words(u32 bytes) { return (failed_block_past(bytes) >> 6) + 1; }
void failed_marks(const u32* addrs, u32 count, u64* bits);
void failed_unmark(const u32* addrs, u32 count, u64* bits);
inline bool failed_marked(const u64* bits, u32 past, u32 at) {
    const u32 k = at >> kFailedBlockShift < past ? at >> kFailedBlockShift : past;
    return (bits[k >> 6] >> (k & 63)) & 1;
}

class Place {
  public:
    Place() = default;
    Place(const Place&) = delete;
    Place& operator=(const Place&) = delete;

    // Lays the place out with the hardware and OS values given, and fills
    // its memory and disk with static. False, with the reason, if the
    // settings can't make it.
    // `incarnation`: 0 the place the world began with; one more each time
    // it is torn down and built again (7.8).
    bool init(u32 id, const WorldSettings& ws, const PlaceHw& hw, TextBuf* why, u32 incarnation = 0);
    // ... with its own draws (draw_hardware), as a world with no fields.
    bool init(u32 id, const WorldSettings& ws, TextBuf* why);
    void tick();
    // The world's tick of it, which may stop before a big fade's cells for
    // the world to share them out over the host threads (optimization-plan.md
    // pass 10). tick_begin runs the tick, but with defer_at cells or more to
    // fail it stops before them and says so: they are noise.fade_jobs(), for
    // fade_chunk to fail in chunks (Noise::fade_cells), and tick_end, given
    // each chunk's hits in order and whether any changed a byte of the reset
    // region, finishes it. Either way it is tick(), the same to the bit.
    bool tick_begin(u64 defer_at);
    bool fade_chunk(const FadeJob& job, u64 k0, u64 k1, Array<u32>* hits, bool shared) const {
        return noise.fade_cells(job, k0, k1, saved_.data(), static_cast<u32>(saved_.size()), saved_blocks_.data(),
                                hits, shared);
    }
    void tick_end(bool in_region, const Array<u32>* hits, u32 chunks);

    // Saves or loads its state (archive.h).
    void serialize(Archive& ar);
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);
    // A hash of its whole state (the world's hash combines these).
    u64 hash();

    u32 id() const { return env.place_id; }
    u32 core_count() const { return core_count_; }

    Memory mem;
    Core cores[kMaxCores];
    PlaceEnv env;
    Os os;
    Scheduler sched;
    Region region;
    Noise noise;

    // Counters for reports (not part of its state).
    u64 births = 0, deaths = 0;

    // Its source (supply.h), and its capacity this tick (a share in 2^-20ths;
    // the schedule gives it from the tick, so it isn't state).
    Supply supply;
    u32 capacity = kCapacityOne;

    // What it drew over its last tick (4.5, appendix C), by device, in
    // microwatts. Part of its state: the next tick's voltage comes from it.
    PlaceDraw draw;
    u64 core_draw[kMaxCores] = {};
    u64 activations = 0;       // memory's row activations in its last tick
    u64 activations_total = 0; // and over all its ticks
    // Its draw summed over its ticks, by device (energy, in microwatt-ticks),
    // and the most it drew in any one tick.
    PlaceDraw energy;
    u64 peak_draw = 0;
    // Its voltage over its ticks, for reports: summed, the lowest, and the
    // ticks it spent below the P cores', the E cores' and the reset region's
    // memory's thresholds.
    u64 mv_total = 0;
    i32 mv_lowest = 0;
    u64 below_p = 0, below_e = 0, below_region = 0;
    const PowerSettings& power() const { return power_; }

    // The devices besides the cores (theirs are in Core): on, or cut out
    // since a tick (4.4). Part of its state.
    DeviceState memory_state[kMaxMemDevices];
    DeviceState disk_state, network_state;
    // Bytes its network device sent or received since its last tick's draw
    // (11.4): the OS's SENDs add theirs, the barrier what's delivered.
    u64 net_bytes = 0;
    u64 network_cutouts = 0; // for reports
    // What its cut-outs did, for reports: core cut-outs and core-ticks spent
    // cut out (P, then E), threads ended at a cut-out and threads paused
    // there (os.cutout_pause), and the disk's cut-outs.
    u64 cutouts[2] = {}, off_ticks[2] = {};
    u64 cut_threads = 0, paused_threads = 0, disk_cutouts = 0;
    // And fading's, for reports: memory devices' cut-outs and the
    // device-ticks they spent cut out, and the threads ended because a bit
    // of their saved state faded (9.4); and with os.saved_state_fails, those
    // ended because a bit of it failed by a cosmic-ray flip or an edge error.
    u64 memory_cutouts = 0, memory_off_ticks = 0, faded_threads = 0, failed_threads = 0;
    // The ground measure's counters (observe.h; build-plan-gate4.md step 1),
    // beside off_ticks, the core-ticks spent cut out: the core-ticks its
    // cores spent off by a heat trip (P, then E; nothing trips before gate
    // 4's heat, step 5), and the ticks the memory device holding its reset
    // region spent cut out.
    u64 heat_ticks[2] = {};
    u64 region_off_ticks = 0;
    // And the core time its cores spent running something, a thread or an
    // idle run, in 2^-20ths of a core-tick: the scheduler's measure of each
    // core's tick for its draw (sched.h: busy16), summed over its cores and
    // ticks. A core cut out or tripped works none. With off_ticks and
    // heat_ticks it can't pass cores x ticks. For the ground record
    // (build-plan-gate4.md 11, the reviewer's measures).
    u64 worked20 = 0;

    // Gate 4's supply: the world's solve gives the voltage (solve.h). The
    // world sets given_mv before each tick.
    bool supplied = false;
    i32 given_mv = 0;

    // Heat (heat.h). Temperatures in millionths of a degree C: the place's,
    // and each device's own. Part of its state when heat is on.
    HeatLaw heat;
    i64 temp = 0;
    i64 core_temp[kMaxCores] = {};
    i64 memory_temp[kMaxMemDevices] = {};
    i64 disk_temp = 0, network_temp = 0, cooler_temp = 0;
    // Off by a heat trip, until it has cooled to its cool-down point (4.4).
    bool core_tripped[kMaxCores] = {};
    bool memory_tripped[kMaxMemDevices] = {};
    bool disk_tripped = false, network_tripped = false, cooler_tripped = false;
    // What its devices passed it over its last tick, in microwatts: their
    // draw less what their own warming kept. The world's heat step adds its
    // links' and sources' losses and moves `temp`.
    i64 passed_uw = 0;
    // Its cooling device (7.7), if it has one: its state, and how hard it
    // worked over its last tick, in 2^-20ths of its full rate.
    bool has_cooler = false;
    DeviceState cooler_state;
    u32 cooler_work = 0;
    // Heat's counters, for reports: trips by kind of device (HeatKind), the
    // device-ticks spent tripped by kind, the threads a trip ended (cause
    // "heat"), the cooling device's cut-outs, the ticks the reset region's
    // memory device spent tripped, and the place's temperature summed over
    // its ticks (in thousandths of a degree) with its highest.
    u64 trips[kHeatKinds] = {}, tripped_ticks[kHeatKinds] = {};
    u64 heat_threads = 0, cooler_cutouts = 0, region_heat_ticks = 0;
    i64 temp_total = 0;
    i64 temp_highest = 0;
    // The world's heat step calls it with the place's new temperature.
    void set_temp(i64 t);

    // Wear (wear.h; 7.4, 7.6): each device's wear and its life as drawn, and
    // whether it has failed. Part of its state while wear is on.
    WearLaw wear;
    Worn core_worn[kMaxCores];
    Worn memory_worn[kMaxMemDevices];
    Worn disk_worn, network_worn, cooler_worn;
    // A device's worn threshold, in mV: its nominal one and how far its wear
    // has raised it (SENSE reads the nominal one, R25).
    i32 core_threshold(u32 c) const {
        return (cores[c].kind == DeviceKind::e_core ? power_.e.threshold : power_.p.threshold) + cores[c].worn_mv;
    }
    i32 memory_threshold(u32 d) const { return devices_[d].threshold + memory_rise_[d]; }
    i32 disk_threshold() const { return power_.disk_threshold + disk_rise_; }
    i32 network_threshold() const { return power_.network_threshold + network_rise_; }
    i32 cooler_threshold() const { return heat.cooler_threshold + cooler_rise_; }
    // A device fails now, whatever its wear (7.6): what the end of its life
    // does, and what a test does by hand. It is off until it is replaced.
    // - A core: the thread on it ends as at a cut-out, cause "failed
    //   device", and the scheduler has one fewer.
    // - A memory device: the threads whose stacks are on it end, with that
    //   cause; everything allocated on it is freed and its pages fault; if
    //   it held the reset region, every idle run ends and none starts, and
    //   port 0's arrivals wait.
    // - The network device: the place can't send, receive or forward.
    // - The cooling device: it cools nothing.
    // - The disk: its data is lost and its calls fail soft (Os::fail_disk);
    //   boot_failures counts those failed by a worn boot block (7.4).
    void fail_core(u32 c);
    void fail_memory(u32 d);
    void fail_disk();
    void fail_network();
    void fail_cooler();
    // Wear's counters, for reports: failures by kind of device (HeatKind),
    // the device-ticks spent failed by kind, and the threads a failure ended.
    u64 failures[kHeatKinds] = {}, failed_ticks[kHeatKinds] = {};
    u64 failure_threads = 0;
    u64 boot_failures = 0; // disks failed by a worn boot block (7.4, 10.5), among failures[kHeatDisk]

    // Maintenance visits (visit.h; 7.8): how many it has had, the tick of
    // the last (0: none yet) and of the next (kNoVisit: never). Part of its
    // state while the world has visits. Its tick makes the visit when the
    // next one's tick comes, after the supply and before the device states.
    u64 visits = 0, last_visit = 0, next_visit = kNoVisit;
    u64 visit_mean() const { return visit_mean_; } // its own mean gap, in ticks (0: never)
    // What a visit does to one device, whatever its state (tests call them
    // by hand): a failed one is replaced fresh, any other like for like.
    void visit();
    void replace_core(u32 c);
    void replace_memory(u32 d);
    void replace_disk();
    void replace_network();
    void replace_cooler();
    // Respawning (7.8; World::respawn): which place of its id this is; how
    // many devices it has, every kind alike, and how many of them have
    // failed (the share a visit looks at).
    u32 incarnation = 0;
    u32 device_count() const;
    u32 failed_devices() const;
    // Torn down: everything living here ends, each program with the cause
    // "failed device", and port 0's queue is emptied.
    void teardown();
    // Built in a torn-down place's stead, it takes what goes with the id:
    // the tick, the visit schedule (this visit counted), the numbering of
    // its programs, and the tick's facts for the observers.
    void take_over(Place& old, u64 tick);

    // Visits' counters, for reports: devices replaced after failing and
    // devices replaced like for like, by kind (HeatKind), and the reset
    // region's device replaced after failing (a hot swap, 10.5).
    u64 replaced[kHeatKinds] = {}, renewed[kHeatKinds] = {};
    u64 region_swaps = 0;

    // The low-voltage interrupt's counters (9.5; build-plan-gate4.md step 7;
    // the scheduler's lv_* count the interrupts raised and taken): threads
    // that went to sleep within a tick of taking one, and of those, the
    // ones that woke again after a core of the place had cut out meanwhile.
    u64 lv_slept = 0, lv_outlived = 0;

    // The widened saved-state rule (os.saved_state_fails; build-plan-gate2.md
    // step 10): each thread whose saved state holds a byte at one of addrs
    // ends, cause "memory", in record order. Its saved state is what fading
    // looks at (9.4): its saved registers while it's off its core, and the
    // frame of a handler it's in, on or off. The memory step calls it with
    // the tick's failed bits; tests call it on its own.
    void end_failed_states(const u32* addrs, u32 count);
    void end_failed_among(const u32* addrs, u32 count, u32 from, u32 to); // the same, threads from..to one by one
    // With many threads and more than a few bytes failed, it first marks the
    // bytes' blocks (below) and holds a thread's states against the bytes
    // only when one starts in a marked block. False, with nothing done, if a
    // byte lies past memory.
    bool end_failed_marked(const u32* addrs, u32 count);
    // The bytes that failed in the last tick's memory step, for tests:
    // empty unless os.saved_state_fails is on.
    const Array<u32>& failed_bytes() const { return failed_; }
    // Its cold starts, for reports (10.5), and those whose boot block a
    // program wrote.
    u64 cold_starts = 0, cold_written = 0;

    // A cold start (10.5, build-plan-gate2.md 2.3): power came back to the
    // memory device holding the reset region after the voltage fell below
    // its threshold. Device states call it then; tests call it on its own.
    // The OS reboots: every program and thread ends (cause "cold start"),
    // and every idle run, their memory freed with its bytes as fading left
    // them; port 0's queue is emptied. Then the boot block goes to its head.
    // A block somebody wrote boots with the cause "boot" and its last writer
    // for parent; one nobody wrote is the disk's static, and boots as static.
    void cold_start();

    // The invariant check (build-plan-gate2.md step 7; the fuzz world runs it
    // after every tick, in the checked build): memory's, the OS's and the
    // idle runs' own checks, then across them: every allocation's holder is
    // a live program or a running idle run; a device that's cut out says so
    // in its speed; one that's on holds no fade; nothing was cut out in the
    // future. False, with the first one broken in why.
    bool check(TextBuf* why) const;

    // Timing a tick's parts, for the benchmark only (build-plan-gate2.md
    // step 7): with `timing` on, each tick adds the time-stamp counter's
    // cycles spent in each part to phase_cycles. Off by default, and not
    // part of the state: it changes no result.
    enum Phase : u32 {
        kPhaseSupply = 0, // the capacity and the voltage (appendix C 1-3)
        kPhaseDevices,    // cut-outs, restarts, speeds, edge rates, a cold start (4)
        kPhaseRun,        // execution: the scheduler's rounds and idle runs (5-6)
        kPhaseBarrier,    // disk I/O (7)
        kPhaseFade,       // memory fading and its edge errors (8)
        kPhaseCosmic,     // the cosmic-ray floor, and threads on failed devices ended (8)
        kPhaseDraw,       // what it drew (9)
        kPhaseWeak,       // weak cells' lost bits (8; build-plan-gate5.md step 2)
        kPhaseHammer,     // rowhammer's flips (8; step 8)
        kPhaseWear,       // the wear the tick added (8; step 2)
        kPhases
    };
    bool timing = false;
    u64 phase_cycles[kPhases] = {};

  private:
    void timed_tick();
    void tick_start();  // the tick up to memory's step
    void tick_finish(); // and after the fade
    void fade_tick();
    bool fade_begin(u64 defer_at);
    void fade_end();
    void supply_tick();
    void device_states();
    void cut_core(u32 c, DeathCause cause);
    void low_voltage_watch();
    void account_draw();
    void wear_tick();     // the tick's wear, and what follows from it: each rise, each device's weak cells
    void wear_failures(); // the devices at the end of their lives fail
    void tell_worn();     // each device's rise and failed state, from its wear, to whoever reads them
    void serialize_heat(Archive& ar); // gate 4's part of its state
    void serialize_wear(Archive& ar); // gate 5's
    void add_wear(Worn* w, i64 t, i32 maximum);
    // What it draws at mv with every core working all tick at stock clock
    // and every device that's on, in microwatts: its full draw (supply.h).
    u64 full_draw_uw(i32 mv) const;

    u64 seed_ = 0;       // its own draws' (place_seed)
    u64 world_seed_ = 0; // the world's: its visit schedule goes with its id
    u32 core_count_ = 0;
    u32 mem_devices_ = 0;
    MemDeviceSpec devices_[kMaxMemDevices] = {};
    PowerSettings power_;
    // Scratch for the saved-state rule, kept to save allocating in a tick:
    // the threads' saved states, the blocks of memory they reach (noise.h),
    // and those a fade reached; and the bytes a bit of which failed by an
    // edge error or a flip, with the rule on.
    Array<SavedState> saved_;
    Array<u64> saved_blocks_;
    Array<u32> hits_;
    Array<u32> failed_;
    Array<u32>* failing() { return os.settings().saved_state_fails ? &failed_ : nullptr; }
    u32 fade_factor_[kMaxMemDevices] = {}; // scratch: each memory device's fading at its temperature
    // How far wear has raised each threshold, in mV: worked from the wear
    // (the cores' is in Core::worn_mv).
    i32 memory_rise_[kMaxMemDevices] = {};
    i32 disk_rise_ = 0, network_rise_ = 0, cooler_rise_ = 0;
    // Visits' (visit.cpp): the settings, its own mean gap, the replacement
    // threshold in 2^-32ths (0: failed devices only), and the number of the
    // disk whose static the disk space began as (0: the place's first).
    VisitSettings visit_;
    u64 visit_mean_ = 0, replace_q32_ = 0;
    u32 disk_static_ = 0;
    void init_visits(const VisitSettings& s, double scale);
    bool due(const Worn& w) const;
    bool renew(Worn* w, u32 slot, u32 kind);
    void note_disk_static();
    void serialize_visits(Archive& ar);
    // The threads that took a low-voltage interrupt, watched for the
    // counters above. Not state: it is saved with the counters, and a world
    // opened from a snapshot alone starts with none watched.
    struct LvWatch {
        u32 thread;
        u32 id;
        u64 program;
        u64 tick;    // when it took the interrupt
        u64 cutouts; // the place's core cut-outs then
        bool asleep; // counted as having slept
    };
    Array<LvWatch> lv_watch_;
    // end_failed_marked's marks (failed_marks): clear between its calls.
    // Last, moving no other member: with the scratch above, the world with
    // fading made negligible measured a little slower (pass 12).
    Array<u64> failed_blocks_;
    // And a bit a thread record for the threads it holds against the marks,
    // likewise clear between calls (pass 13).
    Array<u64> failed_seen_;

  public:
    // end_failed_marked reads the OS's lists by block with more than this
    // many threads for each list the failed bytes may read, else walks every
    // thread (pass 13): the same threads end either way. 12 measured best
    // on open-5 (of 4, 12 and 32): a list read is a random one, the walk
    // reads in order. Not saved; a test sets 0 to take the lists always.
    u32 lists_pay = 12;
};

} // namespace nova2
