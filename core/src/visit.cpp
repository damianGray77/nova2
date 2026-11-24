// Maintenance visits (visit.h; design.md 7.8; build-plan-gate5.md step 4).
#include <cstring>
#include <intrin.h> // _umul128

#include "nova2/keys.h"
#include "nova2/noise.h" // gap_log
#include "nova2/place.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/visit.h"

namespace nova2 {

void read_visit_settings(Config& cfg, VisitSettings* s) {
    s->mean = key_u64(cfg, "visit.mean");
    s->regularity = key_f64(cfg, "visit.regularity");
    s->density_tie = key_f64(cfg, "visit.density_tie");
    s->replace_at = key_f64(cfg, "visit.replace_at");
    s->respawn_share = key_f64(cfg, "visit.respawn_share");
}

namespace {

// a × b ÷ 2^32, saturating.
u64 times_q32(u64 a, u64 b) {
    u64 hi;
    const u64 lo = _umul128(a, b, &hi);
    if (hi >> 32)
        return ~0ull;
    return hi << 32 | lo >> 32;
}

constexpr u64 kOne = 1ull << 32;
constexpr u64 kLn2 = 2977044472ull; // ln 2, in 2^-32ths

} // namespace

u64 visit_gap(u64 seed, u32 place, u64 count, u64 mean, double regularity) {
    if (!mean)
        return kNoVisit;
    const double reg = regularity < 0 ? 0 : (regularity > 1 ? 1 : regularity);
    const u64 r32 = static_cast<u64>(reg * 4294967296.0 + 0.5);
    u64 factor = r32;
    if (r32 < kOne) {
        // An exponential draw of mean 1: -ln U = -log2 U × ln 2, U in (0, 1].
        const u64 u = Rng(seed, place, kPurposeVisit, count).at(0) | 1;
        const u64 e32 = times_q32(gap_log(0 - u) >> 24, kLn2);
        factor += times_q32(kOne - r32, e32);
    }
    const u64 gap = times_q32(mean, factor);
    return gap < 1 ? 1 : (gap > (1ull << 62) ? 1ull << 62 : gap);
}

// The schedule from the start: no visit yet, and the first one drawn.
void Place::init_visits(const VisitSettings& s, double scale) {
    visit_ = s;
    double mean = static_cast<double>(s.mean) * (scale > 0 ? scale : 1.0);
    visit_mean_ = !s.mean ? 0 : (mean < 1 ? 1 : (mean > 4e18 ? 4000000000000000000ull : static_cast<u64>(mean + 0.5)));
    const double at = s.replace_at < 0 ? 0 : s.replace_at;
    replace_q32_ = at >= 1 ? 0 : static_cast<u64>(at * 4294967296.0); // 0: failed devices only
    visits = last_visit = 0;
    next_visit = visit_mean_ ? visit_gap(world_seed_, env.place_id, 0, visit_mean_, s.regularity) : kNoVisit;
    disk_static_ = 0;
    for (u32 k = 0; k < kHeatKinds; ++k)
        replaced[k] = renewed[k] = 0;
    region_swaps = 0;
}

// A worn device the visit replaces like for like: at the threshold's share
// of its life, with the threshold under 1.
bool Place::due(const Worn& w) const {
    if (!replace_q32_ || !wear.on || w.failed)
        return false;
    return w.wear >= times_q32(w.life, replace_q32_);
}

// The visit (7.8): everything failed since the last is replaced, and with
// the threshold under 1 every device worn past it, in the devices' order.
// After the tick's supply, so a new device starts on where the voltage is
// at its threshold, and before the device states.
void Place::visit() {
    ++visits;
    last_visit = env.tick;
    for (u32 c = 0; c < core_count_; ++c)
        if (core_worn[c].failed || due(core_worn[c]))
            replace_core(c);
    for (u32 d = 0; d < mem_devices_; ++d)
        if (memory_worn[d].failed || due(memory_worn[d]))
            replace_memory(d);
    if (os.disk_size() && (disk_worn.failed || due(disk_worn)))
        replace_disk();
    if (network_worn.failed || due(network_worn))
        replace_network();
    if (has_cooler && (cooler_worn.failed || due(cooler_worn)))
        replace_cooler();
    next_visit = env.tick + visit_gap(world_seed_, env.place_id, visits, visit_mean_, visit_.regularity);
}

// A device's wear from new: no wear, and a life of its own drawn again.
// True if it had failed (a fresh device); false like for like.
bool Place::renew(Worn* w, u32 slot, u32 kind) {
    const bool failed = w->failed;
    ++w->fresh;
    w->wear = 0;
    w->failed = false;
    w->life = wear.on ? wear.life(seed_, env.place_id, slot, kind, w->fresh) : 0;
    ++(failed ? replaced : renewed)[kind];
    return failed;
}

void Place::replace_core(u32 c) {
    if (c >= core_count_)
        return;
    Core& k = cores[c];
    const bool e = k.kind == DeviceKind::e_core;
    const bool fresh = renew(&core_worn[c], c, e ? kHeatE : kHeatP);
    k.worn_mv = 0;
    noise.set_core_worn(c, 0);
    noise.set_core_floor(c, noise.flaky_factor(c, core_worn[c].fresh), 0, false); // flaky or not, drawn again (7.5)
    noise.renew_core(c); // its error countdown, drawn again
    env.devices[c].failed = false;
    if (!fresh)
        return; // like for like: the thread on it carries on
    k.working = env.voltage_mv >= (e ? power_.e.threshold : power_.p.threshold);
    k.off_at = 0;
    core_tripped[c] = false;
}

void Place::replace_memory(u32 d) {
    if (d >= mem_devices_)
        return;
    const bool fresh = renew(&memory_worn[d], kWearSlotMemory + d, kHeatMemory);
    memory_rise_[d] = 0;
    // Its leak ranks and countdowns are its own: drawn again. Nothing of it
    // is weak, and nothing has faded.
    noise.set_memory_flaky(d, noise.flaky_factor(kWearSlotMemory + d, memory_worn[d].fresh)); // drawn again (7.5)
    noise.renew_memory(d, memory_worn[d].fresh);
    env.devices[core_count_ + d].failed = false;
    if (!fresh)
        return; // like for like: its contents copied across, corrupted bits and all
    // A new device holds static (7.8), its pages free again; the reset
    // region's comes back as static too, a hot swap: idle runs start again
    // and port 0's queue, kept meanwhile, runs. No cold start (10.5).
    const MemDevice& dv = mem.device(d);
    Rng st(seed_, env.place_id, kPurposeFresh + kWearSlotMemory + d, memory_worn[d].fresh);
    u8* bytes = mem.bytes() + dv.base;
    for (u32 i = 0; i < dv.size; i += 8) {
        const u64 v = st.next();
        for (u32 b = 0; b < 8 && i + b < dv.size; ++b)
            bytes[i + b] = static_cast<u8>(v >> (8 * b));
    }
    mem.renew_device(d);
    for (u32 c = 0; c < core_count_; ++c)
        cores[c].x.forget_pages();
    memory_state[d].on = env.voltage_mv >= devices_[d].threshold;
    memory_state[d].off_at = 0;
    memory_tripped[d] = false;
    region_swaps += d == mem.region_device() ? 1 : 0;
}

// A new disk's static: its own stream, by the disk's number.
void Place::note_disk_static() {
    if (disk_static_)
        os.note_static(Rng::stream_base(seed_, env.place_id, kPurposeFresh + kWearSlotDisk, disk_static_), 0);
}

void Place::replace_disk() {
    if (!os.disk_size())
        return;
    const bool fresh = renew(&disk_worn, kWearSlotDisk, kHeatDisk);
    disk_rise_ = 0;
    env.devices[core_count_ + mem_devices_].failed = false;
    // Its blocks are a new disk's either way: no writes, endurances drawn
    // again, and each block of the disk space on its own block.
    os.init_blocks(seed_, disk_worn.fresh);
    if (!fresh)
        return; // like for like: its contents copied across, the boot block and its writer with them
    disk_static_ = disk_worn.fresh;
    Rng st(seed_, env.place_id, kPurposeFresh + kWearSlotDisk, disk_static_);
    u8* disk = os.disk();
    for (u32 i = 0; i < os.disk_size(); i += 8) {
        const u64 v = st.next();
        for (u32 b = 0; b < 8 && i + b < os.disk_size(); ++b)
            disk[i + b] = static_cast<u8>(v >> (8 * b));
    }
    note_disk_static();
    disk_state.on = env.voltage_mv >= power_.disk_threshold;
    disk_state.off_at = 0;
    disk_tripped = false;
    os.set_disk_on(disk_state.on);
}

void Place::replace_network() {
    const bool fresh = renew(&network_worn, kWearSlotNetwork, kHeatNetwork);
    network_rise_ = 0;
    if (!fresh)
        return;
    network_state.on = env.voltage_mv >= power_.network_threshold;
    network_state.off_at = 0;
    network_tripped = false;
    env.network_on = network_state.on;
}

void Place::replace_cooler() {
    if (!has_cooler)
        return;
    const bool fresh = renew(&cooler_worn, kWearSlotCooler, kHeatCooler);
    cooler_rise_ = 0;
    if (!fresh)
        return;
    cooler_state.on = env.voltage_mv >= heat.cooler_threshold;
    cooler_state.off_at = 0;
    cooler_tripped = false;
}

// The schedule and each device's number in a save (version 6), when the
// world has visits: before memory and the OS, whose disk is saved against
// the static its number gives.
void Place::serialize_visits(Archive& ar) {
    ar.tag(0x504C5631); // "PLV1"
    ar.u64_(visits);
    ar.u64_(last_visit);
    ar.u64_(next_visit);
    for (u32 c = 0; c < core_count_; ++c)
        ar.u32_(core_worn[c].fresh);
    for (u32 d = 0; d < mem_devices_; ++d)
        ar.u32_(memory_worn[d].fresh);
    ar.u32_(disk_worn.fresh);
    ar.u32_(network_worn.fresh);
    ar.u32_(cooler_worn.fresh);
    ar.u32_(disk_static_);
    ar.mark("place: visits (gate 5)");
    if (!ar.loading() || !ar.ok())
        return;
    if (disk_static_ > disk_worn.fresh || next_visit <= last_visit) {
        ar.fail();
        return;
    }
    note_disk_static();
    for (u32 d = 0; d < mem_devices_; ++d)
        noise.set_fresh(d, memory_worn[d].fresh);
}

} // namespace nova2
