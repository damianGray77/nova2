#include "nova2/wear.h"

#include <cmath>    // pow: the table and the drawn lives, worked at setup
#include <cstdio>   // snprintf
#include <intrin.h> // _BitScanReverse64

#include "nova2/keys.h"
#include "nova2/noise.h"    // to_p64
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/supply.h"   // mul_div_u64

namespace nova2 {

namespace {

// a / b rounded down, for b > 0.
i64 floor_div(i64 a, i64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

} // namespace

void read_wear_settings(Config& cfg, WearSettings* out) {
    WearSettings& s = *out;
    s.rate = key_f64(cfg, "wear.rate");
    s.heat = key_f64(cfg, "wear.heat");
    s.reference = key_f64(cfg, "wear.reference");
    s.voltage = key_f64(cfg, "wear.voltage");
    s.rise = key_u32(cfg, "wear.threshold_rise");
    for (u32 k = 0; k < kHeatKinds; ++k) {
        char key[48];
        snprintf(key, sizeof key, "wear.%s.endurance", kHeatKindNames[k]);
        s.endurance[k] = key_u64(cfg, key);
    }
    s.weak_share = key_f64(cfg, "wear.weak.share");
    s.weak_chance = key_f64(cfg, "wear.weak.chance");
}

void WearLaw::init(const WearSettings& s) {
    on = s.rate > 0;
    rate = s.rate <= 0 ? 0 : static_cast<u64>(s.rate * static_cast<double>(kWearTick) + 0.5);
    if (on && rate == 0)
        rate = 1;
    reference = static_cast<i64>(s.reference * 1e6 + (s.reference < 0 ? -0.5 : 0.5));
    heat32k = static_cast<i64>(s.heat * 32000.0 + 0.5);
    volt32k = static_cast<i64>(s.voltage * 32000.0 + 0.5);
    for (u32 k = 0; k < 32; ++k)
        frac[k] = static_cast<u32>(pow(2.0, k / 32.0) * 65536.0 + 0.5);
    rise = static_cast<i32>(s.rise);
    for (u32 k = 0; k < kHeatKinds; ++k)
        endurance[k] = s.endurance[k] ? s.endurance[k] : 1;
    weak_share = s.weak_share <= 0 ? 0 : s.weak_share >= 1 ? 1ull << 32 : static_cast<u64>(s.weak_share * 4294967296.0);
    weak_p64 = to_p64(s.weak_chance);
}

u64 WearLaw::step(i64 t, i32 mv, i32 maximum) const {
    if (!on)
        return 0;
    // The exponent, in 32nds of a doubling: heat's part either way from the
    // reference, the voltage's only over the maximum.
    i64 k = heat32k ? floor_div((t - reference) * heat32k, 10 * kDegree * 1000) : 0;
    if (mv > maximum && volt32k)
        k += static_cast<i64>(mv - maximum) * volt32k / (100 * 1000);
    const i64 n = floor_div(k, 32);
    const u64 v = rate * frac[static_cast<u32>(k - n * 32)]; // in 2^-16ths of a wear unit
    const i64 shift = n - 16;
    if (shift <= 0)
        return shift <= -64 ? 0 : v >> static_cast<u32>(-shift);
    unsigned long msb = 0;
    _BitScanReverse64(&msb, v | 1);
    if (shift + static_cast<i64>(msb) >= 62)
        return kWearMost; // past any life: it fails at once
    return v << static_cast<u32>(shift);
}

u64 WearLaw::life(u64 seed, u32 place, u32 slot, u32 kind, u32 fresh) const {
    // Evenly on a log scale from half to twice: the endurance x 2^(2u - 1).
    const Rng r(seed, place, kPurposeWear + slot, Rng::kInitTick);
    const double u = static_cast<double>(r.at(fresh) >> 11) * 0x1.0p-53;
    double ticks = static_cast<double>(endurance[kind]) * pow(2.0, 2.0 * u - 1.0);
    if (ticks < 1)
        ticks = 1;
    const double most = static_cast<double>(kWearMost >> kWearBits);
    const u64 whole = ticks >= most ? kWearMost >> kWearBits : static_cast<u64>(ticks + 0.5);
    return whole << kWearBits;
}

i32 WearLaw::risen(u64 wear, u64 life) const {
    if (!rise || !life)
        return 0;
    return wear >= life ? rise : static_cast<i32>(mul_div_u64(static_cast<u64>(rise), wear, life));
}

u64 WearLaw::weak_cells(u64 wear, u64 life, u64 cells) const {
    if (!weak_share || !life)
        return 0;
    const u64 at_end = mul_div_u64(cells, weak_share, 1ull << 32);
    return wear >= life ? at_end : mul_div_u64(at_end, wear, life);
}

} // namespace nova2
