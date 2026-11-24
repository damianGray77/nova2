#pragma once

// A place's memory (design.md 6.3, 9.3, 9.4; build-plan-gate1.md step 3).
//
// - One flat address space: the memory devices end to end, in device order.
//   A device boundary is a page boundary, and page 0 is reserved, so address
//   0 is never valid.
// - Allocations below a page come in size classes and share pages: a page
//   holds slots of one class and one permission. Larger allocations take
//   whole pages of their own (a span). First fit by address throughout.
// - Access is by page, ownership by allocation: a holder may touch any byte
//   of a page where it holds at least one allocation, within the page's
//   permission. The reset region's pages belong to nobody, and only idle runs
//   may touch them.
// - Nothing is zeroed: freed bytes stay where they are.
//
// Holders are numbers the OS hands out: 0 is nobody, 1..kRunHolders are idle
// runs (one per core), and programs start at kFirstProgramHolder.
//
// Every table is sized at init, so nothing here allocates during a tick.
#include "nova2/array.h"
#include "nova2/str.h"
#include "nova2/types.h"

namespace nova2 {

class Archive; // archive.h: saving and loading state

// A page's permission, in the call table's order (A.4: 0 data, 1 executable,
// 2 both).
enum class Perm : u8 { data = 0, exec = 1, both = 2 };

// What an access wants, as bits.
inline constexpr u8 kRead = 1;
inline constexpr u8 kWrite = 2;
inline constexpr u8 kRun = 4;

// The smallest size class, and the unit of the allocation map.
inline constexpr u32 kGranule = 8;
inline constexpr u32 kMaxPageSize = 4096;
inline constexpr u32 kMaxMemDevices = 16;

inline constexpr u32 kRunHolders = 64;
inline constexpr u32 kFirstProgramHolder = kRunHolders + 1;
inline constexpr bool is_run_holder(u32 h) { return h >= 1 && h <= kRunHolders; }

enum class PageState : u8 { free, reserved, region, slab, span };

struct MemDeviceSpec {
    u32 size;      // bytes, a whole number of pages
    u32 latency16; // access latency in sixteenths of a cycle (8.4)
    i32 threshold; // lowest working voltage, in millivolts (the reset region goes on the lowest)
};

struct MemorySpec {
    u32 page_size = 256;   // a power of two, 8 to kMaxPageSize
    bool strict = false;   // no page may be writable and executable at once; a lax
                           // place has no no-execute bit, so data pages are "both" (9.3)
    u32 class_steps = 2;   // size classes per doubling: 1 (powers of two) or 2
    u32 max_alloc = 4096;  // the largest allocation; at least a page
    u32 region_size = 1024; // the reset region, rounded up to whole pages
    const MemDeviceSpec* devices = nullptr;
    u32 device_count = 0;
};

struct MemDevice {
    u32 base, size;
    u32 first_page, end_page;
    u32 latency16;
    i32 threshold;
};

struct Page {
    PageState state;
    Perm perm;
    u8 cls;     // slab: its size class
    u8 device;
    u16 used;   // slab: slots in use
    u16 slots;  // slab: slots it holds
    u32 span;   // span: its allocation
    u32 version; // changes whenever who holds what here, or the permission, changes
};

struct Alloc {
    u32 base;
    u32 size;       // bytes asked for
    u32 extent;     // bytes reserved: the slot, or the span's whole pages
    u32 holder;     // 0: this record is free
    u32 prev, next; // the holder's list (0 = none); next also chains free records
    u32 wlo, whi;   // the extent its holder has written, [wlo, whi) from base; empty when wlo >= whi
};

// Why an ALLOC that failed found no room (an observer's question, asked
// after build-plan-gate3.md step 7): the request itself was refused (a size
// of 0 or over the limit, or "both" on a strict place), another memory
// device had room for it, or none did.
enum class AllocMiss : u8 { none = 0, refused, elsewhere, nowhere };

class Memory {
  public:
    // Lays out the devices, page 0 and the reset region. Returns false, with
    // the reason appended to why, if the spec breaks a constraint (the region
    // must fit its device, sizes must be whole pages, ...).
    bool init(const MemorySpec& spec, TextBuf* why);

    // ALLOC (A.4): size, permission code (already taken mod 3), device
    // argument (0 any, k > 0 the k-th fastest, k < 0 the k-th slowest,
    // clamped). On a lax place "data" gives "both". The address, or 0.
    u32 alloc(u32 holder, u32 size, u32 perm_code, i32 device_arg);
    // The same on one device by index (a thread's stack goes on its start's device).
    u32 alloc_on(u32 holder, u32 size, Perm perm, u32 device);
    // Why alloc() with these arguments would find no room (none if it would
    // find some). It changes nothing.
    AllocMiss alloc_miss(u32 size, u32 perm_code, i32 device_arg) const;
    // FREE: addr must be the start of an allocation the holder holds.
    bool free(u32 holder, u32 addr);
    // PROTECT: the page holding addr changes permission if the holder holds
    // every allocation in it (on a strict place never to both; on a lax one
    // "data" gives "both").
    bool protect(u32 holder, u32 addr, u32 perm_code);
    // Frees everything a holder holds (a program or run ending).
    void free_all(u32 holder);
    // Hands an allocation to another holder (SPAWN's gift).
    void give(u32 id, u32 new_holder);

    // A memory device failed (7.6; build-plan-gate5.md step 2): every
    // allocation on it is freed, and its pages are nobody's and no use,
    // as page 0 is, the reset region's among them if it held it. Touching
    // them faults, and nothing is allocated there, until the device is
    // replaced (7.8). Its bytes stay as they were, out of everything's reach.
    void fail_device(u32 d);
    // A failed device replaced (7.8): its pages are free again, and the
    // reset region's are the region's, if it held it. Its bytes are the
    // caller's to fill.
    void renew_device(u32 d);
    bool device_failed(u32 d) const { return pages_[devices_[d].end_page - 1].state == PageState::reserved; }
    // Whether the reset region is there: not while its device has failed.
    bool region_ok() const { return pages_[region_base_ >> page_shift_].state == PageState::region; }

    // The allocation whose reserved extent covers addr, or 0.
    u32 alloc_at(u32 addr) const;
    // Whether holder may do `want` to the byte at addr / every byte of a range.
    bool allows(u32 addr, u32 holder, u8 want) const;
    bool allows_range(u32 addr, u32 len, u32 holder, u8 want) const;
    // How many bytes from addr the holder may use before the first page it
    // can't (a call stops there and returns the count done, 9.3).
    u32 usable_prefix(u32 addr, u32 len, u32 holder, u8 want) const;
    // Records a write for the genome rule (13.2): bytes a holder writes into
    // its own allocations widen their written extent.
    void note_write(u32 addr, u32 len, u32 writer);
    // How many writes have touched the reset region (a halted core restarts
    // when this moves, 10.2). note_write counts the interpreter's; anything
    // else that changes the region (a floor flip, an arrival) calls
    // note_region_change.
    u32 region_writes() const { return region_writes_; }
    void note_region_change() { ++region_writes_; }

    // Asks the host to bring near what a use of the byte at addr will read:
    // ask_for its bytes, its page's record and its granule's entry; then
    // ask_for_record, which reads that entry, the allocation's record. They
    // change nothing a place can see: a hint for the host's cache, for
    // whoever knows ahead where a thread will run (os.h's ask_for_ready).
    void ask_for(u32 addr) const;
    void ask_for_record(u32 addr) const;

    u8* bytes() { return bytes_.data(); }
    const u8* bytes() const { return bytes_.data(); }
    u32 size() const { return total_; }
    u32 page_size() const { return page_size_; }
    u32 page_of(u32 addr) const { return addr >> page_shift_; }
    u32 page_count() const { return static_cast<u32>(pages_.size()); }
    const Page& page(u32 p) const { return pages_[p]; }
    u32 device_count() const { return static_cast<u32>(devices_.size()); }
    const MemDevice& device(u32 d) const { return devices_[d]; }
    u32 device_of(u32 addr) const { return pages_[page_of(addr)].device; }
    u32 device_by_speed(u32 rank) const { return by_speed_[rank]; } // 0 = the fastest
    bool strict() const { return strict_; }
    u32 max_alloc() const { return max_alloc_; }
    u32 class_count() const { return static_cast<u32>(classes_.size()); }
    u32 class_size(u32 c) const { return classes_[c]; }
    u32 region_base() const { return region_base_; }
    u32 region_size() const { return region_size_; }
    u32 region_device() const { return region_device_; }
    const Alloc& record(u32 id) const { return allocs_[id]; }
    u32 record_capacity() const { return static_cast<u32>(allocs_.size()); }
    u32 holder_capacity() const { return static_cast<u32>(head_.size()); }
    u32 first_of(u32 holder) const { return head_[holder]; } // the holder's list, via Alloc::next
    u32 free_bytes() const { return free_bytes_; } // bytes in free pages and free slots
    // The per-access counters (cpu.h): one per page, or none when off, and
    // their total since it was last taken. The total is taken every tick for
    // memory's draw (4.5) and isn't part of the state. The pages' counts are
    // state only with rowhammer on (below): then they run over its window.
    void enable_counters(bool on) {
        acts_.resize(0);
        acts_.resize(on ? pages_.size() : 0);
        act_total_ = 0;
    }
    u32* activations() { return acts_.size() ? acts_.data() : nullptr; }
    u64* activation_total() { return &act_total_; }
    u64 take_activation_count() {
        const u64 n = act_total_;
        act_total_ = 0;
        return n;
    }
    // Rowhammer (9.8; build-plan-gate5.md step 8). With it on, the pages'
    // counts are kept over a refresh window, and are state.
    // - A page whose count reaches the place's lowest threshold is listed
    //   as it does (cpu.cpp), so a tick's end looks at those pages only;
    //   with each, its count at the end of the tick before.
    // - window_end starts every count again.
    // - A lowest threshold of 0: off, and nothing is listed.
    void set_hammer(u32 lowest) {
        hot_at_ = lowest;
        hot_.resize(0);
        seen_.resize(0);
    }
    u32 hot_at() const { return hot_at_; }
    void note_hot(u32 page) const {
        if (hot_at_)
            hot_.push(page);
    }
    Array<u32>& hot() { return hot_; }
    Array<u32>& hot_seen() { return seen_; }
    u32 activations_of(u32 page) const { return acts_[page]; }
    void set_activations(u32 page, u32 n); // tests: a count set by hand, listed if it is past the lowest
    void clear_activations(u32 first, u32 end); // a replaced device's pages
    void window_end();
    void serialize_hammer(Archive& ar);
    u32 device_free(u32 d) const { return dev_free_[d]; } // the same, on one device

    // Checks every table against the others (the checked build and the
    // tests). False, with the first problem appended to why, if any is off.
    bool check(TextBuf* why) const;

    // Saves or loads the state (archive.h); the layout comes from init.
    void serialize(Archive& ar);

  private:
    void serialize_full(Archive& ar);    // every table whole: snapshot versions 3 and 4
    void serialize_compact(Archive& ar); // what is in use: version 5
    u32 class_of(u32 size) const; // the smallest class that fits size (size <= page)
    u32 open_index(u32 cls, Perm perm) const { return cls * 3 + static_cast<u32>(perm); }
    bool open_bit(u32 index, u32 p) const;
    void set_open(u32 index, u32 p, bool on);
    bool free_bit(u32 p) const;
    void set_free(u32 p, bool on);
    u32 find_open(u32 index, u32 lo, u32 hi) const;
    u32 find_free_run(u32 lo, u32 hi, u32 count) const;
    bool room_on(u32 size, Perm perm, u32 device) const; // alloc_on's search, changing nothing
    u32 new_record();
    void link(u32 id, u32 holder);
    void unlink(u32 id);
    void map(u32 base, u32 extent, u32 id);
    void release(u32 id);
    void bump(u32 id);
    bool holds_in_page(u32 p, u32 holder) const;
    bool perm_allows(Perm perm, u8 want) const;

    Array<u8> bytes_;
    Array<Page> pages_;
    Array<MemDevice> devices_;
    Array<u32> by_speed_;
    Array<u32> classes_;
    Array<u32> granule_;   // per granule: the allocation reserving it, or 0
    Array<Alloc> allocs_;  // record 0 is never used
    Array<u32> head_;      // per holder: its first allocation
    Array<u64> free_bits_; // per page: free
    Array<u32> dev_free_;  // per device: free bytes
    Array<u32> acts_;      // per page: row activations (the per-access counters)
    u64 act_total_ = 0;    // all pages' activations since the total was last taken
    u32 hot_at_ = 0;       // rowhammer: the lowest threshold of the place's devices (0: off)
    mutable Array<u32> hot_; // the pages at or past it, in the order they got there
    Array<u32> seen_;      // and each one's count at the end of the tick before
    Array<u64> open_bits_; // per (class, permission), per page: a slab with a free slot
    u32 words_ = 0;        // u64 words per page bitmap
    u32 free_record_ = 0;  // the first free record
    u32 total_ = 0;
    u32 page_size_ = 0;
    u32 page_shift_ = 0;
    u32 max_alloc_ = 0;
    u32 region_base_ = 0;
    u32 region_size_ = 0;
    u32 region_device_ = 0;
    u32 region_writes_ = 0;
    u32 free_bytes_ = 0;
    bool strict_ = false;
};

} // namespace nova2
