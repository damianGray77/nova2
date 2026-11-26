// Saving and loading each part of a place's state, and the network's
// (archive.h; build-plan-gate1.md step 9, build-plan-gate3.md step 5).
// Everything that can change during a run is here; what init builds from
// the settings (device layouts, size classes, rate tables, the map) is not.
// Counters kept for reports are not either: they're not part of the world,
// and must not change its hash.
#include <cstring> // memcmp, memcpy

#include "nova2/archive.h"
#include "nova2/memory.h"
#include "nova2/net.h"
#include "nova2/noise.h"
#include "nova2/os.h"
#include "nova2/region.h"
#include "nova2/rng.h"
#include "nova2/sched.h"

namespace nova2 {

namespace {

// The count of a table, which on load must match the one init built.
bool count(Archive& ar, usize n) {
    u64 c = n;
    ar.u64_(c);
    if (c != n) {
        ar.fail();
        return false;
    }
    return true;
}

// A table whose length changes as the place runs: its length, at most
// `most`, and on load the table resized to it.
template <class T> bool sized(Archive& ar, Array<T>& a, u64 most) {
    u64 n = a.size();
    ar.u64_(n);
    if (n > most) {
        ar.fail();
        return false;
    }
    if (ar.loading())
        a.resize(static_cast<usize>(n));
    return true;
}

} // namespace

void Memory::serialize_full(Archive& ar) {
    ar.tag(0x4D454D31); // "MEM1"
    ar.bytes(bytes_);
    ar.mark("memory: bytes");
    if (!count(ar, pages_.size()))
        return;
    for (usize i = 0; i < pages_.size(); ++i) {
        Page& p = pages_[i];
        ar.enum_(p.state);
        ar.enum_(p.perm);
        ar.u8_(p.cls);
        ar.u8_(p.device);
        ar.u16_(p.used);
        ar.u16_(p.slots);
        ar.u32_(p.span);
        ar.u32_(p.version);
    }
    ar.mark("memory: pages");
    ar.words(granule_);
    ar.mark("memory: granule map");
    if (!count(ar, allocs_.size()))
        return;
    for (usize i = 0; i < allocs_.size(); ++i) {
        Alloc& a = allocs_[i];
        ar.u32_(a.base);
        ar.u32_(a.size);
        ar.u32_(a.extent);
        ar.u32_(a.holder);
        ar.u32_(a.prev);
        ar.u32_(a.next);
        ar.u32_(a.wlo);
        ar.u32_(a.whi);
    }
    ar.mark("memory: allocation records");
    ar.words(head_);
    ar.mark("memory: holder lists");
    ar.quads(free_bits_);
    ar.quads(open_bits_);
    ar.words(dev_free_);
    ar.u32_(free_record_);
    ar.u32_(free_bytes_);
    ar.u32_(region_writes_);
    ar.mark("memory: bitmaps and counts");
}

// Rowhammer's window counts (9.8): the pages that have any, each with its
// count. The pages at or past the lowest threshold are listed again from
// them; between ticks each one's count is what the last tick's end saw.
void Memory::serialize_hammer(Archive& ar) {
    u32 n = 0;
    if (!ar.loading())
        for (usize p = 0; p < acts_.size(); ++p)
            n += acts_[p] ? 1 : 0;
    ar.u32_(n);
    if (!ar.loading()) {
        for (u32 p = 0; p < acts_.size(); ++p)
            if (acts_[p]) {
                u32 page = p, count = acts_[p];
                ar.u32_(page);
                ar.u32_(count);
            }
        return;
    }
    acts_.fill(0);
    for (u32 i = 0; i < n && ar.ok(); ++i) {
        u32 page = 0, count = 0;
        ar.u32_(page);
        ar.u32_(count);
        if (page >= acts_.size() || !count) {
            ar.fail();
            return;
        }
        acts_[page] = count;
    }
    hot_.resize(0);
    seen_.resize(0);
    for (u32 p = 0; hot_at_ && p < acts_.size(); ++p)
        if (acts_[p] >= hot_at_) {
            hot_.push(p);
            seen_.push(acts_[p]);
        }
}

void Memory::serialize(Archive& ar) {
    if (ar.compact())
        serialize_compact(ar);
    else
        serialize_full(ar);
    if (ar.loading())
        resum(); // the bitmaps' summaries, never saved (pass 13)
}

// What is in use, and no more (snapshot version 5). The bytes go whole:
// nothing is zeroed, so freed bytes are still the place's. Of the tables:
//
// - a page's slot counts and span go only where it has any;
// - the allocation records in use go, and of the free ones only their order,
//   which decides the record the next allocation takes. Free records are
//   empty but for their link, and the chain ends in the records never used
//   yet, in rising order, so the records freed since and where the unused
//   ones begin give all of it back;
// - the granule map, each holder's first record and the pages' bitmaps
//   follow from the records and the pages, and are worked out again.
//
// A world loaded from it goes on bit for bit as the one saved.
void Memory::serialize_compact(Archive& ar) {
    ar.tag(0x4D454D32); // "MEM2"
    ar.bytes(bytes_);
    ar.mark("memory: bytes");
    if (!count(ar, pages_.size()))
        return;
    for (usize i = 0; i < pages_.size(); ++i) {
        Page& p = pages_[i];
        ar.enum_(p.state);
        ar.enum_(p.perm);
        ar.u8_(p.cls);
        ar.u8_(p.device);
        ar.u32_(p.version);
        bool more = p.used || p.slots || p.span;
        ar.bool_(more);
        if (ar.loading()) {
            p.used = p.slots = 0;
            p.span = 0;
        }
        if (more) {
            ar.u16_(p.used);
            ar.u16_(p.slots);
            ar.u32_(p.span);
        }
        if (ar.loading() && (static_cast<u8>(p.state) > static_cast<u8>(PageState::span) ||
                             static_cast<u8>(p.perm) > 2 || p.cls >= classes_.size() || p.span >= allocs_.size())) {
            ar.fail();
            return;
        }
    }
    ar.mark("memory: pages");

    // The records. `high`: where the records never used begin (every record
    // from there on is free, and they are chained in rising order);
    // `listed`: the free records below it, in the chain's order.
    const u32 n = static_cast<u32>(allocs_.size());
    u32 high = n, listed = 0;
    if (!ar.loading()) {
        u32 links = 0, run_at = 0, run_id = 0, prev = 0;
        for (u32 id = free_record_; id; id = allocs_[id].next) {
            if (links == 0 || id != prev + 1) {
                run_at = links;
                run_id = id;
            }
            prev = id;
            if (++links >= n) {
                ar.fail(); // a chain that loops: check() would say so
                return;
            }
        }
        listed = links;
        if (links && prev == n - 1) {
            high = run_id;
            listed = run_at;
        }
    }
    ar.u32_(high);
    ar.u32_(listed);
    if (high < 1 || high > n || listed >= high) {
        ar.fail();
        return;
    }
    Array<u8> is_free;
    is_free.resize(high);
    Array<u32> chain;
    chain.resize(listed);
    if (!ar.loading()) {
        u32 id = free_record_;
        for (u32 i = 0; i < listed; ++i, id = allocs_[id].next)
            chain[i] = id;
    }
    for (u32 i = 0; i < listed; ++i) {
        ar.u32_(chain[i]);
        if (chain[i] < 1 || chain[i] >= high || is_free[chain[i]]) {
            ar.fail();
            return;
        }
        is_free[chain[i]] = 1;
    }
    if (ar.loading()) {
        for (u32 id = 0; id < n; ++id)
            allocs_[id] = Alloc{};
        granule_.fill(0);
        head_.fill(0);
    }
    for (u32 id = 1; id < high && ar.ok(); ++id) {
        if (is_free[id])
            continue;
        Alloc& a = allocs_[id];
        ar.u32_(a.base);
        ar.u32_(a.size);
        ar.u32_(a.extent);
        ar.u32_(a.holder);
        ar.u32_(a.prev);
        ar.u32_(a.next);
        ar.u32_(a.wlo);
        ar.u32_(a.whi);
        if (!ar.loading())
            continue;
        if (!ar.ok() || a.holder == 0 || a.holder >= head_.size() || a.extent == 0 || a.base % kGranule ||
            a.extent % kGranule || a.base >= total_ || a.extent > total_ - a.base || a.prev >= high ||
            a.next >= high) {
            ar.fail();
            return;
        }
        if (granule_[a.base / kGranule]) { // two records over the same bytes
            ar.fail();
            return;
        }
        map(a.base, a.extent, id);
        if (!a.prev) {
            if (head_[a.holder]) {
                ar.fail();
                return;
            }
            head_[a.holder] = id;
        }
    }
    if (ar.loading()) {
        // The free chain: the records freed since, then those never used.
        u32 next = high < n ? high : 0;
        for (u32 id = high; id < n; ++id)
            allocs_[id].next = id + 1 < n ? id + 1 : 0;
        for (u32 i = listed; i-- > 0;) {
            allocs_[chain[i]].next = next;
            next = chain[i];
        }
        free_record_ = next;
        // The bitmaps, from the pages.
        free_bits_.fill(0);
        open_bits_.fill(0);
        for (u32 p = 0; p < pages_.size(); ++p) {
            const Page& pg = pages_[p];
            if (pg.state == PageState::free)
                set_free(p, true);
            else if (pg.state == PageState::slab && pg.used < pg.slots)
                set_open(open_index(pg.cls, pg.perm), p, true);
        }
    }
    ar.mark("memory: allocation records");
    ar.words(dev_free_);
    ar.u32_(free_bytes_);
    ar.u32_(region_writes_);
    ar.mark("memory: bitmaps and counts");
}

namespace {

void program_fields(Archive& ar, Program& p) {
    ar.enum_(p.cause);
    ar.u32_(p.threads);
    ar.u32_(p.first_thread);
    ar.u64_(p.uid);
    ar.u64_(p.parent_uid);
    ar.u64_(p.born_tick);
}

// A thread's record but for its state, which goes first.
void thread_fields(Archive& ar, Thread& t) {
    ar.u8_(t.pin);
    ar.u8_(t.clock);
    ar.bool_(t.in_handler);
    ar.u32_(t.frame);
    ar.u32_(t.id);
    ar.u32_(t.program);
    ar.u32_(t.stack);
    ar.u32_(t.core);
    ar.u32_(t.prev);
    ar.u32_(t.next);
    ar.u64_(t.wake_tick);
    for (u32 k = 0; k < 4; ++k)
        ar.u32_(t.handler[k]);
    ar.u32_(t.data_port);
    ar.u32_(t.timer_period);
    ar.u64_(t.timer_next);
    ar.bool_(t.pend);
    ar.u32_(t.pend_a);
    ar.u32_(t.pend_b);
    ar.u8_(t.pending);
    ar.i64_(t.debt16);
    ar.u32_(t.recv_port);
    ar.u32_(t.recv_addr);
    ar.u32_(t.recv_max);
    ar.u64_(t.wait_seq);
    ar.u32_(t.ping_seq);
}

constexpr u32 kDiskPiece = 64; // the disk goes in pieces of this many bytes (version 5)

} // namespace

// Gate 5's state of 9.1's rules (build-plan-gate5.md step 10), each only
// when its rule is on: the killer's clock, and the watchdog's count of each
// live thread, in record order (the threads are loaded by then).
void Os::serialize_guards(Archive& ar) {
    if (s_.killer) {
        ar.tag(0x4F4F4D31); // "OOM1"
        ar.u64_(crowded_for_);
    }
    if (s_.watchdog) {
        ar.tag(0x57444731); // "WDG1"
        for (u32 t = 0; t < thread_top_; ++t)
            if (threads_[t].state != ThreadState::free)
                ar.i64_(threads_[t].quiet16);
    }
    ar.mark("os: the killer's clock and the watchdog's counts (gate 5)");
}

// The records every one, the ready queues' every slot and the disk whole:
// snapshot versions 3 and 4.
void Os::serialize_tables_full(Archive& ar) {
    if (!count(ar, programs_.size()))
        return;
    for (usize i = 0; i < programs_.size(); ++i) {
        ar.bool_(programs_[i].alive);
        program_fields(ar, programs_[i]);
    }
    ar.mark("os: program records");
    if (!count(ar, threads_.size()))
        return;
    for (usize i = 0; i < threads_.size(); ++i) {
        ar.enum_(threads_[i].state);
        thread_fields(ar, threads_[i]);
    }
    ar.mark("os: thread records");
    ar.u32_(next_thread_id_);
    ar.u64_(next_uid_);
    ar.u32_(live_programs_);
    ar.u32_(live_threads_);
    for (u32 q = 0; q < 3; ++q) {
        Ring& r = rq_[q];
        if (!count(ar, r.e.size()))
            return;
        for (usize i = 0; i < r.e.size(); ++i) {
            ar.u32_(r.e[i].t);
            ar.u32_(r.e[i].id);
            ar.u64_(r.e[i].seq);
        }
        ar.u32_(r.head);
        ar.u32_(r.size);
    }
    ar.u64_(rq_seq_);
    for (u32 q = 0; q < 3; ++q)
        ar.bool_(working_[q]);
    ar.mark("os: ready queues");
    ar.bytes(disk_);
    ar.mark("os: disk");
}

// What is in use (snapshot version 5):
//
// - the programs alive and the threads not free. A free thread's record is
//   an empty one, and a dead program's is never read again (the next
//   program to take it starts it afresh), so the rest load as empty; both
//   are taken lowest first, so nothing more fixes their order;
// - each ready queue's entries from its head on, in their places: the
//   slots outside are never read before they are written;
// - the disk's pieces that are no longer the static it began as, which the
//   seed gives back (note_static), with a bit a piece to say which.
void Os::serialize_tables_compact(Archive& ar) {
    u32 ptop = 0, ttop = 0;
    if (!ar.loading()) {
        for (u32 p = 0; p < programs_.size(); ++p)
            ptop = programs_[p].alive ? p + 1 : ptop;
        for (u32 t = 0; t < threads_.size(); ++t)
            ttop = threads_[t].state != ThreadState::free ? t + 1 : ttop;
    }
    if (!count(ar, programs_.size()))
        return;
    ar.u32_(ptop);
    if (ptop > programs_.size()) {
        ar.fail();
        return;
    }
    for (u32 p = 0; p < programs_.size(); ++p) {
        Program& pr = programs_[p];
        bool alive = pr.alive;
        if (p < ptop)
            ar.bool_(alive);
        if (ar.loading()) {
            pr = Program{};
            pr.alive = p < ptop && alive;
        }
        if (p < ptop && alive)
            program_fields(ar, pr);
    }
    ar.mark("os: program records");
    if (!count(ar, threads_.size()))
        return;
    ar.u32_(ttop);
    if (ttop > threads_.size()) {
        ar.fail();
        return;
    }
    for (u32 t = 0; t < threads_.size(); ++t) {
        Thread& th = threads_[t];
        if (ar.loading())
            th = Thread{};
        if (t >= ttop)
            continue;
        ar.enum_(th.state);
        if (static_cast<u8>(th.state) > static_cast<u8>(ThreadState::waiting_ping)) {
            ar.fail();
            return;
        }
        if (th.state != ThreadState::free)
            thread_fields(ar, th);
    }
    ar.mark("os: thread records");
    ar.u32_(next_thread_id_);
    ar.u64_(next_uid_);
    ar.u32_(live_programs_);
    ar.u32_(live_threads_);
    for (u32 q = 0; q < 3; ++q) {
        Ring& r = rq_[q];
        const u32 cap = static_cast<u32>(r.e.size());
        if (!count(ar, cap))
            return;
        ar.u32_(r.head);
        ar.u32_(r.size);
        if (r.size > cap || (cap && r.head >= cap) || (!cap && r.head)) {
            ar.fail();
            return;
        }
        if (ar.loading())
            for (u32 i = 0; i < cap; ++i)
                r.e[i] = ReadyEntry{};
        for (u32 k = 0; k < r.size; ++k) {
            ReadyEntry& e = r.e[(r.head + k) % cap];
            ar.u32_(e.t);
            ar.u32_(e.id);
            ar.u64_(e.seq);
        }
    }
    ar.u64_(rq_seq_);
    for (u32 q = 0; q < 3; ++q)
        ar.bool_(working_[q]);
    ar.mark("os: ready queues");
    // The disk. With no static to set it against (an OS built alone, in a
    // test), every piece goes.
    const u32 size = static_cast<u32>(disk_.size());
    if (!count(ar, size))
        return;
    bool known = static_known_;
    ar.bool_(known);
    if (known != static_known_) {
        ar.fail();
        return;
    }
    const u32 pieces = (size + kDiskPiece - 1) / kDiskPiece;
    Array<u8> changed;
    changed.resize((pieces + 7) / 8);
    u8 was[kDiskPiece];
    const Rng draw = Rng::from_base(static_base_);
    for (u32 pass = ar.loading() ? 1 : 0; pass < 2; ++pass) { // which pieces, then the pieces
        if (pass == 1)
            ar.bytes(changed);
        for (u32 i = 0; i < pieces; ++i) {
            const u32 at = i * kDiskPiece;
            const u32 len = size - at < kDiskPiece ? size - at : kDiskPiece;
            if (known)
                for (u32 b = 0; b < len; b += 8) {
                    const u64 v = draw.at(static_first_ + (at + b) / 8);
                    for (u32 k = 0; k < 8 && b + k < len; ++k)
                        was[b + k] = static_cast<u8>(v >> (8 * k));
                }
            if (pass == 0) {
                if (!ar.loading() && (!known || memcmp(disk_.data() + at, was, len) != 0))
                    changed[i >> 3] |= static_cast<u8>(1u << (i & 7));
                continue;
            }
            if ((changed[i >> 3] >> (i & 7)) & 1)
                ar.plain_u8(disk_.data() + at, len);
            else if (ar.loading() && known)
                memcpy(disk_.data() + at, was, len);
            else if (ar.loading())
                ar.fail(); // nothing to make the piece from
        }
    }
    ar.mark("os: disk");
}

void Os::serialize(Archive& ar) {
    if (ar.compact()) {
        ar.tag(0x4F533036); // "OS06": version 5's records, ready queues and disk, what is in use
        serialize_tables_compact(ar);
    } else {
        ar.tag(0x4F533035); // "OS05": gate 3 adds RECV's and PING's waits, the outbox, the inboxes and neighbour SENSEs
        serialize_tables_full(ar);
    }
    if (!ar.ok())
        return;
    ar.u64_(boot_writer_);
    // Taken at the barrier: the disk requests still waiting are the ones the
    // disk was cut out for (7.6). Each thread has at most one, and each idle run.
    const u64 most = threads_.size() + core_count_;
    if (!sized(ar, ops_, most))
        return;
    for (usize i = 0; i < ops_.size(); ++i) {
        DiskOp& o = ops_[i];
        ar.u32_(o.core);
        ar.u32_(o.thread);
        ar.u32_(o.thread_id);
        ar.u32_(o.holder);
        ar.bool_(o.write);
        ar.bool_(o.live);
        ar.u32_(o.offset);
        ar.u32_(o.addr);
        ar.u32_(o.len);
        ar.u64_(o.seq);
        if (o.core >= core_count_ || o.thread > threads_.size() || o.offset >= disk_.size() ||
            o.len > disk_.size() - o.offset)
            ar.fail();
    }
    ar.u64_(op_seq_);
    // The last barrier's I/O, in flight until the next tick's device states.
    if (!sized(ar, flight_, most))
        return;
    u64 old = 0;
    for (usize i = 0; i < flight_.size(); ++i) {
        Flight& f = flight_[i];
        ar.u32_(f.core);
        ar.u32_(f.thread);
        ar.u32_(f.thread_id);
        ar.bool_(f.write);
        ar.u32_(f.offset);
        ar.u32_(f.n);
        ar.u32_(f.old_at);
        ar.bool_(f.boot);
        ar.u64_(f.writer_before);
        // old_at is the write's offset in flight_old_ to 32 bits: past 4 GiB in
        // one barrier it has wrapped, and Os::tear doesn't use it (2026-11-25).
        if (f.core >= core_count_ || f.thread > threads_.size() || f.offset >= disk_.size() ||
            f.n > disk_.size() - f.offset || (f.write && f.old_at != static_cast<u32>(old)))
            ar.fail();
        old += f.write ? f.n : 0;
    }
    if (!sized(ar, flight_old_, old) || flight_old_.size() != old) {
        ar.fail();
        return;
    }
    ar.bytes(flight_old_);
    // Gate 3's (build-plan-gate3.md step 4). The SENDs and PINGs let go
    // this tick, end to end in their bytes: the barrier takes them, so a
    // snapshot finds none, but they go whole all the same.
    if (!sized(ar, outbox_, most))
        return;
    u64 out = 0;
    for (usize i = 0; i < outbox_.size(); ++i) {
        OutMsg& o = outbox_[i];
        ar.u32_(o.dst);
        ar.u32_(o.port);
        ar.u32_(o.end);
        ar.u32_(o.size);
        ar.u32_(o.at);
        ar.bool_(o.ping);
        ar.u64_(o.sender);
        ar.u64_(o.sent);
        ar.u32_(o.tag_t);
        ar.u32_(o.tag_id);
        ar.u32_(o.tag_seq);
        if (o.at != out)
            ar.fail();
        out += o.size;
    }
    if (!sized(ar, outbox_bytes_, out) || outbox_bytes_.size() != out) {
        ar.fail();
        return;
    }
    ar.bytes(outbox_bytes_);
    // The comms ports' inboxes, in the order they joined, their bytes end to
    // end (11.5).
    if (!sized(ar, inbox_, static_cast<u64>(s_.ports) * s_.inbox_bytes))
        return;
    u64 in = 0;
    for (usize i = 0; i < inbox_.size(); ++i) {
        InMsg& m = inbox_[i];
        ar.u32_(m.port);
        ar.u32_(m.from);
        ar.u32_(m.len);
        ar.u32_(m.at);
        if (ar.loading())
            m.sender = 0; // not the world's: the counters bring it back (Os::counters5)
        if (m.at != in || m.port == 0 || m.port >= s_.ports)
            ar.fail();
        in += m.len;
    }
    if (!sized(ar, inbox_store_, in) || inbox_store_.size() != in) {
        ar.fail();
        return;
    }
    ar.bytes(inbox_store_);
    ar.words(inbox_used_);
    // The neighbour SENSEs waiting for the barrier their requests land at
    // (A.4): at most one for each thread and each idle run.
    if (!sized(ar, asks_, most))
        return;
    for (usize i = 0; i < asks_.size(); ++i) {
        Ask& a = asks_[i];
        ar.u32_(a.core);
        ar.u32_(a.thread);
        ar.u32_(a.thread_id);
        ar.u32_(a.neighbour);
        ar.u32_(a.which);
        ar.u64_(a.lands);
        if (a.core >= core_count_ || a.thread > threads_.size())
            ar.fail();
    }
    ar.u64_(wait_seq_);
    ar.mark("os: disk requests, messages, inboxes");
    if (ar.loading())
        reindex(); // what's kept beside the records follows from them
}

// The network's state at the barrier (net.h).
void Network::serialize(Archive& ar) {
    ar.tag(0x4E455431); // "NET1": gate 3's network
    if (!count(ar, ends_.size()))
        return;
    for (usize i = 0; i < ends_.size(); ++i) {
        End& e = ends_[i];
        ar.u32_(e.head);
        ar.u32_(e.tail);
        ar.u32_(e.wire_head);
        ar.u32_(e.wire_tail);
        ar.u32_(e.queued);
        ar.u32_(e.sending);
        ar.u64_(e.free_at);
    }
    ar.mark("network: link ends");
    // Every slot: a free one is only that; a live one goes whole, with its
    // bytes.
    u64 slots = msgs_.size();
    ar.u64_(slots);
    if (slots > (1u << 24)) {
        ar.fail();
        return;
    }
    if (ar.loading()) {
        msgs_.resize(0);
        msgs_.resize(static_cast<usize>(slots));
        store_.resize(0);
        store_.resize(static_cast<usize>(slots) * max_);
    }
    for (usize i = 0; i < msgs_.size() && ar.ok(); ++i) {
        Message& g = msgs_[i];
        ar.bool_(g.live);
        if (!g.live)
            continue;
        ar.u64_(g.seq);
        ar.u64_(g.sender);
        ar.u64_(g.sent);
        ar.u64_(g.arrive);
        ar.u32_(g.src);
        ar.u32_(g.dst);
        ar.u32_(g.port);
        ar.u32_(g.size);
        ar.u32_(g.at);
        ar.u32_(g.hops);
        ar.u32_(g.flips);
        ar.u32_(g.next);
        ar.enum_(g.kind);
        ar.u32_(g.tag_t);
        ar.u32_(g.tag_id);
        ar.u32_(g.tag_seq);
        if (g.size > max_ || g.src >= n_ || g.dst >= n_ || g.at >= n_ ||
            static_cast<u8>(g.kind) > static_cast<u8>(MsgKind::current)) {
            ar.fail();
            return;
        }
        ar.raw_u8(store_.data() + i * max_, g.size);
    }
    ar.mark("network: message slots");
    if (!sized(ar, free_, slots))
        return;
    ar.words(free_);
    if (!sized(ar, delivered_, slots))
        return;
    ar.words(delivered_);
    ar.quads(bytes_);
    ar.bytes(dark_);
    ar.words(loop_head_);
    ar.words(loop_tail_);
    ar.u64_(seq_);
    ar.u32_(live_);
    ar.mark("network: other");
    // Gate 5's (build-plan-gate5.md step 5), in a world with a message
    // lifetime or links that can fail: each end's waiting messages, when
    // each live message found no way on, and the failed links as of the
    // last barrier. Without either the bytes are gate 3's.
    if (gate5_) {
        ar.tag(0x4E455432); // "NET2"
        for (usize i = 0; i < ends_.size(); ++i) {
            ar.u32_(ends_[i].held_head);
            ar.u32_(ends_[i].held_tail);
        }
        for (usize i = 0; i < msgs_.size(); ++i)
            if (msgs_[i].live)
                ar.u64_(msgs_[i].stuck);
        ar.bytes(down_);
        ar.mark("network: waiting messages and failed links (gate 5)");
    }
    if (ar.loading() && ar.ok()) {
        held_ = 0;
        for (usize i = 0; i < ends_.size(); ++i)
            for (u32 m = ends_[i].held_head, left = static_cast<u32>(msgs_.size()); m != ~0u && left; --left) {
                if (m >= msgs_.size()) {
                    ar.fail();
                    return;
                }
                ++held_;
                m = msgs_[m].next;
            }
        routes_.restore_down(down_.data());
        routes_.restore_dark(dark_.data());
        if (!check(nullptr))
            ar.fail();
    }
}

void Scheduler::serialize(Archive& ar) {
    ar.tag(0x53434831); // "SCH1"
    ar.words(turn_left_);
    ar.bytes(last_clock_);
    ar.u64_(rounds_);
    ar.mark("scheduler");
}

void Region::serialize(Archive& ar) {
    ar.tag(0x52454733); // "REG3": gate 2 adds port 0's queue, gate 3 the arrivals' landed bytes
    if (!count(ar, runs_.size()))
        return;
    for (usize i = 0; i < runs_.size(); ++i) {
        IdleRun& r = runs_[i];
        ar.enum_(r.phase);
        ar.u32_(r.spot);
        ar.i64_(r.used16);
        ar.i64_(r.debt16);
        ar.u64_(r.halted_at);
        ar.u32_(r.seen_writes);
        ar.u32_(r.started_at);
    }
    ar.mark("region: idle runs");
    // Port 0's queue: each arrival is a byte or more, and their bytes lie end
    // to end in order.
    if (!sized(ar, queue_, s_.queue_bytes))
        return;
    u64 at = 0;
    for (usize i = 0; i < queue_.size(); ++i) {
        Arrival& a = queue_[i];
        ar.u32_(a.at);
        ar.u32_(a.len);
        ar.enum_(a.cause);
        ar.u64_(a.sender);
        ar.bool_(a.boot);
        if (a.at != at || a.len == 0)
            ar.fail();
        at += a.len;
    }
    if (!sized(ar, queue_bytes_, s_.queue_bytes) || queue_bytes_.size() != at) {
        ar.fail();
        return;
    }
    ar.bytes(queue_bytes_);
    ar.mark("region: port 0 queue");
    // The arrivals' landed bytes (13.2), each in the region and from a
    // sender (Region::check finds any overlapping).
    if (!sized(ar, landings_, kLandings))
        return;
    for (usize i = 0; i < landings_.size(); ++i) {
        Landing& l = landings_[i];
        ar.u32_(l.at);
        ar.u32_(l.len);
        ar.u64_(l.sender);
        ar.u64_(l.hash);
        if (l.len == 0 || l.at >= mem_->region_size() || l.len > mem_->region_size() - l.at || !l.sender)
            ar.fail();
    }
    ar.mark("region: landed bytes");
}

void Noise::serialize(Archive& ar) {
    ar.tag(0x4E4F4932); // "NOI2": gate 2 adds edge errors and fading
    ar.quads(cosmic_left_);
    if (!count(ar, cores_n_.size()))
        return;
    for (usize i = 0; i < cores_n_.size(); ++i) {
        ar.u64_(cores_n_[i].tick_seen);
        ar.u64_(cores_n_[i].count);
        ar.u64_(cores_n_[i].edge);
    }
    ar.quads(mem_rate_);
    ar.quads(mem_left_);
    if (!count(ar, fade_.size()))
        return;
    for (usize d = 0; d < fade_.size(); ++d) {
        ar.u64_(fade_[d].dose);
        ar.u64_(fade_[d].done);
        if (fade_[d].dose > dose_cap_ || fade_[d].done > fade_[d].cells)
            ar.fail();
    }
    ar.mark("noise");
}

void Noise::serialize_wear(Archive& ar) { ar.quads(weak_left_); }

} // namespace nova2
