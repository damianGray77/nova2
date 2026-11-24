// The disk's blocks (design.md 7.4, 7.6; build-plan-gate5.md step 3): each
// block's writes against its drawn endurance, the flips of a write to a worn
// block, wear levelling, and a failed disk. Os's, beside the raw disk's calls
// in os.cpp.
#include <cmath>
#include <cstring>
#include <intrin.h> // _umul128, _udiv128

#include "nova2/archive.h"
#include "nova2/noise.h" // to_p64, keep_gap_log, geometric_gap
#include "nova2/os.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

// a × b ÷ c with a 128-bit product, saturating if the quotient won't fit.
u64 times_over(u64 a, u64 b, u64 c) {
    u64 hi;
    const u64 lo = _umul128(a, b, &hi);
    if (hi >= c)
        return ~0ull;
    u64 rem;
    return _udiv128(hi, lo, c, &rem);
}

constexpr u64 kHalf = 1ull << 63; // a chance of one in two: a bit written at random

} // namespace

// A new disk's blocks: no writes, each block's endurance drawn evenly on a
// log scale from half to twice the disk's value, in whole writes and at
// least one, by the seed, the place, the disk's number (a replaced disk
// draws again, 7.8) and the block.
void Os::init_blocks(u64 seed, u32 fresh) {
    block_seed_ = seed;
    disk_fresh_ = fresh;
    disk_failed_ = false;
    flips_ = FlipDraw{};
    const u32 size = disk_size();
    block_count_ = 0;
    block_writes_.resize(0);
    block_life_.resize(0);
    block_map_.resize(0);
    block_holds_.resize(0);
    least_ = 0;
    if (!s_.block_endurance || !size)
        return; // blocks don't wear: no counts are kept
    block_count_ = (size + s_.block - 1) / s_.block;
    block_writes_.resize(block_count_);
    block_life_.resize(block_count_);
    flip_p64_ = to_p64(s_.worn_flip);
    draw_block_lives();
    if (s_.levelling) {
        block_map_.resize(block_count_);
        block_holds_.resize(block_count_);
        for (u32 b = 0; b < block_count_; ++b)
            block_map_[b] = block_holds_[b] = b;
    }
}

void Os::draw_block_lives() {
    const Rng draw(block_seed_, env_->place_id, kPurposeBlocks, Rng::kInitTick);
    const u64 first = static_cast<u64>(disk_fresh_) * block_count_;
    for (u32 b = 0; b < block_count_; ++b) {
        const double u = static_cast<double>(draw.at(first + b) >> 11) * 0x1.0p-53;
        const double life = floor(static_cast<double>(s_.block_endurance) * pow(2.0, 2.0 * u - 1.0) + 0.5);
        block_life_[b] = life < 1.0 ? 1u : (life > 4e9 ? 4000000000u : static_cast<u32>(life));
    }
}

bool Os::boot_worn() const {
    if (!block_count_)
        return false;
    const u32 p = block_holding(0);
    return block_writes_[p] > block_life_[p];
}

// One write to physical block `p`, of the disk's bytes [lo, hi), which it
// holds. Past its endurance each bit written lands wrong with a chance of
// the set one × its writes past ÷ the disk's endurance, at most one in two.
void Os::write_block(u32 p, u32 lo, u32 hi) {
    if (block_writes_[p] != ~0u)
        ++block_writes_[p];
    ++block_writes_all;
    if (block_writes_[p] <= block_life_[p])
        return;
    ++worn_writes;
    u64 p64 = times_over(flip_p64_, block_writes_[p] - block_life_[p], s_.block_endurance);
    if (p64 > kHalf)
        p64 = kHalf;
    if (!p64)
        return;
    // The tick's draws, one stream for the place, in the order of its writes.
    if (!flips_.begun || flips_.tick != env_->tick) {
        flips_.begun = true;
        flips_.tick = env_->tick;
        flips_.count = 0;
        flips_.base = Rng::stream_base(block_seed_, env_->place_id, kPurposeWornBlock, env_->tick);
    }
    const Rng r = Rng::from_base(flips_.base);
    const GapLog log = keep_gap_log(p64);
    const u64 bits = static_cast<u64>(hi - lo) * 8;
    for (u64 at = geometric_gap(p64, log, r.at(flips_.count++)) - 1; at < bits;
         at += geometric_gap(p64, log, r.at(flips_.count++))) {
        disk_[lo + static_cast<u32>(at >> 3)] ^= static_cast<u8>(1u << (at & 7));
        ++worn_flips;
    }
}

// The lowest-numbered of the least-written blocks.
void Os::find_least() {
    u32 best = 0;
    for (u32 p = 1; p < block_count_; ++p)
        if (block_writes_[p] < block_writes_[best])
            best = p;
    least_ = best;
}

// A write of `n` bytes at `offset`, just landed: a write to each block it
// touches. With levelling, a block whose writes have passed the least-written
// block's by the gap swaps contents and places with it: two more writes, the
// OS's own, at no thread's cost (7.4).
void Os::wear_blocks(u32 offset, u32 n) {
    if (!block_count_ || !n)
        return;
    const u32 size = disk_size();
    const u32 end = offset + n;
    for (u32 b = offset / s_.block; b < block_count_ && b * s_.block < end; ++b) {
        const u32 start = b * s_.block;
        const u32 lo = offset > start ? offset : start;
        const u32 stop = start + s_.block < size ? start + s_.block : size;
        const u32 hi = end < stop ? end : stop;
        const u32 p = block_holding(b);
        write_block(p, lo, hi);
        if (!s_.levelling)
            continue;
        if (p == least_)
            find_least();
        const u32 q = least_;
        if (block_writes_[p] <= block_writes_[q] || block_writes_[p] - block_writes_[q] <= s_.levelling_gap)
            continue;
        // The swap: b's contents go to q, and what q held to p.
        const u32 c = block_holds_[q];
        block_map_[b] = q;
        block_holds_[q] = b;
        block_map_[c] = p;
        block_holds_[p] = c;
        const u32 cs = c * s_.block;
        write_block(q, start, stop);
        write_block(p, cs, cs + s_.block < size ? cs + s_.block : size);
        ++level_swaps;
        find_least();
    }
}

// The disk failed (7.6): its data is lost, the boot block with it, and
// DREAD and DWRITE on its offsets fail soft: those waiting answer 0 now.
void Os::fail_disk() {
    disk_failed_ = true;
    disk_on_ = false;
    boot_writer_ = 0;
    flight_.clear();
    flight_old_.clear();
    u8* mem = mem_->bytes();
    for (u32 i = 0; i < ops_.size(); ++i) {
        const DiskOp& op = ops_[i];
        if (!op.live)
            continue;
        if (op.thread) {
            const Thread& th = threads_[op.thread - 1];
            if (th.state != ThreadState::waiting_disk || th.id != op.thread_id)
                continue;
            memset(mem + th.stack, 0, 4); // the saved A: nothing moved
            make_ready(op.thread - 1);
        } else if (cores_[op.core].disk_wait) {
            cores_[op.core].x.r.a = 0;
            cores_[op.core].disk_wait = false;
        }
    }
    ops_.clear();
}

// Tests' (R7): a block's writes set by hand.
void Os::set_block_writes(u32 p, u32 writes) {
    if (p >= block_count_)
        return;
    block_writes_[p] = writes;
    if (s_.levelling)
        find_least();
}

// A place's blocks in a save (version 6), when blocks wear: the disk's
// number, each written block's writes, and with levelling each block that
// has moved. The lives are the seed's again.
void Os::serialize_blocks(Archive& ar) {
    ar.tag(0x4F534231); // "OSB1"
    ar.u32_(disk_fresh_);
    u32 count = block_count_;
    ar.u32_(count);
    if (count != block_count_) {
        ar.fail();
        return;
    }
    for (u32 pass = 0; pass < (s_.levelling ? 2u : 1u); ++pass) {
        Array<u32>& a = pass ? block_map_ : block_writes_;
        u32 n = 0;
        for (u32 b = 0; b < count; ++b)
            n += a[b] != (pass ? b : 0u) ? 1 : 0;
        ar.u32_(n);
        if (ar.loading()) {
            for (u32 b = 0; b < count; ++b)
                a[b] = pass ? b : 0u;
            u32 last = 0;
            for (u32 i = 0; i < n && ar.ok(); ++i) {
                u32 b = 0, v = 0;
                ar.u32_(b);
                ar.u32_(v);
                if (b >= count || (i && b <= last) || (pass && v >= count)) {
                    ar.fail();
                    return;
                }
                a[b] = v;
                last = b;
            }
        } else {
            for (u32 b = 0; b < count; ++b)
                if (a[b] != (pass ? b : 0u)) {
                    ar.u32_(b);
                    ar.u32_(a[b]);
                }
        }
    }
    ar.mark("os: disk blocks (gate 5)");
    if (!ar.loading() || !ar.ok())
        return;
    draw_block_lives();
    if (s_.levelling) {
        // The map must be a permutation: each block holds one.
        for (u32 b = 0; b < count; ++b)
            block_holds_[b] = ~0u;
        for (u32 b = 0; b < count; ++b) {
            if (block_holds_[block_map_[b]] != ~0u) {
                ar.fail();
                return;
            }
            block_holds_[block_map_[b]] = b;
        }
        find_least();
    }
}

} // namespace nova2
