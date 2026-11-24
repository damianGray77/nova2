#include "nova2/noise.h"

#include <cmath>    // pow: the rate and fade tables, built once at setup
#include <intrin.h> // _BitScanReverse64, _umul128, __umulh, _udiv128, _mm_prefetch, _InterlockedOr8

#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

u64 to_p64(double p) {
    if (!(p > 0))
        return 0;
    if (p >= 0.5)
        return 1ull << 63;
    return static_cast<u64>(p * 18446744073709551616.0);
}

u64 log2_q56(u64 v) {
    unsigned long msb;
    _BitScanReverse64(&msb, v | 1);
    // The mantissa, in [1, 2) with 62 fractional bits.
    u64 m = msb >= 62 ? v >> (msb - 62) : v << (62 - msb);
    u64 frac = 0;
    // Squaring the mantissa doubles its log; each time it passes 2, the next
    // bit of the fraction is 1 (the classic bit-by-bit log2).
    for (int i = 55; i >= 0; --i) {
        u64 hi;
        const u64 lo = _umul128(m, m, &hi);
        m = (hi << 2) | (lo >> 62); // m * m, back to 62 fractional bits: in [1, 4)
        // At 2 or more, halve it and set the bit. Done without a branch: the
        // bit is as good as random, so a branch here is mispredicted half
        // the time, 56 times a call (optimization-plan.md 4.3).
        const u64 two = m >> 63;
        m >>= two;
        frac |= two << i;
    }
    return (static_cast<u64>(msb) << 56) | frac;
}

namespace {

// The fast log's table: for each of the 256 spans a mantissa's top 8 bits
// pick, the reciprocal of the span's middle and -log2 of that reciprocal.
// Built at startup with the C library's log2: an error there only widens
// how near the fast log comes, which kLogSlack allows for many times over.
struct LogTable {
    double inv[256];
    double log[256];
    LogTable() {
        for (int i = 0; i < 256; ++i) {
            inv[i] = 1.0 / (1.0 + (i + 0.5) / 256.0);
            log[i] = -log2(inv[i]);
        }
    }
};
const LogTable kLogTable;

// How far log2_q56_near may stray from log2_q56, in 2^-56ths. Its own errors
// come to under 64 (the mantissa cut to 52 bits about 23, the doubles'
// rounding about 8 each, the series' cut-off nothing), and test_noise checks
// them on every exponent and every span's edges.
const u64 kLogSlack = 1ull << 12;

// log2_q56_near, gap_quotient and geometric_gap's working, written once
// here for them and, inlined, for the cosmic rays' loop (Noise::cosmic_flips).
NOVA2_INLINE u64 near_log(u64 v) {
    unsigned long msb;
    _BitScanReverse64(&msb, v | 1);
    const u64 top = (v | (v == 0)) << (63 - msb); // the leading 1 at bit 63
    // top >> 11 is under 2^53: signed, it converts the same, with no test of its top bit.
    const double m = static_cast<double>(static_cast<i64>(top >> 11)) * (1.0 / 4503599627370496.0); // [1, 2)
    const u32 i = static_cast<u32>(top >> 55) & 255;
    const double x = m * kLogTable.inv[i] - 1; // within 2^-9 of 0
    // ln(1 + x) to the x^6 term; the next is under 2^-65.
    const double ln = x * (1 - x * (0.5 - x * (1.0 / 3 - x * (0.25 - x * (0.2 - x * (1.0 / 6))))));
    const double frac = kLogTable.log[i] + ln * 1.4426950408889634; // 1 / ln 2
    const i64 l = (static_cast<i64>(msb) << 56) + static_cast<i64>(frac * 72057594037927936.0); // 2^56
    return l < 0 ? 0 : static_cast<u64>(l);
}

NOVA2_INLINE u64 quotient(u64 a, const GapLog& g) {
    const u64 q = __umulh(a, g.inv);
    return q + (a - q * g.log >= g.log ? 1 : 0);
}

// A gap for a log above 0.
NOVA2_INLINE u64 gap_of(const GapLog& g, u64 r) {
    const u64 b = g.log;
    const u64 top = 64ull << 56;
    const u64 v = r ? r : 1;
    const u64 near = near_log(v);
    const u64 a_lo = near + kLogSlack >= top ? 0 : top - (near + kLogSlack);
    const u64 a_hi = near <= kLogSlack ? top : top - (near - kLogSlack);
    const u64 q = quotient(a_lo, g);
    if (a_lo - q * b + (a_hi - a_lo) < b) // a_hi has the same quotient
        return q + 1;
    return quotient(top - log2_q56(v), g) + 1;
}

} // namespace

u64 log2_q56_near(u64 v) { return near_log(v); }

u64 gap_log(u64 p64) { return (64ull << 56) - log2_q56(0 - p64); } // -log2(1 - p)

GapLog gap_divisor(u64 log) {
    GapLog g;
    g.log = log;
    g.inv = log ? ~0ull / log : 0;
    return g;
}

GapLog keep_gap_log(u64 p64) { return gap_divisor(gap_log(p64)); }

// a / b by b's reciprocal, inv = (2^64 - 1) / b rounded down. inv × b is
// over 2^64 - 1 - b, so a × inv / 2^64 falls short of a / b by less than
// a / 2^64, under 1: the multiply's whole part is the quotient or one
// under, and the remainder says which. Exact for every a and every b
// above 0, with no divide.
u64 gap_quotient(u64 a, const GapLog& g) { return quotient(a, g); }

u64 geometric_gap(u64 p64, u64 r) { return p64 ? geometric_gap(p64, keep_gap_log(p64), r) : ~0ull; }

// floor(ln U / ln(1 - p)) + 1, U = r / 2^64: P(gap = 1) = P(U > 1 - p) = p.
// That is a / b + 1 with a = -log2(U) = 64 - log2(r) in 2^-56ths. The fast
// log puts a within kLogSlack either way; where the quotient is the same
// at both ends, it is the exact log's. Only near a multiple of b, for
// about 2 × kLogSlack ÷ b of the draws, does the exact log decide. The same
// numbers either way, a few times faster (optimization-plan.md, the noise log).
u64 geometric_gap(u64 p64, const GapLog& g, u64 r) {
    if (p64 == 0 || g.log == 0) // a log of 0: too small to tell from never
        return ~0ull;
    return gap_of(g, r);
}

namespace {

// a × b ÷ c with a 128-bit product, saturating if the quotient won't fit.
u64 mul_div(u64 a, u64 b, u64 c) {
    u64 hi;
    const u64 lo = _umul128(a, b, &hi);
    if (hi >= c)
        return ~0ull;
    u64 rem;
    return _udiv128(hi, lo, c, &rem);
}

// A chance of 2^64 times a factor in 2^-16ths, at most one half.
u64 scale_q16(u64 p, u64 q16) {
    u64 hi;
    const u64 lo = _umul128(p, q16, &hi);
    if (hi >> 15)
        return 1ull << 63;
    const u64 r = (hi << 48) | (lo >> 16);
    return r > (1ull << 63) ? 1ull << 63 : r;
}

// A positive number of ticks in 2^-32ths, rounded.
u64 ticks_q32(double t) {
    const double v = t * 4294967296.0 + 0.5;
    return v >= 18446744073709551615.0 ? ~0ull : static_cast<u64>(v);
}

// Sets one bit of a byte to v; says whether the byte changed.
bool set_bit(u8* byte, u32 bit, bool v) {
    const u8 old = *byte;
    const u8 m = static_cast<u8>(1u << bit);
    *byte = v ? static_cast<u8>(old | m) : static_cast<u8>(old & ~m);
    return *byte != old;
}

// How many batches of 64 ranks ahead a fade works out cells and asks for
// their bytes (Noise::fade_cells).
constexpr u64 kFadeAhead = 1;

// Four Feistel rounds over 2 × half bits, a bijection on [0, 2^(2 half)):
// one step of a rank's walk to its cell (Noise::cell_of).
inline u64 feistel(const u64* key, u32 half, u64 mask, u64 x) {
    u64 l = x >> half, r = x & mask;
    for (u32 k = 0; k < 4; ++k) {
        const u64 t = l ^ (mix64(key[k] ^ r) & mask);
        l = r;
        r = t;
    }
    return (l << half) | r;
}

// The same for a byte other threads may be setting other bits of at once:
// interlocked, the change told by the bit as it was.
bool set_bit_shared(u8* byte, u32 bit, bool v) {
    volatile char* const p = reinterpret_cast<volatile char*>(byte);
    const char m = static_cast<char>(1u << bit);
    const char old = v ? _InterlockedOr8(p, m) : _InterlockedAnd8(p, static_cast<char>(~m));
    return ((old & m) != 0) != v;
}

} // namespace

bool Noise::init(Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, u64 seed, const NoiseSettings& s,
                 const PowerSettings& power, TextBuf* why) {
    const bool ok = s.p_floor >= 0 && s.p_floor <= 0.5 && s.e_floor >= 0 && s.e_floor <= 0.5 && s.cosmic >= 0 &&
                    s.cosmic <= 0.5 && s.clock_doubling <= 100;
    if (!ok) {
        if (why)
            why->append("noise: the floors must be from 0 to 0.5, and the clock doubling at most 100 tenths");
        return false;
    }
    if (s.edge_band > 1000 || (s.edge_band && (s.edge_doubling == 0 || s.edge_band / s.edge_doubling > 62))) {
        if (why)
            why->append("noise: the edge band must be at most 1,000 mV, with a doubling of at least a millivolt and "
                        "at most 62 doublings in it");
        return false;
    }
    if (!(s.fade_fastest >= 1e-3) || !(s.fade_slowest >= s.fade_fastest) || !(s.fade_slowest <= 1e9) ||
        !(s.fade_at_threshold >= 1) || !(s.fade_at_threshold <= 1e9)) {
        if (why)
            why->append("noise: fading's retention must run from at least a thousandth of a tick to at most a "
                        "billion, slowest at least fastest, and hold 1 to a billion times longer at the threshold");
        return false;
    }
    if (s.cosmic_start != 0 &&
        (!(s.cosmic_start >= s.cosmic) || !(s.cosmic_start <= 0.5) || s.cosmic_half_life == 0 ||
         s.cosmic_half_life > 1000000000000000ull)) {
        if (why)
            why->append("noise: cosmic rays at the start must be 0 (no fall) or from the floor to 0.5, with a "
                        "half-life of 1 to 10^15 ticks");
        return false;
    }
    mem_ = mem;
    cores_ = cores;
    core_count_ = core_count;
    env_ = env;
    seed_ = seed;
    s_ = s;
    power_ = power;
    result_errors = band_errors = cosmic = memory_edge = cells_faded = weak = 0;
    cores_n_.resize(0);
    cores_n_.resize(core_count);
    for (u32 c = 0; c < core_count; ++c) {
        CoreNoise& n = cores_n_[c];
        n.noise = this;
        n.core = c;
        n.tick_seen = ~0ull;
        n.count = 0;
        n.edge = 0;
        n.gap_p = 0;
        n.gap_log = GapLog();
        // The rate at each clock level: the kind's floor, doubling for every
        // `clock_doubling` tenths above stock and halving below (7.5).
        const double base = cores[c].kind == DeviceKind::e_core ? s.e_floor : s.p_floor;
        for (u32 level = 0; level < 256; ++level) {
            const double f = s.clock_doubling ? pow(2.0, (static_cast<double>(level) - 10.0) / s.clock_doubling) : 1.0;
            n.p[level] = to_p64(base * f);
            base_p_[cores[c].kind == DeviceKind::e_core ? 1 : 0][level] = n.p[level];
        }
        ErrorClock& ec = cores[c].clock;
        ec.owner = &n;
        ec.next = next;
        ec.redraw = redraw;
        ec.level = 10;
        ec.left = geometric_gap(n.p[10], Rng(seed, env->place_id, kPurposeErrors + c, Rng::kInitTick).next());
    }
    // The falling rate (7.5): the floor plus an excess halving every eight
    // steps. After 64 halvings the excess is below a 2^64th: the floor.
    fall_.resize(0);
    if (s.cosmic_start > 0) {
        fall_.resize(kFallSteps + 1);
        for (u64 k = 0; k <= kFallSteps; ++k)
            fall_[k] = to_p64(s.cosmic + (s.cosmic_start - s.cosmic) *
                                             pow(2.0, -static_cast<double>(k) / static_cast<double>(kFallPerHalf)));
    }
    cosmic_p64_ = fall_.empty() ? to_p64(s.cosmic) : fall_[0];
    cosmic_log_ = keep_gap_log(cosmic_p64_);
    const u32 devices = mem->device_count();
    cosmic_left_.resize(0);
    cosmic_left_.resize(devices);
    for (u32 d = 0; d < devices; ++d)
        cosmic_left_[d] =
            geometric_gap(cosmic_p64_, Rng(seed, env->place_id, kPurposeCosmic + d, Rng::kInitTick).next());

    // Edge errors (7.5): inside the band, d mV from the nearer edge, the total
    // at stock is the floor × 2^((band − d) ÷ doubling), so the extra is the
    // floor × (that − 1). Memory's floor is the cosmic-ray floor.
    for (u32 k = 0; k < 3; ++k) {
        const double base = k == 0 ? s.p_floor : k == 1 ? s.e_floor : s.cosmic;
        edge_tab_[k].resize(0);
        edge_tab_[k].resize(s.edge_band);
        for (u32 d = 0; d < s.edge_band; ++d) {
            const double f = pow(2.0, static_cast<double>(s.edge_band - d) / s.edge_doubling) - 1.0;
            edge_tab_[k][d] = to_p64(base * f);
        }
    }
    mem_rate_.resize(0);
    mem_rate_.resize(devices);
    mem_left_.resize(0);
    mem_left_.resize(devices);
    for (u32 d = 0; d < devices; ++d)
        mem_left_[d] = ~0ull;
    mem_log_p_.resize(0);
    mem_log_p_.resize(devices);
    mem_log_.resize(0);
    mem_log_.resize(devices); // a rate of 0, kept with a log of 0: never
    // No part flaky, until the place says (start_flaky).
    flaky_p64_ = 0;
    flaky_all_ = false;
    worn_q16_ = 0;
    core_flaky_.resize(0);
    core_step_.resize(0);
    flaky_mem_.resize(0);
    flaky_p_.resize(0);
    flaky_log_.resize(0);
    core_step_.resize(core_count);
    flaky_p_.resize(devices);
    flaky_log_.resize(devices);
    for (u32 c = 0; c < core_count; ++c)
        core_flaky_.push(kFlakyOne);
    for (u32 d = 0; d < devices; ++d)
        flaky_mem_.push(kFlakyOne);
    // Wear's mirrors: nothing worn, nothing weak, nothing gone.
    core_rise_.resize(0);
    core_rise_.resize(core_count);
    rise_.resize(0);
    rise_.resize(devices);
    weak_n_.resize(0);
    weak_n_.resize(devices);
    gone_.resize(0);
    gone_.resize(devices);
    weak_left_.resize(0);
    weak_left_.resize(devices);
    for (u32 d = 0; d < devices; ++d)
        weak_left_[d] = ~0ull;
    weak_p64_ = 0;
    weak_log_ = GapLog();

    // Fading (appendix C): each device's Feistel keys and its dose per tick
    // at each millivolt below its threshold, at_threshold^(−V ÷ Vth).
    fade_lo_ = log2_q56(ticks_q32(s.fade_fastest));
    fade_hi_ = log2_q56(ticks_q32(s.fade_slowest));
    dose_cap_ = ticks_q32(s.fade_slowest);
    fade_.resize(0);
    fade_.resize(devices);
    per_mv_.resize(0);
    for (u32 d = 0; d < devices; ++d) {
        const MemDevice& dv = mem->device(d);
        DeviceFade& f = fade_[d];
        f.cells = static_cast<u64>(dv.size) * 8;
        unsigned long msb = 0;
        if (f.cells > 1)
            _BitScanReverse64(&msb, f.cells - 1);
        const u32 width = f.cells > 1 ? static_cast<u32>(msb) + 1 : 0; // bits to index every cell
        f.half = (width + 1) / 2;
        const Rng keys(seed, env->place_id, kPurposeFade + d, Rng::kInitTick);
        for (u32 k = 0; k < 4; ++k)
            f.key[k] = keys.at(k);
        f.per_mv = static_cast<u32>(per_mv_.size());
        const i32 th = dv.threshold > 0 ? dv.threshold : 0;
        for (i32 mv = 0; mv < th; ++mv)
            per_mv_.push(ticks_q32(pow(s.fade_at_threshold, -static_cast<double>(mv) / th)));
    }
    return true;
}

// The core's own stream: keyed by the tick, with a count of the draws made in
// it, so draws mid-tick follow the order things happen at the place.
u64 Noise::draw(CoreNoise& n) {
    const u64 tick = env_->tick;
    if (n.tick_seen != tick) {
        n.tick_seen = tick;
        n.count = 0;
    }
    Rng r(seed_, env_->place_id, kPurposeErrors + n.core, tick);
    r.set_counter(n.count);
    const u64 v = r.next();
    n.count = r.counter();
    return v;
}

// A core's rate per result at a clock level: the floor at that level plus
// its edge errors, at most a half.
u64 Noise::total(const CoreNoise& n, u8 level) {
    const u64 r = n.p[level] + n.edge;
    return r < n.edge || r > (1ull << 63) ? 1ull << 63 : r;
}

namespace {

// geometric_gap(p, r) for a core, keeping gap_log(p) from one draw to the next.
u64 core_gap(Noise::CoreNoise& n, u64 p, u64 r) {
    if (p != n.gap_p) {
        n.gap_p = p;
        n.gap_log = keep_gap_log(p);
    }
    return geometric_gap(p, n.gap_log, r); // never, for a p of 0, whatever the log
}

} // namespace

u32 Noise::next(ErrorClock* c) {
    CoreNoise& n = *static_cast<CoreNoise*>(c->owner);
    Noise& z = *n.noise;
    const u64 bits = z.draw(n);
    c->left = core_gap(n, total(n, c->level), z.draw(n));
    ++z.result_errors;
    z.band_errors += n.edge ? 1 : 0;
    return static_cast<u32>(bits >> 32);
}

void Noise::redraw(ErrorClock* c) {
    CoreNoise& n = *static_cast<CoreNoise*>(c->owner);
    c->left = core_gap(n, total(n, c->level), n.noise->draw(n));
}

u64 Noise::edge_rate(u32 kind, i32 mv, i32 lo, i32 hi) const {
    const Array<u64>& t = edge_tab_[kind];
    const i32 d = mv - lo < hi - mv ? mv - lo : hi - mv; // to the nearer edge; below 0 outside the range
    if (!t.size() || d >= static_cast<i32>(t.size()))
        return 0;
    return t[d < 0 ? 0 : static_cast<u32>(d)];
}

void Noise::core_edges(i32 mv) {
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores_[c];
        const bool e = k.kind == DeviceKind::e_core;
        const CorePower& kp = e ? power_.e : power_.p;
        const u64 x = k.working ? edge_rate(e ? 1 : 0, mv, kp.threshold + core_rise_[c], kp.maximum) : 0;
        CoreNoise& n = cores_n_[c];
        if (x == n.edge)
            continue;
        n.edge = x;
        redraw(&k.clock);
    }
}

void Noise::memory_errors(const DeviceState* states, i32 mv, Array<u32>* failed) {
    u8* bytes = mem_->bytes();
    const u32 rb = mem_->region_base(), re = rb + mem_->region_size();
    bool region = false;
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        const MemDevice& dv = mem_->device(d);
        const u64 rate = states[d].on ? edge_rate(2, mv, dv.threshold + rise_[d], power_.memory_maximum) : 0;
        Rng r(seed_, env_->place_id, kPurposeMemoryEdge + d, env_->tick);
        u64& left = mem_left_[d];
        if (rate != mem_rate_[d]) { // a new rate: the countdown is drawn afresh (it has no memory)
            mem_rate_[d] = rate;
            left = rate ? geometric_gap(rate, mem_log(d, rate), r.next()) : ~0ull;
        }
        if (left == ~0ull)
            continue;
        const GapLog& g = mem_log(d, rate);
        const u64 bits = static_cast<u64>(dv.size) * 8;
        u64 base = 0; // bits of this tick already passed
        while (left <= bits - base) {
            const u64 idx = base + left - 1;
            const u32 addr = dv.base + static_cast<u32>(idx >> 3);
            // A bit it fails to hold reads back random (7.5).
            if (set_bit(bytes + addr, static_cast<u32>(idx & 7), (r.next() & 1) != 0) && addr >= rb && addr < re)
                region = true;
            if (failed)
                failed->push(addr); // failed whatever it reads back, as a faded cell is
            ++memory_edge;
            base = idx + 1;
            left = geometric_gap(rate, g, r.next());
        }
        left -= bits - base;
    }
    if (region)
        mem_->note_region_change(); // halted cores may start again (10.2)
}

// A device's edge errors' gap_log at a rate, kept from one draw to the
// next: a rate changes only with the voltage and the device's rise, and
// each failed bit's gap would otherwise work out the exact log again.
const GapLog& Noise::mem_log(u32 d, u64 rate) {
    if (rate != mem_log_p_[d]) {
        mem_log_p_[d] = rate;
        mem_log_[d] = keep_gap_log(rate);
    }
    return mem_log_[d];
}

u64 Noise::cell_of(u32 d, u64 rank) const {
    const DeviceFade& f = fade_[d];
    const u64 mask = (1ull << f.half) - 1;
    u64 x = rank;
    // The Feistel rounds are a bijection on [0, 2^(2 half)); walking the
    // cycle until the value is a cell keeps it one on [0, cells).
    do
        x = feistel(f.key, f.half, mask, x);
    while (x >= f.cells);
    return x;
}

u64 Noise::cells_failed(u32 d, u64 dose) const {
    const u64 n = fade_[d].cells;
    if (dose == 0)
        return 0;
    const u64 l = log2_q56(dose);
    if (l < fade_lo_)
        return 0;
    if (l >= fade_hi_)
        return n;
    // Rank r's retention is fastest × (slowest ÷ fastest)^(r ÷ n), so r has
    // failed once r ≤ n × log(dose ÷ fastest) ÷ log(slowest ÷ fastest).
    const u64 k = mul_div(n, l - fade_lo_, fade_hi_ - fade_lo_) + 1;
    return k < n ? k : n;
}

bool Noise::fade_check(const DeviceState* states, i32 mv, const bool* tripped) {
    bool any = false;
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        DeviceFade& f = fade_[d];
        if (gone_[d])
            continue; // failed (7.6): nothing left to fade
        if (states[d].on) { // back on: the cells it still holds are refreshed
            f.dose = f.done = 0;
            continue;
        }
        if ((mv < mem_->device(d).threshold + rise_[d] || (tripped && tripped[d])) && f.done < f.cells)
            any = true;
    }
    return any;
}

void Noise::fade(const DeviceState* states, i32 mv, const SavedState* saved, u32 saved_count, Array<u32>* hits,
                 const bool* tripped, const u32* faster) {
    fade_plan(states, mv, tripped, faster);
    bool region = false;
    for (u32 j = 0; j < fade_jobs_.size(); ++j)
        if (fade_cells(fade_jobs_[j], fade_jobs_[j].from, fade_jobs_[j].upto, saved, saved_count, nullptr, hits, false))
            region = true;
    if (region)
        mem_->note_region_change(); // halted cores may start again (10.2)
}

u64 Noise::fade_plan(const DeviceState* states, i32 mv, const bool* tripped, const u32* faster) {
    fade_jobs_.resize(0);
    u64 cells = 0;
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        const MemDevice& dv = mem_->device(d);
        DeviceFade& f = fade_[d];
        const bool trip = tripped && tripped[d];
        // A worn device at mv fades as a new one at mv less its threshold's rise.
        const i32 as_new = mv - rise_[d];
        if (gone_[d] || states[d].on || (!trip && as_new >= dv.threshold) || f.done >= f.cells)
            continue;
        u64 add = per_mv_[f.per_mv + (trip || as_new < 0 ? 0u : static_cast<u32>(as_new))];
        if (faster && faster[d] != (1u << 16)) {
            // Hot memory: the tick's dose times its fading at its temperature.
            u64 hi;
            const u64 lo = _umul128(add, faster[d], &hi);
            add = hi >> 16 ? ~0ull : (hi << 48) | (lo >> 16);
        }
        f.dose = f.dose + add < f.dose || f.dose + add > dose_cap_ ? dose_cap_ : f.dose + add;
        const u64 upto = cells_failed(d, f.dose);
        if (upto <= f.done)
            continue;
        const u64 base = Rng::stream_base(seed_, env_->place_id, kPurposeFade + d, env_->tick);
        fade_jobs_.push(FadeJob{d, f.done, upto, base});
        cells += upto - f.done;
        cells_faded += upto - f.done;
        f.done = upto;
    }
    return cells;
}

void saved_blocks(const SavedState* saved, u32 count, u32 bytes, Array<u64>* bits) {
    const u32 blocks = (bytes >> kSavedBlockShift) + 1; // and a part block at the end
    bits->resize((blocks + 63) / 64);
    bits->fill(0);
    u64* const b = bits->data();
    for (u32 i = 0; i < count; ++i) {
        const u32 last = (saved[i].hi - 1) >> kSavedBlockShift;
        for (u32 k = saved[i].lo >> kSavedBlockShift; k <= last && k < blocks; ++k)
            b[k >> 6] |= 1ull << (k & 63);
    }
}

bool Noise::fade_cells(const FadeJob& job, u64 k0, u64 k1, const SavedState* saved, u32 saved_count,
                       const u64* blocks, Array<u32>* hits, bool shared) const {
    NOVA2_ASSERT((k0 - job.from) % 64 == 0 && job.from <= k0 && k1 <= job.upto);
    const u32 d = job.device;
    const u32 at = mem_->device(d).base;
    u8* const bytes = mem_->bytes();
    const u32 rb = mem_->region_base(), re = rb + mem_->region_size();
    const Rng r = Rng::from_base(job.base);
    bool region = false;
    const DeviceFade& f = fade_[d];
    const u64 mask = (1ull << f.half) - 1;
    // 64 ranks a batch, a draw's bits. A batch's cells are worked out, and
    // their bytes asked for, kFadeAhead batches before its bits are set, so
    // the bytes' cache misses land while the batches between are worked out.
    u64 cells[kFadeAhead + 1][64];
    const u64 batches = (k1 - k0 + 63) / 64;
    for (u64 b = 0; b < batches + kFadeAhead; ++b) {
        if (b < batches) {
            const u64 k = k0 + 64 * b;
            const u32 n = k1 - k < 64 ? static_cast<u32>(k1 - k) : 64;
            u64* const c = cells[b % (kFadeAhead + 1)];
            // The batch's walks to their cells (cell_of's) a step at a time
            // together: those not on a cell yet listed for the next step, with
            // no branch on each, so different ranks' steps overlap.
            u32 left[64], m = 0;
            for (u32 i = 0; i < n; ++i) {
                c[i] = feistel(f.key, f.half, mask, k + i);
                left[m] = i;
                m += c[i] >= f.cells ? 1 : 0;
            }
            while (m) {
                u32 still = 0;
                for (u32 j = 0; j < m; ++j) {
                    const u32 i = left[j];
                    c[i] = feistel(f.key, f.half, mask, c[i]);
                    left[still] = i;
                    still += c[i] >= f.cells ? 1 : 0;
                }
                m = still;
            }
            for (u32 i = 0; i < n; ++i)
                _mm_prefetch(reinterpret_cast<const char*>(bytes + at + (c[i] >> 3)), _MM_HINT_T0);
        }
        if (b < kFadeAhead)
            continue;
        const u64 k = k0 + 64 * (b - kFadeAhead);
        const u32 n = k1 - k < 64 ? static_cast<u32>(k1 - k) : 64;
        const u64* const c = cells[(b - kFadeAhead) % (kFadeAhead + 1)];
        u64 v = r.at((k - job.from) / 64);
        for (u32 i = 0; i < n; ++i, v >>= 1) {
            const u32 addr = at + static_cast<u32>(c[i] >> 3);
            const u32 bit = static_cast<u32>(c[i] & 7);
            // A failed cell reads back random (appendix C).
            const bool changed =
                shared ? set_bit_shared(bytes + addr, bit, (v & 1) != 0) : set_bit(bytes + addr, bit, (v & 1) != 0);
            if (changed && addr >= rb && addr < re)
                region = true;
            if (!saved_count)
                continue;
            const u32 block = addr >> kSavedBlockShift;
            if (blocks && !((blocks[block >> 6] >> (block & 63)) & 1))
                continue; // no saved state reaches its block
            // The saved states that hold addr start within kSavedRegsBytes
            // below it (none is longer): the first at or past that, onwards.
            const u32 from = addr >= kSavedRegsBytes - 1 ? addr - (kSavedRegsBytes - 1) : 0;
            u32 a = 0, e = saved_count;
            while (a < e) {
                const u32 m = (a + e) / 2;
                if (saved[m].lo < from)
                    a = m + 1;
                else
                    e = m;
            }
            for (u32 s = a; s < saved_count && saved[s].lo <= addr; ++s)
                if (addr < saved[s].hi)
                    hits->push(s);
        }
    }
    return region;
}

void Noise::init_wear(u64 weak_p64) {
    weak_p64_ = weak_p64;
    weak_log_ = keep_gap_log(weak_p64);
    for (u32 d = 0; d < weak_left_.size(); ++d) {
        const Rng r(seed_, env_->place_id, kPurposeWeak + d, Rng::kInitTick);
        weak_left_[d] = weak_p64 ? geometric_gap(weak_p64, weak_log_, r.at(0)) : ~0ull;
    }
}

// Flaky parts (noise.h).
void Noise::init_flaky(const FlakySettings& s) {
    flaky_all_ = s.share >= 1;
    flaky_p64_ = flaky_all_ ? ~0ull : s.share > 0 ? static_cast<u64>(s.share * 18446744073709551616.0) : 0;
    if (s.share > 0 && !flaky_p64_)
        flaky_p64_ = 1;
    const double ratio = s.factor_min > 0 ? s.factor_max / s.factor_min : 1;
    for (u32 k = 0; k < kFlakySteps; ++k)
        flaky_tab_[k] = static_cast<u32>(s.factor_min * pow(ratio, k / static_cast<double>(kFlakySteps - 1)) *
                                             kFlakyOne + 0.5);
    worn_q16_ = s.worn > 1 ? static_cast<u64>((s.worn - 1) * kFlakyOne + 0.5) : 0;
}

u32 Noise::flaky_factor(u32 slot, u32 fresh) const {
    if (!flaky_p64_)
        return kFlakyOne;
    const Rng r(seed_, env_->place_id, kPurposeFlaky + slot, Rng::kInitTick);
    if (!flaky_all_ && r.at(2ull * fresh) >= flaky_p64_)
        return kFlakyOne;
    return flaky_tab_[r.at(2ull * fresh + 1) >> 56];
}

// A core's floor at each clock level: its kind's, times its factor and its
// wear's rise.
void Noise::core_table(u32 core) {
    CoreNoise& n = cores_n_[core];
    const u64* base = base_p_[cores_[core].kind == DeviceKind::e_core ? 1 : 0];
    const u64 worn = kFlakyOne + worn_q16_ * core_step_[core] / 256;
    const u64 q16 = static_cast<u64>(core_flaky_[core]) * worn / kFlakyOne;
    for (u32 level = 0; level < 256; ++level)
        n.p[level] = q16 == kFlakyOne ? base[level] : scale_q16(base[level], q16);
}

void Noise::set_core_floor(u32 core, u32 q16, u32 step, bool again) {
    if (core_flaky_[core] == q16 && core_step_[core] == step)
        return;
    core_flaky_[core] = q16;
    core_step_[core] = step;
    core_table(core);
    if (again)
        redraw(&cores_[core].clock);
}

// The place as built: each flaky device's first countdown is drawn again, by
// the draw it was first given, at its own rate.
void Noise::start_flaky(const u32* core_q16, const u32* memory_q16) {
    for (u32 c = 0; c < core_count_; ++c) {
        if (core_q16[c] == kFlakyOne)
            continue;
        core_flaky_[c] = core_q16[c];
        core_table(c);
        const CoreNoise& n = cores_n_[c];
        cores_[c].clock.left =
            geometric_gap(n.p[10], Rng(seed_, env_->place_id, kPurposeErrors + c, Rng::kInitTick).next());
    }
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        if (memory_q16[d] == kFlakyOne)
            continue;
        flaky_mem_[d] = memory_q16[d];
        GapLog log;
        const u64 p = cosmic_of(d, &log);
        cosmic_left_[d] =
            p ? geometric_gap(p, log, Rng(seed_, env_->place_id, kPurposeCosmic + d, Rng::kInitTick).next()) : ~0ull;
    }
}

// A memory device's cosmic-ray rate now, and gap_log of it: the world's,
// times the device's factor if it is flaky.
u64 Noise::cosmic_of(u32 d, GapLog* log) {
    if (flaky_mem_[d] == kFlakyOne) {
        *log = cosmic_log_;
        return cosmic_p64_;
    }
    const u64 p = scale_q16(cosmic_p64_, flaky_mem_[d]);
    if (p != flaky_p_[d]) {
        flaky_p_[d] = p;
        flaky_log_[d] = keep_gap_log(p);
    }
    *log = flaky_log_[d];
    return p;
}

void Noise::init_hammer(const HammerSettings& s, const u32* thresholds) {
    hammer_p64_ = 0;
    hammer_log_ = GapLog();
    hammer_window_ = s.window ? s.window : 1;
    hammer_at_.resize(0);
    hammered = hammer = 0;
    u32 lowest = 0;
    if (s.strength > 0) {
        hammer_p64_ = s.strength >= 1 ? ~0ull : static_cast<u64>(s.strength * 18446744073709551616.0);
        if (!hammer_p64_)
            hammer_p64_ = 1;
        hammer_log_ = keep_gap_log(hammer_p64_);
        for (u32 d = 0; d < mem_->device_count(); ++d) {
            hammer_at_.push(thresholds[d] ? thresholds[d] : 1);
            lowest = !lowest || hammer_at_[d] < lowest ? hammer_at_[d] : lowest;
        }
    }
    mem_->set_hammer(lowest);
}

// Appendix C's step 8, rowhammer's part. Each listed page: the activations
// it took this tick past its device's threshold, and for each a chance of a
// bit in the page either side. Every draw is the page's own for the tick, so
// the order the pages are gone through changes nothing.
void Noise::hammer_flips(const DeviceState* states, Array<u32>* failed) {
    if (!hammer_p64_)
        return;
    Array<u32>& hot = mem_->hot();
    Array<u32>& seen = mem_->hot_seen();
    seen.resize(hot.size()); // a page listed this tick: nothing seen of it yet
    const u32 lowest = mem_->hot_at(), size = mem_->page_size();
    const u32 rb = mem_->region_base(), re = rb + mem_->region_size();
    u8* bytes = mem_->bytes();
    bool region = false;
    u32 kept = 0;
    for (u32 i = 0; i < hot.size(); ++i) {
        const u32 page = hot[i], count = mem_->activations_of(page);
        if (count < lowest)
            continue; // its device was replaced, and its count began again
        const u32 d = mem_->page(page).device;
        const u32 from = seen[i] > hammer_at_[d] ? seen[i] : hammer_at_[d];
        hot[kept] = page;
        seen[kept++] = count;
        if (count <= from || gone_[d] || !states[d].on)
            continue;
        const u64 past = count - from;
        hammered += past;
        const MemDevice& dv = mem_->device(d);
        Rng r(seed_, env_->place_id, kPurposeHammer, (env_->tick << 28) | page);
        for (u32 side = 0; side < 2; ++side) {
            if (side == 0 ? page == dv.first_page : page + 1 == dv.end_page)
                continue; // no page that side in its device
            const u32 base = (side == 0 ? page - 1 : page + 1) * size;
            // The count: how many of the activations flip a bit.
            u64 flips = 0, done = 0;
            for (;;) {
                const u64 gap = geometric_gap(hammer_p64_, hammer_log_, r.next());
                if (gap > past - done)
                    break;
                done += gap;
                ++flips;
            }
            // Then the bits.
            for (u64 f = 0; f < flips; ++f) {
                const u32 addr = base + static_cast<u32>(r.below(size));
                bytes[addr] ^= static_cast<u8>(1u << r.below(8));
                region = region || (addr >= rb && addr < re);
                if (failed)
                    failed->push(addr);
                ++hammer;
            }
        }
    }
    hot.resize(kept);
    seen.resize(kept);
    if (region)
        mem_->note_region_change(); // halted cores may start again (10.2)
    if ((env_->tick + 1) % hammer_window_ == 0)
        mem_->window_end(); // the refresh window's end: every count starts again
}

void Noise::renew_core(u32 core) { redraw(&cores_[core].clock); }

void Noise::set_fresh(u32 d, u32 fresh) {
    const Rng keys(seed_, env_->place_id, kPurposeFade + d, fresh ? fresh : Rng::kInitTick);
    for (u32 k = 0; k < 4; ++k)
        fade_[d].key[k] = keys.at(k);
}

void Noise::renew_memory(u32 d, u32 fresh) {
    set_fresh(d, fresh);
    fade_[d].dose = fade_[d].done = 0;
    const Rng r(seed_, env_->place_id, kPurposeFresh + 64 + d, fresh);
    GapLog cosmic_log;
    const u64 cosmic_p = cosmic_of(d, &cosmic_log);
    cosmic_left_[d] = geometric_gap(cosmic_p, cosmic_log, r.at(0));
    mem_rate_[d] = 0; // its edge errors' countdown is drawn at the rate the next tick finds
    mem_left_[d] = ~0ull;
    weak_left_[d] = weak_p64_ ? geometric_gap(weak_p64_, weak_log_, r.at(1)) : ~0ull;
    rise_[d] = 0;
    weak_n_[d] = 0;
    gone_[d] = 0;
    if (hammer_p64_) // a new device's rows have taken no activations
        mem_->clear_activations(mem_->device(d).first_page, mem_->device(d).end_page);
}

// The weak cells' step (7.4): each device that's on counts this tick's
// weak-cell-ticks down, rank by rank among its weak cells, and the cell the
// countdown ends on loses its bit.
void Noise::weak_flips(const DeviceState* states, Array<u32>* failed) {
    if (!weak_p64_)
        return;
    u8* bytes = mem_->bytes();
    const u32 rb = mem_->region_base(), re = rb + mem_->region_size();
    bool region = false;
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        const u64 n = weak_n_[d];
        u64& left = weak_left_[d];
        if (!n || gone_[d] || !states[d].on || left == ~0ull)
            continue;
        const MemDevice& dv = mem_->device(d);
        Rng r(seed_, env_->place_id, kPurposeWeak + d, env_->tick);
        u64 base = 0; // weak cells of this tick already passed
        while (left <= n - base) {
            const u64 rank = base + left - 1;
            const u64 cell = cell_of(d, rank);
            const u32 addr = dv.base + static_cast<u32>(cell >> 3);
            // A bit it fails to hold reads back random (7.4).
            if (set_bit(bytes + addr, static_cast<u32>(cell & 7), (r.next() & 1) != 0) && addr >= rb && addr < re)
                region = true;
            if (failed)
                failed->push(addr);
            ++weak;
            base = rank + 1;
            left = geometric_gap(weak_p64_, weak_log_, r.next());
        }
        left -= n - base;
    }
    if (region)
        mem_->note_region_change(); // halted cores may start again (10.2)
}

// The falling rate's step at a tick: tick × 8 ÷ half-life, at most the last.
u64 Noise::fall_step(u64 tick) const {
    const u64 h = s_.cosmic_half_life;
    const u64 k = tick / h * kFallPerHalf + tick % h * kFallPerHalf / h;
    return k < kFallSteps ? k : kFallSteps;
}

namespace {

// How many cosmic rays' flips are drawn ahead of being made (a power of two):
// more than most devices take in a tick (optimization-plan.md, pass 12).
constexpr u32 kFlipsAhead = 1024;

// A cosmic ray's flip: its bit; a write in the reset region noted, as halted
// cores may start again (10.2); and its byte listed for the saved states'
// check, when there is one.
inline void flip_one(Memory* mem, u8* bytes, u32 addr, u8 bit, u32 rb, u32 re, Array<u32>* failed) {
    bytes[addr] ^= bit;
    if (addr >= rb && addr < re)
        mem->note_region_change();
    if (failed)
        failed->push(addr);
}

} // namespace

void Noise::cosmic_flips(Array<u32>* failed) {
    u8* bytes = mem_->bytes();
    const u32 rb = mem_->region_base(), re = rb + mem_->region_size();
    // A falling rate: this tick's, and whether it changed since the last
    // (the countdowns were drawn at step 0's rate at setup).
    bool redraw = false;
    if (!fall_.empty()) {
        const u64 t = env_->tick;
        const u64 p = fall_[fall_step(t)];
        if (p != cosmic_p64_) {
            cosmic_p64_ = p;
            cosmic_log_ = keep_gap_log(p);
        }
        redraw = t > 0 && p != fall_[fall_step(t - 1)];
    }
    for (u32 d = 0; d < mem_->device_count(); ++d) {
        u64& left = cosmic_left_[d];
        if ((left == ~0ull && !redraw) || gone_[d])
            continue;
        const MemDevice& dv = mem_->device(d);
        const u64 bits = static_cast<u64>(dv.size) * 8;
        Rng r(seed_, env_->place_id, kPurposeCosmic + d, env_->tick);
        GapLog p_log;
        const u64 p = cosmic_of(d, &p_log); // the world's rate, or a flaky device's own
        if (redraw) { // a countdown at a fixed rate has no memory, so a redraw keeps the new rate exact
            left = p ? geometric_gap(p, p_log, r.next()) : ~0ull;
            if (left == ~0ull)
                continue;
        }
        u64 base = 0; // bits of this tick already passed
        u64 gap = left;
        const bool never = p == 0 || p_log.log == 0; // geometric_gap's never
        // Where the flips fall is drawn up to kFlipsAhead ahead of making
        // them, and the host asked for each byte as it is drawn. A flip falls
        // anywhere in the device, so making it is a wait for memory: this way
        // most devices' flips for the tick are all drawn before the first is
        // made, and the waits overlap. The draws and the flips are each in the
        // order they always were.
        u32 at[kFlipsAhead];
        u8 bit[kFlipsAhead];
        u32 head = 0, waiting = 0; // the oldest flip drawn and not made, and how many
        u64 made = 0;
        while (gap <= bits - base) {
            const u64 idx = base + gap - 1;
            const u32 addr = dv.base + static_cast<u32>(idx >> 3);
            _mm_prefetch(reinterpret_cast<const char*>(bytes + addr), _MM_HINT_T0);
            if (waiting == kFlipsAhead) { // the oldest flip, made, frees its place for this one
                flip_one(mem_, bytes, at[head], bit[head], rb, re, failed);
                ++made;
            } else {
                ++waiting;
            }
            at[head] = addr;
            bit[head] = static_cast<u8>(1u << (idx & 7));
            head = (head + 1) & (kFlipsAhead - 1);
            base = idx + 1;
            gap = never ? ~0ull : gap_of(p_log, r.next());
        }
        for (u32 i = (head - waiting) & (kFlipsAhead - 1); waiting; --waiting, i = (i + 1) & (kFlipsAhead - 1)) {
            flip_one(mem_, bytes, at[i], bit[i], rb, re, failed);
            ++made;
        }
        cosmic += made;
        left = gap - (bits - base);
    }
}

} // namespace nova2
