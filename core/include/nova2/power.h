#pragma once

// Power on the device side (design.md 4.4, 4.5, 7.1, appendix C;
// build-plan-gate2.md step 2): each kind of device's voltage range, its
// speed against voltage, and what it draws.
//
// - Speed is linear above the threshold, up to the maximum (7.5):
//   s = slope × min(max(V − Vth, 0), Vmax − Vth).
// - A core draws its working power, k × V² × s, for the time it runs
//   something (a thread or an idle run), scaled by the clock it runs at, and
//   its idle draw for the time it's halted (4.5).
// - Memory draws a holding power for each device, plus a power for each row
//   activation in the tick (the per-access counters, cpu.h). The disk and
//   the network device draw a constant power at gate 2.
// - A device below its threshold cuts out, and stays off for at least the
//   minimum off-time; it restarts only once the voltage is back at its
//   threshold plus the restart gap (4.1, 4.4, appendix B). While it's off it
//   draws nothing.
//
// All in integers: millivolts, cycles a tick, microwatts. The keys' bounds
// (kMaxMillivolts, the slope's and k's maxima) keep every product in 64 bits.
#include "nova2/types.h"

namespace nova2 {

// The most a setting can make a place's voltage: a strike's (events.h), which
// is above any source's. The products were checked to fit at this.
inline constexpr i32 kMaxMillivolts = 32000;

// A core kind's power fields (7.1).
struct CorePower {
    i32 threshold = 850;  // mV: below it, no speed (it cuts out, at step 4)
    i32 maximum = 1300;   // mV: above it, no more speed
    u32 slope = 166667;   // cycles a tick per volt above the threshold
    u32 draw_nw = 400000; // appendix C's k: nanowatts per volt squared per cycle a tick
    u32 idle_mw = 1000;   // halted
};

struct PowerSettings {
    CorePower p;
    CorePower e{750, 1200, 72000, 138889, 300};
    i32 memory_maximum = 1300; // mV, for every memory device (their thresholds come from their latency, 7.3)
    u32 memory_hold_mw = 500;  // each memory device, always
    u32 memory_access_uw = 500; // each row activation in the tick
    i32 disk_threshold = 600, disk_maximum = 1300;
    u32 disk_mw = 1000;
    u32 network_mw = 1000;
    u32 network_byte_uw = 1; // and per byte sent or received in a tick (11.4)
    i32 network_threshold = 600, network_maximum = 1300;
    i32 restart_gap = 50; // mV above the threshold before a cut-out device restarts (4.4)
    u64 off_time = 20;    // ticks a cut-out device stays off at least (4.1)
};

// A core's speed at mv, in cycles a tick.
u32 core_speed(const CorePower& k, i32 mv);
// A core's working draw at mv and its speed there, run for the whole tick at
// stock clock, in microwatts.
u64 core_work_uw(const CorePower& k, i32 mv);
// A core's draw over a tick, in microwatts. total16: the tick's time, in
// sixteenths of a cycle at its stock speed; busy16: the part it ran
// something; work10: that time weighted by the clock it ran at, in tenths of
// stock (so busy16 × 10 at stock).
u64 core_draw_uw(const CorePower& k, i32 mv, i64 total16, i64 busy16, i64 work10);
// Memory's draw over a tick, in microwatts.
u64 memory_draw_uw(const PowerSettings& s, u32 devices, u64 activations);

// A device's power state (4.4, appendix B): on, or cut out since a tick.
struct DeviceState {
    bool on = true;
    u64 off_at = 0; // the tick it cut out
};

enum class PowerChange : u8 { none, cut_out, restarted, tripped };

// A device's state for this tick at mv (appendix C step 4): one that's on
// cuts out below its threshold; one that's cut out restarts at its threshold
// plus the restart gap, once the off-time has passed since it cut out.
PowerChange step_device(bool* on, u64* off_at, i32 mv, i32 threshold, const PowerSettings& s, u64 tick);
inline PowerChange step_device(DeviceState* d, i32 mv, i32 threshold, const PowerSettings& s, u64 tick) {
    return step_device(&d->on, &d->off_at, mv, threshold, s, tick);
}

// The same with heat (5; heat.h; build-plan-gate4.md step 5), for a device
// at temperature t whose kind trips at or past `limit` and comes back at or
// under `cool`. A device that's on trips there (tripped), before its voltage
// is looked at. A tripped one stays off whatever the voltage until it has
// cooled; then it's back at once if the voltage is at its threshold
// (restarted: a heat trip needs no off-time), and otherwise is cut out from
// this tick. A cut-out device too hot to start stays off as tripped. With
// `heat` off, step_device and nothing more.
PowerChange step_heat(bool* on, bool* tripped, u64* off_at, i32 mv, i32 threshold, i64 t, i64 limit, i64 cool,
                      bool heat, const PowerSettings& s, u64 tick);

// A constant draw above its device's maximum (4.4, appendix C;
// build-plan-gate4.md 2, gap 5): multiplied by (V / Vmax)^2. Inside the
// range it's unchanged.
u64 over_maximum(u64 uw, i32 mv, i32 maximum);

// A place's draw over its last tick, in microwatts.
struct PlaceDraw {
    u64 cores = 0, memory = 0, disk = 0, network = 0;
    u64 cooling = 0; // its cooling device (7.7), from gate 4
    u64 total() const { return cores + memory + disk + network + cooling; }
};

} // namespace nova2
