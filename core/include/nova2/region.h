#pragma once

// The reset region's idle runs (design.md 10.2, 10.3, appendix B;
// build-plan-gate1.md step 7).
//
// A core with no thread runs the reset region: an idle run, which is not a
// thread. It has no stack and no place in the scheduler, its calls are
// limited (9.2), and any ready thread takes the core from it.
//
// - Each core starts at its own spot, and each start moves that spot on by a
//   fixed step, so over time every byte is an entry point (R2). The
//   registers point at the spot on each start.
// - A run ends by a fault, by its cap (R3), by a ready thread taking the
//   core, or (gate 2) by a cut-out. Whatever it still holds is freed.
// - After a fault or the cap the core halts, drawing little (R1). It starts
//   again when anything writes to the region, or a slow retry comes round,
//   or an arrival waits for it.
// - Port 0's queue (10.4; build-plan-gate2.md 2.4): arrivals for the region,
//   bounded in bytes (R3). An arrival waits for a core starting a run, a
//   halted one or one whose run ended, and never pre-empts anything. The
//   core writes it at its start spot, cut short at the region's end, and runs
//   it from its first byte as a fresh idle run, a "prompt run", with the
//   registers at the landing spot. At gate 2 the only arrival is the boot
//   block, at a cold start (10.5); SENDs from other places join at gate 3.
// - Gate 3 (build-plan-gate3.md step 4): an idle run's SEND or neighbour
//   SENSE makes it wait on its core, as its DREAD does, until the OS's
//   net_until. A ready thread can still take the core, ending the run. Each
//   arrival's prompt run is counted when it ends: it made a program, or it
//   only sent itself on (a traveller, 10.4), or it did neither.
#include "nova2/array.h"
#include "nova2/cpu.h"
#include "nova2/memory.h"
#include "nova2/os.h"
#include "nova2/sched.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h: saving and loading state

struct RegionSettings {
    u32 step = 13;            // bytes each start moves a core's spot (R2); coprime with the region's size
    u32 cap_cycles = 100000;  // an idle run's longest, above the dearest birth (R3, appendix E)
    u32 retry_ticks = 1;      // a halted core tries again this often (R1; 1 at gate 1, where nothing draws power)
    u32 queue_bytes = 4096;   // port 0's queue (10.4, R3)
    // The most of the cores arrivals from other places may hold at once,
    // rounded down but at least 1, so static keeps the rest (10.4; from
    // 2026-11-17, Damian, after build-plan-gate3.md step 8). 1: no limit.
    double arrival_share = 0.5;
};

// An arrival waiting in port 0's queue: its bytes, what a program born from
// its run has for its cause and parent (13.1), and whether it's a boot block.
struct Arrival {
    u32 at, len; // its bytes, in the queue's
    BirthCause cause;
    u64 sender;
    bool boot;
};

// The bytes an arrival landed in the reset region, from a sender (13.2):
// where (from the region's base), how many, whose, and their hash as they
// landed. A static run that starts in them while they're all as they
// landed takes the sender as its parent; a later landing over any of them
// drops them. A block the current landed (12.6) is listed the same way,
// its sender the program that held the bytes where they were copied, with
// the mark that the current carried them (os.h's kCarried; the mark alone
// if no program held them).
struct Landing {
    u32 at, len;
    u64 sender;
    u64 hash;
};
constexpr u32 kLandings = 128; // the most a region lists; the oldest goes first

enum class RunPhase : u8 { none, running, halted };

struct IdleRun {
    RunPhase phase = RunPhase::none;
    u32 spot = 0;          // the next start spot, from the region's base
    i64 used16 = 0;        // this run's cycles so far, in sixteenths
    i64 debt16 = 0;        // an overrun, repaid from its next quantum
    u64 halted_at = 0;     // the tick it halted
    u32 seen_writes = 0;   // the region's write count when it halted
    u32 started_at = 0;    // where this run started, from the region's base
};

class Region {
  public:
    bool init(Os* os, Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const RegionSettings& s,
              const CostTable* p_costs, const CostTable* e_costs, TextBuf* why);

    // The scheduler's hooks for idle cores.
    IdleHooks hooks();
    // Runs (or starts, or keeps halted) the idle run on a core, within the
    // stock time left in its quantum; returns the time it left unused, the
    // core halted or waiting on a DREAD (0 if it ran to the end).
    i64 run(u32 core, i64 left16);
    // A thread takes the core: the run ends, and its holdings go.
    void end(u32 core);
    // The core cut out (appendix B): a run on it ends, its holdings going, and
    // a halted core forgets it was halted. Power coming back starts a run
    // afresh, as a real core starts from its reset vector.
    void cut(u32 core);
    // A cold start (10.5): every run ends, its holdings going, every core
    // starts afresh, and port 0's queue, the old OS's, is emptied.
    void reboot();
    // The memory device holding the region failed (7.6, 10.2): every run
    // ends, its holdings going, and no core starts one until the device is
    // replaced. Port 0's queue stays: its arrivals wait.
    void lose();
    // Queues an arrival for port 0, at its tail or (a boot block) its head.
    // False, and dropped, if the queue hasn't room for it (R3), or it's empty.
    bool arrive(const u8* bytes, u32 len, BirthCause cause, u64 sender, bool boot, bool at_head);
    // A block of the replication current's lands (12.6): its bytes are
    // written into the region at the spot `fraction` of 2^32 of the way
    // through it, cut short at the region's end. No queue and no prompt
    // run; it counts as a write to the region, so halted cores start
    // again. `holder`: the program that held the bytes where they were
    // copied (0: none), for the observers. Returns the bytes written.
    u32 drift(const u8* bytes, u32 len, u32 fraction, u64 holder);
    u32 queued() const { return static_cast<u32>(queue_.size()); } // boot blocks included
    u32 queued_bytes() const { return static_cast<u32>(queue_bytes_.size()); }
    const Arrival& waiting(u32 i) const { return queue_[i]; }
    const u8* waiting_bytes(u32 i) const { return queue_bytes_.data() + queue_[i].at; }
    // The arrivals' landed bytes it lists, oldest first (13.2).
    u32 landing_count() const { return static_cast<u32>(landings_.size()); }
    const Landing& landing(u32 i) const { return landings_[i]; }

    const IdleRun& state(u32 core) const { return runs_[core]; }
    // The invariant check (build-plan-gate2.md step 7): port 0's queue holds
    // its arrivals end to end within its bound, and each core runs an idle
    // run exactly when its run says so. False, with the first one broken.
    bool check(TextBuf* why) const;
    const RegionSettings& settings() const { return s_; }
    void serialize(Archive& ar);
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);

    // Counters (not part of the world's state).
    u64 started = 0, faulted = 0, capped = 0, preempted = 0, instructions = 0;
    u64 cut_runs = 0; // runs ended by their core cutting out
    // Prompt runs (10.4), and those that ran a boot block; arrivals dropped.
    u64 prompt_runs = 0, boot_runs = 0, dropped = 0;
    u64 arrival_runs = 0, arrival_made = 0, travellers = 0, arrival_neither = 0;
    // Arrivals from other places alone (build-plan-gate3.md step 8): those
    // that joined port 0's queue, those dropped for want of room in it, and
    // those still in it when a cold start cleared it. Every one that joined
    // has run (arrival_runs), been cleared, or is waiting.
    u64 arrival_joined = 0, arrival_dropped = 0, arrival_cleared = 0;
    u32 arrivals_waiting() const;
    // The cycles (in 16ths) the runs that ended used, static runs' and
    // arrivals' prompt runs', and the static runs that SENT (asked after
    // build-plan-gate3.md step 8's second pilot).
    u64 static_cycles16 = 0, arrival_cycles16 = 0, static_sent = 0;
    // Of those that did neither (build-plan-gate3.md step 7): the ones a
    // ready thread took the core from (10.2), and of the rest, the ones whose
    // landed bytes had changed by the time they ended (another run, a thread
    // or a flip wrote over them). A run under way when a snapshot is loaded
    // counts as changed: the landed bytes' hashes aren't saved.
    u64 arrival_preempted = 0, arrival_overwritten = 0;
    // Of the rest (they kept their core), the first of their calls that
    // failed (asked after step 7, to find what ends them; not saved
    // either): an ALLOC, by why (AllocMiss: the device it asked for had no
    // room while another had, none had, or the request was refused as
    // asked); a FREE; a PROTECT; a SPAWN, by why (SpawnMiss); or none. And
    // how they ended: a fault, the cap or their core cutting out (the rest
    // of them by a cold start).
    u64 arrival_alloc_elsewhere = 0, arrival_alloc_nowhere = 0, arrival_alloc_refused = 0;
    u64 arrival_free_failed = 0, arrival_protect_failed = 0;
    u64 arrival_spawn_not_held = 0, arrival_spawn_data = 0, arrival_spawn_no_record = 0;
    u64 arrival_no_failure = 0;
    u64 arrival_faulted = 0, arrival_capped = 0, arrival_cut = 0;
    // Static runs that started in an arrival's landed bytes, all as they
    // landed, and so took its sender as their parent (13.2), and those that
    // made a program.
    u64 landed_runs = 0, landed_made = 0;
    // The same for blocks the current landed (12.6), counted apart; and the
    // blocks and bytes it landed here.
    u64 carried_runs = 0, carried_made = 0;
    u64 drift_blocks = 0, drift_bytes = 0;
    u32 landed = 0;   // the bytes the last prompt run landed (cut short at the region's end)

    // Distinct paths tried (R2, 10.6): an observer's measure, kept apart from
    // the state. With tracking on, each idle run hashes the bytes it executes,
    // and its hash is counted once if a bounded table hasn't seen it. When the
    // table fills it starts afresh, so a path long forgotten counts again.
    void track_paths(bool on, u32 table = 65536);
    u64 paths_new = 0, paths_ended = 0;
    // A path that ends isn't looked up in the table there and then: it
    // waits, with the table's line asked for (a prefetch), and the waiting
    // ones are looked up together, in the order they ended, so the counts
    // are what they would have been. The table is far bigger than a
    // processor's nearest cache, and a lookup that waits for its line stalls
    // the place (optimization-plan.md 4.5). flush_paths looks up those
    // waiting: a place does at its tick's end, so paths_new is whole between
    // ticks.
    void flush_paths();
    // CPYs that moved bytes in idle runs, and those into an allocation the
    // run holds: a copy on its way to a birth (the copying ladder).
    u64 run_cpy = 0, run_cpy_own = 0;

  private:
    enum class StopWhy : u8 { other = 0, fault, cap, preempt, cut }; // why a run stops, for the counts
    void start(u32 core);
    void stop(u32 core, RunPhase then, StopWhy why);
    void count_rest(const Core& c, StopWhy why);
    void pop_arrival();
    // A prompt run landed n bytes at a spot: the landings it covers any of
    // go, and an arrival's from a sender is listed.
    void note_landing(u32 spot, u32 n, u64 sender);
    // Whether a core starting now takes port 0's head (10.4): a boot block
    // always, an arrival only while fewer cores than its share hold one.
    bool takes_head() const;
    // The sender of the landing a static run at a spot starts in, if its
    // bytes are all as they landed (0 if not, and a changed one goes).
    u64 landed_sender(u32 spot);

    Os* os_ = nullptr;
    Memory* mem_ = nullptr;
    Core* cores_ = nullptr;
    u32 core_count_ = 0;
    u32 arrival_cores_ = 1; // the most cores arrivals may hold (arrival_share's)
    PlaceEnv* env_ = nullptr;
    RegionSettings s_;
    const CostTable* costs_[2] = {nullptr, nullptr};
    Array<IdleRun> runs_;
    Array<Arrival> queue_;  // port 0's, head first
    Array<u8> queue_bytes_; // their bytes, in the same order
    Array<Landing> landings_; // the arrivals' landed bytes, oldest first, none overlapping
    bool track_ = false;
    Array<u64> path_acc_; // per core: the running hash of its run's path
    Array<u64> landed_hash_; // per core: its arrival's bytes as they landed (for the counts)
    Array<u32> landed_len_;
    Array<u64> path_set_; // the bounded table (0 is empty)
    u32 path_used_ = 0;
    static constexpr u32 kPathsWaiting = 16;
    u64 path_wait_[kPathsWaiting] = {}; // the ended paths not yet looked up, oldest first
    u32 path_waiting_ = 0;
    void end_path(u32 core);
};

} // namespace nova2
