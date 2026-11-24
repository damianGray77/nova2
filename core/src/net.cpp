#include "nova2/net.h"

#include <windows.h> // SRWLOCK, Interlocked*: the route cache, read by places in parallel

#include <cstring> // memcpy
#include <intrin.h> // __rdtsc: the routes' cost, for reports

#include "nova2/archive.h"
#include "nova2/hash.h"
#include "nova2/keys.h"
#include "nova2/noise.h" // to_p64, keep_gap_log, geometric_gap
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

constexpr u32 kNo = ~0u;
constexpr u16 kNoHop = 0xFFFF;

void heap_push(Array<u64>& h, u64 v) {
    h.push(v);
    usize i = h.size() - 1;
    while (i > 0) {
        const usize p = (i - 1) / 2;
        if (h[p] <= h[i])
            break;
        const u64 t = h[p];
        h[p] = h[i];
        h[i] = t;
        i = p;
    }
}

u64 heap_pop(Array<u64>& h) {
    const u64 top = h[0];
    h[0] = h.back();
    h.pop();
    usize i = 0;
    for (;;) {
        const usize l = 2 * i + 1, r = l + 1;
        usize m = i;
        if (l < h.size() && h[l] < h[m])
            m = l;
        if (r < h.size() && h[r] < h[m])
            m = r;
        if (m == i)
            break;
        const u64 t = h[m];
        h[m] = h[i];
        h[i] = t;
        i = m;
    }
    return top;
}

} // namespace

void read_net_settings(Config& cfg, NetSettings* ns) {
    ns->queue_ticks = key_u32(cfg, "net.queue");
    ns->corruption = key_f64(cfg, "net.corruption");
    ns->message_max = key_u32(cfg, "net.message_max");
    ns->latency = key_u32(cfg, "hw.network.latency");
    ns->ping_size = key_u32(cfg, "net.ping_size");
    ns->lifetime = key_u64(cfg, "net.lifetime");
}

// --- routes -------------------------------------------------------------------------

namespace {

constexpr u32 kFar32 = ~0u;
// A search over links no longer than this keeps a bucket for each distance
// in the window it holds (Dial's algorithm); over longer ones, a heap.
constexpr u32 kMostBuckets = 4096;

// Place u reached over one of its links (its index `back` in u's links) at
// distance du: its distance falls, or ties and this is the lower link (ties
// go to the lowest id, and u's links are in id order). True if it fell.
// Every neighbour on one of u's shortest paths relaxes it, so u ends with
// the lowest such link, as choosing among them all afterwards would.
inline bool relax(u32* dist, u16* col, u32 u, u32 du, u32 back) {
    if (du < dist[u]) {
        dist[u] = du;
        col[u] = static_cast<u16>(back);
        return true;
    }
    if (du == dist[u] && back < col[u])
        col[u] = static_cast<u16>(back);
    return false;
}

} // namespace

Routes::~Routes() {
    for (usize i = 0; i < cols_.size(); ++i)
        delete[] cols_[i];
    for (usize i = 0; i < scratch_.size(); ++i)
        delete scratch_[i];
    delete static_cast<SRWLOCK*>(lock_);
}

void Routes::init(const Map* map) {
    for (usize i = 0; i < cols_.size(); ++i)
        delete[] cols_[i];
    map_ = map;
    n_ = map->place_count();
    // The links laid out for the search, each with its index at its far
    // end, so a search reads one array a place.
    begin_.resize(0);
    begin_.resize(static_cast<usize>(n_) + 1);
    hops_.clear();
    max_delay_ = 0;
    for (u32 i = 0; i < n_; ++i) {
        begin_[i] = static_cast<u32>(hops_.size());
        for (u32 j = 0; j < map->degree(i); ++j) {
            const u32 k = map->link_at(i, j);
            Hop h;
            h.to = map->other(k, i);
            h.link = k;
            // A hop takes a tick or more (map.cpp; design.md 6.1), so every
            // step along a route is nearer its end and no route can circle,
            // whatever ties. A map made otherwise is searched the same way.
            h.delay = map->link(k).delay ? map->link(k).delay : 1;
            for (u32 b = 0; b < map->degree(h.to); ++b)
                if (map->link_at(h.to, b) == k) {
                    h.back = b;
                    break;
                }
            max_delay_ = h.delay > max_delay_ ? h.delay : max_delay_;
            hops_.push(h);
        }
    }
    begin_[n_] = static_cast<u32>(hops_.size());
    dark_.resize(0);
    dark_.resize(n_);
    down_.resize(0);
    down_.resize(map->link_count());
    cols_.resize(0);
    cols_.resize(n_);
    col_epoch_.resize(0);
    col_epoch_.resize(n_);
    epoch_ = 1;
    changes = columns = cycles = 0;
    if (!lock_) {
        SRWLOCK* l = new SRWLOCK;
        InitializeSRWLock(l);
        lock_ = l;
    }
}

bool Routes::set_dark(const u8* dark) {
    bool changed = false;
    for (u32 i = 0; i < n_; ++i)
        if ((dark[i] != 0) != (dark_[i] != 0)) {
            dark_[i] = dark[i] ? 1 : 0;
            changed = true;
        }
    if (changed) {
        ++epoch_;
        ++changes;
    }
    return changed;
}

void Routes::restore_dark(const u8* dark) {
    for (u32 i = 0; i < n_; ++i)
        dark_[i] = dark[i] ? 1 : 0;
    ++epoch_;
}

bool Routes::set_down(const u8* down) {
    bool changed = false;
    for (usize k = 0; k < down_.size(); ++k) {
        const u8 v = down && down[k] ? 1 : 0;
        if (v != down_[k]) {
            down_[k] = v;
            changed = true;
        }
    }
    if (changed) {
        ++epoch_;
        ++changes;
    }
    return changed;
}

void Routes::restore_down(const u8* down) {
    for (usize k = 0; k < down_.size(); ++k)
        down_[k] = down && down[k] ? 1 : 0;
    ++epoch_;
}

usize Routes::kept_bytes() const {
    usize n = 0;
    for (usize i = 0; i < cols_.size(); ++i)
        n += cols_[i] ? n_ * sizeof(u16) : 0;
    return n;
}

u32 Routes::next(u32 s, u32 d) {
    if (s >= n_ || d >= n_ || s == d || dark_[s] || dark_[d])
        return kNo;
    const u16 j = column(d)[s];
    return j == kNoHop ? kNo : j;
}

const u16* Routes::column(u32 d) {
    volatile LONG* e = reinterpret_cast<volatile LONG*>(&col_epoch_[d]);
    if (static_cast<u32>(InterlockedCompareExchange(e, 0, 0)) == epoch_)
        return cols_[d];
    // Stale: worked out here, outside the lock, so places asking for
    // different columns at once don't wait for each other. The first to
    // finish keeps its column, the same as any other's. Nobody reads a
    // stale column, so the old one can go.
    Scratch s;
    u16* fresh = new u16[n_];
    const u64 c0 = __rdtsc();
    work_out(d, fresh, s);
    const u64 spent = __rdtsc() - c0;
    SRWLOCK* l = static_cast<SRWLOCK*>(lock_);
    AcquireSRWLockExclusive(l);
    cycles += spent;
    if (col_epoch_[d] != epoch_) {
        delete[] cols_[d];
        cols_[d] = fresh;
        fresh = nullptr;
        ++columns;
        InterlockedExchange(e, static_cast<LONG>(epoch_));
    }
    ReleaseSRWLockExclusive(l);
    delete[] fresh;
    return cols_[d];
}

u32 Routes::begin_batch(const u32* ds, u32 count, u32 tasks) {
    // Each stale destination once, claimed as it's taken: nobody asks for a
    // column while the batch runs, and the batch fills each before it ends.
    batch_.clear();
    for (u32 i = 0; i < count; ++i)
        if (ds[i] < n_ && col_epoch_[ds[i]] != epoch_) {
            col_epoch_[ds[i]] = epoch_;
            batch_.push(ds[i]);
        }
    if (tasks == 0)
        for (usize i = 0; i < batch_.size(); ++i)
            col_epoch_[batch_[i]] = 0; // stale again: 0 is never an epoch
    if (batch_.empty() || tasks == 0)
        return 0;
    if (tasks > batch_.size())
        tasks = static_cast<u32>(batch_.size());
    while (scratch_.size() < tasks)
        scratch_.push(new Scratch);
    cursor_ = 0;
    return tasks;
}

void Routes::run_batch(u32 task) {
    Scratch& s = *scratch_[task];
    for (;;) {
        const u32 i = static_cast<u32>(InterlockedIncrement(&cursor_) - 1);
        if (i >= batch_.size())
            return;
        const u32 d = batch_[i];
        if (!cols_[d])
            cols_[d] = new u16[n_];
        const u64 c0 = __rdtsc();
        work_out(d, cols_[d], s);
        InterlockedExchangeAdd64(reinterpret_cast<volatile LONG64*>(&cycles), static_cast<LONG64>(__rdtsc() - c0));
        InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&columns));
    }
}

// Every place's next hop toward d: a shortest-path search from d over the
// places that aren't dark and the links that haven't failed, by link delay,
// each place taking its neighbour
// with the least delay + distance, ties to the lowest id, as the search
// relaxes its links (relax).
void Routes::work_out(u32 d, u16* col, Scratch& s) const {
    for (u32 i = 0; i < n_; ++i)
        col[i] = kNoHop;
    if (dark_[d])
        return;
    s.dist.resize(n_);
    s.dist.fill(kFar32);
    u32* dist = s.dist.data();
    dist[d] = 0;
    if (max_delay_ < kMostBuckets) {
        // A bucket for each distance in the window ahead, a list of the
        // places reached at it (a place can be in two, the later counting).
        const u32 width = max_delay_ + 1;
        s.head.resize(width);
        s.head.fill(kNo);
        s.entry_place.clear();
        s.entry_next.clear();
        s.entry_place.push(d);
        s.entry_next.push(kNo);
        s.head[0] = 0;
        u32 pending = 1;
        for (u32 at = 0; pending; ++at) {
            u32& h = s.head[at % width];
            while (h != kNo) {
                const u32 e = h;
                h = s.entry_next[e];
                --pending;
                const u32 v = s.entry_place[e];
                if (dist[v] != at)
                    continue; // reached sooner since
                for (u32 x = begin_[v]; x < begin_[v + 1]; ++x) {
                    const Hop& hp = hops_[x];
                    const u32 du = at + hp.delay;
                    if (dark_[hp.to] || down_[hp.link] || hp.to == d || du < at)
                        continue;
                    if (relax(dist, col, hp.to, du, hp.back)) {
                        u32& b = s.head[du % width];
                        s.entry_place.push(hp.to);
                        s.entry_next.push(b);
                        b = static_cast<u32>(s.entry_place.size() - 1);
                        ++pending;
                    }
                }
            }
        }
        return;
    }
    s.heap.clear();
    heap_push(s.heap, d);
    while (!s.heap.empty()) {
        const u64 top = heap_pop(s.heap);
        const u32 v = static_cast<u32>(top);
        const u32 dv = static_cast<u32>(top >> 32);
        if (dv != dist[v])
            continue;
        for (u32 x = begin_[v]; x < begin_[v + 1]; ++x) {
            const Hop& hp = hops_[x];
            const u32 du = dv + hp.delay;
            if (dark_[hp.to] || down_[hp.link] || hp.to == d || du < dv)
                continue;
            if (relax(dist, col, hp.to, du, hp.back))
                heap_push(s.heap, (static_cast<u64>(du) << 32) | hp.to);
        }
    }
}

// --- the network ------------------------------------------------------------------

bool Network::init(const Map* map, const NetSettings& s, u64 seed, TextBuf* why) {
    map_ = map;
    seed_ = seed;
    n_ = map->place_count();
    if (s.ping_size < 1 || s.ping_size > s.message_max) {
        if (why)
            why->append("net.ping_size: a PING must be a byte or more, and no bigger than the message limit");
        return false;
    }
    max_ = s.message_max;
    latency_ = s.latency ? s.latency : 1;
    ping_size_ = s.ping_size;
    lifetime_ = s.lifetime;
    gate5_ = s.lifetime > 0 || s.outages;
    held_ = 0;
    down_.resize(0);
    down_.resize(map->link_count());
    msgs_.clear();
    store_.clear();
    free_.clear();
    delivered_.clear();
    seq_ = 0;
    live_ = 0;
    counts_ = NetCounts{};
    dropped_.resize(0);
    dropped_.resize(n_);
    bytes_.resize(0);
    bytes_.resize(n_);
    dark_.resize(0);
    dark_.resize(n_);
    loop_head_.resize(n_);
    loop_tail_.resize(n_);
    loop_head_.fill(kNo);
    loop_tail_.fill(kNo);
    queue_ticks_ = s.queue_ticks;
    corruption_ = s.corruption;
    size_links();
    for (u32 i = 0; i < n_; ++i)
        if (map->degree(i) >= kNoHop) {
            if (why)
                why->append("the map: a place has too many links for the route tables");
            return false;
        }
    routes_.init(map);
    return true;
}

// Every link end empty, and each link's queue size and chance of a flipped
// bit, for the map as it stands.
void Network::size_links() {
    const Map* map = map_;
    const u32 E = map->link_count();
    ends_.resize(0);
    ends_.resize(static_cast<usize>(E) * 2);
    for (usize e = 0; e < ends_.size(); ++e)
        ends_[e] = End{};
    cap_.resize(E);
    p64_.resize(E);
    plog_.resize(E);
    for (u32 k = 0; k < E; ++k) {
        const MapLink& l = map->link(k);
        // At least one message of the limit, so no link is closed to one.
        u64 cap = static_cast<u64>(queue_ticks_) * l.bandwidth;
        cap = cap < max_ ? max_ : cap;
        cap_[k] = cap > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<u32>(cap);
        double p = corruption_ * l.length / map->mean_length();
        p = p > 0.5 ? 0.5 : p;
        p64_[k] = to_p64(p);
        plog_[k] = p64_[k] ? keep_gap_log(p64_[k]) : GapLog();
    }
}

// The map's links changed under it (net.h's head; 7.8).
void Network::relink(const u32* to, u32 old_links, u32 place, u64 now) {
    NOVA2_ASSERT(ends_.size() == static_cast<usize>(old_links) * 2);
    Array<End> old;
    old.assign(ends_.data(), ends_.size());
    Array<u8> old_down;
    old_down.assign(down_.data(), down_.size());
    size_links();
    down_.resize(0);
    down_.resize(map_->link_count());
    Array<u32> waited, queued; // what a gone link's ends held, in order
    for (u32 k = 0; k < old_links; ++k) {
        if (to[k] != kNoLink) {
            ends_[2 * to[k]] = old[2 * k];
            ends_[2 * to[k] + 1] = old[2 * k + 1];
            down_[to[k]] = k < old_down.size() ? old_down[k] : 0;
            continue;
        }
        for (u32 side = 0; side < 2; ++side) {
            const End& x = old[2 * k + side];
            for (u32 m = x.wire_head; m != kNo;) { // crossing it: lost with the wire
                const u32 nx = msgs_[m].next;
                if (!adrift(m)) {
                    ++dropped_[msgs_[m].at];
                    release(m);
                    ++counts_.unlinked;
                }
                m = nx;
            }
            for (u32 m = x.held_head; m != kNo; m = msgs_[m].next) {
                waited.push(m);
                --held_;
            }
            for (u32 m = x.head; m != kNo; m = msgs_[m].next)
                queued.push(m);
        }
    }
    // What a gone end held waits on at its place's first link: those that
    // were waiting first, with their clocks, then those that were queued.
    for (u32 pass = 0; pass < 2; ++pass) {
        const Array<u32>& list = pass ? queued : waited;
        for (usize i = 0; i < list.size(); ++i) {
            const u32 m = list[i];
            if (adrift(m))
                continue; // the current's never wait (12.6)
            Message& g = msgs_[m];
            const u32 at = g.at;
            if (lifetime_ == 0 || at == place || map_->degree(at) == 0) {
                ++dropped_[at];
                release(m);
                ++(at == place ? counts_.cut : counts_.unlinked);
                continue;
            }
            const u32 e = end_of(at, 0);
            if (pass) {
                hold(e, m, now);
                continue;
            }
            End& x = ends_[e];
            g.next = kNo;
            if (x.held_tail == kNo)
                x.held_head = m;
            else
                msgs_[x.held_tail].next = m;
            x.held_tail = m;
            x.queued += g.size;
            ++held_;
        }
    }
    // The torn-down place's messages to itself went with its device.
    for (u32 m = loop_head_[place]; m != kNo;) {
        const u32 nx = msgs_[m].next;
        ++dropped_[place];
        release(m);
        ++counts_.cut;
        m = nx;
    }
    loop_head_[place] = loop_tail_[place] = kNo;
    // The routes on the new map, from the dark places and failed links as
    // the last barrier left them; their counts go on.
    const u64 changes = routes_.changes, columns = routes_.columns, cycles = routes_.cycles;
    routes_.init(map_);
    routes_.restore_dark(dark_.data());
    routes_.restore_down(down_.data());
    routes_.changes = changes + 1;
    routes_.columns = columns;
    routes_.cycles = cycles;
}

u32 Network::end_of(u32 place, u32 j) const {
    const u32 k = map_->link_at(place, j);
    return 2 * k + (map_->link(k).a == place ? 0 : 1);
}

u32 Network::queued(u32 k, u32 from) const { return ends_[2 * k + (map_->link(k).a == from ? 0 : 1)].queued; }

u32 Network::waiting_at(u32 k, u32 from) const {
    u32 bytes = 0;
    for (u32 m = ends_[2 * k + (map_->link(k).a == from ? 0 : 1)].held_head; m != kNo; m = msgs_[m].next)
        bytes += msgs_[m].size;
    return bytes;
}

u32 Network::alloc(const u8* bytes, u32 size) {
    u32 m;
    if (!free_.empty()) {
        m = free_.back();
        free_.pop();
    } else {
        m = static_cast<u32>(msgs_.size());
        msgs_.push(Message{});
        // The bytes grow as the slots do, doubling, so a filling network
        // doesn't copy its whole store for each new slot (resize grows to
        // the size asked, exactly). The new slot starts zeroed.
        store_.resize_unfilled(static_cast<usize>(m + 1) * max_);
        memset(store_.data() + static_cast<usize>(m) * max_, 0, max_);
    }
    memcpy(store_.data() + static_cast<usize>(m) * max_, bytes, size);
    ++live_;
    return m;
}

void Network::release(u32 m) {
    msgs_[m].live = false;
    free_.push(m);
    --live_;
}

void Network::loop_join(u32 place, u32 m, u64 arrive) {
    msgs_[m].next = kNo;
    msgs_[m].arrive = arrive;
    if (loop_tail_[place] == kNo)
        loop_head_[place] = m;
    else
        msgs_[loop_tail_[place]].next = m;
    loop_tail_[place] = m;
}

void Network::join(u32 e, u32 m) {
    End& x = ends_[e];
    msgs_[m].next = kNo;
    if (x.tail == kNo)
        x.head = m;
    else
        msgs_[x.tail].next = m;
    x.tail = m;
    x.queued += msgs_[m].size;
}

// The current's blocks (12.6) are never held, and never counted with
// programs' messages.
bool Network::adrift(u32 m) {
    if (msgs_[m].kind != MsgKind::current)
        return false;
    release(m);
    ++counts_.adrift;
    return true;
}

void Network::drop_all(End& x) {
    for (u32 m = x.head; m != kNo;) {
        const u32 nx = msgs_[m].next;
        if (!adrift(m)) {
            ++dropped_[msgs_[m].at];
            release(m);
            ++counts_.cut;
        }
        m = nx;
    }
    for (u32 m = x.held_head; m != kNo;) { // and what waited there (gate 5)
        const u32 nx = msgs_[m].next;
        ++dropped_[msgs_[m].at];
        release(m);
        ++counts_.cut;
        --held_;
        m = nx;
    }
    x.head = x.tail = x.held_head = x.held_tail = kNo;
    x.queued = 0;
}

// Whether link end e can send now: not while its link is failed or the
// place at its far end is dark (11.6).
bool Network::blocked(u32 e, const u8* dark, const u8* down) const {
    const MapLink& l = map_->link(e >> 1);
    return dark[(e & 1) ? l.a : l.b] || (down && down[e >> 1]);
}

void Network::hold(u32 e, u32 m, u64 now) {
    End& x = ends_[e];
    Message& g = msgs_[m];
    g.next = kNo;
    g.stuck = now;
    if (x.held_tail == kNo)
        x.held_head = m;
    else
        msgs_[x.held_tail].next = m;
    x.held_tail = m;
    x.queued += g.size;
    ++held_;
    ++counts_.waited;
}

void Network::hold_queue(u32 e, u64 now) {
    End& x = ends_[e];
    for (u32 m = x.head; m != kNo;) {
        Message& g = msgs_[m];
        const u32 nx = g.next;
        if (g.kind == MsgKind::current) { // the current's never wait (12.6)
            x.queued -= g.size;
            adrift(m);
            m = nx;
            continue;
        }
        g.stuck = now;
        g.next = kNo;
        if (x.held_tail == kNo)
            x.held_head = m;
        else
            msgs_[x.held_tail].next = m;
        x.held_tail = m;
        ++held_;
        ++counts_.waited;
        m = nx;
    }
    x.head = x.tail = kNo;
}

// What's crossing from a blocked end comes back to it: the near end still
// held it (store and forward, 11.6). It waits there ahead of the end's
// queue, which joins the waiting messages later in the step. At a place
// whose own device is dark nothing is held (11.4).
void Network::pull_back(u64 now, const u8* dark, const u8* down) {
    for (u32 e = 0; e < static_cast<u32>(ends_.size()); ++e) {
        End& x = ends_[e];
        if (x.wire_head == kNo || !blocked(e, dark, down))
            continue;
        const MapLink& l = map_->link(e >> 1);
        const u32 from = (e & 1) ? l.b : l.a;
        const bool parted = down && down[e >> 1] == 2; // its strait has parted (links.h): what was crossing is lost
        for (u32 m = x.wire_head; m != kNo;) {
            const u32 nx = msgs_[m].next;
            if (adrift(m)) {
                // the current's: let go
            } else if (parted) {
                release(m);
                ++counts_.parted;
            } else if (dark[from]) {
                release(m);
                ++counts_.cut;
                ++dropped_[from];
            } else {
                hold(e, m, now);
                ++counts_.returned;
            }
            m = nx;
        }
        x.wire_head = x.wire_tail = kNo;
        x.sending = kNo;
        x.free_at = now;
    }
}

// Each place's waiting messages, in the order of its links and then their
// order at each, take the route that is there now (11.6): ahead of the
// barrier's arrivals, as room frees. One with no route waits, up to the
// lifetime from the barrier it found none.
void Network::reroute(u64 now, const u8* dark, const u8* down) {
    if (!held_)
        return;
    const Map& m = *map_;
    for (u32 p = 0; p < n_; ++p) {
        if (dark[p])
            continue;
        for (u32 j = 0; j < m.degree(p); ++j) {
            const u32 e = end_of(p, j);
            End& x = ends_[e];
            u32 prev = kNo;
            for (u32 id = x.held_head; id != kNo;) {
                Message& g = msgs_[id];
                const u32 nx = g.next;
                const u32 nj = routes_.next(p, g.dst);
                u32 ne = nj == kNo ? kNo : end_of(p, nj);
                if (ne != kNo && blocked(ne, dark, down))
                    ne = kNo; // its route's link or next place has gone since the routes were worked out
                bool gone = false;
                if (ne == kNo) {
                    if (g.stuck == kNoStuck)
                        g.stuck = now;
                    if (now - g.stuck >= lifetime_) {
                        x.queued -= g.size;
                        ++counts_.stranded;
                        ++counts_.expired;
                        ++dropped_[p];
                        release(id);
                        gone = true;
                    }
                } else if (ne == e || ends_[ne].queued + static_cast<u64>(g.size) <= cap_[ne >> 1]) {
                    // Onto its route: its own link, back (its room there is
                    // its already), or another's queue with room for it.
                    x.queued -= g.size;
                    g.stuck = kNoStuck;
                    join(ne, id);
                    ++counts_.rerouted;
                    gone = true;
                } else {
                    g.stuck = kNoStuck; // it has a route, and waits for room
                }
                if (gone) {
                    if (prev == kNo)
                        x.held_head = nx;
                    else
                        msgs_[prev].next = nx;
                    if (nx == kNo)
                        x.held_tail = prev;
                    --held_;
                } else {
                    prev = id;
                }
                id = nx;
            }
        }
    }
}

void Network::clear_counts() {
    counts_ = NetCounts{};
    dropped_.fill(0);
}

u32 Network::exit_end(u32 s, u32 d) {
    if (s >= n_ || d >= n_)
        return kNone;
    if (s == d)
        return kLoop;
    const u32 j = routes_.next(s, d);
    return j == kNo ? kNone : end_of(s, j);
}

SendFail Network::can_send(u32 s, u32 d, u32 size) {
    if (size > max_)
        return SendFail::too_big;
    if (s >= n_ || dark_[s])
        return SendFail::dark;
    const u32 e = exit_end(s, d);
    if (e == kNone)
        return SendFail::no_route;
    if (e == kLoop)
        return SendFail::none;
    return ends_[e].queued + static_cast<u64>(size) > cap_[e >> 1] ? SendFail::full : SendFail::none;
}

SendFail Network::send(u32 s, u32 d, u32 port, const u8* bytes, u32 size, u64 sender, u64 now) {
    const SendFail f = can_send(s, d, size);
    if (f != SendFail::none)
        return f;
    post(s, d, exit_end(s, d), MsgKind::data, port, bytes, size, sender, now, 0, 0, 0);
    return SendFail::none;
}

void Network::post(u32 s, u32 d, u32 e, MsgKind kind, u32 port, const u8* bytes, u32 size, u64 sender, u64 sent,
                   u32 tag_t, u32 tag_id, u32 tag_seq) {
    if (e != kLoop && (e >= ends_.size() || ends_[e].queued + static_cast<u64>(size) > cap_[e >> 1])) {
        ++counts_.full; // no room (cannot happen: the OS kept it)
        ++dropped_[s];
        return;
    }
    const u32 m = alloc(bytes, size);
    Message& g = msgs_[m];
    g = Message{};
    g.seq = seq_++;
    g.sender = sender;
    g.sent = sent;
    g.src = s;
    g.dst = d;
    g.port = port;
    g.size = size;
    g.at = s;
    g.kind = kind;
    g.tag_t = tag_t;
    g.tag_id = tag_id;
    g.tag_seq = tag_seq;
    g.live = true;
    if (e == kLoop) {
        loop_join(s, m, sent + latency_);
        ++counts_.looped;
    } else {
        join(e, m);
    }
    if (kind == MsgKind::data) {
        ++counts_.sent;
        counts_.sent_bytes += size;
    }
    bytes_[s] += size; // the sender's device draws for them; forwarding is free (11.4)
}

// Into place at's queue toward m's destination (or, for its own place, back
// round after the latency), or dropped: no route, or no room.
void Network::route_on(u32 m, u32 at, u64 now, u32 came) {
    Message& g = msgs_[m];
    if (g.dst == at) {
        loop_join(at, m, now + latency_);
        return;
    }
    const u32 nj = routes_.next(at, g.dst);
    if (nj == kNo) {
        // With a lifetime it waits here, in the room of the end it came in
        // by, if that has any (11.6).
        if (lifetime_ && came != kNo && ends_[came].queued + static_cast<u64>(g.size) <= cap_[came >> 1]) {
            hold(came, m, now);
            return;
        }
        ++counts_.stranded;
        ++dropped_[at];
        release(m);
        return;
    }
    const u32 ne = end_of(at, nj);
    if (ends_[ne].queued + static_cast<u64>(g.size) > cap_[ne >> 1]) {
        ++counts_.full;
        ++dropped_[at];
        release(m);
        return;
    }
    join(ne, m);
}

void Network::reach(u32 m, u32 at, u64 now, u32 came) {
    Message& g = msgs_[m];
    g.at = at;
    if (at != g.dst) {
        if (!adrift(m)) // (the current's go one link: it is never here)
            route_on(m, at, now, came);
        return;
    }
    bytes_[at] += g.size; // the receiver's device draws for them
    if (g.kind == MsgKind::current) {
        // A block of the current's (12.6): the world writes it into this
        // place's reset region.
        delivered_.push(m);
        ++counts_.drifted;
        counts_.drifted_bytes += g.size;
        return;
    }
    if (g.kind == MsgKind::ping) {
        // The destination's OS answers by itself (11.2): the request turns
        // round as its echo, and goes back by the same queues as anything.
        ++counts_.pings;
        g.kind = MsgKind::echo;
        g.dst = g.src;
        g.src = at;
        bytes_[at] += g.size;
        route_on(m, at, now, came);
        return;
    }
    delivered_.push(m);
    if (g.kind == MsgKind::echo) {
        ++counts_.echoes;
        return;
    }
    ++counts_.delivered;
    counts_.delivered_bytes += g.size;
}

void Network::corrupt(Message& g, u32 k, Rng& r) {
    const u64 p = p64_[k];
    const GapLog plog = plog_[k];
    if (!p || !g.size)
        return;
    const u64 bits = static_cast<u64>(g.size) * 8;
    u8* b = store_.data() + static_cast<usize>(&g - msgs_.data()) * max_;
    u64 pos = 0;
    bool any = false;
    for (;;) {
        // The gap-th bit on is the next to flip.
        const u64 gap = geometric_gap(p, plog, r.next());
        if (gap > bits - pos)
            break;
        pos += gap;
        b[(pos - 1) >> 3] ^= static_cast<u8>(1u << ((pos - 1) & 7));
        ++g.flips;
        ++counts_.flips;
        any = true;
    }
    counts_.corrupted += any ? 1 : 0;
}

void Network::step(u64 now, const u8* dark, const u8* down) {
    // The last step's deliveries are done with.
    for (usize i = 0; i < delivered_.size(); ++i)
        release(delivered_[i]);
    delivered_.clear();
    if (!active()) {
        routes_.set_dark(dark);
        memcpy(dark_.data(), dark, n_);
        return;
    }
    const Map& m = *map_;

    // 1. A dark network device drops what's queued at it, and the message
    //    it was sending (11.4).
    for (u32 p = 0; p < n_; ++p) {
        if (!dark[p])
            continue;
        for (u32 q = loop_head_[p]; q != kNo;) { // and its messages to itself
            const u32 nx = msgs_[q].next;
            release(q);
            ++counts_.cut;
            ++dropped_[p];
            q = nx;
        }
        loop_head_[p] = loop_tail_[p] = kNo;
        for (u32 j = 0; j < m.degree(p); ++j) {
            End& x = ends_[end_of(p, j)];
            drop_all(x);
            if (x.sending != kNo && x.free_at > now) {
                // It's the last on the link.
                u32 prev = kNo;
                for (u32 q = x.wire_head; q != x.sending; q = msgs_[q].next)
                    prev = q;
                if (prev == kNo)
                    x.wire_head = kNo;
                else
                    msgs_[prev].next = kNo;
                x.wire_tail = prev;
                if (!adrift(x.sending)) {
                    release(x.sending);
                    ++counts_.cut;
                    ++dropped_[p];
                }
            }
            x.sending = kNo;
        }
    }

    // With a lifetime (gate 5, 11.6): what's crossing from a blocked end
    // comes back to it, and the waiting messages take their new routes,
    // ahead of the arrivals.
    if (lifetime_) {
        pull_back(now, dark, down);
        reroute(now, dark, down);
    }

    // 2. The messages reaching their next place, by the place they came
    //    from: first its messages to itself, then those on its links, in
    //    their order on each.
    Rng r(seed_, Rng::kWorldPlace, kPurposeCorrupt, now);
    for (u32 q = 0; q < n_; ++q) {
        while (loop_head_[q] != kNo && msgs_[loop_head_[q]].arrive <= now) {
            const u32 id = loop_head_[q];
            loop_head_[q] = msgs_[id].next;
            if (loop_head_[q] == kNo)
                loop_tail_[q] = kNo;
            reach(id, q, now, kNo);
        }
        for (u32 j = 0; j < m.degree(q); ++j) {
            const u32 e = end_of(q, j), k = e >> 1;
            const u32 to = m.other(k, q);
            End& x = ends_[e];
            while (x.wire_head != kNo && msgs_[x.wire_head].arrive <= now) {
                const u32 id = x.wire_head;
                x.wire_head = msgs_[id].next;
                if (x.wire_head == kNo)
                    x.wire_tail = kNo;
                if (x.sending == id)
                    x.sending = kNo;
                Message& g = msgs_[id];
                ++g.hops;
                ++counts_.hops;
                g.at = to;
                if (dark[to] || (down && down[k])) {
                    if (adrift(id))
                        continue;
                    ++counts_.lost; // with no lifetime: a failed link's are lost as a dark place's are
                    ++dropped_[to];
                    release(id);
                    continue;
                }
                corrupt(g, k, r);
                reach(id, to, now, e ^ 1);
            }
        }
    }

    // 3. Each link end that's free starts sending its queue's head: its size
    //    over the bandwidth, rounded up, then the link's delay. One whose
    //    next hop is dark is dropped, stranded (11.5).
    for (u32 e = 0; e < static_cast<u32>(ends_.size()); ++e) {
        End& x = ends_[e];
        if (x.sending != kNo && x.free_at <= now)
            x.sending = kNo;
        const MapLink& l = m.link(e >> 1);
        const u32 to = (e & 1) ? l.a : l.b;
        const bool stop = dark[to] || (down && down[e >> 1]);
        if (lifetime_ && stop) {
            hold_queue(e, now); // its queue waits, for a new route or the old one back
            continue;
        }
        while (x.head != kNo && x.free_at <= now) {
            const u32 id = x.head;
            Message& g = msgs_[id];
            x.head = g.next;
            if (x.head == kNo)
                x.tail = kNo;
            x.queued -= g.size;
            if (stop) {
                if (adrift(id))
                    continue;
                ++counts_.stranded;
                ++dropped_[g.at];
                release(id);
                continue;
            }
            const u64 tx = (static_cast<u64>(g.size) + l.bandwidth - 1) / l.bandwidth;
            g.arrive = now + tx + l.delay;
            g.next = kNo;
            if (x.wire_tail == kNo)
                x.wire_head = id;
            else
                msgs_[x.wire_tail].next = id;
            x.wire_tail = id;
            x.free_at = now + tx;
            x.sending = tx ? id : kNo;
        }
    }

    // 4. The routes, for the next tick, if a network device went off or
    //    came on (11.3).
    routes_.set_dark(dark);
    routes_.set_down(down);
    memcpy(dark_.data(), dark, n_);
    for (usize k = 0; k < down_.size(); ++k)
        down_[k] = down && down[k] ? 1 : 0;
}

// The next barrier's arrivals ask for a column when they reach a place on
// their way (their destination's), and when a PING reaches its own (the
// sender's, for its echo). Messages queued, or to their own place, ask for
// none. Asked for at a dark place, a column isn't needed.
u32 Network::plan_routes(u64 now, u32 tasks) {
    ahead_.clear();
    if (!active() || tasks == 0)
        return 0;
    const Map& m = *map_;
    seen_.resize(n_);
    for (u32 e = 0; e < static_cast<u32>(ends_.size()); ++e) {
        const MapLink& l = m.link(e >> 1);
        const u32 to = (e & 1) ? l.a : l.b;
        if (routes_.dark(to))
            continue;
        for (u32 q = ends_[e].wire_head; q != kNo; q = msgs_[q].next) {
            const Message& g = msgs_[q];
            if (g.arrive > now + 1)
                continue;
            const u32 need = g.dst != to ? g.dst : g.kind == MsgKind::ping ? g.src : kNo;
            if (need == kNo || need == to || routes_.dark(need) || seen_[need])
                continue;
            seen_[need] = 1;
            ahead_.push(need);
        }
    }
    // And the waiting messages', which ask at the next barrier too.
    for (u32 e = 0; held_ && e < static_cast<u32>(ends_.size()); ++e) {
        const MapLink& l = m.link(e >> 1);
        const u32 from = (e & 1) ? l.b : l.a;
        if (routes_.dark(from))
            continue;
        for (u32 q = ends_[e].held_head; q != kNo; q = msgs_[q].next) {
            const u32 need = msgs_[q].dst;
            if (need == from || routes_.dark(need) || seen_[need])
                continue;
            seen_[need] = 1;
            ahead_.push(need);
        }
    }
    for (usize i = 0; i < ahead_.size(); ++i)
        seen_[ahead_[i]] = 0;
    return routes_.begin_batch(ahead_.data(), static_cast<u32>(ahead_.size()), tasks);
}

u32 Network::on_links() const {
    u32 n = 0;
    for (usize e = 0; e < ends_.size(); ++e)
        for (u32 m = ends_[e].wire_head; m != kNo; m = msgs_[m].next)
            ++n;
    return n;
}

u64 Network::take_bytes(u32 i) {
    const u64 b = bytes_[i];
    bytes_[i] = 0;
    return b;
}

u64 Network::hash() {
    ByteWriter w;
    Archive ar(&w);
    serialize(ar);
    return hash_bytes(w.data(), w.size(), 0x6E6574ull); // "net"
}

namespace {

bool net_broken(TextBuf* why, const char* what, u64 n) {
    if (why) {
        why->append(what);
        why->append_char(' ');
        why->append_u64(n);
    }
    return false;
}

// Message m found in a list: false if it can't be (no such slot, free, or
// already found in one).
bool take(const Array<Message>& msgs, Array<u8>& seen, u32 m) {
    if (m >= msgs.size() || !msgs[m].live || seen[m])
        return false;
    seen[m] = 1;
    return true;
}

// The list from head by next, each found; its last must be tail. Its bytes
// in *bytes.
bool walk(const Array<Message>& msgs, Array<u8>& seen, u32 head, u32 tail, u64* bytes) {
    u32 last = kNo;
    u64 b = 0;
    for (u32 m = head; m != kNo; m = msgs[m].next) {
        if (!take(msgs, seen, m))
            return false;
        b += msgs[m].size;
        last = m;
    }
    *bytes = b;
    return last == tail;
}

} // namespace

bool Network::check(TextBuf* why) const {
    const u32 slots = static_cast<u32>(msgs_.size());
    u32 live = 0;
    for (u32 i = 0; i < slots; ++i)
        live += msgs_[i].live ? 1 : 0;
    if (live != live_)
        return net_broken(why, "network check: live messages counted", live);
    Array<u8> seen;
    seen.resize(slots);
    u64 bytes = 0;
    u32 held = 0;
    for (u32 e = 0; e < static_cast<u32>(ends_.size()); ++e) {
        const End& x = ends_[e];
        u64 waiting = 0;
        if (!walk(msgs_, seen, x.held_head, x.held_tail, &waiting))
            return net_broken(why, "network check: a link end's waiting messages are broken at end", e);
        for (u32 m = x.held_head; m != kNo; m = msgs_[m].next) {
            ++held;
            if (msgs_[m].kind == MsgKind::current)
                return net_broken(why, "network check: a block of the current's is waiting at end", e);
        }
        if (!walk(msgs_, seen, x.head, x.tail, &bytes) || bytes + waiting != x.queued)
            return net_broken(why, "network check: a link end's queue is broken at end", e);
        if (!walk(msgs_, seen, x.wire_head, x.wire_tail, &bytes))
            return net_broken(why, "network check: a link is broken at end", e);
        if (x.sending != kNo && x.sending != x.wire_tail)
            return net_broken(why, "network check: an end is sending what isn't last on its link, at end", e);
    }
    if (held != held_ || (held && !lifetime_))
        return net_broken(why, "network check: waiting messages counted", held);
    for (u32 p = 0; p < static_cast<u32>(loop_head_.size()); ++p)
        if (!walk(msgs_, seen, loop_head_[p], loop_tail_[p], &bytes))
            return net_broken(why, "network check: messages to a place itself are broken at place", p);
    for (usize i = 0; i < delivered_.size(); ++i)
        if (!take(msgs_, seen, delivered_[i]))
            return net_broken(why, "network check: a delivered message isn't one", delivered_[i]);
    for (u32 i = 0; i < slots; ++i)
        if (msgs_[i].live && !seen[i])
            return net_broken(why, "network check: a live message is in no list:", i);
    if (free_.size() + live_ != slots)
        return net_broken(why, "network check: free slots", free_.size());
    for (usize i = 0; i < free_.size(); ++i) {
        const u32 m = free_[i];
        if (m >= slots || msgs_[m].live || seen[m])
            return net_broken(why, "network check: the free list is broken at", i);
        seen[m] = 2;
    }
    return true;
}

} // namespace nova2
