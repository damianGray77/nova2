// The replication current (current.h; design.md 12.6; build-plan-gate5.md
// step 9).
#include <cmath>

#include "nova2/current.h"
#include "nova2/keys.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

constexpr double kTurn = 6.283185307179586;

// A 64-bit draw as a number from 0 up to 1.
double unit_of(u64 v) { return static_cast<double>(v >> 11) * 0x1.0p-53; }

} // namespace

void read_current_settings(Config& cfg, CurrentSettings* s) {
    s->strength = key_f64(cfg, "current.strength");
    s->block = key_u32(cfg, "current.block");
    s->season = key_u64(cfg, "current.season");
    s->turn = key_f64(cfg, "current.turn");
    s->strength_min = key_f64(cfg, "current.strength.min");
    s->strength_max = key_f64(cfg, "current.strength.max");
    if (s->strength_max < s->strength_min)
        cfg.fail("current.strength.max", "must be at least current.strength.min");
    s->night = key_f64(cfg, "current.night");
    s->day = key_u64(cfg, "supply.day");
}

void Current::init(const CurrentSettings& s, u64 seed) {
    s_ = s;
    seed_ = seed;
    have_ = false;
    tick_ = 0;
    if (on())
        seek(0);
}

double Current::knot_turn(u64 k) const {
    const double u = unit_of(Rng(seed_, Rng::kWorldPlace, kPurposeSeason, k).at(0));
    return k == 0 ? u : s_.turn * (2 * u - 1);
}

double Current::knot_heading(u64 k) const {
    double h = 0;
    for (u64 j = 0; j <= k; ++j)
        h += knot_turn(j);
    return h;
}

double Current::knot_strength(u64 k) const {
    const double u = unit_of(Rng(seed_, Rng::kWorldPlace, kPurposeSeason, k).at(1));
    return s_.strength_min * pow(s_.strength_max / s_.strength_min, u);
}

void Current::seek(u64 tick) {
    tick_ = tick;
    const u64 k = tick / s_.season;
    if (have_ && k == knot_ + 1) {
        h0_ = h1_;
        f0_ = f1_;
        h1_ = h0_ + knot_turn(k + 1);
        f1_ = knot_strength(k + 1);
    } else if (!have_ || k != knot_) {
        h0_ = knot_heading(k);
        h1_ = h0_ + knot_turn(k + 1);
        f0_ = knot_strength(k);
        f1_ = knot_strength(k + 1);
    }
    have_ = true;
    knot_ = k;
    const double f = static_cast<double>(tick % s_.season) / static_cast<double>(s_.season);
    const double ease = f * f * (3 - 2 * f); // the smoothstep (supply.cpp)
    heading_ = h0_ + (h1_ - h0_) * ease;
    factor_ = f0_ + (f1_ - f0_) * ease;
    dx_ = cos(kTurn * heading_);
    dy_ = sin(kTurn * heading_);
}

double Current::day_at(u64 tick, u64 phase) const {
    if (s_.night >= 1)
        return 1;
    const double d = static_cast<double>(s_.day);
    const double x = static_cast<double>((tick + phase) % s_.day); // 0 at midnight, d / 2 at noon
    const double w = x * (d - x);
    const double b = 16 * w / (5 * d * d - 4 * w); // sin(pi x / d), by Bhaskara's approximation
    return s_.night + (1 - s_.night) * b * b;
}

double Current::chance(u64 phase, double gust) const {
    return s_.strength * factor_ * day_at(tick_, phase) * gust / static_cast<double>(s_.day);
}

bool Current::copies(u32 place, double chance) const {
    return unit_of(Rng(seed_, place, kPurposeCurrent, tick_).at(0)) < chance;
}

Current::Draw Current::draw(u32 place, u32 span) const {
    const Rng r(seed_, place, kPurposeCurrent, tick_);
    Draw d;
    u64 hi = 0;
    _umul128(r.at(1), span, &hi);
    d.addr = static_cast<u32>(hi);
    d.link = unit_of(r.at(2));
    d.spot = static_cast<u32>(r.at(3) >> 32);
    return d;
}

double Current::weight(double x0, double y0, double x1, double y1) const {
    const double vx = x1 - x0, vy = y1 - y0;
    const double len = sqrt(vx * vx + vy * vy);
    if (!(len > 0))
        return 0;
    const double c = (vx * dx_ + vy * dy_) / len;
    return c > 0 ? c : 0;
}

} // namespace nova2
