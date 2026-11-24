#include "nova2/power.h"

namespace nova2 {

namespace {
constexpr u32 kShareBits = 20;
} // namespace

u32 core_speed(const CorePower& k, i32 mv) {
    i32 over = mv - k.threshold;
    if (over <= 0)
        return 0;
    if (over > k.maximum - k.threshold)
        over = k.maximum - k.threshold;
    return static_cast<u32>(static_cast<u64>(k.slope) * static_cast<u64>(over) / 1000);
}

u64 core_work_uw(const CorePower& k, i32 mv) {
    if (mv <= 0)
        return 0;
    const u64 v = static_cast<u64>(mv);
    // k × s in nW per V², then × V twice (mV ÷ 1000 each): µW.
    const u64 per_v2 = static_cast<u64>(k.draw_nw) * core_speed(k, mv) / 1000;
    return per_v2 * v / 1000 * v / 1000;
}

u64 over_maximum(u64 uw, i32 mv, i32 maximum) {
    if (mv <= maximum || maximum <= 0)
        return uw;
    const u64 v = static_cast<u64>(mv), m = static_cast<u64>(maximum);
    return uw * v / m * v / m;
}

PowerChange step_heat(bool* on, bool* tripped, u64* off_at, i32 mv, i32 threshold, i64 t, i64 limit, i64 cool,
                      bool heat, const PowerSettings& s, u64 tick) {
    if (!heat)
        return step_device(on, off_at, mv, threshold, s, tick);
    if (*tripped) {
        if (t > cool)
            return PowerChange::none; // still cooling
        *tripped = false;
        if (mv >= threshold) {
            *on = true;
            return PowerChange::restarted;
        }
        *off_at = tick; // cooled, but the voltage won't carry it: cut out from now
        return PowerChange::none;
    }
    if (*on) {
        if (t >= limit) {
            *on = false;
            *tripped = true;
            return PowerChange::tripped;
        }
        return step_device(on, off_at, mv, threshold, s, tick);
    }
    const PowerChange ch = step_device(on, off_at, mv, threshold, s, tick);
    if (ch == PowerChange::restarted && t >= limit) { // too hot to start
        *on = false;
        *tripped = true;
        return PowerChange::none;
    }
    return ch;
}

u64 core_draw_uw(const CorePower& k, i32 mv, i64 total16, i64 busy16, i64 work10) {
    // A halted core's draw is constant inside the range, and rises above it.
    const u64 idle = over_maximum(static_cast<u64>(k.idle_mw) * 1000, mv, k.maximum);
    if (total16 <= 0)
        return idle;
    if (busy16 < 0)
        busy16 = 0;
    if (busy16 > total16)
        busy16 = total16;
    if (work10 < 0)
        work10 = 0;
    // The shares of the tick in 2^-20ths, so the products stay in 64 bits
    // whatever the speed: the idle share, and the work share (above 1 when
    // it ran above stock).
    const u64 t = static_cast<u64>(total16);
    const u64 idle_share = (static_cast<u64>(total16 - busy16) << kShareBits) / t;
    const u64 work_share = (static_cast<u64>(work10) << kShareBits) / (10 * t);
    return (idle * idle_share + core_work_uw(k, mv) * work_share) >> kShareBits;
}

PowerChange step_device(bool* on, u64* off_at, i32 mv, i32 threshold, const PowerSettings& s, u64 tick) {
    if (*on) {
        if (mv >= threshold)
            return PowerChange::none;
        *on = false;
        *off_at = tick;
        return PowerChange::cut_out;
    }
    if (tick - *off_at < s.off_time || mv < threshold + s.restart_gap)
        return PowerChange::none;
    *on = true;
    return PowerChange::restarted;
}

u64 memory_draw_uw(const PowerSettings& s, u32 devices, u64 activations) {
    return static_cast<u64>(s.memory_hold_mw) * 1000 * devices + static_cast<u64>(s.memory_access_uw) * activations;
}

} // namespace nova2
