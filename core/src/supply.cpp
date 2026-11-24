#include "nova2/supply.h"

#include <cmath>    // pow, ldexp: the schedule's tables, worked out once at setup
#include <intrin.h> // _umul128, _udiv128

#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

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

// The same, rounded to the nearest.
u64 mul_div_round(u64 a, u64 b, u64 c) {
    u64 hi;
    u64 lo = _umul128(a, b, &hi);
    const u64 half = c / 2;
    lo += half;
    if (lo < half)
        ++hi;
    if (hi >= c)
        return ~0ull;
    u64 rem;
    return _udiv128(hi, lo, c, &rem);
}

u64 add_sat(u64 a, u64 b) { return a + b < a ? ~0ull : a + b; }

// A share in [0, 1] as 2^-20ths, rounded.
u32 to_capacity(double share) {
    if (share <= 0)
        return 0;
    if (share >= 1)
        return kCapacityOne;
    return static_cast<u32>(share * kCapacityOne + 0.5);
}

// A chance in [0, 1] as a threshold for a 64-bit draw.
u64 to_threshold(double p) {
    if (p <= 0)
        return 0;
    if (p >= 1)
        return ~0ull;
    return static_cast<u64>(ldexp(p, 64));
}

} // namespace

u64 mul_div_u64(u64 a, u64 b, u64 c) { return mul_div(a, b, c); }
u64 mul_div_round_u64(u64 a, u64 b, u64 c) { return mul_div_round(a, b, c); }

u64 dropout_block(u64 longest) {
    u64 b = 1;
    while (b < longest)
        b <<= 1;
    return b;
}

u64 source_conductance(i32 voltage, i32 sag, u64 rated_uw) {
    if (sag <= 0 || sag >= voltage || rated_uw == 0)
        return 0;
    // Gs = G × V ÷ (Vs − V), with G = P ÷ V² the load's conductance at the
    // sagged voltage V.
    const u64 v = static_cast<u64>(voltage - sag);
    const u64 g = mul_div(rated_uw, 1ull << 20, v * v);
    return mul_div(g, v, static_cast<u64>(sag));
}

bool CapacityLaw::init(const SupplySettings& s, TextBuf* why) {
    if (s.day == 0 || s.day > kMaxDay || s.swing_period == 0 || s.dropout_min == 0 ||
        s.dropout_max < s.dropout_min) {
        if (why)
            why->append("supply: the day, the swing period and the drop-outs' lengths must be at least a tick");
        return false;
    }
    day_ = s.day;
    night_ = to_capacity(s.night);
    // Swings.
    swing_ = to_capacity(s.swing);
    period_ = s.swing_period;
    // Drop-outs: the chance one starts in a stretch, and the table of lengths.
    block_ = dropout_block(s.dropout_max);
    const double p = s.dropouts / 1e6 * static_cast<double>(block_);
    if (p > 1) {
        if (why)
            why->append("supply.dropouts: too many for drop-outs this long (at most one starts in each stretch)");
        return false;
    }
    start_ = to_threshold(p);
    blackout_ = to_threshold(s.blackout_share);
    keep_min_ = to_capacity(s.keep_min);
    keep_max_ = to_capacity(s.keep_max);
    if (keep_max_ < keep_min_) {
        if (why)
            why->append("supply.dropout.keep_max: must be at least supply.dropout.keep_min");
        return false;
    }
    const double lo = static_cast<double>(s.dropout_min), hi = static_cast<double>(s.dropout_max);
    for (u32 k = 0; k < kDropoutLengths; ++k) {
        const double f = (k + 0.5) / kDropoutLengths;
        double len = lo * pow(hi / lo, f) + 0.5;
        if (len < lo)
            len = lo;
        if (len > hi)
            len = hi;
        lengths_[k] = static_cast<u32>(len);
    }
    return true;
}

u32 CapacityLaw::day_at(u64 at) const {
    if (night_ >= kCapacityOne)
        return kCapacityOne;
    const u64 d = day_;
    const u64 x = at % d; // 0 at midnight, d/2 at noon
    const u64 w = x * (d - x);
    // sin(pi x / d) by Bhaskara's approximation, 16w / (5d^2 - 4w), in 2^-20ths.
    const u64 b = (16 * w << kCapacityBits) / (5 * d * d - 4 * w);
    const u64 sun = b * b >> kCapacityBits; // sin^2: 0 at midnight, 1 at noon
    return night_ + static_cast<u32>((static_cast<u64>(kCapacityOne - night_) * sun) >> kCapacityBits);
}

u32 CapacityLaw::knot(u64 seed, u32 stream, u32 purpose, u64 i) const {
    Rng r(seed, stream, purpose, i);
    return kCapacityOne - static_cast<u32>((static_cast<u64>(swing_) * (r.next() >> 44)) >> kCapacityBits);
}

u32 CapacityLaw::swing(u64 seed, u32 stream, u32 purpose, u64 tick) const {
    if (swing_ == 0)
        return kCapacityOne;
    const u64 i = tick / period_, u = tick % period_;
    const i64 a = knot(seed, stream, purpose, i), b = knot(seed, stream, purpose, i + 1);
    const u64 f = (u << kCapacityBits) / period_;
    const u64 f2 = f * f >> kCapacityBits;
    const u64 h = f2 * (3ull * kCapacityOne - 2 * f) >> kCapacityBits; // smoothstep: 3f^2 - 2f^3
    return static_cast<u32>(a + (b - a) * static_cast<i64>(h) / static_cast<i64>(kCapacityOne));
}

bool CapacityLaw::dropout_in(u64 seed, u32 stream, u32 purpose, u64 j, Dropout* d) const {
    if (start_ == 0)
        return false;
    Rng r(seed, stream, purpose, j);
    if (r.next() >= start_)
        return false;
    // Every draw is taken, in order, whatever is used.
    d->start = j * block_ + r.below(block_);
    d->length = lengths_[r.next() >> 54]; // 10 bits: kDropoutLengths
    const bool blackout = r.next() < blackout_;
    const u32 keep = keep_min_ + static_cast<u32>(r.below(static_cast<u64>(keep_max_ - keep_min_) + 1));
    d->keep = blackout ? 0 : keep;
    return true;
}

u32 CapacityLaw::dropout(u64 seed, u32 stream, u32 purpose, u64 tick, Dropout* which) const {
    u32 c = kCapacityOne;
    if (start_ == 0)
        return c;
    const u64 j = tick / block_;
    for (u64 k = j ? j - 1 : 0; k <= j; ++k) {
        Dropout d;
        if (dropout_in(seed, stream, purpose, k, &d) && tick >= d.start && tick - d.start < d.length &&
            d.keep < c) {
            c = d.keep;
            if (which)
                *which = d;
        }
    }
    return c;
}

bool Supply::init(const SupplySettings& s, u64 seed, u32 place, u64 full_draw_uw, TextBuf* why) {
    seed_ = seed;
    place_ = place;
    vs_ = s.voltage;
    sag_ = s.sag;
    if (vs_ <= 0 || sag_ < 0 || sag_ >= vs_) {
        if (why)
            why->append("supply.sag: must be below supply.voltage");
        return false;
    }
    if (!law_.init(s, why))
        return false;
    // The source's conductance, from the place's full draw at the full-draw
    // voltage.
    full_draw_ = full_draw_uw;
    gs_ = source_conductance(vs_, sag_, full_draw_uw);
    stiff_ = gs_ == 0;
    // The day, at the place's own phase (as if at a random longitude).
    Rng r(seed, place, kPurposeSupply, Rng::kInitTick);
    phase_ = r.below(s.day);
    return true;
}

u32 Supply::day(u64 tick) const { return law_.day_at(tick + phase_); }
u32 Supply::swing(u64 tick) const { return law_.swing(seed_, place_, kPurposeSwing, tick); }
bool Supply::dropout_in(u64 j, Dropout* d) const { return law_.dropout_in(seed_, place_, kPurposeDropout, j, d); }
u32 Supply::dropout(u64 tick, Dropout* which) const {
    return law_.dropout(seed_, place_, kPurposeDropout, tick, which);
}

u32 Supply::capacity(u64 tick) const {
    u64 c = day(tick);
    c = c * swing(tick) >> kCapacityBits;
    c = c * dropout(tick) >> kCapacityBits;
    return static_cast<u32>(c);
}

i32 Supply::voltage(u32 c, u64 last_draw_uw, i32 last_mv) const {
    if (c == 0)
        return 0;
    if (stiff_)
        return vs_;
    // The place's conductance from its last tick, G = P / V^2 (appendix C 2).
    const u64 g = last_mv > 0 && last_draw_uw > 0
                      ? mul_div(last_draw_uw, 1ull << 20, static_cast<u64>(last_mv) * static_cast<u64>(last_mv))
                      : 0;
    if (g == 0)
        return vs_; // nothing drawn: the source's open-circuit voltage
    const u64 y = mul_div(gs_, c, kCapacityOne);
    if (y == 0)
        return 0;
    // V = Vs x Gs / (Gs + G) (appendix C 3, one source and one place).
    return static_cast<i32>(mul_div_round(static_cast<u64>(vs_), y, add_sat(y, g)));
}

} // namespace nova2
