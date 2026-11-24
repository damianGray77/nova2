#include "nova2/heat.h"

#include <cmath>    // pow: the fade table, worked once
#include <cstdio>   // snprintf
#include <intrin.h> // _umul128

#include "nova2/keys.h"
#include "nova2/supply.h" // mul_div_u64

namespace nova2 {

const char* const kHeatKindNames[kHeatKinds] = {"p", "e", "memory", "disk", "network", "cooler"};

namespace {

// a x b / c for a signed b, toward nothing.
i64 mul_div_signed(u64 a, i64 b, u64 c) {
    const u64 m = mul_div_u64(a, static_cast<u64>(b < 0 ? -b : b), c);
    const i64 v = m > static_cast<u64>(0x7FFFFFFFFFFFFFFFll) ? 0x7FFFFFFFFFFFFFFFll : static_cast<i64>(m);
    return b < 0 ? -v : v;
}

i64 to_temp(double degrees) { return static_cast<i64>(degrees * 1e6 + (degrees < 0 ? -0.5 : 0.5)); }
u64 to_micro(double v) { return v <= 0 ? 0 : static_cast<u64>(v * 1e6 + 0.5); }

} // namespace

void read_heat_settings(Config& cfg, HeatSettings* out) {
    HeatSettings& s = *out;
    s.on = key_flag(cfg, "heat.on");
    for (u32 k = 0; k < kHeatKinds; ++k) {
        char key[48];
        snprintf(key, sizeof key, "heat.%s.limit", kHeatKindNames[k]);
        s.part[k].limit = key_f64(cfg, key);
        snprintf(key, sizeof key, "heat.%s.sink", kHeatKindNames[k]);
        s.part[k].sink = key_f64(cfg, key);
        snprintf(key, sizeof key, "heat.%s.capacity", kHeatKindNames[k]);
        s.part[k].capacity = key_f64(cfg, key);
    }
    s.cooldown = key_f64(cfg, "heat.cooldown");
    s.leakage = key_f64(cfg, "heat.leakage");
    s.place_cooling = key_f64(cfg, "heat.place.cooling");
    s.place_capacity = key_f64(cfg, "heat.place.capacity");
    s.coast = key_f64(cfg, "heat.coast");
    s.spread = key_f64(cfg, "heat.spread");
    s.fade_from = key_f64(cfg, "heat.fade.from");
    s.fade_halving = key_f64(cfg, "heat.fade.halving");
    s.cooler_share = key_f64(cfg, "cooler.share");
    s.cooler_start = key_f64(cfg, "cooler.start");
    s.cooler_top = key_f64(cfg, "cooler.top");
    if (!(s.cooler_top > s.cooler_start))
        cfg.fail("cooler.top", "must be above cooler.start");
    s.cooler_rate = key_f64(cfg, "cooler.rate");
    s.cooler_draw = key_f64(cfg, "cooler.draw");
    s.cooler_threshold = static_cast<i32>(key_u32(cfg, "cooler.threshold"));
    s.cooler_maximum = static_cast<i32>(key_u32(cfg, "cooler.maximum"));
    if (s.cooler_threshold + static_cast<i32>(key_peek_u64(cfg, "power.restart_gap")) > s.cooler_maximum)
        cfg.fail("cooler.maximum", "must be at least cooler.threshold plus power.restart_gap (4.4)");
    s.outside = static_cast<i32>(key_i64(cfg, "hw.temperature"));
}

void HeatLaw::init(const HeatSettings& s) {
    on = s.on;
    outside = static_cast<i64>(s.outside) * (kDegree / 10);
    for (u32 k = 0; k < kHeatKinds; ++k) {
        limit[k] = to_temp(s.part[k].limit);
        cool[k] = to_temp(s.part[k].limit - s.cooldown);
        sink[k] = to_micro(s.part[k].sink);
        capacity[k] = to_micro(s.part[k].capacity);
        if (capacity[k] == 0)
            capacity[k] = 1;
    }
    leak = s.leakage <= 0 ? 0 : static_cast<u64>(s.leakage * 4294967296.0 + 0.5);
    place_cooling = to_micro(s.place_cooling);
    place_capacity = to_micro(s.place_capacity);
    if (place_capacity == 0)
        place_capacity = 1;
    spread = to_micro(s.spread);
    fade_from = to_temp(s.fade_from);
    fade_halving = to_temp(s.fade_halving);
    if (fade_halving < 1)
        fade_halving = 1;
    for (u32 k = 0; k < 32; ++k)
        fade_frac[k] = static_cast<u32>(pow(2.0, k / 32.0) * 65536.0 + 0.5);
    cooler_start = to_temp(s.cooler_start);
    cooler_top = to_temp(s.cooler_top);
    if (cooler_top <= cooler_start)
        cooler_top = cooler_start + 1;
    cooler_rate = to_micro(s.cooler_rate);
    cooler_draw = to_micro(s.cooler_draw);
    cooler_threshold = s.cooler_threshold;
    cooler_maximum = s.cooler_maximum;
}

u64 HeatLaw::leaky(u64 draw_uw, i64 t) const {
    if (!on || !leak || t <= outside || !draw_uw)
        return draw_uw;
    // leak x degrees above the outside, in 2^-32ths; then that share more.
    const u64 f = mul_div_u64(leak, static_cast<u64>(t - outside), static_cast<u64>(kDegree));
    u64 hi;
    const u64 lo = _umul128(draw_uw, f, &hi);
    return draw_uw + ((hi << 32) | (lo >> 32));
}

u32 HeatLaw::fade_factor(i64 t) const {
    if (!on || t <= fade_from)
        return 1u << 16;
    const u64 q = mul_div_u64(static_cast<u64>(t - fade_from), 32, static_cast<u64>(fade_halving));
    const u64 n = q / 32;
    if (n >= 15)
        return 1u << 31;
    return fade_frac[q % 32] << n;
}

u32 HeatLaw::cooler_share(i64 place_t) const {
    if (place_t <= cooler_start)
        return 0;
    if (place_t >= cooler_top)
        return 1u << 20;
    return static_cast<u32>((static_cast<u64>(place_t - cooler_start) << 20) /
                            static_cast<u64>(cooler_top - cooler_start));
}

i64 heat_flow(u64 rate, i64 t, i64 to) { return mul_div_signed(rate, t - to, static_cast<u64>(kDegree)); }

i64 heat_step_net(i64 t, i64 net, u64 rate, u64 c) {
    // Its balance is net / rate away. A step of net / capacity would pass it
    // when the capacity is under the rate, so the larger of the two divides:
    // it never passes its balance, and the rounding is toward where it is.
    const u64 by = c > rate ? c : rate;
    return by ? t + mul_div_signed(static_cast<u64>(kDegree), net, by) : t;
}

i64 heat_step(i64 t, i64 around, u64 p, u64 h, u64 c, i64* passed) {
    // What's made in it, less what leaves at its heatsink's rate.
    const i64 net = static_cast<i64>(p) - heat_flow(h, t, around);
    const i64 nt = heat_step_net(t, net, h, c);
    if (passed)
        *passed = static_cast<i64>(p) - mul_div_signed(c, nt - t, static_cast<u64>(kDegree));
    return nt;
}

} // namespace nova2
