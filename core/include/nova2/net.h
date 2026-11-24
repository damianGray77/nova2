#pragma once

// The network (design.md 11.3-11.5, appendix C step 6; build-plan-gate3.md
// step 3): routes, link-end queues, store and forward, corruption, and what
// a dark network device does to them.
//
// - Routes take the least static cost: each link's delay (the map's per-hop
//   cost plus length x the per-length cost, rounded up). A place's next hop
//   toward d is the neighbour n with the least delay(place, n) + dist(n, d),
//   ties to the lowest id. Dark places (network device off) are out of the
//   graph: they forward nothing, and no route reaches them. A destination's
//   column of next hops is worked out when first asked for and kept until
//   the topology changes (11.3: found on demand and cached), so a lookup
//   from places running in parallel is safe: each works out a stale column
//   on its own, and the first to finish keeps it. At the barrier, the
//   columns the next barrier's arrivals will ask for are worked out ahead,
//   across the host threads (plan_routes). Which thread works a column out,
//   or when, changes nothing: a column follows from the dark places alone.
// - Each link end has a queue of net.queue ticks of its link's bandwidth, in
//   bytes, and never less than net.message_max, so a message of any allowed
//   size can cross any link. A message that finds no room is dropped, and
//   those waiting keep their place (tail drop, 11.5). The end sends one
//   message at a time: it takes ceil(size / bandwidth) ticks to send, then
//   the link's delay to cross, and is then at the far place (store and
//   forward, R28).
// - Time is in whole ticks, and messages move only at the barrier (gap 7).
//   The barrier's network step, at tick t, in this order:
//   1. every place whose network device is dark drops what's queued at it:
//      its link-end queues and the message it was sending (11.4);
//   2. the messages that reach their next place at t, by the place they
//      came from (by id), then their order on the link. Each hop flips
//      bits at net.corruption for each average link length crossed (11.3).
//      One that reaches a dark place is lost; one at its destination is
//      delivered; any other joins the queue toward its next hop, or is
//      dropped if there's no room, or stranded if there's no route;
//   3. each link end that's free starts sending the message at its queue's
//      head, but drops it, as stranded, if the next hop is dark (11.5);
//   4. if any network device went off or came on, the routes are worked
//      out again, for the next tick (11.3).
//   The OS's SENDs and PINGs in tick t (post(), in place order, by the
//   world; send() in tests) are already in their queues then, ahead of the
//   barrier's arrivals.
// - So a hop takes its queue's wait, plus ceil(size / bandwidth), plus the
//   link's delay: at least a tick, and a message crosses at most one hop at
//   a barrier.
// - A message to its own place crosses no link (build-plan-gate3.md step 4;
//   agreed with Damian): it reaches its port after the network device's
//   latency, first among its place's arrivals, unless the device goes dark
//   first. It has no queue to fill and nothing to corrupt it.
// - From gate 5 (11.6; build-plan-gate5.md step 5) a link can fail (the
//   world says which, each barrier), and a message whose way on is gone may
//   wait, up to net.lifetime ticks. A link end is blocked while its link is
//   failed or the place at its far end is dark. With a lifetime of 0 a
//   blocked end's messages are dropped as gate 3 drops a dark place's: those
//   queued as stranded, at the queue's head, and those crossing as lost,
//   when they would have arrived. With a lifetime, the barrier's step is:
//   1. a dark place drops everything at it, as before;
//   2. what's crossing from a blocked end comes back to it (store and
//      forward: the near end still held it), ahead of what's queued there;
//   3. each place's waiting messages, in the order of its links and then
//      their order at each, take the route that is there now: into that
//      queue's tail if it has room, and otherwise they wait on. One with no
//      route waits, and is dropped, stranded, when it has had none for the
//      lifetime;
//   4. the arrivals, as before, behind those. One that reaches a place with
//      no route on waits there, in the room of the end it came in by;
//   5. each free end starts sending its queue's head; a blocked end's
//      queue joins its waiting messages instead;
//   6. the routes are worked out again if a device or a link changed.
//   Waiting messages count against their end's queue, which bounds them
//   (but for those that came back from the link). The routes used at a
//   barrier are those worked out after the last one (11.3: once a tick), so
//   a message found blocked at one barrier takes its new route at the next,
//   and a lifetime of 0 leaves it none: gate 3's drops, exactly.
// - A place can respawn (7.8; build-plan-gate5.md step 6), and the map's
//   links change under the network (relink). A link that is still there
//   keeps its queues and what is crossing it. A link that is gone takes
//   what was crossing it. What was queued or waiting at its end at a
//   place that is still there waits on at that place's first link, in its
//   room, and takes its new route at the next barrier; with a lifetime of
//   0, or at a place left with no link, it is lost too. Everything at the
//   torn-down place goes with its network device. Messages for the id are
//   untouched: they are delivered to the place built in its stead.
// - A block of the replication current (12.6; the world's, current.h) is a
//   message to the next place along one link. It takes queue room and the
//   link's bandwidth as any message does, and it never waits: wherever a
//   message would be held, or dropped, it is let go and counted apart
//   (adrift), with no place's drops. Delivered, it is counted apart too
//   (drifted), and the world writes it into that place's reset region.
// - A PING is a message of net.ping_size bytes. At its destination the
//   network answers it by itself, if that place's device is on (11.2): the
//   same message, turned round as an echo, joins the queue back. An echo
//   delivered is the round trip done.
#include "nova2/array.h"
#include "nova2/config.h"
#include "nova2/map.h"
#include "nova2/noise.h" // GapLog
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h
class Rng;     // rng.h

struct NetSettings {
    u32 queue_ticks = 8;      // each link end's queue, in ticks of its bandwidth
    double corruption = 1e-5; // per bit, for each average link length crossed
    u32 message_max = 1024;   // bytes
    u32 latency = 1;          // the network device's, in ticks (11.4): a message to its own place takes it
    u32 ping_size = 8;        // PING's request and echo, in bytes
    u64 lifetime = 1000;      // ticks a message with no way on may wait (11.6); 0: dropped at once
    bool outages = false;     // links can fail in this world (the world's: link wear or cuts are on)
};

void read_net_settings(Config& cfg, NetSettings* ns);

class Routes {
  public:
    Routes() = default;
    ~Routes();
    Routes(const Routes&) = delete;
    Routes& operator=(const Routes&) = delete;

    void init(const Map* map);
    // The places whose network device is off (dark[i] != 0). True if that
    // changed the topology: the routes are then worked out afresh as asked.
    bool set_dark(const u8* dark);
    bool dark(u32 i) const { return dark_[i] != 0; }
    // A snapshot's dark places: every column is worked out again as asked,
    // the same as before it was taken, since the routes follow from them.
    void restore_dark(const u8* dark);
    // The failed links (down[k] != 0, by data link), likewise: out of the
    // graph until they're back. nullptr: none.
    bool set_down(const u8* down);
    void restore_down(const u8* down);
    bool down(u32 k) const { return down_[k] != 0; }
    // Which of place s's links (an index into its links, Map::link_at) leads
    // toward d, or ~0: none (s or d dark, d unreachable, or s == d).
    u32 next(u32 s, u32 d);
    // How many times the topology changed, and columns worked out, and the
    // time-stamp counter's cycles spent working them out: for reports.
    u64 changes = 0, columns = 0, cycles = 0;
    // The bytes the columns kept take: a next hop a place for each
    // destination asked for since the start.
    usize kept_bytes() const;

    // A batch of columns worked out together, at the barrier, with nothing
    // else asking: begin_batch takes the destinations (stale ones are kept)
    // and says how many tasks to run them in, at most `tasks` (0: none
    // stale); then each task, run_batch(0 .. that - 1), possibly at once,
    // works columns out until none is left.
    u32 begin_batch(const u32* ds, u32 count, u32 tasks);
    void run_batch(u32 task);

  private:
    // A search's working space: each place's distance, the bucket queue's
    // heads and entries (or the heap, for long delays).
    struct Scratch {
        Array<u32> dist, head, entry_place, entry_next;
        Array<u64> heap;
    };
    // A link as the search sees it, from place i: the place at its far end,
    // its delay, and its index in that place's own links.
    struct Hop {
        u32 to = 0, delay = 0, back = 0, link = 0;
    };

    const u16* column(u32 d);
    void work_out(u32 d, u16* col, Scratch& s) const;

    const Map* map_ = nullptr;
    u32 n_ = 0;
    Array<u32> begin_; // place i's hops are hops_[begin_[i] .. begin_[i + 1])
    Array<Hop> hops_;
    u32 max_delay_ = 0;
    Array<u8> dark_;
    Array<u8> down_; // by data link
    Array<u16*> cols_;
    Array<u32> col_epoch_; // a column is current when it equals epoch_
    u32 epoch_ = 1;
    void* lock_ = nullptr; // an SRWLOCK, in net.cpp
    Array<u32> batch_;
    volatile long cursor_ = 0; // the batch's next, taken with InterlockedIncrement
    Array<Scratch*> scratch_;  // a batch task's each
};

enum class MsgKind : u8 { data = 0, ping, echo, current };

// A message on its way.
struct Message {
    u64 seq = 0;    // its number, in the order sent
    u64 sender = 0; // the program that sent it, for the observers (0: none)
    u64 sent = 0;   // the tick it was sent
    u64 arrive = 0; // on a link: the barrier it reaches the far place
    u32 src = 0, dst = 0, port = 0, size = 0;
    u32 at = 0;     // the place it's at, or crossing to
    u32 hops = 0;   // links crossed
    u32 flips = 0;  // bits flipped on the way
    u32 next = 0;   // the next in its queue or on its link, or ~0
    MsgKind kind = MsgKind::data;
    bool live = false; // in use (a slot of the network's)
    // Waiting, with no way on (11.6): the barrier it found none, or kNoStuck
    // while it has a route and waits only for room.
    u64 stuck = ~0ull;
    // A PING's: the thread waiting for its echo (its record, its id, and
    // which of its PINGs), so a late echo finds nobody.
    u32 tag_t = 0, tag_id = 0, tag_seq = 0;
};

// What the network did, for reports.
struct NetCounts {
    u64 sent = 0, sent_bytes = 0;
    u64 delivered = 0, delivered_bytes = 0;
    u64 hops = 0;
    u64 full = 0;      // dropped at a hop: no room in the next queue
    u64 stranded = 0;  // dropped: no route on, or the next hop dark at the queue's head
    u64 lost = 0;      // reached a dark place
    u64 cut = 0;       // queued, or being sent, at a network device that went dark
    u64 flips = 0, corrupted = 0; // bits flipped, and messages with any
    u64 looped = 0;               // sent to their own place
    u64 pings = 0, echoes = 0;    // PINGs answered, and their echoes delivered
    // Gate 5 (11.6): messages that began a wait (their way on gone), those
    // among them that had been crossing a link and came back, waits that
    // ended on a route, and those whose lifetime ran out (stranded counts
    // them too).
    u64 waited = 0, returned = 0, rerouted = 0, expired = 0;
    // Messages lost with a link that went at a respawn (7.8): crossing it,
    // or queued at it with nowhere to wait.
    u64 unlinked = 0;
    // Messages crossing a bridge as its strait parted (11.6): lost.
    u64 parted = 0;
    // The current's blocks (12.6), which none of the counts above include:
    // those that reached the next place, their bytes, and those let go on
    // the way (a device or the next place gone dark, a link failed or
    // parted, or gone at a respawn).
    u64 drifted = 0, drifted_bytes = 0, adrift = 0;
};

inline constexpr u64 kNoStuck = ~0ull;

enum class SendFail : u8 { none, dark, no_route, full, too_big };

class Network {
  public:
    // Empty, and doing nothing, for a world with no map.
    bool init(const Map* map, const NetSettings& s, u64 seed, TextBuf* why);
    bool active() const { return map_ != nullptr && map_->placed(); }

    // The exit a message from s to d takes: one of s's link ends (2 x the
    // link + 1 if s is its b end), kLoop when d is s, or kNone: no route.
    static constexpr u32 kNone = ~0u, kLoop = ~0u - 1;
    u32 exit_end(u32 s, u32 d);
    // (Messages that came back from a failed link can put an end over its
    // queue's size: no room, then.)
    u32 end_room(u32 e) const { return ends_[e].queued >= cap_[e >> 1] ? 0 : cap_[e >> 1] - ends_[e].queued; }
    u64 lifetime() const { return lifetime_; }
    u32 end_bandwidth(u32 e) const { return map_->link(e >> 1).bandwidth; }
    u32 place_count() const { return n_; }
    u32 message_max() const { return max_; }
    u32 latency() const { return latency_; }
    u32 ping_size() const { return ping_size_; }
    const Map* map() const { return map_; }

    // A message from place s to port `port` at place d, joining s's queue
    // toward d now. Fails, sending nothing, for local reasons only (11.2):
    // s dark, no route, no room in its queue, or bigger than the limit.
    SendFail send(u32 s, u32 d, u32 port, const u8* bytes, u32 size, u64 sender, u64 now);
    // Room in s's queue toward d now, for a message of `size` bytes.
    SendFail can_send(u32 s, u32 d, u32 size);
    // An OS's SEND or PING from tick `sent`, at the barrier: it joins exit e
    // (exit_end's), whose room the OS kept for it. A PING's tags name the
    // thread waiting for its echo.
    void post(u32 s, u32 d, u32 e, MsgKind kind, u32 port, const u8* bytes, u32 size, u64 sender, u64 sent,
              u32 tag_t, u32 tag_id, u32 tag_seq);

    // The barrier's network step at tick `now` (net.h's order). dark[i] != 0
    // when place i's network device is off, and down[k] != 0 when data link
    // k is failed (nullptr: none is).
    void step(u64 now, const u8* dark, const u8* down = nullptr);
    // Messages waiting with their way on gone, now, and at place i's end of
    // link k, in bytes (they are among queued()'s).
    u32 waiting() const { return held_; }
    u32 waiting_at(u32 k, u32 from) const;
    // The map's data links changed (place `place` respawned, 7.8): `to[k]`
    // is where the map's link k of before is now, or kNoLink: gone. Between
    // barriers, at tick `now` (the next to run).
    void relink(const u32* to, u32 old_links, u32 place, u64 now);
    // After step(now): the stale columns the next barrier's arrivals will
    // ask for, made ready to work out now. The number of tasks to run them
    // in, at most `tasks` (ThreadPool::run, each calling route_task), or 0.
    u32 plan_routes(u64 now, u32 tasks);
    void route_task(u32 task) { routes_.run_batch(task); }
    // The messages delivered at the last step, in the order they arrived;
    // they're freed at the next.
    u32 delivered_count() const { return static_cast<u32>(delivered_.size()); }
    const Message& delivered(u32 i) const { return msgs_[delivered_[i]]; }
    const u8* data(const Message& m) const { return store_.data() + static_cast<usize>(&m - msgs_.data()) * max_; }

    // Bytes place i's device sent (at send()) or received (delivered there)
    // since the last call; forwarding is free (11.4).
    u64 take_bytes(u32 i);

    Routes& routes() { return routes_; }
    const NetCounts& counts() const { return counts_; }
    // The messages dropped at place i since the network was built or opened,
    // for the viewer (13.4): full, stranded and lost where the message was,
    // and cut at the dark place. For reports, like the counts: not saved.
    u64 dropped_at(u32 i) const { return i < dropped_.size() ? dropped_[i] : 0; }
    // Both back to nothing (the viewer's counters start again).
    void clear_counts();
    // Messages in queues and on links now.
    u32 in_flight() const { return live_ - static_cast<u32>(delivered_.size()); }
    // Messages part way across a link now: sent, not yet at its far end.
    u32 on_links() const;
    // Every message slot, live or not (Message::live), for the observers.
    u32 message_slots() const { return static_cast<u32>(msgs_.size()); }
    const Message& message_slot(u32 i) const { return msgs_[i]; }
    // Bytes queued at place i's end of link k, and that end's room.
    u32 queued(u32 k, u32 from) const;
    u32 capacity(u32 k) const { return cap_[k]; }

    // The network's state at the barrier, for snapshots and the world hash
    // (serialize.cpp; build-plan-gate3.md step 5): the link ends' queues and
    // what's on their links, each place's messages to itself, the messages
    // delivered at the last step, every message slot as it is (free ones
    // too, so they're reused in the same order), and which places were
    // dark. The routes follow from the dark places and are worked out again
    // as asked, and the counts are for reports, so neither is saved.
    void serialize(Archive& ar);
    // Its counters kept for reports, which a snapshot leaves out: saved beside one
    // for a test carried on (counters.cpp; build-plan-viewer.md step 9).
    void counters(Archive& ar);
    u64 hash();
    // The invariant check (the fuzz world runs it after every tick): every
    // live message is in one list only (a queue, a link, a place's messages
    // to itself, or the delivered), each list ends at its tail, an end is
    // sending the last on its link, the queues' bytes match, and the free
    // list holds every other slot. False, with the first broken in why.
    bool check(TextBuf* why) const;

  private:
    struct End {
        u32 head = ~0u, tail = ~0u; // its queue
        u32 wire_head = ~0u, wire_tail = ~0u; // sent, still crossing
        u32 queued = 0;    // bytes waiting
        u32 sending = ~0u; // the message it's sending, until free_at
        u64 free_at = 0;   // the barrier it may start the next
        u32 held_head = ~0u, held_tail = ~0u; // waiting, their way on gone (gate 5); their bytes are in queued
    };

    u32 end_of(u32 place, u32 j) const; // place's end of its j-th link
    u32 alloc(const u8* bytes, u32 size);
    void release(u32 m);
    void drop_all(End& e);
    bool adrift(u32 m); // m is the current's: let go, counted (true), where another would wait or be dropped
    void join(u32 e, u32 m); // to its queue's tail
    void loop_join(u32 place, u32 m, u64 arrive);
    void corrupt(Message& m, u32 link, Rng& r);
    // Message m is at place `at` (not dark) at the barrier `now`: delivered,
    // answered (a PING), or on toward its next hop.
    void reach(u32 m, u32 at, u64 now, u32 came);
    // Into at's queue toward its destination, or dropped; with a lifetime,
    // one with no route waits at the end it came in by (`came`, or ~0).
    void route_on(u32 m, u32 at, u64 now, u32 came);
    bool blocked(u32 e, const u8* dark, const u8* down) const;
    void hold(u32 e, u32 m, u64 now);      // to e's waiting messages' tail, taking its room
    void hold_queue(u32 e, u64 now);       // e's whole queue, likewise (its room is already theirs)
    void pull_back(u64 now, const u8* dark, const u8* down);
    void reroute(u64 now, const u8* dark, const u8* down);

    const Map* map_ = nullptr;
    u64 seed_ = 0;
    u32 n_ = 0, max_ = 0, latency_ = 1, ping_size_ = 8;
    u64 lifetime_ = 0;
    u32 queue_ticks_ = 8;      // net.queue and net.corruption, for the links a respawn makes
    double corruption_ = 0;
    void size_links();         // each link's queue size and chance of a flipped bit, from the map
    bool gate5_ = false; // a lifetime or failing links: the save carries the waiting messages and the failed links
    u32 held_ = 0;       // messages waiting
    Array<u8> down_;     // the failed links, as of the last barrier
    Routes routes_;
    Array<End> ends_;
    Array<u32> cap_;   // each link's queue, in bytes
    Array<u64> p64_;   // each link's chance of flipping a bit (noise.h's p64)
    Array<GapLog> plog_; // gap_log of each: the half of a gap's draw that a link's chance fixes
    Array<Message> msgs_;
    Array<u8> store_;  // each message's bytes, max_ a message
    Array<u32> free_;
    Array<u32> delivered_;
    Array<u64> bytes_;
    Array<u8> dark_;
    Array<u32> loop_head_, loop_tail_; // each place's messages to itself, by when they arrive
    Array<u32> ahead_;                 // plan_routes' destinations
    Array<u8> seen_;                   // and which it has, by place
    u64 seq_ = 0;
    u32 live_ = 0;
    NetCounts counts_;
    Array<u64> dropped_; // by place
};

} // namespace nova2
