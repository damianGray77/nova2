#include "nova2/place.h"

#include <cmath>   // pow: rowhammer's thresholds
#include <cstdio>  // snprintf
#include <cstdlib> // qsort: the saved states, when memory fades
#include <cstring> // memcmp
#include <intrin.h> // __rdtsc: timing a tick's parts, for the benchmark

#include "nova2/hash.h"
#include "nova2/keys.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

// The words of a space-separated list, one at a time.
bool next_word(Str s, usize* at, Str* word) {
    while (*at < s.n && (s.p[*at] == ' ' || s.p[*at] == '\t'))
        ++*at;
    if (*at >= s.n)
        return false;
    const usize b = *at;
    while (*at < s.n && s.p[*at] != ' ' && s.p[*at] != '\t')
        ++*at;
    *word = Str(s.p + b, *at - b);
    return true;
}

const char* const kCallKeys[kCallCount] = {
    "cost.call.alloc", "cost.call.free",       "cost.call.protect", "cost.call.spawn",
    "cost.call.exit",  "cost.call.sleep",      "cost.call.setclock", "cost.call.setcore",
    "cost.call.sethandler", "cost.call.hret",  "cost.call.sense",   "cost.call.send",
    "cost.call.recv",  "cost.call.dread",      "cost.call.dwrite",  "cost.call.ping"};

CorePower read_core_power(Config& cfg, const char* kind) {
    char key[48];
    CorePower k;
    snprintf(key, sizeof key, "hw.%s.threshold", kind);
    k.threshold = static_cast<i32>(key_u32(cfg, key));
    snprintf(key, sizeof key, "hw.%s.maximum", kind);
    k.maximum = static_cast<i32>(key_u32(cfg, key));
    snprintf(key, sizeof key, "hw.%s.slope", kind);
    k.slope = key_u32(cfg, key);
    snprintf(key, sizeof key, "hw.%s.draw", kind);
    k.draw_nw = key_u32(cfg, key);
    snprintf(key, sizeof key, "hw.%s.idle_draw", kind);
    k.idle_mw = key_u32(cfg, key);
    return k;
}

CostTable read_costs(Config& cfg, const char* kind) {
    char key[48];
    CostTable t{};
    snprintf(key, sizeof key, "cost.%s.simple", kind);
    const u16 simple = static_cast<u16>(key_u32(cfg, key));
    for (u32 i = 0; i < 32; ++i)
        t.work[i] = simple;
    snprintf(key, sizeof key, "cost.%s.mul", kind);
    t.work[kMul] = static_cast<u16>(key_u32(cfg, key));
    snprintf(key, sizeof key, "cost.%s.div", kind);
    t.work[kDiv] = static_cast<u16>(key_u32(cfg, key));
    snprintf(key, sizeof key, "cost.%s.cpy_setup", kind);
    t.cpy_setup = static_cast<u16>(key_u32(cfg, key));
    snprintf(key, sizeof key, "cost.%s.cpy_byte", kind);
    t.cpy_per_byte = static_cast<u16>(key_u32(cfg, key));
    return t;
}

} // namespace

void read_world_settings(Config& cfg, WorldSettings* ws) {
    WorldSettings& w = *ws;
    w.seed = key_u64(cfg, "world.seed");
    w.places = key_u32(cfg, "world.places");

    w.cores_min = key_u32(cfg, "hw.cores.min");
    w.cores_max = key_u32(cfg, "hw.cores.max");
    if (w.cores_max < w.cores_min)
        cfg.fail("hw.cores.max", "must be at least hw.cores.min");
    w.e_share = key_f64(cfg, "hw.e_share");
    // The power fields (4.4, 7.1).
    PowerSettings& pw = w.power;
    pw.p = read_core_power(cfg, "p");
    pw.e = read_core_power(cfg, "e");
    pw.network_mw = key_u32(cfg, "hw.network.draw");
    pw.network_byte_uw = key_u32(cfg, "hw.network.byte_draw");
    pw.network_threshold = static_cast<i32>(key_u32(cfg, "hw.network.threshold"));
    pw.network_maximum = static_cast<i32>(key_u32(cfg, "hw.network.maximum"));
    pw.memory_maximum = static_cast<i32>(key_u32(cfg, "memory.maximum"));
    pw.memory_hold_mw = key_u32(cfg, "memory.hold_draw");
    pw.memory_access_uw = key_u32(cfg, "memory.access_draw");
    pw.disk_threshold = static_cast<i32>(key_u32(cfg, "disk.threshold"));
    pw.disk_maximum = static_cast<i32>(key_u32(cfg, "disk.maximum"));
    pw.disk_mw = key_u32(cfg, "disk.draw");
    pw.restart_gap = static_cast<i32>(key_u32(cfg, "power.restart_gap"));
    pw.off_time = key_u64(cfg, "power.off_time");
    w.temperature = static_cast<i32>(key_i64(cfg, "hw.temperature"));
    read_heat_settings(cfg, &w.heat);
    {
        const Str model = key_choice(cfg, "supply.model");
        w.sources = model.n == 7 && memcmp(model.p, "sources", 7) == 0;
    }
    // The supply (4.2, 4.3; supply.h).
    SupplySettings& su = w.supply;
    su.voltage = static_cast<i32>(key_u32(cfg, "supply.voltage"));
    su.sag = static_cast<i32>(key_u32(cfg, "supply.sag"));
    su.day = key_u64(cfg, "supply.day");
    su.night = key_f64(cfg, "supply.night");
    su.swing = key_f64(cfg, "supply.swing");
    su.swing_period = key_u64(cfg, "supply.swing_period");
    su.dropouts = key_f64(cfg, "supply.dropouts");
    su.dropout_min = key_u64(cfg, "supply.dropout.min");
    su.dropout_max = key_u64(cfg, "supply.dropout.max");
    su.blackout_share = key_f64(cfg, "supply.blackout_share");
    su.keep_min = key_f64(cfg, "supply.dropout.keep_min");
    su.keep_max = key_f64(cfg, "supply.dropout.keep_max");
    if (su.sag >= su.voltage)
        cfg.fail("supply.sag", "must be below supply.voltage");
    if (su.dropout_max < su.dropout_min)
        cfg.fail("supply.dropout.max", "must be at least supply.dropout.min");
    if (su.keep_max < su.keep_min)
        cfg.fail("supply.dropout.keep_max", "must be at least supply.dropout.keep_min");
    if (su.dropouts / 1e6 * static_cast<double>(dropout_block(su.dropout_max)) > 1)
        cfg.fail("supply.dropouts", "too many for drop-outs this long: at most one can start in each stretch of the "
                                    "longest's length, rounded up to a power of two");
    // A device that cuts out restarts at its threshold plus the gap, so that
    // must be in its range; and both cores must run at the source's voltage.
    if (pw.p.threshold + pw.restart_gap > pw.p.maximum)
        cfg.fail("hw.p.maximum", "must be at least hw.p.threshold plus power.restart_gap (4.4)");
    if (pw.e.threshold + pw.restart_gap > pw.e.maximum)
        cfg.fail("hw.e.maximum", "must be at least hw.e.threshold plus power.restart_gap (4.4)");
    if (pw.disk_threshold + pw.restart_gap > pw.disk_maximum)
        cfg.fail("disk.maximum", "must be at least disk.threshold plus power.restart_gap (4.4)");
    if (pw.network_threshold + pw.restart_gap > pw.network_maximum)
        cfg.fail("hw.network.maximum", "must be at least hw.network.threshold plus power.restart_gap (4.4)");
    if (core_speed(pw.p, su.voltage) == 0 || core_speed(pw.e, su.voltage) == 0)
        cfg.fail("supply.voltage", "must be above both cores' thresholds, or they never run");

    w.memory_size = key_u32(cfg, "memory.size");
    w.mem_devices_min = key_u32(cfg, "memory.devices.min");
    w.mem_devices_max = key_u32(cfg, "memory.devices.max");
    if (w.mem_devices_max < w.mem_devices_min)
        cfg.fail("memory.devices.max", "must be at least memory.devices.min");
    w.latency_min = key_u32(cfg, "memory.latency.min");
    w.latency_max = key_u32(cfg, "memory.latency.max");
    if (w.latency_max < w.latency_min)
        cfg.fail("memory.latency.max", "must be at least memory.latency.min");
    w.threshold_fast = static_cast<i32>(key_u32(cfg, "memory.threshold.fast"));
    w.threshold_slow = static_cast<i32>(key_u32(cfg, "memory.threshold.slow"));
    if (w.threshold_fast < w.threshold_slow)
        cfg.fail("memory.threshold.fast", "must be at least memory.threshold.slow: fast memory fades first (6.3)");
    if (w.threshold_fast >= w.power.p.threshold || w.threshold_fast >= w.power.e.threshold)
        cfg.fail("memory.threshold.fast", "must be below the cores' thresholds (4.4)");
    if (w.threshold_fast + w.power.restart_gap > w.power.memory_maximum)
        cfg.fail("memory.maximum", "must be at least memory.threshold.fast plus power.restart_gap (4.4)");
    w.class_steps = key_u32(cfg, "memory.class_steps");
    w.max_alloc = key_u32(cfg, "memory.max_alloc");
    w.disk_size = key_u32(cfg, "disk.size");
    w.os.boot_block = key_u32(cfg, "disk.boot_block");
    w.os.disk_latency_cycles = key_u32(cfg, "disk.latency");
    w.os.disk_cycles_per_byte = key_u32(cfg, "disk.cycles_per_byte");
    w.os.block = key_u32(cfg, "disk.block");
    w.os.block_endurance = key_u32(cfg, "disk.block_endurance");
    w.os.worn_flip = key_f64(cfg, "disk.worn_flip");
    w.os.levelling = key_flag(cfg, "os.levelling");
    w.os.levelling_gap = key_u32(cfg, "os.levelling_gap");
    w.region_size = key_u32(cfg, "region.size");

    // The OS values' choices (9.6).
    const Str sizes = key_str(cfg, "os.page_sizes");
    w.page_size_count = 0;
    usize at = 0;
    Str word;
    while (next_word(sizes, &at, &word)) {
        u64 v = 0;
        if (!parse_u64(word, &v) || v < 8 || v > kMaxPageSize || (v & (v - 1)) != 0 ||
            w.page_size_count == kMaxPageSizes) {
            cfg.fail("os.page_sizes", "must list up to 8 powers of two from 8 to 4096");
            break;
        }
        w.page_sizes[w.page_size_count++] = static_cast<u32>(v);
    }
    if (w.page_size_count == 0)
        cfg.fail("os.page_sizes", "must list at least one page size");
    w.strict_share = key_f64(cfg, "os.strict_share");
    // The limits on a program's threads (9.6, 2026-11-25): 0 is no limit.
    const Str limits = key_str(cfg, "os.thread_limits");
    w.thread_limit_count = 0;
    at = 0;
    while (next_word(limits, &at, &word)) {
        u64 v = 0;
        if (!parse_u64(word, &v) || v > 0xFFFFFFFFull || w.thread_limit_count == kMaxThreadLimits) {
            cfg.fail("os.thread_limits", "must list up to 8 whole numbers, each 0 (no limit) or at least 1");
            break;
        }
        w.thread_limits[w.thread_limit_count++] = static_cast<u32>(v);
    }
    if (w.thread_limit_count == 0)
        cfg.fail("os.thread_limits", "must list at least one limit (0 for none)");

    // The call numbering (9.2).
    const Str slots = key_str(cfg, "os.slots");
    w.os.slot_count = 0;
    at = 0;
    while (next_word(slots, &at, &word)) {
        u64 v = 0;
        if (!parse_u64(word, &v) || v >= kCallCount || w.os.slot_count == kMaxSlots) {
            cfg.fail("os.slots", "must list up to 64 call numbers, each 0 to 15");
            break;
        }
        w.os.slots[w.os.slot_count++] = static_cast<u8>(v);
    }
    if (w.os.slot_count == 0)
        cfg.fail("os.slots", "must list at least one call");
    w.os.stack_size = key_u32(cfg, "os.stack");
    w.os.ports = key_u32(cfg, "os.ports");
    w.os.clock_min = key_u32(cfg, "os.clock.min");
    w.os.clock_max = key_u32(cfg, "os.clock.max");
    if (w.os.clock_max < w.os.clock_min)
        cfg.fail("os.clock.max", "must be at least os.clock.min");
    w.os.ping_timeout = key_u32(cfg, "os.ping_timeout");
    w.os.send_byte_cycles = key_u32(cfg, "os.send_byte_cycles");
    w.os.inbox_bytes = key_u32(cfg, "net.inbox");
    if (key_u32(cfg, "net.message_max") > w.os.inbox_bytes)
        cfg.fail("net.message_max", "must be no more than net.inbox: a message must fit an inbox (11.2)");
    w.os.cutout_pause = key_flag(cfg, "os.cutout_pause");
    w.os.saved_state_fails = key_flag(cfg, "os.saved_state_fails");
    w.os.idle_send = key_flag(cfg, "os.idle_send");
    w.os.stack_held = key_flag(cfg, "os.stack_held");
    w.os.killer = key_u64(cfg, "os.killer");
    w.os.watchdog = key_u64(cfg, "os.watchdog");
    for (u32 c = 0; c < kCallCount; ++c)
        w.os.call_cycles[c] = key_u32(cfg, kCallKeys[c]);
    w.os.trap_cycles = key_u32(cfg, "cost.trap");
    w.os.call_factor16[0] = 16;
    w.os.call_factor16[1] = key_u32(cfg, "cost.e_calls");

    w.sched.quanta_per_tick = key_u32(cfg, "sched.quanta");
    w.sched.quanta_per_turn = key_u32(cfg, "sched.turn");
    w.sched.switch_cycles = key_u32(cfg, "sched.switch");
    w.sched.clock_switch_cycles = key_u32(cfg, "sched.clock_switch");
    w.sched.low_voltage_margin = static_cast<i32>(key_u32(cfg, "power.low_voltage_margin"));

    w.region.step = key_u32(cfg, "region.step");
    w.region.cap_cycles = key_u32(cfg, "region.cap");
    w.region.retry_ticks = key_u32(cfg, "region.retry");
    w.region.queue_bytes = key_u32(cfg, "region.queue");
    w.region.arrival_share = key_f64(cfg, "region.arrival_share");
    // The boot block waits in port 0's queue (10.5), so it must fit there.
    const u32 boot = w.os.boot_block < w.disk_size ? w.os.boot_block : w.disk_size;
    if (w.region.queue_bytes < boot)
        cfg.fail("region.queue", "must hold the boot block (disk.boot_block, or the whole disk if that's smaller)");

    w.noise.p_floor = key_f64(cfg, "noise.p_floor");
    w.noise.e_floor = key_f64(cfg, "noise.e_floor");
    w.noise.clock_doubling = key_u32(cfg, "noise.clock_doubling");
    w.noise.cosmic = key_f64(cfg, "noise.cosmic");
    w.noise.cosmic_start = key_f64(cfg, "noise.cosmic_start");
    w.noise.cosmic_half_life = key_u64(cfg, "noise.cosmic_half_life");
    if (w.noise.cosmic_start != 0 && w.noise.cosmic_start < w.noise.cosmic)
        cfg.fail("noise.cosmic_start", "must be 0 (no fall) or at least noise.cosmic, the rate it falls to");
    w.noise.edge_band = key_u32(cfg, "noise.edge_band");
    w.noise.edge_doubling = key_u32(cfg, "noise.edge_doubling");
    if (w.noise.edge_band && w.noise.edge_band / w.noise.edge_doubling > 62)
        cfg.fail("noise.edge_doubling", "too small for noise.edge_band: at most 62 doublings fit in the band");
    w.noise.fade_fastest = key_f64(cfg, "memory.fade.fastest");
    w.noise.fade_slowest = key_f64(cfg, "memory.fade.slowest");
    w.noise.fade_at_threshold = key_f64(cfg, "memory.fade.at_threshold");
    if (w.noise.fade_slowest < w.noise.fade_fastest)
        cfg.fail("memory.fade.slowest", "must be at least memory.fade.fastest");

    w.p_costs = read_costs(cfg, "p");
    w.e_costs = read_costs(cfg, "e");

    // Wear (7.4; wear.h). A worn device must still be able to restart.
    read_wear_settings(cfg, &w.wear);
    read_visit_settings(cfg, &w.visit);
    // Flaky parts (7.5).
    w.flaky.share = key_f64(cfg, "flaky.share");
    w.flaky.factor_min = key_f64(cfg, "flaky.factor.min");
    w.flaky.factor_max = key_f64(cfg, "flaky.factor.max");
    w.flaky.worn = key_f64(cfg, "flaky.worn");
    if (w.flaky.factor_max < w.flaky.factor_min)
        cfg.fail("flaky.factor.max", "must be at least flaky.factor.min");
    // Rowhammer (9.8).
    w.hammer.strength = key_f64(cfg, "hammer.strength");
    w.hammer.window = key_u64(cfg, "hammer.window");
    w.hammer.fast = key_u32(cfg, "hammer.threshold.fast");
    w.hammer.slow = key_u32(cfg, "hammer.threshold.slow");
    if (w.hammer.slow < w.hammer.fast)
        cfg.fail("hammer.threshold.slow", "must be at least hammer.threshold.fast");
    if (w.wear.rate > 0) {
        const i32 up = static_cast<i32>(w.wear.rise) + pw.restart_gap;
        if (pw.p.threshold + up > pw.p.maximum || pw.e.threshold + up > pw.e.maximum ||
            w.threshold_fast + up > pw.memory_maximum || pw.disk_threshold + up > pw.disk_maximum ||
            pw.network_threshold + up > pw.network_maximum || w.heat.cooler_threshold + up > w.heat.cooler_maximum)
            cfg.fail("wear.threshold_rise", "too large: every device's threshold, with this and power.restart_gap, "
                                            "must be within its maximum (4.4, 7.4)");
    }
}

bool thread_limits_on(const WorldSettings& ws) {
    for (u32 i = 0; i < ws.thread_limit_count; ++i)
        if (ws.thread_limits[i])
            return true;
    return false;
}

void thread_limit_text(const WorldSettings& ws, u32 limit, char* out, usize size) {
    if (!size)
        return;
    out[0] = 0;
    if (!thread_limits_on(ws))
        return;
    if (!limit)
        snprintf(out, size, ", no thread limit");
    else
        snprintf(out, size, ", %u thread%s a program", limit, limit == 1 ? "" : "s");
}

void draw_hardware(const WorldSettings& ws, u32 id, PlaceHw* out) { draw_hardware(ws, id, out, ws.seed); }

u64 place_seed(u64 seed, u32 id, u32 incarnation) {
    return incarnation ? Rng::stream_base(seed, id, kPurposeRespawn, incarnation) : seed;
}

void draw_hardware(const WorldSettings& ws, u32 id, PlaceHw* out, u64 seed) {
    PlaceHw& p = *out;
    p = PlaceHw{};
    // The hardware, drawn from the ranges (7.3).
    Rng hw(seed, id, kPurposeHardware, Rng::kInitTick);
    p.cores = ws.cores_min + static_cast<u32>(hw.below(ws.cores_max - ws.cores_min + 1));
    if (p.cores > kMaxCores)
        p.cores = kMaxCores;
    for (u32 c = 0; c < p.cores; ++c)
        p.e[c] = hw.chance(ws.e_share) ? 1 : 0;
    p.devices = ws.mem_devices_min + static_cast<u32>(hw.below(ws.mem_devices_max - ws.mem_devices_min + 1));
    const u32 span = ws.latency_max - ws.latency_min;
    for (u32 d = 0; d < p.devices && d < kMaxMemDevices; ++d)
        p.latency[d] = ws.latency_min + static_cast<u32>(hw.below(span + 1));
    // The OS values (9.6).
    Rng osr(seed, id, kPurposeOs, Rng::kInitTick);
    p.page_size = ws.page_sizes[osr.below(ws.page_size_count)];
    p.strict = osr.chance(ws.strict_share);
    // The limit on a program's threads (9.6, 2026-11-25), from a stream of
    // its own, so that adding it moves no other draw.
    Rng tl(seed, id, kPurposeThreadLimit, Rng::kInitTick);
    p.thread_limit = ws.thread_limits[tl.below(ws.thread_limit_count)];
}

bool Place::init(u32 id, const WorldSettings& ws, TextBuf* why) {
    PlaceHw hw;
    draw_hardware(ws, id, &hw);
    return init(id, ws, hw, why);
}

bool Place::init(u32 id, const WorldSettings& ws, const PlaceHw& hw, TextBuf* why, u32 inc) {
    const u64 seed = place_seed(ws.seed, id, inc);
    incarnation = inc;
    // The hardware (7.3).
    core_count_ = hw.cores;
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores[c];
        k = Core{};
        k.kind = hw.e[c] ? DeviceKind::e_core : DeviceKind::p_core;
        k.stock_speed = core_speed(k.kind == DeviceKind::e_core ? ws.power.e : ws.power.p, ws.supply.voltage); // 7.5
        k.speed = k.stock_speed;
    }
    seed_ = seed;
    world_seed_ = ws.seed;
    // Memory devices: the memory shared out in whole kilobytes, each with its
    // own latency; faster memory has the higher threshold (6.3).
    const u32 ndev = hw.devices;
    const u32 kb = ws.memory_size / 1024;
    if (ndev == 0 || kb < ndev || ndev > kMaxMemDevices) {
        if (why)
            why->append("memory.size: too small to give each memory device a kilobyte");
        return false;
    }
    const u32 span = ws.latency_max - ws.latency_min;
    for (u32 d = 0; d < ndev; ++d) {
        const u32 share = d + 1 < ndev ? kb / ndev : kb - (kb / ndev) * (ndev - 1);
        const u32 lat = hw.latency[d];
        devices_[d].size = share * 1024;
        devices_[d].latency16 = lat * 16;
        const i32 fall = ws.threshold_fast - ws.threshold_slow;
        devices_[d].threshold =
            span ? ws.threshold_fast - static_cast<i32>(lat - ws.latency_min) * fall / static_cast<i32>(span)
                 : ws.threshold_fast;
    }
    // The OS values (9.6).
    MemorySpec ms;
    ms.page_size = hw.page_size;
    ms.strict = hw.strict;
    ms.class_steps = ws.class_steps;
    ms.max_alloc = ws.max_alloc;
    ms.region_size = ws.region_size;
    ms.devices = devices_;
    ms.device_count = ndev;
    if (!mem.init(ms, why))
        return false;
    mem_devices_ = ndev;
    mem.enable_counters(true); // memory draws per access (4.5): the counters are always on from gate 2
    power_ = ws.power;
    // Gate 4: where its voltage comes from, and heat. Everything starts at
    // the outside temperature (5), with nothing tripped.
    supplied = ws.sources;
    given_mv = ws.supply.voltage;
    heat.init(ws.heat);
    temp = heat.outside;
    for (u32 c = 0; c < kMaxCores; ++c) {
        core_temp[c] = heat.outside;
        core_tripped[c] = false;
    }
    for (u32 d = 0; d < kMaxMemDevices; ++d) {
        memory_temp[d] = heat.outside;
        memory_tripped[d] = false;
    }
    disk_temp = network_temp = cooler_temp = heat.outside;
    disk_tripped = network_tripped = cooler_tripped = false;
    passed_uw = 0;
    has_cooler = heat.on && hw.cooler;
    cooler_state = DeviceState{};
    cooler_work = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        trips[k] = tripped_ticks[k] = 0;
    heat_threads = cooler_cutouts = region_heat_ticks = 0;
    temp_total = 0;
    temp_highest = heat.outside;
    lv_slept = lv_outlived = 0;
    lv_watch_.resize(0);
    draw = PlaceDraw{};
    for (u32 c = 0; c < kMaxCores; ++c)
        core_draw[c] = 0;
    activations = activations_total = peak_draw = 0;
    energy = PlaceDraw{};

    // What SENSE sees (A.4): cores, memory devices, the disk.
    env = PlaceEnv{};
    env.place_id = id;
    env.tick = 0;
    env.voltage_mv = ws.supply.voltage;
    env.temperature_dc = ws.temperature;
    env.disk_size = ws.disk_size;
    for (u32 c = 0; c < core_count_; ++c) {
        SenseDevice s;
        s.kind = cores[c].kind;
        s.speed = cores[c].stock_speed;
        s.threshold_mv = s.kind == DeviceKind::e_core ? ws.power.e.threshold : ws.power.p.threshold;
        s.memory_index = 0;
        s.size = 0;
        env.devices.push(s);
    }
    for (u32 d = 0; d < ndev; ++d) {
        SenseDevice s;
        s.kind = DeviceKind::memory;
        s.speed = devices_[d].latency16 / 16;
        s.threshold_mv = devices_[d].threshold;
        s.memory_index = d;
        s.size = devices_[d].size;
        env.devices.push(s);
    }
    if (ws.disk_size) {
        SenseDevice s;
        s.kind = DeviceKind::disk;
        s.speed = ws.os.disk_latency_cycles;
        s.threshold_mv = ws.power.disk_threshold;
        s.memory_index = 0;
        s.size = ws.disk_size;
        env.devices.push(s);
    }

    OsSettings oss = ws.os;
    oss.thread_limit = hw.thread_limit; // the place's own (9.6)
    if (!os.init(&mem, cores, core_count_, &env, oss, why))
        return false;
    if (!sched.init(&os, &mem, cores, core_count_, &env, ws.sched, &ws.p_costs, &ws.e_costs, why))
        return false;
    if (!region.init(&os, &mem, cores, core_count_, &env, ws.region, &ws.p_costs, &ws.e_costs, why))
        return false;
    if (!noise.init(&mem, cores, core_count_, &env, seed, ws.noise, ws.power, why))
        return false;
    sched.idle = region.hooks();

    // Wear (wear.h): nothing is worn, and each device's life is drawn.
    wear.init(ws.wear);
    for (u32 c = 0; c < kMaxCores; ++c)
        core_worn[c] = Worn{};
    for (u32 d = 0; d < kMaxMemDevices; ++d) {
        memory_worn[d] = Worn{};
        memory_rise_[d] = 0;
    }
    disk_worn = network_worn = cooler_worn = Worn{};
    os.init_blocks(seed, 0); // the disk's blocks, unwritten (7.4)
    boot_failures = 0;
    init_visits(ws.visit, hw.visits); // the visit schedule, and its first visit (7.8)
    disk_rise_ = network_rise_ = cooler_rise_ = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        failures[k] = failed_ticks[k] = 0;
    failure_threads = 0;
    if (wear.on) {
        for (u32 c = 0; c < core_count_; ++c)
            core_worn[c].life = wear.life(seed, id, c, cores[c].kind == DeviceKind::e_core ? kHeatE : kHeatP, 0);
        for (u32 d = 0; d < ndev; ++d)
            memory_worn[d].life = wear.life(seed, id, kWearSlotMemory + d, kHeatMemory, 0);
        disk_worn.life = wear.life(seed, id, kWearSlotDisk, kHeatDisk, 0);
        network_worn.life = wear.life(seed, id, kWearSlotNetwork, kHeatNetwork, 0);
        cooler_worn.life = wear.life(seed, id, kWearSlotCooler, kHeatCooler, 0);
        noise.init_wear(wear.weak_share ? wear.weak_p64 : 0);
    }
    // Flaky parts (7.5): which of the cores and memory devices are, and
    // each one's factor, from the place's seed.
    {
        u32 cq[kMaxCores], mq[kMaxMemDevices];
        noise.init_flaky(ws.flaky);
        for (u32 c = 0; c < core_count_; ++c)
            cq[c] = noise.flaky_factor(c, 0);
        for (u32 d = 0; d < ndev; ++d)
            mq[d] = noise.flaky_factor(kWearSlotMemory + d, 0);
        noise.start_flaky(cq, mq);
    }
    // Rowhammer (9.8): each memory device's threshold by its latency, the
    // fastest memory's the lowest, evenly on a log scale to the slowest's.
    {
        u32 at[kMaxMemDevices] = {};
        const u32 lats = ws.latency_max - ws.latency_min;
        const double ratio = static_cast<double>(ws.hammer.slow) / static_cast<double>(ws.hammer.fast);
        for (u32 d = 0; d < ndev; ++d) {
            const double part = lats ? static_cast<double>(hw.latency[d] - ws.latency_min) / lats : 0;
            at[d] = static_cast<u32>(ws.hammer.fast * pow(ratio, part) + 0.5);
        }
        noise.init_hammer(ws.hammer, at);
    }

    // The source (supply.h): its internal resistance from the place's full draw.
    if (!supply.init(ws.supply, ws.seed, id, full_draw_uw(ws.supply.voltage - ws.supply.sag), why))
        return false;
    capacity = kCapacityOne;
    mv_total = below_p = below_e = below_region = 0;
    mv_lowest = ws.supply.voltage;
    // Every device starts on (4.4).
    for (u32 d = 0; d < kMaxMemDevices; ++d)
        memory_state[d] = DeviceState{};
    disk_state = network_state = DeviceState{};
    for (u32 k = 0; k < 2; ++k)
        cutouts[k] = off_ticks[k] = 0;
    cut_threads = paused_threads = disk_cutouts = 0;
    memory_cutouts = memory_off_ticks = faded_threads = failed_threads = 0;
    heat_ticks[0] = heat_ticks[1] = region_off_ticks = 0;
    worked20 = 0;
    cold_starts = cold_written = 0;

    // Memory and disk start as static (10.1).
    Rng st(seed, id, kPurposeStatic, Rng::kInitTick);
    u8* bytes = mem.bytes();
    for (u32 i = 0; i < mem.size(); i += 8) {
        const u64 v = st.next();
        for (u32 k = 0; k < 8 && i + k < mem.size(); ++k)
            bytes[i + k] = static_cast<u8>(v >> (8 * k));
    }
    u8* disk = os.disk();
    for (u32 i = 0; i < os.disk_size(); i += 8) {
        const u64 v = st.next();
        for (u32 k = 0; k < 8 && i + k < os.disk_size(); ++k)
            disk[i + k] = static_cast<u8>(v >> (8 * k));
    }
    // The disk's static follows memory's in the stream, eight bytes a draw.
    os.note_static(Rng::stream_base(seed, id, kPurposeStatic, Rng::kInitTick),
                   (static_cast<u64>(mem.size()) + 7) / 8);
    births = deaths = 0;
    return true;
}

// --- respawning (7.8; World::respawn) -----------------------------------------------

u32 Place::device_count() const {
    return core_count_ + mem_devices_ + (os.disk_size() ? 1 : 0) + 1 + (has_cooler ? 1 : 0);
}

u32 Place::failed_devices() const {
    u32 n = 0;
    for (u32 c = 0; c < core_count_; ++c)
        n += core_worn[c].failed ? 1 : 0;
    for (u32 d = 0; d < mem_devices_; ++d)
        n += memory_worn[d].failed ? 1 : 0;
    n += os.disk_size() && disk_worn.failed ? 1 : 0;
    n += network_worn.failed ? 1 : 0;
    n += has_cooler && cooler_worn.failed ? 1 : 0;
    return n;
}

void Place::teardown() {
    region.reboot();                              // every idle run ends; port 0's queue goes
    os.end_everything(DeathCause::device_failed); // every thread and program
    region.flush_paths();
    lv_watch_.resize(0);
}

void Place::take_over(Place& old, u64 tick) {
    env.tick = tick;
    visits = old.visits + 1;
    last_visit = tick;
    next_visit = visit_mean_ ? tick + visit_gap(world_seed_, env.place_id, visits, visit_mean_, visit_.regularity)
                             : kNoVisit;
    os.set_next_uid(old.os.next_uid());
    for (usize i = 0; i < old.os.deaths.size(); ++i)
        os.deaths.push(old.os.deaths[i]);
    timing = old.timing;
    for (u32 k = 0; k < kPhases; ++k)
        phase_cycles[k] = old.phase_cycles[k];
}

void Place::tick() { tick_begin(~0ull); }

bool Place::tick_begin(u64 defer_at) {
    if (timing) {
        timed_tick();
        return false;
    }
    tick_start();
    if (fade_begin(defer_at)) // memory: fading, then its edge errors
        return true;          // the fade's cells left for the world to share out
    tick_finish();
    return false;
}

void Place::tick_end(bool in_region, const Array<u32>* hits, u32 chunks) {
    for (u32 c = 0; c < chunks; ++c)
        for (u32 i = 0; i < hits[c].size(); ++i)
            hits_.push(hits[c][i]);
    if (in_region)
        mem.note_region_change(); // halted cores may start again (10.2)
    fade_end();
    tick_finish();
}

void Place::tick_start() {
    supply_tick();       // the source's capacity, and the voltage from the last tick's draw
    if (env.tick == next_visit)
        visit();         // a maintenance visit, when its tick comes (7.8)
    device_states();     // cut-outs and restarts at that voltage, and each core's speed
    sched.tick();        // execution: rounds of rotating quanta, idle runs on idle cores
    low_voltage_watch(); // who took the low-voltage interrupt, for reports
    os.barrier();        // disk I/O, in core order
}

// After the fade and memory's edge errors (fade_tick), the rest of memory's
// step (appendix C step 8): the cosmic-ray floor, a worn device's weak cells
// (7.4) and rowhammer (9.8); then, with os.saved_state_fails, each thread a
// bit of whose saved state failed by any of those or an edge error ends,
// once all of them are done. Then the rest of the tick.
void Place::tick_finish() {
    noise.cosmic_flips(failing());
    noise.weak_flips(memory_state, failing());
    noise.hammer_flips(memory_state, failing());
    end_failed_states(failed_.data(), static_cast<u32>(failed_.size()));
    account_draw();       // what it drew (4.5), for the supply's sag next tick
    wear_tick();          // the wear the tick added (7.4)
    region.flush_paths(); // the paths that ended this tick, counted (an observer's measure, region.h)
    // The tick's facts wait in os.births and os.deaths for the world to hand
    // them to its observers at the barrier (13.1).
    ++env.tick;
}

// The same tick, its parts timed by the time-stamp counter (the benchmark's).
void Place::timed_tick() {
    u64 t0 = __rdtsc(), t1;
    supply_tick();
    t1 = __rdtsc();
    phase_cycles[kPhaseSupply] += t1 - t0;
    if (env.tick == next_visit)
        visit();
    device_states();
    t0 = __rdtsc();
    phase_cycles[kPhaseDevices] += t0 - t1;
    sched.tick();
    low_voltage_watch();
    t1 = __rdtsc();
    phase_cycles[kPhaseRun] += t1 - t0;
    os.barrier();
    t0 = __rdtsc();
    phase_cycles[kPhaseBarrier] += t0 - t1;
    fade_tick();
    t1 = __rdtsc();
    phase_cycles[kPhaseFade] += t1 - t0;
    noise.cosmic_flips(failing());
    t0 = __rdtsc();
    phase_cycles[kPhaseCosmic] += t0 - t1;
    noise.weak_flips(memory_state, failing());
    t1 = __rdtsc();
    phase_cycles[kPhaseWeak] += t1 - t0;
    noise.hammer_flips(memory_state, failing());
    t0 = __rdtsc();
    phase_cycles[kPhaseHammer] += t0 - t1;
    end_failed_states(failed_.data(), static_cast<u32>(failed_.size()));
    t1 = __rdtsc();
    phase_cycles[kPhaseCosmic] += t1 - t0;
    account_draw();
    t0 = __rdtsc();
    phase_cycles[kPhaseDraw] += t0 - t1;
    wear_tick();
    phase_cycles[kPhaseWear] += __rdtsc() - t0;
    region.flush_paths();
    ++env.tick;
}

// Appendix C's steps 1 to 3 for one source and one place: the capacity for
// this tick from the schedule, then the voltage from the last tick's draw,
// as a conductance, through the source's internal resistance (supply.h).
// From gate 4 the world's solve has done all three for every place at once
// (solve.h), and the place takes what it was given.
void Place::supply_tick() {
    i32 mv = given_mv;
    if (!supplied) {
        capacity = supply.capacity(env.tick);
        mv = supply.voltage(capacity, draw.total(), env.voltage_mv);
    }
    env.voltage_mv = mv;
    mv_total += static_cast<u64>(mv);
    if (mv < mv_lowest)
        mv_lowest = mv;
    below_p += mv < power_.p.threshold ? 1 : 0;
    below_e += mv < power_.e.threshold ? 1 : 0;
    below_region += mv < mem.device(mem.region_device()).threshold ? 1 : 0;
}

// Appendix C's step 4 (4.4, 7.6, appendix B): at this tick's voltage, a
// device that's on cuts out below its threshold, and one that's cut out
// restarts at its threshold plus the restart gap, once the minimum off-time
// has passed. A cut-out loses what the device had in flight. Then each core's
// speed for the tick follows the voltage (7.5), and SENSE reads it live.
// With heat on (heat.h), a device at its heat limit trips, which is a
// cut-out but for its cause and its way back: it's off until it has cooled
// to its cool-down point, and needs no off-time (power.h, step_heat).
void Place::device_states() {
    const i32 mv = env.voltage_mv;
    const u64 now = env.tick;
    const bool hot = heat.on;
    // The disk first. Cutting out, it cuts the last barrier's I/O, still in
    // flight, at a point drawn along it (7.6).
    if (os.disk_size() && disk_worn.failed) {
        ++failed_ticks[kHeatDisk]; // failed (7.6): off until it is replaced
    } else if (os.disk_size()) {
        const PowerChange ch =
            step_heat(&disk_state.on, &disk_tripped, &disk_state.off_at, mv, disk_threshold(), disk_temp,
                      heat.limit[kHeatDisk], heat.cool[kHeatDisk], hot, power_, now);
        trips[kHeatDisk] += ch == PowerChange::tripped ? 1 : 0;
        tripped_ticks[kHeatDisk] += disk_tripped ? 1 : 0;
        if (ch == PowerChange::cut_out || ch == PowerChange::tripped) {
            disk_cutouts += ch == PowerChange::cut_out ? 1 : 0;
            const u64 n = os.in_flight_bytes();
            if (n) {
                Rng r(seed_, env.place_id, kPurposeTear, now);
                os.tear(r.below(n));
            }
        }
        os.set_disk_on(disk_state.on);
    }
    os.settle_disk();
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores[c];
        const u32 e = k.kind == DeviceKind::e_core ? 1 : 0;
        const u32 hk = e ? kHeatE : kHeatP;
        if (core_worn[c].failed) {
            ++failed_ticks[hk]; // failed (7.6): neither cut out nor tripped, and no core of the scheduler's
            continue;
        }
        // Its threshold as its wear has left it (7.4): it cuts out sooner, and is slower at any voltage.
        CorePower kp = e ? power_.e : power_.p;
        kp.threshold += k.worn_mv;
        const PowerChange ch = step_heat(&k.working, &core_tripped[c], &k.off_at, mv, kp.threshold, core_temp[c],
                                         heat.limit[hk], heat.cool[hk], hot, power_, now);
        if (ch == PowerChange::cut_out) {
            ++cutouts[e];
            cut_core(c, DeathCause::cut_out);
        } else if (ch == PowerChange::tripped) {
            ++trips[hk];
            cut_core(c, DeathCause::heat);
        }
        k.speed = k.working ? core_speed(kp, mv) : 0; // at its worn threshold (appendix C)
        env.devices[c].speed = k.speed;
        // Off by the voltage, or off by heat: counted apart (observe.h).
        off_ticks[e] += k.working || core_tripped[c] ? 0 : 1;
        heat_ticks[e] += core_tripped[c] ? 1 : 0;
        tripped_ticks[hk] += core_tripped[c] ? 1 : 0;
    }
    noise.core_edges(mv); // each working core's edge errors at this voltage (7.5)
    bool cold = false;
    for (u32 d = 0; d < mem_devices_; ++d) {
        const bool in_region = d == mem.region_device();
        if (memory_worn[d].failed) {
            ++failed_ticks[kHeatMemory];
            continue;
        }
        const PowerChange ch =
            step_heat(&memory_state[d].on, &memory_tripped[d], &memory_state[d].off_at, mv, memory_threshold(d),
                      memory_temp[d], heat.limit[kHeatMemory], heat.cool[kHeatMemory], hot, power_, now);
        memory_cutouts += ch == PowerChange::cut_out ? 1 : 0;
        trips[kHeatMemory] += ch == PowerChange::tripped ? 1 : 0;
        // Back after a cut-out or a heat trip (gap 7): the reset region's
        // device coming back is a cold start either way.
        cold = cold || (ch == PowerChange::restarted && in_region);
        memory_off_ticks += memory_state[d].on ? 0 : 1;
        region_off_ticks += !memory_state[d].on && in_region ? 1 : 0;
        tripped_ticks[kHeatMemory] += memory_tripped[d] ? 1 : 0;
        region_heat_ticks += memory_tripped[d] && in_region ? 1 : 0;
    }
    if (network_worn.failed) {
        ++failed_ticks[kHeatNetwork];
    } else {
        const PowerChange ch =
            step_heat(&network_state.on, &network_tripped, &network_state.off_at, mv, network_threshold(),
                      network_temp, heat.limit[kHeatNetwork], heat.cool[kHeatNetwork], hot, power_, now);
        // What was queued at it is dropped at the barrier (11.4).
        network_cutouts += ch == PowerChange::cut_out ? 1 : 0;
        trips[kHeatNetwork] += ch == PowerChange::tripped ? 1 : 0;
        tripped_ticks[kHeatNetwork] += network_tripped ? 1 : 0;
    }
    env.network_on = network_state.on; // SEND, PING and RECV's deliveries need it (11.4)
    if (has_cooler && cooler_worn.failed) {
        ++failed_ticks[kHeatCooler];
    } else if (has_cooler) {
        const PowerChange ch =
            step_heat(&cooler_state.on, &cooler_tripped, &cooler_state.off_at, mv, cooler_threshold(),
                      cooler_temp, heat.limit[kHeatCooler], heat.cool[kHeatCooler], hot, power_, now);
        cooler_cutouts += ch == PowerChange::cut_out ? 1 : 0;
        trips[kHeatCooler] += ch == PowerChange::tripped ? 1 : 0;
        tripped_ticks[kHeatCooler] += cooler_tripped ? 1 : 0;
    }
    // Power back to the reset region's device after it fell below its
    // threshold: a cold start, a discrete event (10.5).
    if (cold)
        cold_start();
    // Then the failures from wear (appendix C 4).
    wear_failures();
}

namespace {

// How far through its life a core is, in 256ths, for its floor's rise (7.5).
u32 floor_step(const Worn& w) {
    const u64 part = w.life >> 8;
    const u64 k = part ? w.wear / part : 0;
    return k > 256 ? 256 : static_cast<u32>(k);
}

} // namespace

void Place::add_wear(Worn* w, i64 t, i32 maximum) {
    const u64 add = wear.step(t, env.voltage_mv, maximum);
    w->wear = w->wear + add < w->wear || w->wear + add > kWearMost ? kWearMost : w->wear + add;
}

// Appendix C's step 8, its last part: every device that hasn't failed wears,
// on or off, by its own temperature and the place's voltage this tick; then
// each one's threshold, and each memory device's weak cells, follow its wear.
void Place::wear_tick() {
    if (!wear.on)
        return;
    for (u32 c = 0; c < core_count_; ++c) {
        Worn& w = core_worn[c];
        if (w.failed)
            continue;
        const bool e = cores[c].kind == DeviceKind::e_core;
        add_wear(&w, core_temp[c], e ? power_.e.maximum : power_.p.maximum);
        const i32 rise = wear.risen(w.wear, w.life);
        if (rise != cores[c].worn_mv) {
            cores[c].worn_mv = rise;
            noise.set_core_worn(c, rise);
            // Its speed at the tick's voltage follows, as a load works it out
            // again from the wear saved: for whatever reads it between ticks
            // (the viewer). The next tick's device_states sets it afresh
            // before anything runs, so the run doesn't see this.
            Core& k = cores[c];
            if (k.working) {
                CorePower kp = e ? power_.e : power_.p;
                kp.threshold += rise;
                k.speed = core_speed(kp, env.voltage_mv);
                env.devices[c].speed = k.speed;
            }
        }
        // Its floor rises with its wear too (7.5), a step at a time.
        if (noise.worn_floor() && floor_step(w) != noise.core_step(c))
            noise.set_core_floor(c, noise.core_flaky(c), floor_step(w), true);
    }
    for (u32 d = 0; d < mem_devices_; ++d) {
        Worn& w = memory_worn[d];
        if (w.failed)
            continue;
        add_wear(&w, memory_temp[d], power_.memory_maximum);
        memory_rise_[d] = wear.risen(w.wear, w.life);
        noise.set_worn(d, memory_rise_[d], wear.weak_cells(w.wear, w.life, static_cast<u64>(devices_[d].size) * 8),
                       false);
    }
    if (os.disk_size() && !disk_worn.failed) {
        add_wear(&disk_worn, disk_temp, power_.disk_maximum);
        disk_rise_ = wear.risen(disk_worn.wear, disk_worn.life);
    }
    if (!network_worn.failed) {
        add_wear(&network_worn, network_temp, power_.network_maximum);
        network_rise_ = wear.risen(network_worn.wear, network_worn.life);
    }
    if (has_cooler && !cooler_worn.failed) {
        add_wear(&cooler_worn, cooler_temp, heat.cooler_maximum);
        cooler_rise_ = wear.risen(cooler_worn.wear, cooler_worn.life);
    }
}

void Place::wear_failures() {
    // A disk whose first block has passed its write endurance has failed
    // (7.4, 10.5), whether or not devices wear by time.
    if (os.disk_size() && !disk_worn.failed && os.boot_worn()) {
        ++boot_failures;
        fail_disk();
    }
    if (!wear.on)
        return;
    for (u32 c = 0; c < core_count_; ++c)
        if (!core_worn[c].failed && core_worn[c].wear >= core_worn[c].life)
            fail_core(c);
    for (u32 d = 0; d < mem_devices_; ++d)
        if (!memory_worn[d].failed && memory_worn[d].wear >= memory_worn[d].life)
            fail_memory(d);
    if (os.disk_size() && !disk_worn.failed && disk_worn.wear >= disk_worn.life)
        fail_disk();
    if (!network_worn.failed && network_worn.wear >= network_worn.life)
        fail_network();
    if (has_cooler && !cooler_worn.failed && cooler_worn.wear >= cooler_worn.life)
        fail_cooler();
}

void Place::fail_core(u32 c) {
    if (c >= core_count_ || core_worn[c].failed)
        return;
    core_worn[c].failed = true;
    Core& k = cores[c];
    ++failures[k.kind == DeviceKind::e_core ? kHeatE : kHeatP];
    cut_core(c, DeathCause::device_failed); // the thread on it ends as at a cut-out, and an idle run with it
    k.working = false;
    core_tripped[c] = false;
    k.speed = 0;
    env.devices[c].speed = 0;
    env.devices[c].failed = true;
}

void Place::fail_memory(u32 d) {
    if (d >= mem_devices_ || memory_worn[d].failed)
        return;
    memory_worn[d].failed = true;
    ++failures[kHeatMemory];
    // The threads whose stacks were there end, in record order: their saved
    // state is lost with the device.
    for (u32 t = 0; t < os.thread_top(); ++t) {
        const Thread& th = os.thread(t);
        if (th.state == ThreadState::free || mem.device_of(th.stack) != d)
            continue;
        os.end_thread(t, DeathCause::device_failed);
        ++failure_threads;
    }
    if (d == mem.region_device())
        region.lose(); // the reset region is gone (10.2)
    mem.fail_device(d);
    for (u32 c = 0; c < core_count_; ++c)
        cores[c].x.forget_pages(); // what each core remembers of the pages it may touch
    memory_state[d].on = false;
    memory_tripped[d] = false;
    env.devices[core_count_ + d].failed = true;
    noise.set_worn(d, memory_rise_[d], 0, true);
}

void Place::fail_disk() {
    if (!os.disk_size() || disk_worn.failed)
        return;
    disk_worn.failed = true;
    ++failures[kHeatDisk];
    disk_state.on = false;
    disk_tripped = false;
    os.fail_disk(); // its data is lost, the boot block with it; its calls fail soft (7.6)
    env.devices[core_count_ + mem_devices_].failed = true;
}

void Place::fail_network() {
    if (network_worn.failed)
        return;
    network_worn.failed = true;
    ++failures[kHeatNetwork];
    network_state.on = false;
    network_tripped = false;
    env.network_on = false;
}

void Place::fail_cooler() {
    if (!has_cooler || cooler_worn.failed)
        return;
    cooler_worn.failed = true;
    ++failures[kHeatCooler];
    cooler_state.on = false;
    cooler_tripped = false;
}

// Each device's rise and failed state, from its wear: for the cores, noise's
// mirrors and SENSE's list, after a load.
void Place::tell_worn() {
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores[c];
        const Worn& w = core_worn[c];
        k.worn_mv = wear.risen(w.wear, w.life);
        noise.set_core_worn(c, k.worn_mv);
        // Its floor, by the device at its slot and its wear: the countdown
        // it was saved with stands.
        noise.set_core_floor(c, noise.flaky_factor(c, w.fresh), noise.worn_floor() ? floor_step(w) : 0, false);
        env.devices[c].failed = w.failed;
        if (env.tick) {
            CorePower kp = k.kind == DeviceKind::e_core ? power_.e : power_.p;
            kp.threshold += k.worn_mv;
            k.speed = k.working ? core_speed(kp, env.voltage_mv) : 0;
            env.devices[c].speed = k.speed;
        }
    }
    for (u32 d = 0; d < mem_devices_; ++d) {
        const Worn& w = memory_worn[d];
        memory_rise_[d] = wear.risen(w.wear, w.life);
        noise.set_memory_flaky(d, noise.flaky_factor(kWearSlotMemory + d, w.fresh));
        env.devices[core_count_ + d].failed = w.failed;
        const u64 bits = static_cast<u64>(devices_[d].size) * 8;
        noise.set_worn(d, memory_rise_[d], w.failed ? 0 : wear.weak_cells(w.wear, w.life, bits), w.failed);
    }
    disk_rise_ = wear.risen(disk_worn.wear, disk_worn.life);
    network_rise_ = wear.risen(network_worn.wear, network_worn.life);
    cooler_rise_ = wear.risen(cooler_worn.wear, cooler_worn.life);
    if (os.disk_size())
        env.devices[core_count_ + mem_devices_].failed = disk_worn.failed;
    os.set_disk_failed(disk_worn.failed);
}

void Place::cold_start() {
    ++cold_starts;
    region.reboot(); // every idle run ends, its holdings freed; port 0's queue goes with the old OS
    os.cold_start(); // every thread and program ends
    // A failed disk has no boot block to run (7.6): nothing boots until a
    // visit brings a new one, and that one holds static.
    const u32 n = disk_worn.failed ? 0 : os.boot_size();
    if (n) {
        const u64 writer = os.boot_writer();
        cold_written += writer ? 1 : 0;
        region.arrive(os.disk(), n, writer ? BirthCause::boot : BirthCause::static_bytes, writer, true, true);
    }
}

namespace {

// Saved states by where they start, then by thread (so the order never
// depends on the sort's).
int by_start(const void* a, const void* b) {
    const SavedState& x = *static_cast<const SavedState*>(a);
    const SavedState& y = *static_cast<const SavedState*>(b);
    if (x.lo != y.lo)
        return x.lo < y.lo ? -1 : 1;
    if (x.thread != y.thread)
        return x.thread < y.thread ? -1 : 1;
    return 0;
}

// Whether end_failed_states marks the failed bytes' blocks first
// (end_failed_marked). Holding the threads against the bytes four at a time
// takes about 2.5 cycles for four threads and a byte; the marks take about
// 9 a byte to set and clear and 5 a thread to look at (pass 12, measured on
// the late world and open-3's early one). So they pay with many threads
// and more than a few bytes, and not in a place with few threads, as most
// are early, however many bytes failed.
bool marks_pay(u32 threads, u32 bytes) {
    return static_cast<u64>(threads) * bytes > 8ull * threads + 16ull * bytes;
}

// A failed byte's two blocks (place.h): its own, and the one a saved
// state's length less one before it, or block 0.
NOVA2_INLINE u32 failed_lo(u32 a) {
    return (a < kSavedRegsBytes - 1 ? 0 : a - (kSavedRegsBytes - 1)) >> kFailedBlockShift;
}

} // namespace

void failed_marks(const u32* addrs, u32 count, u64* bits) {
    for (u32 i = 0; i < count; ++i) {
        const u32 lo = failed_lo(addrs[i]), hi = addrs[i] >> kFailedBlockShift;
        bits[lo >> 6] |= 1ull << (lo & 63);
        bits[hi >> 6] |= 1ull << (hi & 63);
    }
}

void failed_unmark(const u32* addrs, u32 count, u64* bits) {
    for (u32 i = 0; i < count; ++i) {
        bits[failed_lo(addrs[i]) >> 6] = 0;
        bits[(addrs[i] >> kFailedBlockShift) >> 6] = 0;
    }
}

void Place::end_failed_states(const u32* addrs, u32 count) {
    if (!count)
        return;
    if (marks_pay(os.thread_top(), count) && end_failed_marked(addrs, count))
        return;
    // Where each thread's saved registers and handler frame lie, kNowhere for
    // a state it doesn't have just now (os.h): eight bytes a thread, not its
    // record.
    const u32* const saved = os.saved_at();
    const u32* const frame = os.frame_at();
    const u32 top = os.thread_top();
    // Four threads at a time: nearly always none of the four is hit, and
    // then nothing has changed and the next four are looked at. When one
    // is, those four are gone through one by one, as the rest below are.
    // The compares are unsigned ones (an address below a state's start wraps
    // past its length), made from signed ones by turning both sides' top
    // bits.
    const __m128i top_bit = _mm_set1_epi32(static_cast<int>(0x80000000u));
    const __m128i saved_len = _mm_set1_epi32(static_cast<int>(kSavedRegsBytes ^ 0x80000000u));
    const __m128i frame_len = _mm_set1_epi32(static_cast<int>(kHandlerFrameBytes ^ 0x80000000u));
    u32 t = 0;
    for (; t + 4 <= top; t += 4) {
        const __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(saved + t));
        const __m128i f = _mm_loadu_si128(reinterpret_cast<const __m128i*>(frame + t));
        __m128i hit = _mm_setzero_si128();
        for (u32 i = 0; i < count; ++i) {
            const __m128i a = _mm_set1_epi32(static_cast<int>(addrs[i]));
            const __m128i in_saved = _mm_cmplt_epi32(_mm_xor_si128(_mm_sub_epi32(a, s), top_bit), saved_len);
            const __m128i in_frame = _mm_cmplt_epi32(_mm_xor_si128(_mm_sub_epi32(a, f), top_bit), frame_len);
            hit = _mm_or_si128(hit, _mm_or_si128(in_saved, in_frame));
        }
        if (_mm_movemask_epi8(hit))
            end_failed_among(addrs, count, t, t + 4);
    }
    end_failed_among(addrs, count, t, top);
}

void Place::end_failed_among(const u32* addrs, u32 count, u32 from, u32 to) {
    const u32* const saved = os.saved_at();
    const u32* const frame = os.frame_at();
    for (u32 t = from; t < to; ++t) {
        bool hit = false;
        for (u32 i = 0; i < count && !hit; ++i) {
            // Unsigned: an address below the state's start wraps past its length.
            hit = addrs[i] - saved[t] < kSavedRegsBytes || addrs[i] - frame[t] < kHandlerFrameBytes;
        }
        if (hit) {
            os.end_thread(t, DeathCause::memory_lost);
            ++failed_threads;
        }
    }
}

// The threads one by one, in order, each as it stands when it's reached, as
// in end_failed_among: so the same threads end, in the same order, with the
// same cause. A thread neither of whose states starts in a marked block
// holds no failed byte (place.h) and is passed by; for one that does, the
// bytes decide. A byte past memory's end, which no state holds, is left to
// the plain way, with its kNowhere edge (os.h).
//
// Only the threads that may have a state starting in a marked block are
// looked at (pass 13): its saved registers start at its stack's base, and a
// handler's frame at least kSavedRegsBytes past it and no less than
// kHandlerFrameBytes before the stack's end (enter_handler). So such a
// thread's stack starts in the marked block or at most `reach` blocks
// before it, in the OS's lists by block. Each found is marked in
// failed_seen_, and they are taken in record order from there.
// That reads a few lists for each failed byte, where the walk of every
// thread reads two words a thread: so with few threads for the bytes
// (lists_pay, place.h), every thread is walked as before. Either way, the
// same threads in the same order.
bool Place::end_failed_marked(const u32* addrs, u32 count) {
    const u32 size = mem.size();
    u32 hi = 0; // the last failed byte
    for (u32 i = 0; i < count; ++i) {
        if (addrs[i] >= size)
            return false;
        hi = addrs[i] > hi ? addrs[i] : hi;
    }
    const u32 words = failed_mark_words(size);
    if (failed_blocks_.size() < words)
        failed_blocks_.resize(words); // zero-filled; cleared again after each use
    u64* const bits = failed_blocks_.data();
    failed_marks(addrs, count, bits);
    const u32 top = os.thread_top();
    const u32 past = failed_block_past(size);
    const u32* const saved = os.saved_at();
    const u32* const frame = os.frame_at();
    const u32 reach = (os.settings().stack_size - kHandlerFrameBytes + Os::kStackBlockBytes - 1) >> kFailedBlockShift;
    if (top <= static_cast<u64>(lists_pay) * (reach + 2) * count) {
        for (u32 t = 0; t < top; ++t)
            if (failed_marked(bits, past, saved[t]) | failed_marked(bits, past, frame[t]))
                end_failed_among(addrs, count, t, t + 1);
        failed_unmark(addrs, count, bits);
        return true;
    }
    if (failed_seen_.size() < (top + 63) >> 6)
        failed_seen_.resize((top + 63) >> 6); // likewise
    u64* const seen = failed_seen_.data();
    u32 next = 0, first = ~0u, last = 0; // the next block whose list is unread; the threads found
    for (u32 w = 0, end = (hi >> kFailedBlockShift >> 6) + 1; w < end; ++w)
        for (u64 m = bits[w]; m; m &= m - 1) {
            unsigned long i;
            _BitScanForward64(&i, m);
            const u32 k = w * 64 + static_cast<u32>(i);
            for (u32 b = k - (k < next + reach ? k - next : reach); b <= k; ++b)
                for (u32 t = os.first_in_block(b); t; t = os.next_in_block(t - 1)) {
                    seen[(t - 1) >> 6] |= 1ull << ((t - 1) & 63);
                    first = t - 1 < first ? t - 1 : first;
                    last = t - 1 > last ? t - 1 : last;
                }
            next = k + 1;
        }
    for (u32 w = first >> 6; first != ~0u && w <= last >> 6; ++w) {
        for (u64 m = seen[w]; m; m &= m - 1) {
            unsigned long i;
            _BitScanForward64(&i, m);
            const u32 t = w * 64 + static_cast<u32>(i);
            if (failed_marked(bits, past, saved[t]) | failed_marked(bits, past, frame[t]))
                end_failed_among(addrs, count, t, t + 1);
        }
        seen[w] = 0;
    }
    failed_unmark(addrs, count, bits);
    return true;
}

// Appendix C's step 8 for memory, its first part: fading and the edge
// errors (the timed tick times them apart from the cosmic-ray floor, which
// follows). A device cut out below its threshold fades, and a thread a bit
// of whose saved state fades ends, cause "memory" (9.4): its saved
// registers while it's off its core, or the frame of a handler it's in.
// Then the edge errors of the devices that are on.
void Place::fade_tick() { fade_begin(~0ull); }

// The same, but with defer_at cells or more to fail it stops before them
// and says so (tick_begin); fade_end is then the rest of it.
bool Place::fade_begin(u64 defer_at) {
    failed_.resize(0);
    hits_.resize(0);
    const i32 mv = env.voltage_mv;
    // With heat on: a tripped device fades as one with no voltage, and a hot
    // one faster (5), by its temperature as the last tick left it.
    const bool* const tripped = heat.on ? memory_tripped : nullptr;
    const u32* const faster = heat.on ? fade_factor_ : nullptr;
    if (heat.on)
        for (u32 d = 0; d < mem_devices_; ++d)
            fade_factor_[d] = heat.fade_factor(memory_temp[d]);
    if (noise.fade_check(memory_state, mv, tripped)) {
        // The threads' saved states, sorted by where they start.
        saved_.resize(0);
        for (u32 t = 0, top = os.thread_top(); t < top; ++t) {
            const Thread& th = os.thread(t);
            if (th.state == ThreadState::free)
                continue;
            if (!th.core)
                saved_.push(SavedState{th.stack, th.stack + kSavedRegsBytes, t});
            if (th.in_handler)
                saved_.push(SavedState{th.frame, th.frame + kHandlerFrameBytes, t});
        }
        if (saved_.size() > 1)
            qsort(saved_.data(), saved_.size(), sizeof(SavedState), by_start);
        // And the blocks of memory they reach, for the cells to look in.
        const u64 cells = noise.fade_plan(memory_state, mv, tripped, faster);
        if (cells && saved_.size())
            saved_blocks(saved_.data(), static_cast<u32>(saved_.size()), mem.size(), &saved_blocks_);
        if (cells >= defer_at)
            return true;
        const Array<FadeJob>& jobs = noise.fade_jobs();
        bool in_region = false;
        for (u32 j = 0; j < jobs.size(); ++j)
            if (fade_chunk(jobs[j], jobs[j].from, jobs[j].upto, &hits_, false))
                in_region = true;
        if (in_region)
            mem.note_region_change(); // halted cores may start again (10.2)
    }
    fade_end();
    return false;
}

void Place::fade_end() {
    for (u32 i = 0; i < hits_.size(); ++i) {
        const u32 t = saved_[hits_[i]].thread;
        if (os.thread(t).state == ThreadState::free)
            continue; // already ended, by another bit
        os.end_thread(t, DeathCause::memory_lost);
        ++faded_threads;
    }
    noise.memory_errors(memory_state, env.voltage_mv, failing());
}

// A core cut out (7.6, appendix B): the thread running on it ends, as a fault
// with no handler would, or with os.cutout_pause goes back to the ready queue
// as it was; and an idle run on it ends. Threads queued, asleep or waiting
// keep their saved state in their stacks and survive; the scheduler moves the
// queued ones to powered cores (9.5). A heat trip does the same, and the
// thread it ends has the cause "heat" (5).
void Place::cut_core(u32 c, DeathCause cause) {
    Core& k = cores[c];
    if (k.thread) {
        if (os.settings().cutout_pause) {
            os.take_off(c, ThreadState::ready);
            ++paused_threads;
        } else {
            os.end_thread(k.thread - 1, cause);
            if (cause == DeathCause::heat)
                ++heat_threads;
            else if (cause == DeathCause::device_failed)
                ++failure_threads;
            else
                ++cut_threads;
        }
    }
    region.cut(c);
}

// The low-voltage interrupt's use, for reports (9.5; build-plan-gate4.md
// step 7): each thread that took one this tick is watched. One that goes to
// sleep within a tick of it is counted (lv_slept), and counted again
// (lv_outlived) if a core of the place cut out before it woke.
void Place::low_voltage_watch() {
    if (sched.lv_taken_now.empty() && lv_watch_.empty())
        return;
    const u64 now = env.tick;
    const u64 cuts = cutouts[0] + cutouts[1];
    for (usize i = 0; i < sched.lv_taken_now.size(); ++i) {
        const u32 t = sched.lv_taken_now[i];
        const Thread& th = os.thread(t);
        if (th.state == ThreadState::free)
            continue;
        lv_watch_.push(LvWatch{t, th.id, os.program(th.program).uid, now, cuts, false});
    }
    usize keep = 0;
    for (usize i = 0; i < lv_watch_.size(); ++i) {
        LvWatch w = lv_watch_[i];
        if (w.thread >= os.thread_top())
            continue;
        const Thread& th = os.thread(w.thread);
        if (th.state == ThreadState::free || th.id != w.id || os.program(th.program).uid != w.program)
            continue; // it ended
        if (!w.asleep) {
            if (th.state == ThreadState::sleeping) {
                w.asleep = true;
                ++lv_slept;
            } else if (now > w.tick) {
                continue; // a tick on, and it didn't sleep
            }
        } else if (th.state != ThreadState::sleeping) {
            lv_outlived += cuts > w.cutouts ? 1 : 0;
            continue;
        }
        lv_watch_[keep++] = w;
    }
    lv_watch_.resize(keep);
}

void Place::set_temp(i64 t) {
    temp = t;
    // SENSE reads it in tenths of a degree (A.4).
    env.temperature_dc = static_cast<i32>((t >= 0 ? t + 50000 : t - 50000) / 100000);
    temp_total += t / 1000;
    if (t > temp_highest)
        temp_highest = t;
}

// Each device's draw over the tick just run (appendix C): a core's working
// draw for the time it ran something, at its clock, and its idle draw for the
// rest; memory's holding draw and its draw per row activation; the disk's and
// the network device's. A device that's cut out draws nothing (4.4).
// A constant draw rises above its device's maximum (power.h, over_maximum).
//
// With heat on (heat.h; appendix C 7, the devices' half): each device draws
// more by its temperature (leakage), and then its temperature takes a step:
// up by what it drew, down by what its heatsink passed to the place. What
// they passed is summed for the world's step of the place's own temperature.
void Place::account_draw() {
    const i32 mv = env.voltage_mv;
    const bool hot = heat.on;
    i64 passed = 0, p = 0;
    u64 cores_uw = 0;
    for (u32 c = 0; c < core_count_; ++c) {
        const bool e = cores[c].kind == DeviceKind::e_core;
        CorePower k = e ? power_.e : power_.p;
        k.threshold += cores[c].worn_mv; // a worn core is slower, and draws by its speed (appendix C)
        const i64 total = sched.quantum16(c) * sched.settings().quanta_per_tick;
        u64 d = cores[c].working ? core_draw_uw(k, mv, total, sched.busy16(c), sched.work10(c)) : 0;
        if (cores[c].working && total > 0) {
            const i64 busy = sched.busy16(c) < 0 ? 0 : sched.busy16(c) > total ? total : sched.busy16(c);
            worked20 += (static_cast<u64>(busy) << 20) / static_cast<u64>(total);
        }
        if (hot) {
            const u32 hk = e ? kHeatE : kHeatP;
            d = heat.leaky(d, core_temp[c]);
            core_temp[c] = heat_step(core_temp[c], temp, d, heat.sink[hk], heat.capacity[hk], &p);
            passed += p;
        }
        core_draw[c] = d;
        cores_uw += d;
    }
    activations = mem.take_activation_count();
    activations_total += activations;
    u32 on = 0;
    for (u32 d = 0; d < mem_devices_; ++d)
        on += memory_state[d].on ? 1 : 0;
    draw.cores = cores_uw;
    // Memory: each device that's on draws its holding power, and they share
    // the tick's row activations evenly (the first takes what's left over).
    {
        const u64 hold = over_maximum(static_cast<u64>(power_.memory_hold_mw) * 1000, mv, power_.memory_maximum);
        const u64 access = on ? static_cast<u64>(power_.memory_access_uw) * activations : 0;
        u64 memory_uw = 0;
        bool first = true;
        for (u32 d = 0; d < mem_devices_; ++d) {
            u64 dd = 0;
            if (memory_state[d].on) {
                dd = hold + access / on + (first ? access % on : 0);
                first = false;
            }
            if (hot) {
                dd = heat.leaky(dd, memory_temp[d]);
                memory_temp[d] =
                    heat_step(memory_temp[d], temp, dd, heat.sink[kHeatMemory], heat.capacity[kHeatMemory], &p);
                passed += p;
            }
            memory_uw += dd;
        }
        draw.memory = memory_uw;
    }
    draw.disk = os.disk_size() && disk_state.on
                    ? over_maximum(static_cast<u64>(power_.disk_mw) * 1000, mv, power_.disk_maximum)
                    : 0;
    if (hot && os.disk_size()) {
        draw.disk = heat.leaky(draw.disk, disk_temp);
        disk_temp = heat_step(disk_temp, temp, draw.disk, heat.sink[kHeatDisk], heat.capacity[kHeatDisk], &p);
        passed += p;
    }
    // The network device: constant while on, and a little for each byte it
    // sent or received since the last tick's draw (11.4).
    draw.network = network_state.on
                       ? over_maximum(static_cast<u64>(power_.network_mw) * 1000, mv, power_.network_maximum) +
                             net_bytes * power_.network_byte_uw
                       : 0;
    net_bytes = 0;
    if (hot) {
        draw.network = heat.leaky(draw.network, network_temp);
        network_temp =
            heat_step(network_temp, temp, draw.network, heat.sink[kHeatNetwork], heat.capacity[kHeatNetwork], &p);
        passed += p;
    }
    // The cooling device (7.7): how hard it works follows the place's
    // temperature, and its draw with it. Cut out or tripped it does nothing.
    draw.cooling = 0;
    cooler_work = 0;
    if (has_cooler) {
        if (cooler_state.on) {
            cooler_work = heat.cooler_share(temp);
            draw.cooling = over_maximum((heat.cooler_draw * cooler_work) >> 20, mv, heat.cooler_maximum);
        }
        cooler_temp =
            heat_step(cooler_temp, temp, draw.cooling, heat.sink[kHeatCooler], heat.capacity[kHeatCooler], &p);
        passed += p;
    }
    passed_uw = passed;
    const u64 t = draw.total();
    energy.cores += draw.cores;
    energy.memory += draw.memory;
    energy.disk += draw.disk;
    energy.network += draw.network;
    energy.cooling += draw.cooling;
    if (t > peak_draw)
        peak_draw = t;
}

u64 Place::full_draw_uw(i32 mv) const {
    u64 t = 0;
    for (u32 c = 0; c < core_count_; ++c) {
        const CorePower& k = cores[c].kind == DeviceKind::e_core ? power_.e : power_.p;
        t += mv >= k.threshold ? core_work_uw(k, mv) : 0;
    }
    u32 on = 0;
    for (u32 d = 0; d < mem_devices_; ++d)
        on += mv >= devices_[d].threshold ? 1 : 0;
    t += memory_draw_uw(power_, on, 0);
    t += os.disk_size() && mv >= power_.disk_threshold ? static_cast<u64>(power_.disk_mw) * 1000 : 0;
    t += mv >= power_.network_threshold ? static_cast<u64>(power_.network_mw) * 1000 : 0;
    return t;
}

void Place::serialize(Archive& ar) {
    ar.tag(0x504C4335); // "PLC5": gate 3 adds an idle run's network wait and the bytes the device sent and received
    ar.u64_(env.tick);
    // The supply's state: the voltage the last tick ran at and what it drew
    // there, from which the next tick's voltage comes. The schedule is a
    // function of the tick, so nothing more.
    ar.i32_(env.voltage_mv);
    ar.u64_(draw.cores);
    ar.u64_(draw.memory);
    ar.u64_(draw.disk);
    ar.u64_(draw.network);
    u32 n = core_count_;
    ar.u32_(n);
    if (n != core_count_) {
        ar.fail();
        return;
    }
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores[c];
        ar.bool_(k.working);
        ar.u64_(k.off_at);
        ar.u32_(k.thread);
        ar.bool_(k.idle_run);
        ar.enum_(k.run_cause);
        ar.u64_(k.run_parent);
        ar.u32_(k.turn_left16);
        ar.u32_(k.x.r.a);
        ar.u32_(k.x.r.b);
        ar.u32_(k.x.r.c);
        ar.u32_(k.x.r.d);
        ar.u32_(k.x.r.ip);
        ar.u32_(k.x.r.sp);
        ar.u32_(k.x.holder);
        ar.u32_(k.x.stack_lo);
        ar.u32_(k.x.stack_hi);
        ar.u64_(k.clock.left);
        ar.u8_(k.clock.level);
        ar.bool_(k.disk_wait);
        ar.i64_(k.disk_debt16);
        ar.u64_(k.net_until);
        if (ar.loading()) {
            k.x.clock = &k.clock;
            k.x.forget_pages(); // a cache: it changes no result, so it isn't saved
            // The per-access counters: last_row is forgotten at each tick's
            // start, so between ticks there's nothing more to restore.
            k.x.acts = mem.activations();
            k.x.act_total = mem.activation_total();
            k.x.last_row = ~0u;
            // Its speed in the last tick follows from the voltage and its
            // state (device_states sets it afresh each tick, so it isn't
            // saved); restored for whatever reads it between ticks, the
            // viewer and SENSE's view of the devices.
            if (env.tick) {
                const CorePower& kp = k.kind == DeviceKind::e_core ? power_.e : power_.p;
                k.speed = k.working ? core_speed(kp, env.voltage_mv) : 0;
                env.devices[c].speed = k.speed;
            }
        }
    }
    // The last tick's capacity, likewise: the schedule gives it from the tick.
    if (ar.loading() && !supplied)
        capacity = env.tick ? supply.capacity(env.tick - 1) : kCapacityOne;
    // The other devices' states (4.4): on, or cut out since a tick.
    for (u32 d = 0; d < mem_devices_; ++d) {
        ar.bool_(memory_state[d].on);
        ar.u64_(memory_state[d].off_at);
    }
    ar.bool_(disk_state.on);
    ar.u64_(disk_state.off_at);
    ar.bool_(network_state.on);
    ar.u64_(network_state.off_at);
    ar.u64_(net_bytes); // the barrier's, for the next tick's draw (11.4)
    ar.mark("place: cores and device states");
    // A world with visits saves its schedule and each device's number here,
    // before the disk, which is saved against its number's static.
    if (visit_mean_ && ar.ok())
        serialize_visits(ar);
    mem.serialize(ar);
    os.serialize(ar);
    sched.serialize(ar);
    region.serialize(ar);
    noise.serialize(ar);
    // Gate 4's state, after the rest so that a world with the supply by
    // place and heat off saves exactly what gate 3's did; then gate 5's, so
    // that one with nothing wearing saves exactly what gate 4's did.
    if (supplied || heat.on)
        serialize_heat(ar);
    if ((wear.on || os.blocks_on()) && ar.ok())
        serialize_wear(ar);
    if (os.blocks_on() && ar.ok())
        os.serialize_blocks(ar);
    // Rowhammer's window counts, only when it is on (step 8).
    if (noise.hammer_on() && ar.ok()) {
        ar.tag(0x48414D31); // "HAM1"
        mem.serialize_hammer(ar);
        ar.mark("place: rowhammer's counts (gate 5)");
    }
    // The killer's clock and the watchdog's counts, only when on (step 10).
    if ((os.killer_on() || os.watchdog_on()) && ar.ok())
        os.serialize_guards(ar);
}

void Place::serialize_wear(Archive& ar) {
    ar.tag(0x504C5731); // "PLW1"
    Worn* all[kMaxCores + kMaxMemDevices + 3];
    u32 n = 0;
    for (u32 c = 0; c < core_count_; ++c)
        all[n++] = &core_worn[c];
    for (u32 d = 0; d < mem_devices_; ++d)
        all[n++] = &memory_worn[d];
    all[n++] = &disk_worn;
    all[n++] = &network_worn;
    all[n++] = &cooler_worn;
    for (u32 i = 0; i < n; ++i) {
        ar.u64_(all[i]->wear);
        ar.u64_(all[i]->life);
        ar.bool_(all[i]->failed);
    }
    noise.serialize_wear(ar);
    ar.mark("place: wear (gate 5)");
    if (ar.loading())
        tell_worn();
}

void Place::serialize_heat(Archive& ar) {
    ar.tag(0x504C4831); // "PLH1"
    ar.u64_(draw.cooling);
    ar.i64_(temp);
    ar.i64_(passed_uw);
    for (u32 c = 0; c < core_count_; ++c) {
        ar.i64_(core_temp[c]);
        ar.bool_(core_tripped[c]);
    }
    for (u32 d = 0; d < mem_devices_; ++d) {
        ar.i64_(memory_temp[d]);
        ar.bool_(memory_tripped[d]);
    }
    ar.i64_(disk_temp);
    ar.bool_(disk_tripped);
    ar.i64_(network_temp);
    ar.bool_(network_tripped);
    bool cooler = has_cooler;
    ar.bool_(cooler);
    if (cooler != has_cooler) {
        ar.fail();
        return;
    }
    ar.i64_(cooler_temp);
    ar.bool_(cooler_tripped);
    ar.bool_(cooler_state.on);
    ar.u64_(cooler_state.off_at);
    ar.u32_(cooler_work);
    ar.mark("place: heat (gate 4)");
    if (ar.loading()) {
        env.temperature_dc = static_cast<i32>((temp >= 0 ? temp + 50000 : temp - 50000) / 100000);
        lv_watch_.resize(0);
    }
}

u64 Place::hash() {
    ByteWriter w;
    Archive ar(&w);
    serialize(ar);
    return hash_bytes(w.data(), w.size(), 0x706C616365ull); // "place"
}

} // namespace nova2

namespace nova2 {

namespace {

bool place_broken(TextBuf* why, const char* what, u64 n) {
    if (why) {
        why->append(what);
        why->append_char(' ');
        why->append_u64(n);
    }
    return false;
}

} // namespace

bool Place::check(TextBuf* why) const {
    if (!mem.check(why) || !os.check(why) || !region.check(why))
        return false;
    // Every allocation is held by something alive: a live program, or the
    // idle run running on a core (a run's holdings go when it ends).
    for (u32 id = 1; id < mem.record_capacity(); ++id) {
        const u32 h = mem.record(id).holder;
        if (!h)
            continue;
        if (is_run_holder(h)) {
            if (h > core_count_ || region.state(h - 1).phase != RunPhase::running)
                return place_broken(why, "place check: an allocation is held by an idle run that isn't running: record",
                                    id);
        } else if (h - kFirstProgramHolder >= os.program_capacity() || !os.program(h).alive) {
            return place_broken(why, "place check: an allocation is held by a dead program: record", id);
        }
    }
    // The devices' states.
    for (u32 c = 0; c < core_count_; ++c) {
        if (!cores[c].working && cores[c].speed != 0)
            return place_broken(why, "place check: a core that's cut out has a speed: core", c);
        if (cores[c].off_at > env.tick)
            return place_broken(why, "place check: a core cut out in the future: core", c);
        if (core_tripped[c] && cores[c].working)
            return place_broken(why, "place check: a core tripped by heat is working: core", c);
    }
    for (u32 d = 0; d < mem_devices_; ++d)
        if (memory_tripped[d] && memory_state[d].on)
            return place_broken(why, "place check: memory tripped by heat is on: device", d);
    // A failed device is off, and isn't counted as tripped (7.6).
    for (u32 c = 0; c < core_count_; ++c)
        if (core_worn[c].failed && (cores[c].working || core_tripped[c] || cores[c].thread || cores[c].idle_run))
            return place_broken(why, "place check: a failed core is working, tripped or running: core", c);
    for (u32 d = 0; d < mem_devices_; ++d) {
        if (memory_worn[d].failed != mem.device_failed(d))
            return place_broken(why, "place check: memory and the place disagree on a failed device:", d);
        if (memory_worn[d].failed && (memory_state[d].on || memory_tripped[d]))
            return place_broken(why, "place check: a failed memory device is on or tripped: device", d);
    }
    if ((disk_worn.failed && (disk_state.on || disk_tripped)) ||
        (network_worn.failed && (network_state.on || network_tripped)) ||
        (cooler_worn.failed && (cooler_state.on || cooler_tripped)))
        return place_broken(why, "place check: a failed device is on or tripped at tick", env.tick);
    if (os.disk_size() && os.disk_failed() != disk_worn.failed)
        return place_broken(why, "place check: the OS and the place disagree on a failed disk at tick", env.tick);
    // The next visit is ahead (or this coming tick's), and never without visits.
    if (visit_mean_ ? next_visit < env.tick || (visits && last_visit >= env.tick) : next_visit != kNoVisit)
        return place_broken(why, "place check: the visit schedule is behind the tick", env.tick);
    if ((disk_tripped && disk_state.on) || (network_tripped && network_state.on) ||
        (cooler_tripped && cooler_state.on))
        return place_broken(why, "place check: a device tripped by heat is on at tick", env.tick);
    if (!heat.on && (temp != heat.outside || passed_uw != 0 || has_cooler))
        return place_broken(why, "place check: heat is off and the place isn't at the outside temperature at tick",
                            env.tick);
    for (u32 d = 0; d < mem_devices_; ++d) {
        if (memory_state[d].off_at > env.tick)
            return place_broken(why, "place check: memory cut out in the future: device", d);
        if (memory_state[d].on && (noise.dose(d) || noise.faded(d)))
            return place_broken(why, "place check: memory that's on holds a fade: device", d);
        if (noise.faded(d) > static_cast<u64>(mem.device(d).size) * 8)
            return place_broken(why, "place check: more cells faded than a device has: device", d);
    }
    if (disk_state.off_at > env.tick || network_state.off_at > env.tick)
        return place_broken(why, "place check: the disk or network cut out in the future at tick", env.tick);
    if (env.voltage_mv < 0)
        return place_broken(why, "place check: a negative voltage at tick", env.tick);
    return true;
}

} // namespace nova2
