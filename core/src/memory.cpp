#include "nova2/memory.h"

#include <intrin.h> // _BitScanForward64, _mm_prefetch

namespace nova2 {

namespace {

bool is_pow2(u32 v) { return v && (v & (v - 1)) == 0; }

u32 log2_of(u32 v) {
    u32 s = 0;
    while ((1u << s) < v)
        ++s;
    return s;
}

void say(TextBuf* why, const char* a, u64 n = ~0ull, const char* b = nullptr) {
    if (!why)
        return;
    why->append(a);
    if (n != ~0ull)
        why->append_u64(n);
    if (b)
        why->append(b);
}

} // namespace

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

bool Memory::init(const MemorySpec& s, TextBuf* why) {
    if (!is_pow2(s.page_size) || s.page_size < kGranule || s.page_size > kMaxPageSize) {
        say(why, "memory: the page size must be a power of two from 8 to ", kMaxPageSize, " bytes");
        return false;
    }
    if (s.device_count < 1 || s.device_count > kMaxMemDevices || !s.devices) {
        say(why, "memory: a place needs 1 to ", kMaxMemDevices, " memory devices");
        return false;
    }
    if (s.class_steps < 1 || s.class_steps > 2) {
        say(why, "memory.class_steps: must be 1 or 2");
        return false;
    }
    u64 total = 0;
    for (u32 d = 0; d < s.device_count; ++d) {
        const u32 sz = s.devices[d].size;
        if (sz == 0 || sz % s.page_size != 0) {
            say(why, "memory: device ", d, "'s size must be a whole number of pages");
            return false;
        }
        total += sz;
    }
    if (total > (1ull << 30)) {
        say(why, "memory: a place's memory must be at most 1 GiB");
        return false;
    }
    if (s.max_alloc < s.page_size || s.max_alloc > total) {
        say(why, "memory.max_alloc: must be at least a page and at most the place's memory");
        return false;
    }

    page_size_ = s.page_size;
    page_shift_ = log2_of(s.page_size);
    strict_ = s.strict;
    max_alloc_ = s.max_alloc;
    total_ = static_cast<u32>(total);
    const u32 npages = total_ >> page_shift_;

    // Size classes: 8, then 16, 24, 32, 48, 64, 96, ... (two per doubling) or
    // 16, 32, 64, ... (one), up to the page size. All are whole granules.
    classes_.clear();
    classes_.push(kGranule);
    for (u32 c = 2 * kGranule; c <= page_size_; c *= 2) {
        classes_.push(c);
        if (s.class_steps == 2 && c + c / 2 <= page_size_)
            classes_.push(c + c / 2);
    }

    // Devices, end to end, and their speed order (lowest latency first; ties
    // by device order).
    devices_.clear();
    u32 base = 0;
    for (u32 d = 0; d < s.device_count; ++d) {
        MemDevice m;
        m.base = base;
        m.size = s.devices[d].size;
        m.first_page = base >> page_shift_;
        m.end_page = (base + m.size) >> page_shift_;
        m.latency16 = s.devices[d].latency16;
        m.threshold = s.devices[d].threshold;
        devices_.push(m);
        base += m.size;
    }
    by_speed_.clear();
    for (u32 d = 0; d < s.device_count; ++d) {
        u32 at = static_cast<u32>(by_speed_.size());
        while (at > 0 && devices_[by_speed_[at - 1]].latency16 > devices_[d].latency16)
            --at;
        by_speed_.insert(at, d);
    }

    // The reset region: whole pages at the start of the device with the
    // lowest threshold (ties by device order), after page 0 if it is there.
    region_device_ = 0;
    for (u32 d = 1; d < s.device_count; ++d)
        if (devices_[d].threshold < devices_[region_device_].threshold)
            region_device_ = d;
    const MemDevice& rd = devices_[region_device_];
    const u32 region_pages = (s.region_size + page_size_ - 1) >> page_shift_;
    const u32 region_first = rd.first_page == 0 ? 1 : rd.first_page;
    if (s.region_size == 0 || region_first + region_pages > rd.end_page) {
        say(why, "region.size: the reset region (", static_cast<u64>(region_pages) << page_shift_, " bytes");
        say(why, " in whole pages) must fit its memory device (", rd.size, " bytes, less page 0 if it holds it)");
        return false;
    }
    region_base_ = region_first << page_shift_;
    region_size_ = region_pages << page_shift_;
    region_writes_ = 0;

    pages_.resize(0);
    pages_.resize(npages);
    words_ = (npages + 63) / 64;
    free_bits_.resize(0);
    free_bits_.resize(words_);
    open_bits_.resize(0);
    open_bits_.resize(static_cast<usize>(words_) * classes_.size() * 3);
    resum(); // sized, and clear, before the first set_free
    free_bytes_ = 0;
    dev_free_.resize(0);
    dev_free_.resize(s.device_count);
    for (u32 d = 0; d < s.device_count; ++d) {
        for (u32 p = devices_[d].first_page; p < devices_[d].end_page; ++p) {
            Page& pg = pages_[p];
            pg.device = static_cast<u8>(d);
            if (p == 0) {
                pg.state = PageState::reserved;
            } else if (p >= region_first && p < region_first + region_pages) {
                pg.state = PageState::region;
                pg.perm = Perm::both;
            } else {
                pg.state = PageState::free;
                set_free(p, true);
                free_bytes_ += page_size_;
                dev_free_[d] += page_size_;
            }
        }
    }

    bytes_.resize(0);
    bytes_.resize(total_);
    granule_.resize(0);
    granule_.resize(total_ / kGranule);
    // At most one allocation per granule, so records never run out.
    allocs_.resize(0);
    allocs_.resize(total_ / kGranule + 1);
    free_record_ = 0;
    for (u32 id = static_cast<u32>(allocs_.size()) - 1; id >= 1; --id) {
        allocs_[id].next = free_record_;
        free_record_ = id;
    }
    head_.resize(0);
    head_.resize(static_cast<usize>(kFirstProgramHolder) + allocs_.size());
    return true;
}

u32 Memory::class_of(u32 size) const {
    u32 c = 0;
    while (classes_[c] < size)
        ++c;
    return c;
}

// ---------------------------------------------------------------------------
// Bitmaps
// ---------------------------------------------------------------------------

bool Memory::free_bit(u32 p) const { return (free_bits_[p >> 6] >> (p & 63)) & 1; }

// A word's summary bit (free_sum_, open_sum_) follows the word.
static void sum_follows(u64* sum, u32 word, u64 w) {
    if (w)
        sum[word >> 6] |= 1ull << (word & 63);
    else
        sum[word >> 6] &= ~(1ull << (word & 63));
}

void Memory::set_free(u32 p, bool on) {
    if (on)
        free_bits_[p >> 6] |= 1ull << (p & 63);
    else
        free_bits_[p >> 6] &= ~(1ull << (p & 63));
    sum_follows(free_sum_.data(), p >> 6, free_bits_[p >> 6]);
}

bool Memory::open_bit(u32 index, u32 p) const {
    return (open_bits_[static_cast<usize>(index) * words_ + (p >> 6)] >> (p & 63)) & 1;
}

void Memory::set_open(u32 index, u32 p, bool on) {
    u64& w = open_bits_[static_cast<usize>(index) * words_ + (p >> 6)];
    if (on)
        w |= 1ull << (p & 63);
    else
        w &= ~(1ull << (p & 63));
    sum_follows(open_sum_.data() + static_cast<usize>(index) * sum_words_, p >> 6, w);
}

void Memory::resum() {
    sum_words_ = (words_ + 63) / 64;
    const usize maps = words_ ? open_bits_.size() / words_ : 0;
    free_sum_.resize(0);
    free_sum_.resize(sum_words_);
    open_sum_.resize(0);
    open_sum_.resize(maps * sum_words_);
    for (u32 k = 0; k < words_; ++k)
        sum_follows(free_sum_.data(), k, free_bits_[k]);
    for (usize i = 0; i < open_bits_.size(); ++i)
        sum_follows(open_sum_.data() + i / words_ * sum_words_, static_cast<u32>(i % words_), open_bits_[i]);
}

// The lowest page in [lo, hi) whose bit is set in the bitmap at `bits`, or
// hi. Past lo's own word, the words with a bit set are found from the
// bitmap's summary `sum`, a bit a word (pass 13; until then, word by word).
static u32 first_set(const u64* bits, const u64* sum, u32 lo, u32 hi) {
    if (lo >= hi)
        return hi;
    u64 w = bits[lo >> 6] >> (lo & 63);
    unsigned long i;
    if (w) {
        _BitScanForward64(&i, w);
        const u32 found = lo + static_cast<u32>(i);
        return found < hi ? found : hi;
    }
    const u32 end = (hi + 63) >> 6; // the words that begin below hi
    for (u32 k = (lo >> 6) + 1; k < end; k = (k | 63) + 1) {
        const u64 s = sum[k >> 6] >> (k & 63);
        if (s) {
            _BitScanForward64(&i, s);
            k += static_cast<u32>(i);
            if (k >= end)
                return hi;
            _BitScanForward64(&i, bits[k]);
            const u32 found = k * 64 + static_cast<u32>(i);
            return found < hi ? found : hi;
        }
    }
    return hi;
}

u32 Memory::find_open(u32 index, u32 lo, u32 hi) const {
    return first_set(open_bits_.data() + static_cast<usize>(index) * words_,
                     open_sum_.data() + static_cast<usize>(index) * sum_words_, lo, hi);
}

u32 Memory::find_free_run(u32 lo, u32 hi, u32 count) const {
    u32 p = first_set(free_bits_.data(), free_sum_.data(), lo, hi);
    while (p + count <= hi) {
        u32 n = 1;
        while (n < count && free_bit(p + n))
            ++n;
        if (n == count)
            return p;
        p = first_set(free_bits_.data(), free_sum_.data(), p + n + 1, hi);
    }
    return hi;
}

// ---------------------------------------------------------------------------
// Records and holder lists
// ---------------------------------------------------------------------------

u32 Memory::new_record() {
    const u32 id = free_record_;
    if (!id)
        NOVA2_FATAL("memory: out of allocation records (cannot happen: one per granule)");
    free_record_ = allocs_[id].next;
    allocs_[id] = Alloc{};
    return id;
}

void Memory::link(u32 id, u32 holder) {
    if (holder == 0 || holder >= head_.size())
        NOVA2_FATAL("memory: holder id out of range");
    Alloc& a = allocs_[id];
    a.holder = holder;
    a.prev = 0;
    a.next = head_[holder];
    if (a.next)
        allocs_[a.next].prev = id;
    head_[holder] = id;
}

void Memory::unlink(u32 id) {
    Alloc& a = allocs_[id];
    if (a.prev)
        allocs_[a.prev].next = a.next;
    else
        head_[a.holder] = a.next;
    if (a.next)
        allocs_[a.next].prev = a.prev;
    a.prev = a.next = 0;
}

void Memory::map(u32 base, u32 extent, u32 id) {
    u32* g = granule_.data() + base / kGranule;
    for (u32 i = 0; i < extent / kGranule; ++i)
        g[i] = id;
}

void Memory::bump(u32 id) {
    const Alloc& a = allocs_[id];
    const u32 first = page_of(a.base), last = page_of(a.base + a.extent - 1);
    for (u32 p = first; p <= last; ++p)
        ++pages_[p].version;
}

// ---------------------------------------------------------------------------
// ALLOC, FREE, PROTECT, gifts
// ---------------------------------------------------------------------------

u32 Memory::alloc(u32 holder, u32 size, u32 perm_code, i32 device_arg) {
    if (size == 0 || size > max_alloc_ || perm_code > 2)
        return 0;
    const Perm perm = static_cast<Perm>(perm_code);
    if (strict_ && perm == Perm::both)
        return 0;
    const i64 n = static_cast<i64>(devices_.size());
    if (device_arg == 0) {
        for (i64 r = 0; r < n; ++r) {
            const u32 a = alloc_on(holder, size, perm, by_speed_[static_cast<usize>(r)]);
            if (a)
                return a;
        }
        return 0;
    }
    i64 r = device_arg > 0 ? static_cast<i64>(device_arg) - 1 : n + static_cast<i64>(device_arg);
    r = r < 0 ? 0 : (r >= n ? n - 1 : r);
    return alloc_on(holder, size, perm, by_speed_[static_cast<usize>(r)]);
}

AllocMiss Memory::alloc_miss(u32 size, u32 perm_code, i32 device_arg) const {
    if (size == 0 || size > max_alloc_ || perm_code > 2 || (strict_ && static_cast<Perm>(perm_code) == Perm::both))
        return AllocMiss::refused;
    const Perm perm = static_cast<Perm>(perm_code);
    const i64 n = static_cast<i64>(devices_.size());
    u32 asked = ~0u; // any device, for a device argument of 0
    if (device_arg != 0) {
        i64 r = device_arg > 0 ? static_cast<i64>(device_arg) - 1 : n + static_cast<i64>(device_arg);
        r = r < 0 ? 0 : (r >= n ? n - 1 : r);
        asked = by_speed_[static_cast<usize>(r)];
        if (room_on(size, perm, asked))
            return AllocMiss::none;
    }
    for (u32 d = 0; d < devices_.size(); ++d)
        if (room_on(size, perm, d))
            return asked == ~0u ? AllocMiss::none : AllocMiss::elsewhere;
    return AllocMiss::nowhere;
}

bool Memory::room_on(u32 size, Perm perm, u32 device) const {
    if (!strict_ && perm == Perm::data)
        perm = Perm::both;
    const MemDevice& dv = devices_[device];
    if (size <= page_size_)
        return find_open(open_index(class_of(size), perm), dv.first_page, dv.end_page) != dv.end_page ||
               first_set(free_bits_.data(), free_sum_.data(), dv.first_page, dv.end_page) != dv.end_page;
    return find_free_run(dv.first_page, dv.end_page, (size + page_size_ - 1) >> page_shift_) != dv.end_page;
}

u32 Memory::alloc_on(u32 holder, u32 size, Perm perm, u32 device) {
    if (size == 0 || size > max_alloc_ || device >= devices_.size() || (strict_ && perm == Perm::both))
        return 0;
    if (!strict_ && perm == Perm::data)
        perm = Perm::both; // a lax place has no no-execute bit (9.3)
    const MemDevice& dv = devices_[device];
    if (size <= page_size_) {
        const u32 c = class_of(size);
        const u32 cs = classes_[c];
        const u32 oi = open_index(c, perm);
        u32 p = find_open(oi, dv.first_page, dv.end_page);
        if (p == dv.end_page) {
            p = first_set(free_bits_.data(), free_sum_.data(), dv.first_page, dv.end_page);
            if (p == dv.end_page)
                return 0;
            Page& pg = pages_[p];
            pg.state = PageState::slab;
            pg.perm = perm;
            pg.cls = static_cast<u8>(c);
            pg.used = 0;
            pg.slots = static_cast<u16>(page_size_ / cs);
            pg.span = 0;
            set_free(p, false);
            set_open(oi, p, true);
            free_bytes_ -= page_size_ - pg.slots * cs; // the tail no slot covers
            dev_free_[device] -= page_size_ - pg.slots * cs;
        }
        Page& pg = pages_[p];
        const u32 page_base = p << page_shift_;
        u32 slot = 0;
        while (granule_[(page_base + slot * cs) / kGranule] != 0)
            ++slot;
        const u32 id = new_record();
        Alloc& a = allocs_[id];
        a.base = page_base + slot * cs;
        a.size = size;
        a.extent = cs;
        a.wlo = size;
        a.whi = 0;
        map(a.base, cs, id);
        link(id, holder);
        if (++pg.used == pg.slots)
            set_open(oi, p, false);
        free_bytes_ -= cs;
        dev_free_[device] -= cs;
        bump(id);
        return a.base;
    }
    const u32 count = (size + page_size_ - 1) >> page_shift_;
    const u32 p = find_free_run(dv.first_page, dv.end_page, count);
    if (p == dv.end_page)
        return 0;
    const u32 id = new_record();
    Alloc& a = allocs_[id];
    a.base = p << page_shift_;
    a.size = size;
    a.extent = count << page_shift_;
    a.wlo = size;
    a.whi = 0;
    for (u32 i = 0; i < count; ++i) {
        Page& pg = pages_[p + i];
        pg.state = PageState::span;
        pg.perm = perm;
        pg.cls = 0;
        pg.used = 1;
        pg.slots = 1;
        pg.span = id;
        set_free(p + i, false);
    }
    free_bytes_ -= a.extent;
    dev_free_[device] -= a.extent;
    map(a.base, a.extent, id);
    link(id, holder);
    bump(id);
    return a.base;
}

void Memory::release(u32 id) {
    bump(id);
    Alloc& a = allocs_[id];
    map(a.base, a.extent, 0);
    const u32 p = page_of(a.base);
    if (pages_[p].state == PageState::slab) {
        Page& pg = pages_[p];
        const u32 cs = classes_[pg.cls];
        const u32 oi = open_index(pg.cls, pg.perm);
        free_bytes_ += cs;
        dev_free_[pg.device] += cs;
        if (--pg.used == 0) {
            set_open(oi, p, false);
            free_bytes_ += page_size_ - pg.slots * cs;
            dev_free_[pg.device] += page_size_ - pg.slots * cs;
            pg.state = PageState::free;
            pg.slots = 0;
            pg.cls = 0;
            pg.perm = Perm::data;
            set_free(p, true);
        } else {
            set_open(oi, p, true);
        }
    } else {
        const u32 count = a.extent >> page_shift_;
        for (u32 i = 0; i < count; ++i) {
            Page& pg = pages_[p + i];
            pg.state = PageState::free;
            pg.used = pg.slots = 0;
            pg.span = 0;
            set_free(p + i, true);
        }
        free_bytes_ += a.extent;
        dev_free_[pages_[p].device] += a.extent;
    }
    unlink(id);
    a = Alloc{};
    a.next = free_record_;
    free_record_ = id;
}

bool Memory::free(u32 holder, u32 addr) {
    const u32 id = alloc_at(addr);
    if (!id || allocs_[id].base != addr || allocs_[id].holder != holder)
        return false;
    release(id);
    return true;
}

void Memory::fail_device(u32 d) {
    if (d >= devices_.size())
        return;
    const MemDevice& dv = devices_[d];
    // Its allocations, by address.
    for (u32 g = dv.base / kGranule, end = (dv.base + dv.size) / kGranule; g < end; ++g)
        if (granule_[g])
            release(granule_[g]);
    // Every page is free now, but page 0 and the reset region's.
    for (u32 p = dv.first_page; p < dv.end_page; ++p) {
        Page& pg = pages_[p];
        if (pg.state == PageState::free) {
            set_free(p, false);
            free_bytes_ -= page_size_;
            dev_free_[d] -= page_size_;
        }
        pg.state = PageState::reserved;
        pg.perm = Perm::data;
        ++pg.version;
    }
}

void Memory::renew_device(u32 d) {
    if (d >= devices_.size())
        return;
    const MemDevice& dv = devices_[d];
    const u32 region_first = region_base_ >> page_shift_, region_end = (region_base_ + region_size_) >> page_shift_;
    for (u32 p = dv.first_page; p < dv.end_page; ++p) {
        Page& pg = pages_[p];
        if (pg.state != PageState::reserved || p == 0)
            continue;
        ++pg.version;
        if (d == region_device() && p >= region_first && p < region_end) {
            pg.state = PageState::region;
            pg.perm = Perm::both;
            continue;
        }
        pg.state = PageState::free;
        set_free(p, true);
        free_bytes_ += page_size_;
        dev_free_[d] += page_size_;
    }
}

void Memory::free_all(u32 holder) {
    if (holder == 0 || holder >= head_.size())
        return;
    while (head_[holder])
        release(head_[holder]);
}

void Memory::give(u32 id, u32 new_holder) {
    if (id == 0 || id >= allocs_.size() || allocs_[id].holder == 0)
        NOVA2_FATAL("memory: give() of a free record");
    unlink(id);
    link(id, new_holder);
    bump(id);
}

bool Memory::protect(u32 holder, u32 addr, u32 perm_code) {
    if (addr == 0 || addr >= total_ || perm_code > 2)
        return false;
    Perm perm = static_cast<Perm>(perm_code);
    if (strict_ && perm == Perm::both)
        return false;
    if (!strict_ && perm == Perm::data)
        perm = Perm::both; // a lax place has no no-execute bit (9.3)
    const u32 p = page_of(addr);
    Page& pg = pages_[p];
    if (pg.state == PageState::span) {
        if (allocs_[pg.span].holder != holder)
            return false;
    } else if (pg.state == PageState::slab) {
        const u32 cs = classes_[pg.cls];
        const u32 page_base = p << page_shift_;
        for (u32 s = 0; s < pg.slots; ++s) {
            const u32 id = granule_[(page_base + s * cs) / kGranule];
            if (id && allocs_[id].holder != holder)
                return false;
        }
        const bool open = pg.used < pg.slots;
        set_open(open_index(pg.cls, pg.perm), p, false);
        if (open)
            set_open(open_index(pg.cls, perm), p, true);
    } else {
        return false; // free, reserved, or the reset region
    }
    pg.perm = perm;
    ++pg.version;
    return true;
}

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------

u32 Memory::alloc_at(u32 addr) const { return addr < total_ ? granule_[addr / kGranule] : 0; }

bool Memory::perm_allows(Perm perm, u8 want) const {
    const u8 ok = perm == Perm::data ? (kRead | kWrite) : perm == Perm::exec ? (kRead | kRun) : (kRead | kWrite | kRun);
    return (want & ~ok) == 0;
}

bool Memory::holds_in_page(u32 p, u32 holder) const {
    const Page& pg = pages_[p];
    if (pg.state == PageState::span)
        return allocs_[pg.span].holder == holder;
    if (pg.state != PageState::slab)
        return false;
    const u32 cs = classes_[pg.cls];
    const u32 page_base = p << page_shift_;
    for (u32 s = 0; s < pg.slots; ++s) {
        const u32 id = granule_[(page_base + s * cs) / kGranule];
        if (id && allocs_[id].holder == holder)
            return true;
    }
    return false;
}

void Memory::ask_for(u32 addr) const {
    if (addr >= total_)
        return;
    _mm_prefetch(reinterpret_cast<const char*>(bytes_.data() + addr), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(pages_.data() + page_of(addr)), _MM_HINT_T0);
    _mm_prefetch(reinterpret_cast<const char*>(granule_.data() + addr / kGranule), _MM_HINT_T0);
}

void Memory::ask_for_record(u32 addr) const {
    if (addr < total_)
        _mm_prefetch(reinterpret_cast<const char*>(allocs_.data() + granule_[addr / kGranule]), _MM_HINT_T0);
}

bool Memory::allows(u32 addr, u32 holder, u8 want) const {
    if (addr >= total_)
        return false;
    const u32 p = page_of(addr);
    const Page& pg = pages_[p];
    if (pg.state == PageState::region)
        return is_run_holder(holder);
    if (pg.state != PageState::slab && pg.state != PageState::span)
        return false;
    if (!perm_allows(pg.perm, want))
        return false;
    // Nearly always the holder holds the very allocation it touches, which is
    // one lookup; that allocation lies in this page, so the walk of the
    // page's slots below would find it too.
    const u32 id = granule_[addr / kGranule];
    if (id && allocs_[id].holder == holder)
        return true;
    return holds_in_page(p, holder);
}

bool Memory::allows_range(u32 addr, u32 len, u32 holder, u8 want) const {
    if (len == 0)
        return true;
    if (addr >= total_ || len > total_ - addr)
        return false;
    const u32 last = page_of(addr + len - 1);
    for (u32 p = page_of(addr); p <= last; ++p)
        if (!allows(p << page_shift_, holder, want))
            return false;
    return true;
}

u32 Memory::usable_prefix(u32 addr, u32 len, u32 holder, u8 want) const {
    u32 done = 0;
    while (done < len) {
        const u32 a = addr + done;
        if (a < addr || !allows(a, holder, want)) // wrapped past the top, or not allowed
            return done;
        const u32 page_end = (page_of(a) + 1) << page_shift_;
        const u32 room = page_end - a;
        done += room < len - done ? room : len - done;
    }
    return done;
}

void Memory::note_write(u32 addr, u32 len, u32 writer) {
    if (addr >= total_)
        return;
    const u32 end = len > total_ - addr ? total_ : addr + len;
    if (addr < region_base_ + region_size_ && end > region_base_)
        ++region_writes_; // anything writing to the reset region wakes its halted cores (10.2)
    u32 a = addr;
    while (a < end) {
        const u32 id = granule_[a / kGranule];
        if (!id) {
            a = (a / kGranule + 1) * kGranule;
            continue;
        }
        Alloc& r = allocs_[id];
        const u32 stop = end < r.base + r.extent ? end : r.base + r.extent;
        if (r.holder == writer) {
            const u32 lo = a - r.base;
            u32 hi = stop - r.base;
            hi = hi < r.size ? hi : r.size;
            if (lo < hi) {
                r.wlo = lo < r.wlo ? lo : r.wlo;
                r.whi = hi > r.whi ? hi : r.whi;
            }
        }
        a = stop;
    }
}

// ---------------------------------------------------------------------------
// The invariant check
// ---------------------------------------------------------------------------

// Rowhammer's counts (memory.h).
void Memory::set_activations(u32 page, u32 n) {
    const u32 was = acts_[page];
    acts_[page] = n;
    if (hot_at_ && was < hot_at_ && n >= hot_at_)
        hot_.push(page);
}

void Memory::clear_activations(u32 first, u32 end) {
    for (u32 p = first; p < end && p < acts_.size(); ++p)
        acts_[p] = 0;
}

void Memory::window_end() {
    acts_.fill(0);
    hot_.resize(0);
    seen_.resize(0);
}

bool Memory::check(TextBuf* why) const {
    // The summaries (pass 13): a word's bit set if and only if the word has one.
    if (sum_words_ != (words_ + 63) / 64 || free_sum_.size() != sum_words_ ||
        open_sum_.size() != (words_ ? open_bits_.size() / words_ : 0) * sum_words_) {
        say(why, "memory check: the bitmaps' summaries are the wrong size");
        return false;
    }
    for (usize i = 0; i < free_bits_.size() + open_bits_.size(); ++i) {
        const bool open = i >= free_bits_.size();
        const usize k = open ? i - free_bits_.size() : i;
        const u64* sum = open ? open_sum_.data() + k / words_ * sum_words_ : free_sum_.data();
        const u32 word = static_cast<u32>(k % words_);
        if (((sum[word >> 6] >> (word & 63)) & 1) != ((open ? open_bits_[k] : free_bits_[k]) != 0 ? 1u : 0u)) {
            say(why, "memory check: a summary bit disagrees with bitmap word ", word);
            return false;
        }
    }
    const u32 npages = page_count();
    u64 free_bytes = 0;
    u64 dev_free[kMaxMemDevices] = {};
    for (u32 p = 0; p < npages; ++p) {
        const Page& pg = pages_[p];
        const u32 page_base = p << page_shift_;
        const bool is_free = pg.state == PageState::free;
        if (free_bit(p) != is_free) {
            say(why, "memory check: page ", p, "'s free bit disagrees with its state");
            return false;
        }
        for (u32 c = 0; c < classes_.size(); ++c)
            for (u32 m = 0; m < 3; ++m) {
                const bool want = pg.state == PageState::slab && pg.cls == c && static_cast<u32>(pg.perm) == m &&
                                  pg.used < pg.slots;
                if (open_bit(open_index(c, static_cast<Perm>(m)), p) != want) {
                    say(why, "memory check: page ", p, "'s open bit disagrees with its slots");
                    return false;
                }
            }
        if (pg.state == PageState::free || pg.state == PageState::reserved || pg.state == PageState::region) {
            for (u32 g = 0; g < page_size_ / kGranule; ++g)
                if (granule_[page_base / kGranule + g]) {
                    say(why, "memory check: page ", p, " is not in use but holds an allocation");
                    return false;
                }
            if (is_free) {
                free_bytes += page_size_;
                dev_free[pg.device] += page_size_;
            }
        } else if (pg.state == PageState::slab) {
            const u32 cs = classes_[pg.cls];
            if (pg.slots != page_size_ / cs) {
                say(why, "memory check: page ", p, "'s slot count is wrong");
                return false;
            }
            u32 used = 0;
            for (u32 s = 0; s < pg.slots; ++s) {
                const u32 id = granule_[(page_base + s * cs) / kGranule];
                if (!id) {
                    free_bytes += cs;
                    dev_free[pg.device] += cs;
                    continue;
                }
                ++used;
                const Alloc& a = allocs_[id];
                if (a.holder == 0 || a.base != page_base + s * cs || a.extent != cs || a.size > cs) {
                    say(why, "memory check: page ", p, " has a slot whose record disagrees");
                    return false;
                }
            }
            for (u32 g = pg.slots * cs / kGranule; g < page_size_ / kGranule; ++g)
                if (granule_[page_base / kGranule + g]) {
                    say(why, "memory check: page ", p, "'s tail holds an allocation");
                    return false;
                }
            if (used != pg.used || used == 0) {
                say(why, "memory check: page ", p, "'s used count is wrong");
                return false;
            }
        } else { // span
            const Alloc& a = allocs_[pg.span];
            if (a.holder == 0 || page_base < a.base || page_base >= a.base + a.extent || pg.used != 1 ||
                a.size <= page_size_) {
                say(why, "memory check: span page ", p, " disagrees with its record");
                return false;
            }
        }
    }
    for (u32 d = 0; d < devices_.size(); ++d)
        if (dev_free[d] != dev_free_[d]) {
            say(why, "memory check: device ", d, "'s free bytes disagree with its pages");
            return false;
        }
    if (free_bytes != free_bytes_) {
        say(why, "memory check: free bytes are ", free_bytes_, " but count ");
        say(why, "", free_bytes);
        return false;
    }
    // Every record in use: its granules, and its place in its holder's list.
    u64 in_use = 0;
    for (u32 id = 1; id < allocs_.size(); ++id) {
        const Alloc& a = allocs_[id];
        if (!a.holder)
            continue;
        ++in_use;
        for (u32 g = 0; g < a.extent / kGranule; ++g)
            if (granule_[a.base / kGranule + g] != id) {
                say(why, "memory check: allocation ", id, "'s granules are not all its own");
                return false;
            }
        if (a.size == 0 || a.size > max_alloc_ || a.size > a.extent) {
            say(why, "memory check: allocation ", id, " has a bad size");
            return false;
        }
    }
    u64 listed = 0;
    for (u32 h = 1; h < head_.size(); ++h) {
        u32 prev = 0;
        for (u32 id = head_[h]; id; id = allocs_[id].next) {
            if (allocs_[id].holder != h || allocs_[id].prev != prev) {
                say(why, "memory check: holder ", h, "'s list is broken");
                return false;
            }
            prev = id;
            if (++listed > in_use) {
                say(why, "memory check: a holder list loops");
                return false;
            }
        }
    }
    if (listed != in_use) {
        say(why, "memory check: ", in_use - listed, " allocations are in no holder's list");
        return false;
    }
    return true;
}

} // namespace nova2
