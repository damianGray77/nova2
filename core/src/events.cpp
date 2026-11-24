// Ramps, regional events and strikes (events.h; design.md 12.2 to 12.4;
// build-plan-gate5.md step 7).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "nova2/events.h"
#include "nova2/keys.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"
#include "nova2/solve.h" // kSiemens

namespace nova2 {

const char* const kPatchTypeNames[kPatchTypes] = {"storm", "swell", "brownout", "heatwave", "outage", "gust"};

namespace {

constexpr double kTurn = 6.283185307179586;

// A 64-bit draw as a number from 0 up to 1.
double unit_of(u64 v) { return static_cast<double>(v >> 11) * 0x1.0p-53; }

// A chance as the threshold a 64-bit draw falls under.
u64 threshold_of(double p) {
    if (!(p > 0))
        return 0;
    if (p >= 1)
        return ~0ull;
    return static_cast<u64>(p * 18446744073709551616.0);
}

u32 share_of(double v) {
    if (!(v > 0))
        return 0;
    return v >= 1 ? kCapacityOne : static_cast<u32>(v * kCapacityOne + 0.5);
}

} // namespace

void read_ramp_settings(Config& cfg, RampSettings* s) {
    s->lead_min = key_u64(cfg, "ramp.lead.min");
    s->lead_max = key_u64(cfg, "ramp.lead.max");
    if (s->lead_max && s->lead_max < s->lead_min)
        cfg.fail("ramp.lead.max", "must be at least ramp.lead.min, or 0 for no ramps");
    s->false_share = key_f64(cfg, "ramp.false_share");
    s->false_min = key_f64(cfg, "ramp.false.min");
    s->false_max = key_f64(cfg, "ramp.false.max");
    if (s->false_max < s->false_min)
        cfg.fail("ramp.false.max", "must be at least ramp.false.min");
}

void read_event_settings(Config& cfg, EventSettings* s) {
    read_ramp_settings(cfg, &s->ramp);
    s->speed = key_f64(cfg, "patch.speed");
    s->still = key_f64(cfg, "patch.still");
    s->turn = key_f64(cfg, "patch.turn");
    s->day = key_u64(cfg, "supply.day");
    s->life_min = key_u64(cfg, "patch.life.min");
    s->life_max = key_u64(cfg, "patch.life.max");
    if (s->life_max < s->life_min)
        cfg.fail("patch.life.max", "must be at least patch.life.min");
    s->strike_mv = static_cast<i32>(key_u32(cfg, "strike.voltage"));
    s->strike_mohm = key_f64(cfg, "strike.resistance");
    for (u32 t = 0; t < kPatchTypes; ++t) {
        PatchKind& k = s->kind[t];
        char key[64];
        snprintf(key, sizeof key, "patch.%s.rate", kPatchTypeNames[t]);
        k.rate = key_f64(cfg, key);
        snprintf(key, sizeof key, "patch.%s.radius.min", kPatchTypeNames[t]);
        k.radius_min = key_f64(cfg, key);
        snprintf(key, sizeof key, "patch.%s.radius.max", kPatchTypeNames[t]);
        k.radius_max = key_f64(cfg, key);
        if (k.radius_max < k.radius_min)
            cfg.fail(key, "must be at least the type's radius.min");
        snprintf(key, sizeof key, "patch.%s.strength.min", kPatchTypeNames[t]);
        k.strength_min = key_f64(cfg, key);
        snprintf(key, sizeof key, "patch.%s.strength.max", kPatchTypeNames[t]);
        k.strength_max = key_f64(cfg, key);
        if (k.strength_max < k.strength_min)
            cfg.fail(key, "must be at least the type's strength.min");
        const PatchType type = static_cast<PatchType>(t);
        const bool up = type == PatchType::swell || type == PatchType::outage;
        if (up && k.strength_min < 1)
            cfg.fail(key, "a swell's and an outage's strengths multiply: they must be 1 or more");
        if ((type == PatchType::brownout || type == PatchType::storm) && k.strength_max > 1)
            cfg.fail(key, "a brownout's strength is the share of capacity left and a storm's a chance: at most 1");
    }
}

// --- ramps ---------------------------------------------------------------------------

void RampLaw::init(const RampSettings& s) {
    s_ = s;
    on_ = s.lead_max > 0;
    if (!on_)
        return;
    const double lo = static_cast<double>(s.lead_min < 1 ? 1 : s.lead_min);
    const double hi = static_cast<double>(s.lead_max) > lo ? static_cast<double>(s.lead_max) : lo;
    for (u32 k = 0; k < kDropoutLengths; ++k) {
        const double v = lo * pow(hi / lo, static_cast<double>(k) / (kDropoutLengths - 1));
        leads_[k] = v >= 4e9 ? 4000000000u : static_cast<u32>(v + 0.5);
        if (leads_[k] < 1)
            leads_[k] = 1;
    }
    false_cut_ = threshold_of(s.false_share);
    peak_lo_ = share_of(s.false_min);
    peak_hi_ = share_of(s.false_max);
    if (peak_hi_ < peak_lo_)
        peak_hi_ = peak_lo_;
}

Ramp RampLaw::draw(u64 seed, u32 stream, u32 purpose, u64 key) const {
    Ramp r;
    if (!on_)
        return r;
    const Rng g(seed, stream, purpose, key);
    r.lead = leads_[g.at(0) >> 54];
    r.false_alarm = g.at(1) < false_cut_;
    if (r.false_alarm)
        r.peak = peak_lo_ + static_cast<u32>(g.at(2) % (static_cast<u64>(peak_hi_ - peak_lo_) + 1));
    return r;
}

u32 RampLaw::level(const Ramp& r, u64 age, u64 hold) {
    const u64 top = r.false_alarm ? 0 : hold;
    if (age >= top + 2 * r.lead)
        return 0;
    u64 l = kCapacityOne;
    if (age < r.lead)
        l = (age << kCapacityBits) / r.lead; // the rise
    else if (age >= r.lead + top)
        l = ((top + 2 * r.lead - age) << kCapacityBits) / r.lead; // the ease
    return static_cast<u32>((l * r.peak) >> kCapacityBits);
}

// --- patches -------------------------------------------------------------------------

bool Events::init(const EventSettings& s, const Map* map, u64 seed, TextBuf* why) {
    s_ = s;
    map_ = map;
    seed_ = seed;
    ramp_.init(s.ramp);
    link_ = map->placed() && map->mean_length() > 0 ? map->mean_length() : 1;
    sought_ = false;
    tick_ = 0;
    now_.resize(0);
    on_ = false;
    land_.clear();
    cum_.clear();
    if (!(s.strike_mohm > 0) || s.strike_mv <= 0 || s.day == 0 || !(s.speed >= 0) || !(s.turn >= 0)) {
        if (why)
            why->append("events: a strike needs a voltage and a resistance above nothing, and a patch a day, a "
                        "speed and a turn of 0 or more");
        return false;
    }
    strike_uv_ = static_cast<i64>(s.strike_mv) * 1000;
    const double g = static_cast<double>(kSiemens) * 1000.0 / s.strike_mohm;
    strike_g_ = g >= 9e18 ? static_cast<u64>(9e18) : (g < 1 ? 1 : static_cast<u64>(g + 0.5));
    for (u32 t = 0; t < kPatchTypes; ++t) {
        const PatchKind& k = s.kind[t];
        cand_[t].clear();
        n_[t] = 0;
        block_[t] = dropout_block(s.life_max + 2 * ramp_.longest());
        slots_[t] = 0;
        chance_[t] = 0;
        if (!(k.rate > 0) || !map->placed() || map->place_count() == 0)
            continue;
        if (s.life_min == 0 || !(k.radius_min > 0)) {
            if (why)
                why->append("events: a patch needs a life of a tick or more and a radius above nothing");
            return false;
        }
        // How many form in a stretch, on average; four slots for each, so
        // that two in a stretch are as likely as they should be.
        const double mean =
            k.rate / 1e6 * static_cast<double>(map->place_count()) / 100.0 * static_cast<double>(block_[t]);
        const double slots = ceil(mean * 4);
        if (slots > 4096) {
            if (why)
                why->append("events: too many patches of one type at once (lower its rate or its life)");
            return false;
        }
        slots_[t] = slots < 1 ? 1 : static_cast<u32>(slots);
        chance_[t] = threshold_of(mean / slots_[t]);
        on_ = true;
    }
    if (on_ && map->kind() == MapKind::landscape)
        map->land_table(&land_, &cum_);
    return true;
}

bool Events::make(PatchType type, u64 block, u32 slot, Patch* out) const {
    const u32 t = static_cast<u32>(type);
    if (slot >= slots_[t])
        return false;
    // Every draw is the slot's own: sixteen to a slot.
    const Rng r(seed_, Rng::kWorldPlace, kPurposePatch + t, block);
    const u64 at = static_cast<u64>(slot) * 16;
    if (r.at(at) >= chance_[t])
        return false;
    const PatchKind& k = s_.kind[t];
    Patch p;
    p.type = static_cast<u8>(t);
    p.block = block;
    p.slot = slot;
    p.start = block * block_[t] + r.at(at + 1) % block_[t];
    const double lo = static_cast<double>(s_.life_min), hi = static_cast<double>(s_.life_max);
    const double life = lo * pow(hi / lo, unit_of(r.at(at + 2)));
    p.life = life < 1 ? 1 : static_cast<u64>(life + 0.5);
    p.ramp = ramp_.draw(seed_, Rng::kWorldPlace, kPurposePatchRamp + t, (block << 12) | slot);
    p.radius = (k.radius_min + (k.radius_max - k.radius_min) * unit_of(r.at(at + 3))) * link_;
    p.speed = unit_of(r.at(at + 4)) < s_.still ? 0 : unit_of(r.at(at + 5)) * s_.speed * link_;
    p.heading = unit_of(r.at(at + 6)) * kTurn;
    p.turn = (2 * unit_of(r.at(at + 7)) - 1) * s_.turn * kTurn;
    p.strength = k.strength_min + (k.strength_max - k.strength_min) * unit_of(r.at(at + 8));
    if (!land_.empty()) {
        // A land cell by density, and a point in it, as a place's dart.
        const u32 L = static_cast<u32>(land_.size());
        const double u = unit_of(r.at(at + 9)) * cum_[L - 1];
        u32 a = 0, b = L - 1;
        while (a < b) {
            const u32 mid = a + (b - a) / 2;
            if (cum_[mid] > u)
                b = mid;
            else
                a = mid + 1;
        }
        const u32 c = land_[a];
        const double h = map_->raster_h();
        p.x = (c % map_->raster_w() + unit_of(r.at(at + 10))) / h;
        p.y = (c / map_->raster_w() + unit_of(r.at(at + 11))) / h;
    } else {
        const u32 i = static_cast<u32>(r.at(at + 9) % map_->place_count());
        p.x = map_->x(i);
        p.y = map_->y(i);
    }
    *out = p;
    return true;
}

// Its centre at a tick: a day at a time along its heading, which turns by
// the same amount each day; so an arc, summed in one step.
void Events::where(const Patch& p, u64 tick, double* x, double* y) const {
    *x = p.x;
    *y = p.y;
    if (p.speed == 0 || tick <= p.start)
        return;
    const u64 age = tick - p.start;
    const double days = static_cast<double>(age / s_.day);
    const double part = static_cast<double>(age % s_.day) / static_cast<double>(s_.day);
    double sc = 0, ss = 0;
    if (days > 0) {
        if (fabs(p.turn) < 1e-9) {
            sc = days * cos(p.heading);
            ss = days * sin(p.heading);
        } else {
            const double f = sin(days * p.turn / 2) / sin(p.turn / 2);
            const double mid = p.heading + (days - 1) * p.turn / 2;
            sc = f * cos(mid);
            ss = f * sin(mid);
        }
    }
    const double last = p.heading + days * p.turn;
    *x += p.speed * (sc + part * cos(last));
    *y += p.speed * (ss + part * sin(last));
}

void Events::seek(u64 tick) {
    if (!on_ || (sought_ && tick == tick_))
        return;
    tick_ = tick;
    now_.resize(0);
    for (u32 t = 0; t < kPatchTypes; ++t) {
        n_[t] = 0;
        if (!slots_[t])
            continue;
        const u64 j = tick / block_[t];
        if (!sought_ || cand_block_[t] != j) {
            cand_block_[t] = j;
            cand_[t].resize(0);
            for (u64 b = j ? j - 1 : 0; b <= j; ++b)
                for (u32 slot = 0; slot < slots_[t]; ++slot) {
                    Patch p;
                    if (make(static_cast<PatchType>(t), b, slot, &p))
                        cand_[t].push(p);
                }
        }
        for (u32 k = 0; k < cand_[t].size(); ++k) {
            const Patch& p = cand_[t][k];
            if (tick < p.start)
                continue;
            const u32 level = RampLaw::level(p.ramp, tick - p.start, p.life);
            if (!level)
                continue;
            PatchNow q;
            q.type = static_cast<u8>(t);
            q.index = k;
            where(p, tick, &q.x, &q.y);
            q.radius = p.radius;
            q.level = static_cast<double>(level) / kCapacityOne;
            q.strength = p.strength;
            now_.push(q);
            ++n_[t];
        }
    }
    sought_ = true;
}

// A patch's strength at a point, 0 to 1: full at its centre, nothing at its
// radius, times its ramp's level.
double Events::strength_at(const PatchNow& p, double x, double y) const {
    const double dx = x - p.x, dy = y - p.y, d2 = dx * dx + dy * dy;
    if (d2 >= p.radius * p.radius)
        return 0;
    return (1 - sqrt(d2) / p.radius) * p.level;
}

double Events::factor(PatchType t, double x, double y) const {
    double f = 1;
    if (!n_[static_cast<u32>(t)])
        return f;
    for (u32 k = 0; k < now_.size(); ++k) {
        const PatchNow& p = now_[k];
        if (p.type != static_cast<u8>(t))
            continue;
        const double s = strength_at(p, x, y);
        if (s > 0)
            f *= 1 + (p.strength - 1) * s;
    }
    return f < 0 ? 0 : f;
}

double Events::sum(PatchType t, double x, double y) const {
    double v = 0;
    if (!n_[static_cast<u32>(t)])
        return v;
    for (u32 k = 0; k < now_.size(); ++k) {
        const PatchNow& p = now_[k];
        if (p.type == static_cast<u8>(t))
            v += p.strength * strength_at(p, x, y);
    }
    return v;
}

double Events::storm_at(double x, double y) const {
    const double c = sum(PatchType::storm, x, y);
    return c > 1 ? 1 : c;
}

bool Events::strike(u32 place, u64 tick, double chance) const {
    if (!(chance > 0))
        return false;
    return unit_of(Rng(seed_, place, kPurposeStrike, tick).at(0)) < chance;
}

bool Events::patch_cut(u32 a, u32 b, u64 tick, double chance) const {
    if (!(chance > 0))
        return false;
    return unit_of(Rng(seed_, a, kPurposePatchCut, tick).at(b)) < chance;
}

} // namespace nova2
