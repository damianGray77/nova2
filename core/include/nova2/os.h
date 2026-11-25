#pragma once

// A place's OS (design.md 9.1-9.5, appendix A.3-A.4, appendix B;
// build-plan-gate1.md step 5): programs, threads and their stacks, and the
// 16 calls with appendix A's argument rules.
//
// The OS is neutral infrastructure: it acts only on requests and never names
// a program. Its records live outside simulated memory (R16), except each
// thread's saved registers, which sit at the base of its stack (A.3).
//
// The scheduler (step 6) decides who runs; this file keeps the records it
// works from (who is on which core, who waits for what) and carries out
// calls. Interrupt delivery is step 6's too.
//
// Gate 3 (build-plan-gate3.md step 4; 10.4, 11.2, 11.5, A.4) adds the
// network's calls, when the world has a map (attach_net):
// - SEND copies its bytes into the place's outbox, which the world hands to
//   the network at the barrier in place order. Room in the exit queue is
//   kept for what's in the outbox, so a SEND that was let go always joins.
//   The thread sleeps for the device's latency plus its size over the first
//   link's bandwidth; an idle run waits on its core as long.
// - Messages for comms ports reach deliver() at the barrier: the thread
//   that has waited longest in RECV on that port takes it, or it joins the
//   port's inbox and every thread with a data handler there gets the
//   interrupt pending (once, however many join).
// - PING posts an 8-byte request and the thread waits for its echo, or its
//   timeout; a neighbour SENSE waits twice the link's delay, its reading
//   taken by the world at the barrier the request lands at.
#include "nova2/array.h"
#include "nova2/cpu.h"
#include "nova2/hashmap.h"
#include "nova2/memory.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h: saving and loading state
class Network; // net.h

// The calls, in the call table's order (9.2).
enum Call : u8 {
    kAlloc = 0, kFree, kProtect, kSpawn, kExit, kSleep, kSetClock, kSetCore,
    kSetHandler, kHret, kSense, kSend, kRecv, kDread, kDwrite, kPing,
    kCallCount
};

// SENSE's readings (A.4), in order.
enum Reading : u8 {
    kTemperature = 0, kVoltage, kTick, kPlaceId, kTurnLeft, kPortBytes, kIdleCores, kFreeMemory,
    kPageSize, kStrict, kDeviceCount, kNeighbourCount,
    kDeviceKind, kDeviceSpeed, kDeviceThreshold, kDeviceFree,
    kNeighbourId, kNeighbourVoltage, kNeighbourTemperature, kNeighbourIdleCores, kNeighbourFreeMemory,
    kReadingCount
};

enum class DeviceKind : u8 { p_core = 1, e_core, memory, disk, network, cooling };

// Handler kinds (A.4: SETHANDLER's A mod 4).
enum HandlerKind : u8 { kOnFault = 0, kOnData, kOnTimer, kOnLowVoltage };
// A thread's `pending` byte: a bit for each HandlerKind waiting to be
// delivered, and at the top the low-voltage latch (build-plan-gate4.md 2,
// gap 6): set when the thread takes the low-voltage interrupt, and cleared
// when it starts a quantum with the voltage back above the margin, so it
// takes one a dip.
inline constexpr u8 kPendingKinds = 0x0F;
inline constexpr u8 kLowVoltageTaken = 0x80;

// What a fault handler finds in A (A.3): from gate 5 the watchdog's too
// (9.1; build-plan-gate5.md step 10).
inline constexpr u32 kFaultMemory = 1, kFaultStack = 2, kFaultWatchdog = 3;

inline constexpr u32 kMaxSlots = 64;
inline constexpr u32 kPinAny = 0, kPinP = 1, kPinE = 2;
inline constexpr u64 kNever = ~0ull;

struct OsSettings {
    // The numbering (9.2): SYS s picks slot (s mod slot_count), and the table
    // names its call. Default: the 16 calls, then a second ALLOC and SPAWN.
    u32 slot_count = 18;
    u8 slots[kMaxSlots] = {kAlloc, kFree, kProtect, kSpawn, kExit, kSleep, kSetClock, kSetCore, kSetHandler,
                           kHret,  kSense, kSend, kRecv, kDread, kDwrite, kPing, kAlloc, kSpawn};
    u32 stack_size = 64;          // bytes per thread (appendix E)
    u32 clock_min = 5;            // SETCLOCK's range, in tenths of stock speed (7.5)
    u32 clock_max = 15;
    u32 ports = 16;               // per place; port 0 is the reset region (11.2)
    u32 ping_timeout = 100;       // ticks, when PING's own is 0 or less
    // Each call's whole cost in cycles, the crossing included (9.7; placeholders).
    u32 call_cycles[kCallCount] = {2000, 300, 5000, 20000, 300, 300, 30000, 300,
                                   300,  300, 150,  150,   150, 150, 150,   150};
    u32 trap_cycles = 150;        // a fault's trap: the crossing
    u32 call_factor16[2] = {16, 26}; // P and E cores: calls 60-70% slower on E (7.2)
    u32 disk_latency_cycles = 20000; // DREAD and DWRITE: on top of waiting for the barrier (9.7)
    u32 disk_cycles_per_byte = 1;
    // 7.6's fallback (build-plan-gate2.md 2): a thread running on a core that
    // cuts out is paused, back in the ready queue with its registers, instead
    // of ended. Off by default.
    bool cutout_pause = false;
    // The saved-state rule (9.4; widened at gate 2, build-plan-gate2.md 11
    // step 10): a thread ends when any bit of its saved state fails, by a
    // cosmic-ray flip or an edge error as well as by fading. Off, only fading
    // ends it, as in gate 2's first run (runs/gate2-1).
    bool saved_state_fails = true;
    // Whether an idle run may SEND (9.2; off from 2026-11-17, Damian, after
    // build-plan-gate3.md step 8's flood test). Off, it fails soft, so code
    // travels only in a program's SEND.
    bool idle_send = false;
    // A live thread's stack counts as in use (R15, 9.4; Damian, 2026-11-19):
    // a SPAWN into it starts a thread of the same program, and a FREE of it
    // fails, so a thread's saved registers always lie in memory its program
    // holds. Off, as before, a program can give a sibling thread's stack away
    // or free it, and the thread's registers go on being written there.
    bool stack_held = true;
    u32 boot_block = 512; // bytes: the first block of the disk space, run at a cold start (10.5)
    // The disk's blocks (7.4; build-plan-gate5.md step 3): the unit of its
    // wear by writes. A block takes its endurance in writes, each block's own
    // drawn from half to twice the value here (0: blocks don't wear). Past
    // it, each bit written to the block lands wrong with a chance of
    // worn_flip × its writes past ÷ the endurance. With levelling the OS
    // swaps a block whose writes pass the least-written block's by the gap.
    u32 block = 512;
    u32 block_endurance = 10000;
    double worn_flip = 0.001;
    bool levelling = false;
    u32 levelling_gap = 100;
    u32 inbox_bytes = 1024;    // each comms port's inbox (11.5)
    u32 send_byte_cycles = 2;  // SEND's copy into the network device, a byte (9.7, 11.2)
    // Gate 5's two rules of 9.1's amendment (build-plan-gate5.md step 10):
    // - The out-of-memory killer: a place crowded (free memory under a
    //   tenth, 13.3) at this many barriers in a row loses one program, drawn
    //   by the bytes it holds, and its clock starts again. 0: off.
    // - The watchdog: a thread that has run this many cycles of its own
    //   since its last call, any call, takes the watchdog fault before its
    //   next instruction. 0: off.
    u64 killer = 2000;
    u64 watchdog = 1000000;
};

enum class ThreadState : u8 {
    free,              // an unused record
    ready,
    running,
    sleeping,          // SLEEP n: until wake_tick
    waiting_interrupt, // SLEEP 0
    waiting_recv,      // RECV with a timeout: until wake_tick (kNever: forever)
    waiting_disk,      // DREAD or DWRITE: until the barrier (9.3)
    waiting_ping,      // PING: until its echo, or wake_tick (11.2)
};

struct Thread {
    ThreadState state = ThreadState::free;
    u8 pin = kPinAny;
    u8 clock = 10;           // tenths of stock speed
    bool in_handler = false;
    u32 frame = 0;           // in a handler: where its frame (kHandlerFrameBytes) sits, for the saved-state rule (9.4)
    u32 id = 0;              // SPAWN's answer
    u32 program = 0;         // its program's holder id
    u32 stack = 0;           // its stack allocation's base
    u32 core = 0;            // 1 + the core it's on, or 0
    u32 prev = 0, next = 0;  // its program's threads (record index + 1; 0 = none)
    u64 wake_tick = kNever;
    u32 handler[4] = {};     // by HandlerKind: addresses, 0 = none
    u32 data_port = 0;
    u32 timer_period = 0;
    u64 timer_next = kNever;
    bool pend = false;       // results to hand over when a wait ends (RECV)
    u32 pend_a = 0, pend_b = 0;
    u8 pending = 0;          // interrupts waiting to be delivered (kPendingKinds), and the low-voltage latch
    i64 debt16 = 0;          // an overrun to repay in its next quantum, in stock sixteenths (R29)
    u32 recv_port = 0, recv_addr = 0, recv_max = 0; // a RECV's wait: where its message goes
    u64 wait_seq = 0;        // when that wait began: the longest waiting takes the next message (11.2)
    u32 ping_seq = 0;        // its PINGs so far, so a late echo finds nobody
    // With the watchdog on (9.1): the cycles it has run since its last call,
    // in sixteenths at its own clock.
    i64 quiet16 = 0;
};

// No address: further than any state's length from every address a place
// has, so "address - kNowhere < length" (unsigned) is false for all of them.
inline constexpr u32 kNowhere = 0x80000000u;

enum class BirthCause : u8 { thread = 0, static_bytes, arrival, boot, hand };
// Why a SPAWN failed: the address isn't in an allocation the caller holds,
// its page is data, or no program or thread record was free.
enum class SpawnMiss : u8 { none = 0, not_held, data_page, no_record };
// `heat`, from gate 4: its core tripped at its heat limit (5). From gate 5
// (9.1, 13.1): `oom`, ended by the out-of-memory killer, and `watchdog`,
// the watchdog's fault with no handler to take it.
enum class DeathCause : u8 { exit = 0, fault, cut_out, device_failed, memory_lost, cold_start, heat, oom, watchdog };

struct Program {
    bool alive = false;
    BirthCause cause = BirthCause::thread;
    u32 threads = 0;      // live threads
    u32 first_thread = 0; // record index + 1
    u64 uid = 0;          // unique in the world, for the facts (13.1)
    u64 parent_uid = 0;   // 0: none
    u64 born_tick = 0;
};

// Genomes and birth images are hashed in this domain, so the two compare (13.2, 13.3).
inline constexpr u64 kGenomeDomain = 0x67656e6f6d65ull; // "genome"

// The facts a place records (13.1), drained by the observers at the barrier.
struct BirthFact {
    u64 tick, uid, parent_uid;
    BirthCause cause;
    u32 place, base, size;
    u32 wlo, whi;           // the extent its parent wrote (the genome, 13.2)
    u64 genome_hash, image_hash;
    u32 genome_at, genome_len; // the genome's bytes, in fact_bytes
};

struct DeathFact {
    u64 tick, uid;
    u32 place;
    DeathCause cause;
};

// A parent marked as carried by the replication current (12.6), for the
// observers alone: a program made by a static run in bytes the current
// landed takes as its parent the program that held those bytes where they
// were copied, with this mark (the mark alone: no program held them). No
// uid reaches the top bit. The OS's cause stays "static".
inline constexpr u64 kCarried = 1ull << 63;
inline u64 parent_of(u64 parent_uid) { return parent_uid & ~kCarried; }
inline bool carried_by_current(u64 parent_uid) { return (parent_uid & kCarried) != 0; }

// The current copied a block a program held, toward place dst (12.6). The
// observers keep that program's record until nothing of the block can still
// make a program.
struct DriftFact {
    u64 tick, uid;
    u32 place, dst;
};

// A SEND that was let go (11.2): who sent it (0: an idle run, which has no
// program, A.4), and where to. The observers keep the record of a program
// that sent to a port 0 until nothing it sent can still make a program.
struct SendFact {
    u64 tick, uid;
    u32 place, dst, port, size;
};

// A RECV that returned bytes (13.1; build-plan-gate5.md 2, gap 1): the
// program that read, the program whose SEND it took (0: an idle run's, or
// not known), where the bytes went and how many, and how many of them lay
// past the end of the allocation the buffer began in: an overrun (9.8). An
// allocation ends with the bytes reserved for it; a buffer that begins in
// no allocation has none to run past.
struct RecvFact {
    u64 tick, uid, sender;
    u32 place, addr, len, past;
};

// One stretch of an overrun: bytes a RECV wrote past its buffer's
// allocation, into one other allocation or one stretch nobody holds.
struct OverrunFact {
    u64 tick, uid, sender;
    u64 owner;    // the program holding the stretch (0: none: free memory, the reset region, an idle run's)
    u32 recv;     // its RecvFact, in recvs
    u32 place, addr, len;
    u32 bytes_at; // the bytes as written, in fact_bytes
    bool stack;   // a live thread's stack
};

// A cold start booted the boot block (13.1, 10.5): whose DWRITE last landed
// in it (0: nobody's, so it's the disk's static).
struct ColdStartFact {
    u64 tick;
    u32 place;
    u64 writer;
};

// What a core is doing.
struct Core {
    DeviceKind kind = DeviceKind::p_core;
    bool working = true;      // on: not cut out (4.4); from gate 5, not failed either (7.6)
    u64 off_at = 0;           // the tick it last cut out
    u32 stock_speed = 25000;  // cycles per tick at the source's voltage (7.5)
    u32 speed = 25000;        // cycles per tick this tick, at the place's voltage; 0 while cut out
    i32 worn_mv = 0;          // how far wear has raised its threshold (7.4; its place keeps it, from its wear)
    u32 thread = 0;           // record index + 1 of the thread on it, or 0
    bool idle_run = false;    // running the reset region (thread is 0 then)
    BirthCause run_cause = BirthCause::static_bytes; // what the idle run started from
    u64 run_parent = 0;       // and whose it was: a boot's, the boot block's last writer (13.1)
    u32 turn_left16 = 0;      // the scheduler's: what's left of the current turn
    ExecContext x;            // the live registers of whatever is on it
    ErrorClock clock;
    bool disk_wait = false;   // its idle run waits for a DREAD at the barrier
    i64 disk_debt16 = 0;      // what that DREAD costs it, once done
    u64 net_until = 0;        // its idle run waits on the network (a SEND, a neighbour SENSE) until this tick
    bool run_made = false;    // its idle run made a program, or let a SEND go (an arrival's, for the counts)
    bool run_sent = false;
    // An arrival's idle run, for the counts (not saved): the first of its
    // calls that failed (ALLOC, FREE, PROTECT or SPAWN), as the call + 1, or
    // 0; and why, for an ALLOC (AllocMiss) or a SPAWN (SpawnMiss).
    u8 run_failed = 0;
    u8 run_miss = 0;
};

// What SENSE can see beyond memory and the cores (gate 1: one place, a
// steady supply, no neighbours).
struct SenseDevice {
    DeviceKind kind;
    u32 speed;        // cores: cycles per tick this tick (0 cut out); memory and disks: latency in cycles
    i32 threshold_mv; // nominal (R25)
    u32 memory_index; // memory devices: their index, for free bytes
    u32 size;         // disks: their size
    bool failed = false; // at the end of its life (7.6): every reading but its kind is 0
};

struct PlaceEnv {
    u32 place_id = 0;
    u64 tick = 0;
    i32 voltage_mv = 1000;
    i32 temperature_dc = 250; // tenths of a degree C
    u32 neighbours = 0;         // its data links (A.4)
    bool network_on = true;     // its network device, this tick (11.4)
    Array<SenseDevice> devices; // cores, then memory devices, then disks
    u32 disk_size = 0;          // the place's disk space, its disks end to end (6.3)
};

struct CallOutcome {
    u32 cost16 = 0;
    bool stays = true; // false: the caller left its core (ended, slept, waits or yielded)
    bool disk = false; // an idle run's DREAD: it waits for the barrier
    bool wait = false; // an idle run's SEND or neighbour SENSE: it waits on its core (net_until)
};

class Os {
  public:
    // The tables are sized from memory: a thread needs a stack, so there can't
    // be more threads (or programs) than stacks fit.
    bool init(Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const OsSettings& s, TextBuf* why);

    // The SYS that just ran on `core` (its IP has moved past it; sys_ip is
    // the SYS's own address). Carries out the call for whatever is on the
    // core, writes its results to the core's live registers, and says what
    // it cost.
    CallOutcome call(u32 core, i32 sys_value, u32 sys_ip);

    // SPAWN (9.4, A.4) for whatever is on `core`; the thread id, or 0.
    u32 spawn(u32 core, u32 addr, u32 value, u32 sys_ip);
    // A fault on `core` (an instruction's, A.3): with a fault handler the
    // thread enters it, past the faulting instruction; otherwise it ends. A
    // fault inside a handler, or a handler entry that overflows the stack,
    // ends it too. Threads only: an idle run's fault ends the run (step 7).
    CallOutcome fault(u32 core, StepKind kind, u8 length);
    // The watchdog's fault (9.1) for the thread on `core`, whose count of
    // cycles since its last call is up: as an instruction's, but with
    // nothing to step past, so a handler's HRET goes on at the instruction
    // the thread was about to run. Its count starts again.
    CallOutcome watchdog(u32 core);
    bool watchdog_on() const { return s_.watchdog != 0; }
    i64 watchdog16() const { return static_cast<i64>(s_.watchdog) * 16; }
    // The out-of-memory killer (9.1), at the barrier: `crowded` says whether
    // the place's free memory is under a tenth now. True when it has been
    // so at the set number of barriers in a row; the clock starts again.
    bool killer_on() const { return s_.killer != 0; }
    bool crowded_tick(bool crowded);
    u64 crowded_for() const { return crowded_for_; }
    // The holder of the living program the bytes held by programs put
    // `draw` in, each byte (stacks included) as likely as any other: of the
    // allocations' reserved bytes, programs in record order (0: no program
    // holds any). Idle runs' holdings and the OS's own aren't among them.
    u32 pick_by_bytes(u64 draw) const;
    u64 bytes_held(u32 holder) const;
    // Ends every thread of a program, so the program, its memory freed with
    // its bytes left (9.4).
    void end_program(u32 holder, DeathCause cause);
    // Enters a handler (A.3): pushes A, B, C, D and IP, jumps, and puts
    // a_value in A. False (and nothing changed) if it can't.
    bool enter_handler(u32 core, u32 handler, u32 a_value);
    // Ends a thread, and its program with its last thread.
    void end_thread(u32 t, DeathCause cause);
    // A cold start reboots the OS (10.5): every thread and program ends, cause
    // "cold start", their memory freed with its bytes as fading left them; the
    // disk requests waiting are forgotten and the ready queues emptied. The
    // disk keeps its bytes, and who last wrote its boot block. Records the
    // fact.
    void cold_start();
    // Every thread and program ends, with the cause given, and the OS's
    // queues, inboxes and waits are emptied: the cold start's, and a
    // teardown's (7.8).
    void end_everything(DeathCause cause);
    // The number its next program takes (uids are unique in the world: a
    // place built again at a respawn carries on its id's).
    u64 next_uid() const { return next_uid_; }
    void set_next_uid(u64 v) { next_uid_ = v; }
    // The invariant check (build-plan-gate2.md step 7; the fuzz world runs it
    // after every tick): the program and thread tables agree with each other
    // and with the cores; every ready thread is queued; every thread or idle
    // run waiting on the disk has its request; no request outlives a barrier
    // with the disk on. False, with the first one broken in why.
    bool check(TextBuf* why) const;
    // What SENSE gives a caller on `core` for a reading (A.4), for tests
    // and views: the readings a neighbour answers aside.
    u32 reading(u32 core, u32 which, u32 index) const { return sense(core, which, index); }
    // A hand-placed program (tests and test worlds only, R7): the allocation
    // at addr, held by idle run `run`, is given to a new program whose thread
    // starts at start with A = value. The thread id, or 0.
    u32 place_by_hand(u32 run, u32 addr, u32 start, u32 value);

    // Registers between turns live at the base of the stack (A.3).
    void load(u32 t, ExecContext& x) const;
    void save(u32 t, const ExecContext& x);
    // Puts thread t on a core / takes it off (the scheduler's moves).
    void put_on(u32 t, u32 core);
    void take_off(u32 core, ThreadState now);

    // The ready queues (R16: scheduler queues are OS records). Plain round
    // robin: one FIFO for unpinned threads and one per pinned kind, ordered
    // together by when each thread became ready (9.5).
    void make_ready(u32 t);
    // The next ready thread a core of this kind may run, taken off its queue;
    // ~0 if none.
    u32 pick(DeviceKind kind);
    bool any_ready(DeviceKind kind);
    // Whether the ready queues hold nothing at all, stale entries included.
    // Then pick would find no thread and change nothing, so a core with no
    // thread needn't ask: most quanta of a place of static are such.
    bool none_queued() const { return (rq_[0].size | rq_[1].size | rq_[2].size) == 0; }
    // Asks the host to bring near what switching to the threads at the front
    // of the ready queues will read: each one's record, its saved registers,
    // and the code and stack those point at with the allocator's entries for
    // them. Each of those is found through the one before, so a switch
    // waits for them one after another; asked for here, for several threads
    // at once, the waits overlap. A hint for the host's cache alone: it
    // changes nothing in the place, and a wrong guess costs only its time.
    // ask_for_ready, at a tick's start, asks for the first few of each queue
    // (kAskAhead); ask_for_next, after each switch, for the ones that have
    // come that near the front since.
    void ask_for_ready() const;
    void ask_for_next() const;
    // Which kinds have a working core (a pin to a kind with none falls back
    // to any, 9.5). The scheduler refreshes it each tick.
    void note_working();
    // Ends a wait: hands over any results it was holding (RECV's count and
    // sender, into the saved A and B) and queues the thread.
    void wake(u32 t);

    // The raw disk (9.3, R14): DREAD and DWRITE complete here, at the
    // barrier, in core order and then request order, rechecking access.
    // While the disk is cut out they wait here for it (7.6).
    void barrier();
    // The disk's power this tick, from the place's device states (4.4).
    void set_disk_on(bool on) { disk_on_ = on; }
    // What the last barrier's I/O is still doing (7.6): it landed at the
    // barrier, but the disk needs until the next tick's device states to
    // finish it (its latency, which the callers pay then). The bytes it moved,
    // in the order it moved them.
    u64 in_flight_bytes() const;
    // The disk cut out with its last barrier's I/O in flight, `at` bytes
    // along it. Writes from there on are put back as they were, so the one
    // it cuts is torn, part new and part old; reads from there on fail,
    // answering 0.
    void tear(u64 at);
    // The in-flight window closes at each tick's device states.
    void settle_disk() { flight_.clear(); flight_old_.clear(); }
    // An idle run that ends forgets its DREAD.
    void cancel_disk(u32 core);
    u8* disk() { return disk_.data(); }
    u32 disk_size() const { return static_cast<u32>(disk_.size()); }
    u32 pending_disk() const { return static_cast<u32>(ops_.size()); }
    // The boot block (10.5): the disk space's first bytes, its size a setting
    // (the whole disk if that's smaller), and the uid of the program whose
    // DWRITE last landed in it (0: nobody's, so it holds the disk's static).
    u32 boot_size() const { return s_.boot_block < disk_size() ? s_.boot_block : disk_size(); }
    u64 boot_writer() const { return boot_writer_; }

    // The disk's blocks (7.4; blocks.cpp). A new disk's: no writes, and each
    // block's endurance drawn by the seed, the place, the disk's number
    // (`fresh`: a replaced disk draws again) and the block. After init.
    void init_blocks(u64 seed, u32 fresh);
    bool blocks_on() const { return block_count_ != 0; } // blocks wear (disk.block_endurance above 0)
    u32 block_size() const { return s_.block; }
    u32 block_count() const { return block_count_; }
    // Where levelling has put a block of the disk space: the block holding it.
    u32 block_holding(u32 b) const { return s_.levelling ? block_map_[b] : b; }
    u32 block_writes(u32 p) const { return block_writes_[p]; }
    u32 block_life(u32 p) const { return block_life_[p]; }
    // The block holding offset 0 has passed its endurance: its disk counts
    // as failed (7.4, 10.5), which the place sees to at its next device
    // states.
    bool boot_worn() const;
    // A write of n bytes at offset, just landed (the barrier's; tests call
    // it for a block's thousands): a write to each block it touches, the
    // flips of those past their endurance, and levelling's swaps.
    void wear_blocks(u32 offset, u32 n);
    void set_block_writes(u32 p, u32 writes); // tests' (R7)
    // The disk failed (7.6): its data is lost, the boot block with it;
    // requests waiting answer 0, and DREAD and DWRITE fail soft from now on.
    void fail_disk();
    void set_disk_failed(bool failed) { // after a load
        disk_failed_ = failed;
        disk_on_ = disk_on_ && !failed;
    }
    bool disk_failed() const { return disk_failed_; }
    void serialize_blocks(Archive& ar);
    // Block writes (programs' and levelling's), those of them to a block past
    // its endurance, the bits those landed wrong, and levelling's swaps.
    u64 block_writes_all = 0, worn_writes = 0, worn_flips = 0, level_swaps = 0;

    // The network (gate 3), when the world has a map; without it SEND fails,
    // PING answers -1 and RECV waits for nothing, as at gates 1 and 2.
    void attach_net(Network* net) { net_ = net; }
    // The SENDs and PINGs let go this tick, for the barrier.
    struct OutMsg {
        u32 dst, port, end, size, at; // its exit (Network::exit_end), and its bytes in outbox_bytes()
        bool ping;
        u64 sender;                   // the sending program's uid; 0 from an idle run
        u64 sent;                     // the tick
        u32 tag_t, tag_id, tag_seq;   // a PING's: the thread waiting for the echo
    };
    u32 outbox_count() const { return static_cast<u32>(outbox_.size()); }
    const OutMsg& outbox(u32 i) const { return outbox_[i]; }
    const u8* outbox_bytes() const { return outbox_bytes_.data(); }
    void clear_outbox() {
        outbox_.clear();
        outbox_bytes_.clear();
    }
    // A message for comms port `port` (not 0), delivered at the barrier from
    // place `from`: to the longest-waiting RECV there, copied in with access
    // checked now; else into the port's inbox, if there's room, raising the
    // data interrupt for every thread with a handler for the port (gap 5).
    void deliver(u32 port, const u8* bytes, u32 size, u32 from, u64 sender = 0);
    // The messages waiting in the comms ports' inboxes, and who sent each
    // (0: none, or not known), for the observers.
    u32 inbox_count() const { return static_cast<u32>(inbox_.size()); }
    u64 inbox_sender(u32 i) const { return inbox_[i].sender; }
    // A PING's echo, back at the barrier `rtt` ticks after the PING.
    void ping_reply(u32 t, u32 id, u32 seq, u32 rtt);
    // A neighbour SENSE waiting for the barrier its request lands at (A.4).
    struct Ask {
        u32 core, thread, thread_id; // the caller: a thread (record + 1), or the idle run on core
        u32 neighbour, which;
        u64 lands;
    };
    const Array<Ask>& asks() const { return asks_; }
    void answer(const Ask& a, u32 value); // the reading, for the caller when its wait ends
    void drop_asks(u64 landed);           // those that landed at or before this barrier
    // An idle run that ends forgets its network waits.
    void cancel_net(u32 core);
    u32 inbox_bytes(u32 port) const { return port < inbox_used_.size() ? inbox_used_[port] : 0; }

    u32 thread_capacity() const { return static_cast<u32>(threads_.size()); }
    // One past the highest thread record in use: every record from here on
    // is free, so a walk of the threads in record order can stop here.
    u32 thread_top() const { return thread_top_; }
    // Whether any thread sleeps or waits (for a tick, an interrupt, a RECV or
    // an echo) or has a timer handler: if none does, a tick's start has no
    // thread to wake and no timer to fire.
    bool wake_scan() const { return (waiters_ | timers_) != 0; }
    // Beside the records, an entry a thread, for the walks that look at every
    // thread each tick: they read these few bytes a thread and not its whole
    // record (optimization-plan.md 4.1).
    // attention: bit 0 if it sleeps or waits (as wake_scan counts), bit 1 if
    // it has a timer handler; 0 for a thread a tick's start has nothing to do
    // for, and for a free record.
    const u8* attention() const { return attention_.data(); }
    // saved_at: where its saved registers lie (its stack's base) while it is
    // off its core; frame_at: where its handler's frame lies while it is in a
    // handler. kNowhere otherwise, and for a free record.
    const u32* saved_at() const { return saved_at_.data(); }
    const u32* frame_at() const { return frame_at_.data(); }
    // due: the first tick a tick's start has anything to do for it: its
    // timer's next firing, the tick its sleep or wait runs out, or 0 (now) if
    // it waits for an interrupt and one is pending. kNever if none of them,
    // and for a free record. A late world's threads nearly all sleep or wait,
    // and the walk reads this and not their records (optimization-plan.md,
    // pass 7).
    const u64* due() const { return due_.data(); }
    // due_min: for each group of kDueGroup thread records, from the first,
    // the least of their due (pass 13): a tick's start passes over a group
    // with nothing due.
    static constexpr u32 kDueGroupShift = 6;
    static constexpr u32 kDueGroup = 1u << kDueGroupShift;
    const u64* due_min() const { return due_min_.data(); }
    // The live threads by the block of kStackBlockBytes their stack starts
    // in (pass 13): first_in_block, a block's first thread record + 1 (0:
    // none); next_in_block, the one after thread t in its block's, likewise.
    // In no order. A thread's saved registers lie at its stack's base, and a
    // handler's frame inside its stack (enter_handler), so a thread with
    // either in a block is in a list near it, found without a walk.
    static constexpr u32 kStackBlockShift = 6;
    static constexpr u32 kStackBlockBytes = 1u << kStackBlockShift;
    u32 first_in_block(u32 block) const { return block_first_[block]; }
    u32 next_in_block(u32 t) const { return block_next_[t]; }
    // After a change to a thread's record made outside the OS (a timer the
    // scheduler fired): what's kept beside the record follows it again.
    void thread_changed(u32 t) { note(t); }
    Thread& thread(u32 t) { return threads_[t]; }
    const Thread& thread(u32 t) const { return threads_[t]; }
    const Program& program(u32 holder) const { return programs_[holder - kFirstProgramHolder]; }
    // The living program holding the most of the bytes [addr, addr + len),
    // the first of them in the stretch if several hold as many (0: none
    // holds any): whose a block the current copies is, for the observers
    // (12.6). Of the first 64 allocations in the stretch.
    u64 holder_of(u32 addr, u32 len) const;
    u32 program_capacity() const { return static_cast<u32>(programs_.size()); }
    u32 live_programs() const { return live_programs_; }
    u32 live_threads() const { return live_threads_; }
    const OsSettings& settings() const { return s_; }
    u32 call_of(i32 sys_value) const { return s_.slots[static_cast<u32>(sys_value) % s_.slot_count]; }

    Array<BirthFact> births;
    Array<DeathFact> deaths;
    Array<ColdStartFact> cold_starts;
    Array<SendFact> sends;
    Array<DriftFact> drifts;     // gate 5: the current's blocks that a program held (the world's, at the barrier)
    Array<RecvFact> recvs;       // gate 5: every RECV that returned bytes
    Array<OverrunFact> overruns; // and each stretch one wrote past its buffer's allocation
    Array<u8> fact_bytes; // genomes, by BirthFact::genome_at, and overruns' bytes
    // Counted for the samples (13.1); not part of the state.
    u64 threads_started = 0, idle_sends = 0;
    u64 lv_handlers = 0; // low-voltage handlers registered (9.5), for reports
    // What idle runs ask for (the copying ladder, build-plan-gate1.md 11):
    // calls by kind, and the ALLOCs and SPAWNs that worked.
    u64 run_calls[kCallCount] = {};
    u64 run_alloc_ok = 0, run_spawn_ok = 0;
    // What programs' threads ask for (design.md 13.1; build-plan-gate5.md 1):
    // calls by kind, and those that did what they asked, counted when that is
    // known. ALLOC, FREE, PROTECT, SPAWN, SETCORE and SETHANDLER answer with
    // something other than 0; EXIT, SLEEP, SETCLOCK and SENSE always do;
    // HRET returns from a handler; SEND is let go; RECV returns bytes; DREAD
    // and DWRITE move bytes; PING hears its echo in time. And the SENDs to
    // a port other than 0: those asked for, and those let go.
    u64 calls[kCallCount] = {}, calls_ok[kCallCount] = {};
    u64 sends_other = 0, sends_other_ok = 0;
    // SPAWN two ways (Damian, 2026-11-20; build-plan-gate4.md 11): those
    // that asked for a thread in the caller's own program (the address is
    // in an allocation one of its threads runs in, or a stack of its), and
    // those that asked for a new program (any other allocation it holds),
    // with those of each that got it. A SPAWN at an address the caller
    // doesn't hold, or on a data page, asked for neither.
    u64 spawns_thread = 0, spawns_thread_ok = 0, spawns_program = 0, spawns_program_ok = 0;
    // What the disk's cut-outs did (7.6): writes torn or lost, reads failed.
    u64 torn_writes = 0, failed_reads = 0;
    // The network's calls: messages taken by a waiting RECV at delivery,
    // joined to an inbox, or dropped there (full); and taken from one.
    u64 recv_direct = 0, inbox_joined = 0, inbox_dropped = 0, inbox_taken = 0;
    // Gate 5's rules of 9.1 (step 10): the programs the killer ended, the
    // bytes they held and their threads; the watchdog's faults, and those a
    // handler took.
    u64 kills = 0, killed_bytes = 0, killed_threads = 0;
    u64 watchdog_faults = 0, watchdog_handled = 0;
    void clear_facts();

    // Saves or loads the records (archive.h), at the barrier, with any disk
    // requests waiting for the disk and the I/O still in flight.
    void serialize(Archive& ar);
    // Gate 5's state of 9.1's rules (step 10): the killer's clock, and each
    // live thread's count for the watchdog, each only when its rule is on.
    void serialize_guards(Archive& ar);
    // The static the disk began as (10.1): draw `first` on of the stream with
    // this base, eight bytes a draw. A save then leaves out the pieces of the
    // disk that are still that static, and a load makes them again.
    void note_static(u64 base, u64 first) {
        static_known_ = true;
        static_base_ = base;
        static_first_ = first;
    }
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);
    void counters5(Archive& ar); // gate 5's, and who sent each message waiting in an inbox

  private:
    SpawnMiss spawn_miss_ = SpawnMiss::none; // why the last SPAWN that failed did
    u8 spawn_asked_ = 0;                     // what the last SPAWN asked for: 1 a thread, 2 a program, 0 neither
    u32 new_program(BirthCause cause, u64 parent_uid);
    u32 new_thread(u32 holder, u32 start, u32 value, u32 device);
    void record_birth(u32 holder, u32 alloc_id);
    u32 holder_on(u32 core) const;
    u32 caller_ip(u32 core, u32 t) const; // a thread's current IP (live or saved)
    // Whether code at `ip`, run by `holder`, runs on allocation `id` (R15).
    bool runs_on(u32 holder, u32 ip, u32 id) const;
    // Whether allocation `id` is the stack of a live thread of program `holder`.
    bool stack_of(u32 holder, u32 id) const;
    u32 sense(u32 core, u32 which, u32 index) const;
    void send_call(u32 core, CallOutcome* out);
    void recv_call(u32 core, CallOutcome* out);
    void ping_call(u32 core, CallOutcome* out);
    void neighbour_sense(u32 core, u32 which, u32 index, CallOutcome* out);
    u32 kept_room(u32 end) const; // what the outbox holds for that exit
    // Copies a message into a holder's buffer, stopping at the first page it
    // can't write (A.4); the bytes copied.
    u32 copy_in(u32 holder, u32 addr, u32 max, const u8* bytes, u32 len, u64 sender);
    void note_recv(u32 holder, u32 addr, const u8* bytes, u32 n, u64 sender);
    bool hret(Core& c, Thread& th);
    u32 set_handler(Core& c, Thread& th, u32 holder);
    u32 cost16(const Core& c, u32 cycles) const;

    Memory* mem_ = nullptr;
    Core* cores_ = nullptr;
    u32 core_count_ = 0;
    PlaceEnv* env_ = nullptr;
    OsSettings s_;
    Array<Program> programs_; // by holder - kFirstProgramHolder
    Array<Thread> threads_;
    u32 next_thread_id_ = 1;
    u64 next_uid_ = 1;
    u32 live_programs_ = 0;
    u32 live_threads_ = 0;
    // What the walks and searches of the records need, kept as the records
    // change so none of them reads every record (optimization-plan.md 4.1).
    // All of it follows from the records, so it isn't saved: reindex() works
    // it out again after a load, and check() checks it.
    u32 thread_top_ = 0;   // one past the highest thread record in use
    u32 thread_free_ = 0;  // no thread record below this is free
    u32 program_free_ = 0; // no program record below this is free
    u32 waiters_ = 0;      // threads sleeping, or waiting for an interrupt, a RECV or an echo
    u32 timers_ = 0;       // threads with a timer handler
    Array<u8> attention_;  // per thread record: see attention()
    Array<u32> saved_at_;  // see saved_at()
    Array<u32> frame_at_;  // see frame_at()
    Array<u64> due_;       // see due()
    Array<u64> free_bits_; // a bit per thread record, set if it is free: new_thread's search
    void note(u32 t); // thread t's entries in the first four, from its record: after any change to what they follow
    // ask_for_ready's three steps for one thread, each reading what the one
    // before asked for.
    static constexpr u32 kAskAhead = 8;
    void ask_thread(u32 t) const;  // its record and saved registers
    void ask_code(u32 t) const;    // the code and stack those point at
    void ask_records(u32 t) const; // the allocations those lie in
    void set_state(Thread& th, ThreadState now);
    void reindex();
    void serialize_tables_full(Archive& ar);    // serialize's records, ready queues and disk, whole
    void serialize_tables_compact(Archive& ar); // and what is in use of them
    bool static_known_ = false;
    u64 static_base_ = 0, static_first_ = 0;

    struct DiskOp {
        u32 core;   // who asked
        u32 thread; // record index + 1, or 0 for an idle run
        u32 thread_id;
        u32 holder;
        bool write; // DWRITE
        bool live;
        u32 offset, addr, len;
        u64 seq;
    };
    Array<u8> disk_;
    Array<DiskOp> ops_;
    u64 op_seq_ = 0;
    u64 crowded_for_ = 0; // the out-of-memory killer's clock: barriers in a row the place was crowded
    // A fault with its code for a handler (A.3), and the cause it ends the thread with without one.
    CallOutcome take_fault(u32 core, u32 code, u8 length, DeathCause cause);
    bool disk_on_ = true;
    u64 boot_writer_ = 0; // whose DWRITE last landed in the boot block
    // The last barrier's I/O, in flight until the next tick's device states:
    // whose it was and what it moved, and each write's old bytes.
    struct Flight {
        u32 core, thread, thread_id; // as DiskOp's
        bool write;
        u32 offset, n; // where on the disk, and the bytes moved
        u32 old_at;    // a write's old bytes, in flight_old_
        bool boot;     // a write landing in the boot block: it made its writer the block's last
        u64 writer_before; // and who was before, should a cut-out put the write back whole
    };
    Array<Flight> flight_;
    Array<u8> flight_old_;
    u32 disk_call(u32 core, bool write, CallOutcome* out);
    // The blocks (blocks.cpp): each physical block's writes and endurance;
    // with levelling, the block holding each block of the disk space and the
    // one each holds, and the lowest-numbered least-written block.
    u32 block_count_ = 0;
    Array<u32> block_writes_, block_life_, block_map_, block_holds_;
    u32 least_ = 0;
    u64 block_seed_ = 0;
    u32 disk_fresh_ = 0;
    u64 flip_p64_ = 0;
    bool disk_failed_ = false;
    struct FlipDraw {
        bool begun = false;
        u64 tick = 0, base = 0, count = 0;
    };
    FlipDraw flips_;
    void draw_block_lives();
    void write_block(u32 p, u32 lo, u32 hi);
    void find_least();

    struct ReadyEntry {
        u32 t, id;
        u64 seq;
    };
    struct Ring {
        Array<ReadyEntry> e;
        u32 head = 0, size = 0;
    };
    // The network's (gate 3).
    Network* net_ = nullptr;
    Array<OutMsg> outbox_;
    Array<u8> outbox_bytes_;
    struct InMsg {
        u32 port, from, len, at; // at: in inbox_store_
        // The program that sent it, for the observers (0: none, or not known).
        // Not part of the world: saved with the counters (counters5).
        u64 sender;
    };
    Array<InMsg> inbox_; // every comms port's, in the order they joined
    Array<u8> inbox_store_;
    Array<u32> inbox_used_; // bytes, by port
    Array<Ask> asks_;
    u64 wait_seq_ = 0;

    Ring rq_[3]; // by pin: any, P only, E only
    u64 rq_seq_ = 0;
    bool working_[3] = {true, true, true}; // any, P, E
    bool front(u32 q, ReadyEntry* out); // the first live entry, stale ones dropped

    // The live threads' stacks, kept with the records above (reindex, check):
    // a program's holder and a stack's base, as stack_key makes them, to how
    // many of that program's live threads have their stack there. One, but
    // for a stack freed under its live thread and handed out again, as it
    // can be with os.stack_held off. So stack_of finds a stack without
    // walking the program's threads (pass 13). Last, moving no other member.
    HashMap<u32> stacks_;
    Array<u32> block_first_; // by block of memory: see first_in_block
    Array<u32> block_next_;  // by thread record: see next_in_block
    void add_stack(u32 t);   // thread t, just started, in the two above
    void drop_stack(u32 t);  // and out of them, as it ends
    Array<u64> due_min_; // see due_min()
    void note_due(u32 t, u64 old, u64 d);
    // What deliver() looks for, kept by note() (pass 13): by thread record,
    // 1 + the port it waits on in RECV and 1 + the port its data handler is
    // on (0: none, or a port past os.ports, left to the old walk), and by
    // port, how many threads have each. So deliver() finds a port's threads
    // without reading every record, and stops once it has seen them all.
    Array<u32> recv_key_, data_key_;
    Array<u32> recv_count_, data_count_;
    // One past the highest live program record, kept as programs start and
    // end (reindex, check), so the killer's draw reads no further (pass 13).
    u32 program_top_ = 0;
    void lower_program_top(); // after a program's end
};

} // namespace nova2
