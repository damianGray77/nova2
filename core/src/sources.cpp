#include "nova2/sources.h"

#include <cmath>    // pow, log: a source's numbers and the tables, worked once
#include <cstdio>   // snprintf
#include <cstring>  // memcmp, strlen
#include <intrin.h> // _umul128

#include "nova2/keys.h"
#include "nova2/noise.h" // to_p64, geometric_gap
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

const char* const kSourceTypeNames[kSourceTypes] = {"steady", "daylight", "gusty"};

namespace {

constexpr u64 kForever = ~0ull;

u32 to_share(double v) {
    if (v <= 0)
        return 0;
    return static_cast<u32>(v * kCapacityOne + 0.5);
}

u64 to_threshold(double p) {
    if (p <= 0)
        return 0;
    if (p >= 1)
        return ~0ull;
    return static_cast<u64>(ldexp(p, 64));
}

// a × b >> 20, with a 128-bit product.
u64 scale20(u64 a, u64 b) {
    u64 hi;
    const u64 lo = _umul128(a, b, &hi);
    return (hi << (64 - kCapacityBits)) | (lo >> kCapacityBits);
}

bool word(Str s, usize* at, Str* out) {
    while (*at < s.n && (s.p[*at] == ' ' || s.p[*at] == '\t'))
        ++*at;
    if (*at >= s.n || s.p[*at] == ',')
        return false;
    const usize b = *at;
    while (*at < s.n && s.p[*at] != ' ' && s.p[*at] != '\t' && s.p[*at] != ',')
        ++*at;
    *out = Str(s.p + b, *at - b);
    return true;
}

} // namespace

double source_mean_life(u64 life) { return static_cast<double>(life) * 3.75 / log(16.0); }

void read_source_settings(Config& cfg, SourceSettings* out) {
    SourceSettings& s = *out;
    const Str model = key_choice(cfg, "supply.model");
    s.on = model.n == 7 && memcmp(model.p, "sources", 7) == 0;
    s.start = key_f64(cfg, "sources.start");
    s.life = key_u64(cfg, "sources.life");
    // The chance that keeps the count the run starts with: the long-run
    // count is the chance × the average lifetime (4.2).
    s.chance = key_f64(cfg, "sources.chance", key_default(s.start / (source_mean_life(s.life) / 1e6)));
    s.area_power = key_f64(cfg, "sources.area_power");
    s.size_min = key_f64(cfg, "sources.size.min");
    s.size_max = key_f64(cfg, "sources.size.max");
    s.size_skew = key_f64(cfg, "sources.size.skew");
    if (s.size_max < s.size_min)
        cfg.fail("sources.size.max", "must be at least sources.size.min");
    s.day = key_u64(cfg, "supply.day");
    s.day_span = key_f64(cfg, "sources.day_span");
    s.dropout_min = key_u64(cfg, "supply.dropout.min");
    s.dropout_max = key_u64(cfg, "supply.dropout.max");
    s.blackout_share = key_f64(cfg, "supply.blackout_share");
    s.keep_min = key_f64(cfg, "supply.dropout.keep_min");
    s.keep_max = key_f64(cfg, "supply.dropout.keep_max");
    s.swell_min = key_f64(cfg, "sources.swell.min");
    s.swell_max = key_f64(cfg, "sources.swell.max");
    if (s.swell_max < s.swell_min)
        cfg.fail("sources.swell.max", "must be at least sources.swell.min");
    read_ramp_settings(cfg, &s.ramp);
    s.swell_shortest = key_u64(cfg, "sources.swell.shortest");
    s.swell_longest = key_u64(cfg, "sources.swell.longest");
    if (s.swell_longest < s.swell_shortest)
        cfg.fail("sources.swell.longest", "must be at least sources.swell.shortest");
    double shares = 0;
    for (u32 t = 0; t < kSourceTypes; ++t) {
        SourceType& y = s.type[t];
        char key[64];
        snprintf(key, sizeof key, "source.%s.share", kSourceTypeNames[t]);
        y.share = key_f64(cfg, key);
        shares += y.share;
        snprintf(key, sizeof key, "source.%s.voltage", kSourceTypeNames[t]);
        y.voltage = static_cast<i32>(key_u32(cfg, key));
        snprintf(key, sizeof key, "source.%s.sag.min", kSourceTypeNames[t]);
        y.sag_min = static_cast<i32>(key_u32(cfg, key));
        snprintf(key, sizeof key, "source.%s.sag.max", kSourceTypeNames[t]);
        y.sag_max = static_cast<i32>(key_u32(cfg, key));
        if (y.sag_max < y.sag_min || y.sag_max >= y.voltage)
            cfg.fail(key, "must be at least the type's sag.min, and below its voltage");
        snprintf(key, sizeof key, "source.%s.night", kSourceTypeNames[t]);
        y.night = key_f64(cfg, key);
        snprintf(key, sizeof key, "source.%s.swing", kSourceTypeNames[t]);
        y.swing = key_f64(cfg, key);
        snprintf(key, sizeof key, "source.%s.swing_period", kSourceTypeNames[t]);
        y.swing_period = key_u64(cfg, key);
        snprintf(key, sizeof key, "source.%s.dropouts", kSourceTypeNames[t]);
        y.dropouts = key_f64(cfg, key);
        if (y.dropouts / 1e6 * static_cast<double>(dropout_block(s.dropout_max)) > 1)
            cfg.fail(key, "too many for drop-outs this long: at most one can start in each stretch of the "
                          "longest's length, rounded up to a power of two");
        snprintf(key, sizeof key, "source.%s.swells", kSourceTypeNames[t]);
        y.swells = key_f64(cfg, key);
        if (y.swells / 1e6 * static_cast<double>(dropout_block(s.swell_longest)) > 1)
            cfg.fail(key, "too many for swells this long: at most one can start in each stretch of the "
                          "longest's length, rounded up to a power of two");
    }
    if (s.on && !(shares > 0))
        cfg.fail("source.steady.share", "the source types' shares must not all be nothing");
    // The sources placed by hand: "place type watts sag", with commas between.
    const Str fixed = key_str(cfg, "sources.fixed");
    const u32 places = static_cast<u32>(key_peek_u64(cfg, "world.places"));
    s.fixed_count = 0;
    usize at = 0;
    while (at < fixed.n) {
        Str w[4];
        u32 n = 0;
        while (n < 4 && word(fixed, &at, &w[n]))
            ++n;
        Str extra;
        const bool more = word(fixed, &at, &extra);
        if (at < fixed.n && fixed.p[at] == ',')
            ++at;
        if (n == 0 && !more)
            continue; // an empty entry
        FixedSource f;
        u64 place = 0, sag = 0;
        u32 type = kSourceTypes;
        for (u32 t = 0; t < kSourceTypes && n == 4; ++t)
            if (w[1].n == strlen(kSourceTypeNames[t]) && memcmp(w[1].p, kSourceTypeNames[t], w[1].n) == 0)
                type = t;
        if (n != 4 || more || type == kSourceTypes || !parse_u64(w[0], &place) || place >= places ||
            !parse_f64(w[2], &f.watts) || !(f.watts > 0) || f.watts > 1e9 || !parse_u64(w[3], &sag) || sag == 0 ||
            sag >= static_cast<u64>(s.type[type].voltage) || s.fixed_count == kMaxFixedSources) {
            cfg.fail("sources.fixed", "must list sources as 'place type watts sag', with commas between: a place "
                                      "of the world, steady, daylight or gusty, the load it's rated for, and its "
                                      "sag there in mV, below the type's voltage");
            break;
        }
        f.place = static_cast<u32>(place);
        f.type = type;
        f.sag = static_cast<i32>(sag);
        s.fixed[s.fixed_count++] = f;
    }
}

bool Sources::init(const SourceSettings& s, const Map* map, u64 seed, TextBuf* why) {
    s_ = s;
    map_ = map;
    seed_ = seed;
    const u32 n = map->place_count();
    double shares = 0;
    for (u32 t = 0; t < kSourceTypes; ++t) {
        SupplySettings law;
        law.day = s.day;
        law.night = s.type[t].night;
        law.swing = s.type[t].swing;
        law.swing_period = s.type[t].swing_period;
        law.dropouts = s.type[t].dropouts;
        law.dropout_min = s.dropout_min;
        law.dropout_max = s.dropout_max;
        law.blackout_share = s.blackout_share;
        law.keep_min = s.keep_min;
        law.keep_max = s.keep_max;
        if (!law_[t].init(law, why))
            return false;
        shares += s.type[t].share > 0 ? s.type[t].share : 0;
    }
    double run = 0;
    for (u32 t = 0; t < kSourceTypes; ++t) {
        run += s.type[t].share > 0 ? s.type[t].share : 0;
        type_cut_[t] = t + 1 == kSourceTypes || !(shares > 0) ? ~0ull : to_threshold(run / shares);
    }
    const double per_tick = s.chance * n / 100.0 / 1e6;
    if (per_tick > 0.5) {
        if (why)
            why->append("sources.chance: too many for this many places (at most one new source every other tick)");
        return false;
    }
    birth_p64_ = to_p64(per_tick);
    start_count_ = static_cast<u32>(s.start * n / 100.0 + 0.5);
    // The places' points now, at setup: the sources' days and the fixed
    // sources' landing points are by them, whoever respawns later (7.8).
    x0_.resize(0);
    y0_.resize(0);
    for (u32 i = 0; i < n && map->placed(); ++i) {
        x0_.push(map->x(i));
        y0_.push(map->y(i));
    }
    // Landing: each place's chance, its catchment's area to the power (4.2).
    land_place_.resize(0);
    land_sum_.resize(0);
    double sum = 0;
    for (u32 i = 0; i < n; ++i) {
        const u32 a = map->catchment(i);
        if (!a)
            continue;
        sum += pow(static_cast<double>(a), s.area_power);
        land_place_.push(i);
        land_sum_.push(sum);
    }
    cell_begin_.resize(0);
    cells_.resize(0);
    if (map->kind() == MapKind::landscape) {
        const u32 w = map->raster_w(), h = map->raster_h();
        cell_begin_.resize(n + 1);
        cell_begin_.fill(0);
        for (u32 i = 0; i < n; ++i)
            cell_begin_[i + 1] = cell_begin_[i] + map->catchment(i);
        cells_.resize(cell_begin_[n]);
        Array<u32> at;
        at.assign(cell_begin_.data(), n);
        for (u32 cy = 0; cy < h; ++cy)
            for (u32 cx = 0; cx < w; ++cx) {
                const u32 p = map->cell_place(cx, cy);
                if (p != kNoPlace)
                    cells_[at[p]++] = cy * w + cx;
            }
    }
    // Swells.
    swell_block_ = dropout_block(s.swell_longest);
    ramp_.init(s.ramp);
    const u64 drop_block = dropout_block(s.dropout_max);
    drop_back_ = (s.dropout_max + 2 * ramp_.longest() + drop_block - 1) / drop_block;
    swell_back_ = (s.swell_longest + 2 * ramp_.longest() + swell_block_ - 1) / swell_block_;
    for (u32 t = 0; t < kSourceTypes; ++t) {
        const double p = s.type[t].swells / 1e6 * static_cast<double>(swell_block_);
        if (p > 1) {
            if (why)
                why->append("source swells: too many for swells this long (at most one starts in each stretch)");
            return false;
        }
        swell_start_[t] = to_threshold(p);
    }
    swell_lo_ = to_share(s.swell_min);
    swell_hi_ = to_share(s.swell_max);
    if (swell_lo_ < kCapacityOne || swell_hi_ < swell_lo_ || s.swell_shortest == 0) {
        if (why)
            why->append("sources.swell: the factors must be 1 or more, the most at least the least, and the "
                        "shortest at least a tick");
        return false;
    }
    const double lo = static_cast<double>(s.swell_shortest), hi = static_cast<double>(s.swell_longest);
    for (u32 k = 0; k < kDropoutLengths; ++k) {
        double len = lo * pow(hi / lo, (k + 0.5) / kDropoutLengths) + 0.5;
        len = len < lo ? lo : (len > hi ? hi : len);
        swell_len_[k] = static_cast<u32>(len);
    }
    for (u32 k = 0; k < s.fixed_count; ++k)
        if (s.fixed[k].place >= n) {
            if (why)
                why->append("sources.fixed: a source at a place the world doesn't have");
            return false;
        }
    walked_ = false;
    restart();
    return true;
}

u64 Sources::phase_of(u32 place) const {
    if (!map_->placed() || !(map_->width() > 0))
        return 0;
    // East is the map's right-hand edge: a place further east is further
    // into its day.
    double f = (place < x0_.size() ? x0_[place] : map_->x(place)) / map_->width() * s_.day_span;
    f -= floor(f);
    const u64 p = static_cast<u64>(f * static_cast<double>(s_.day));
    return p >= s_.day ? s_.day - 1 : p;
}

// A place by its chance, then a cell of its catchment.
u32 Sources::land(Rng& r, u32* cell) const {
    const double u = r.unit();
    const u64 pick = r.next();
    *cell = ~0u;
    if (land_place_.empty())
        return 0;
    const double want = u * land_sum_[land_sum_.size() - 1];
    usize lo = 0, hi = land_sum_.size() - 1;
    while (lo < hi) {
        const usize mid = (lo + hi) / 2;
        if (land_sum_[mid] > want)
            hi = mid;
        else
            lo = mid + 1;
    }
    const u32 place = land_place_[lo];
    if (!cells_.empty()) {
        const u32 count = cell_begin_[place + 1] - cell_begin_[place];
        u64 top;
        _umul128(pick, count, &top);
        *cell = cells_[cell_begin_[place] + static_cast<u32>(top)];
    }
    return place;
}

Source Sources::make(u32 index, i64 birth) const {
    Source s;
    s.index = index;
    s.cell = ~0u;
    if (index < s_.fixed_count) {
        const FixedSource& f = s_.fixed[index];
        s.place = f.place;
        s.type = static_cast<u8>(f.type);
        s.fixed = true;
        s.sag = f.sag;
        s.rated_uw = static_cast<u64>(f.watts * 1e6 + 0.5);
        s.birth = 0;
        s.life = kForever;
        if (map_->placed()) {
            s.x = s.place < x0_.size() ? x0_[s.place] : map_->x(s.place);
            s.y = s.place < y0_.size() ? y0_[s.place] : map_->y(s.place);
        }
    } else {
        // Every draw is taken, in order, whatever is used.
        Rng r(seed_, index, kPurposeSource, Rng::kInitTick);
        const u64 pick = r.next();
        u32 type = 0;
        while (type + 1 < kSourceTypes && pick >= type_cut_[type])
            ++type;
        s.type = static_cast<u8>(type);
        s.place = land(r, &s.cell);
        if (s.cell != ~0u) {
            const double h = map_->raster_h();
            s.x = (s.cell % map_->raster_w() + 0.5) / h;
            s.y = (s.cell / map_->raster_w() + 0.5) / h;
        } else if (map_->placed()) {
            s.x = s.place < x0_.size() ? x0_[s.place] : map_->x(s.place);
            s.y = s.place < y0_.size() ? y0_[s.place] : map_->y(s.place);
        }
        const double us = r.unit(), ug = r.unit(), ul = r.unit(), ua = r.unit();
        const double watts = s_.size_min * pow(s_.size_max / s_.size_min, pow(us, s_.size_skew));
        s.rated_uw = static_cast<u64>(watts * 1e6 + 0.5);
        const SourceType& y = s_.type[type];
        const double lo = y.sag_min > 0 ? y.sag_min : 1, hi = y.sag_max > lo ? y.sag_max : lo;
        s.sag = static_cast<i32>(lo * pow(hi / lo, ug) + 0.5);
        const double life = static_cast<double>(s_.life) * 0.25 * pow(16.0, ul);
        s.life = life < 10 ? 10 : static_cast<u64>(life);
        s.birth = index < s_.fixed_count + start_count_ ? -static_cast<i64>(ua * static_cast<double>(s.life)) : birth;
    }
    s.voltage = s_.type[s.type].voltage;
    s.gs = source_conductance(s.voltage, s.sag, s.rated_uw);
    s.phase = phase_of(s.place);
    attach(&s);
    return s;
}

// The place a source is at, once places have respawned: the one nearest its
// landing point.
void Sources::attach(Source* s) const {
    if (!map_->respawned() || map_->kind() != MapKind::landscape)
        return;
    if (s->cell != ~0u) {
        const u32 p = map_->cell_place(s->cell % map_->raster_w(), s->cell / map_->raster_w());
        if (p != kNoPlace)
            s->place = p;
        return;
    }
    u32 best = s->place;
    double bd = -1;
    for (u32 i = 0; i < map_->place_count(); ++i) {
        const double ex = map_->x(i) - s->x, ey = map_->y(i) - s->y, d2 = ex * ex + ey * ey;
        if (bd < 0 || d2 < bd) {
            best = i;
            bd = d2;
        }
    }
    s->place = best;
}

void Sources::reattach() {
    for (usize k = 0; k < live_.size(); ++k)
        live_[k] = make(live_[k].index, live_[k].birth);
}

u32 Sources::envelope(const Source& s, u64 tick) const {
    if (s.fixed)
        return kCapacityOne;
    const u64 age = static_cast<u64>(static_cast<i64>(tick) - s.birth);
    if (age >= s.life)
        return 0;
    const u64 tenth = s.life / 10 ? s.life / 10 : 1;
    const u64 left = s.life - age;
    const u64 edge = age < left ? age : left;
    return edge >= tenth ? kCapacityOne : static_cast<u32>((edge << kCapacityBits) / tenth);
}

u32 Sources::day(const Source& s, u64 tick) const { return law_[s.type].day_at(tick + s.phase); }
u32 Sources::swing(const Source& s, u64 tick) const {
    return law_[s.type].swing(seed_, s.index, kPurposeSourceSwing, tick);
}
bool Sources::dropout_in(const Source& s, u64 j, Dropout* d) const {
    return law_[s.type].dropout_in(seed_, s.index, kPurposeSourceDropout, j, d);
}
u64 Sources::dropout_stretch(const Source& s) const { return law_[s.type].block(); }
u32 Sources::dropout(const Source& s, u64 tick, Dropout* which) const {
    const CapacityLaw& law = law_[s.type];
    if (!ramp_.on() || !law.has_dropouts())
        return law.dropout(seed_, s.index, kPurposeSourceDropout, tick, which);
    // Ramped (12.4): the capacity falls from full to the drop-out's own over
    // its lead, and comes back over as long. The deepest counts.
    u32 c = kCapacityOne;
    const u64 j = tick / law.block();
    for (u64 k = j > drop_back_ ? j - drop_back_ : 0; k <= j; ++k) {
        Dropout d;
        if (!law.dropout_in(seed_, s.index, kPurposeSourceDropout, k, &d) || tick < d.start)
            continue;
        const Ramp rp = ramp_.draw(seed_, s.index, kPurposeRampDropout, k);
        const u64 level = RampLaw::level(rp, tick - d.start, d.length);
        if (!level)
            continue;
        const u32 keep = kCapacityOne - static_cast<u32>((level * (kCapacityOne - d.keep)) >> kCapacityBits);
        if (keep < c) {
            c = keep;
            if (which)
                *which = d;
        }
    }
    return c;
}

bool Sources::swell_in(const Source& s, u64 j, Swell* w) const {
    if (swell_start_[s.type] == 0)
        return false;
    Rng r(seed_, s.index, kPurposeSourceSwell, j);
    if (r.next() >= swell_start_[s.type])
        return false;
    w->start = j * swell_block_ + r.below(swell_block_);
    w->length = swell_len_[r.next() >> 54];
    w->factor = swell_lo_ + static_cast<u32>(r.below(static_cast<u64>(swell_hi_ - swell_lo_) + 1));
    return true;
}

u32 Sources::swell(const Source& s, u64 tick, Swell* which) const {
    u32 f = kCapacityOne;
    if (swell_start_[s.type] == 0)
        return f;
    const u64 j = tick / swell_block_;
    if (ramp_.on()) {
        // Ramped (12.4): the factor rises from 1 to the swell's own over its
        // lead, and eases over as long. The highest counts.
        for (u64 k = j > swell_back_ ? j - swell_back_ : 0; k <= j; ++k) {
            Swell w;
            if (!swell_in(s, k, &w) || tick < w.start)
                continue;
            const Ramp rp = ramp_.draw(seed_, s.index, kPurposeRampSwell, k);
            const u64 level = RampLaw::level(rp, tick - w.start, w.length);
            const u32 up = kCapacityOne + static_cast<u32>((level * (w.factor - kCapacityOne)) >> kCapacityBits);
            if (level && up > f) {
                f = up;
                if (which)
                    *which = w;
            }
        }
        return f;
    }
    for (u64 k = j ? j - 1 : 0; k <= j; ++k) {
        Swell w;
        if (swell_in(s, k, &w) && tick >= w.start && tick - w.start < w.length && w.factor > f) {
            f = w.factor;
            if (which)
                *which = w;
        }
    }
    return f;
}

SourceState Sources::state_at(const Source& s, u64 tick) const {
    SourceState st;
    u64 c = envelope(s, tick);
    c = c * day(s, tick) >> kCapacityBits;
    c = c * swing(s, tick) >> kCapacityBits;
    c = c * dropout(s, tick) >> kCapacityBits;
    st.capacity = static_cast<u32>(c);
    st.swell = swell(s, tick);
    st.g = scale20(s.gs, c);
    st.uv = static_cast<i64>(scale20(static_cast<u64>(s.voltage) * 1000, st.swell));
    return st;
}

void Sources::restart() {
    live_.resize(0);
    state_.resize(0);
    next_index_ = 0;
    next_birth_ = 0;
    begun_ = ended_ = 0;
    tick_ = 0;
}

void Sources::seek(u64 tick) {
    if (!walked_ || tick < tick_)
        restart();
    walked_ = true;
    tick_ = tick;
    const u32 first = s_.fixed_count + start_count_;
    // Those there from the start, then each birth due by now. The first
    // birth's gap is counted from tick 0; a gap is at least a tick, so no
    // two are born together.
    for (;;) {
        if (next_index_ < first) {
            live_.push(make(next_index_, 0));
        } else {
            if (birth_p64_ == 0)
                break;
            if (next_index_ == first && next_birth_ == 0) {
                Rng r(seed_, Rng::kWorldPlace, kPurposeSourceBirth, next_index_);
                next_birth_ = static_cast<i64>(geometric_gap(birth_p64_, r.next())) - 1;
            }
            if (static_cast<u64>(next_birth_) > tick)
                break;
            live_.push(make(next_index_, next_birth_));
            Rng r(seed_, Rng::kWorldPlace, kPurposeSourceBirth, next_index_ + 1);
            const u64 gap = geometric_gap(birth_p64_, r.next());
            next_birth_ = gap > (1ull << 62) ? static_cast<i64>(1ull << 62) : next_birth_ + static_cast<i64>(gap);
        }
        ++next_index_;
        ++begun_;
    }
    // Those whose lives are over go, the rest keep their order.
    usize keep = 0;
    for (usize k = 0; k < live_.size(); ++k) {
        const Source& s = live_[k];
        if (!s.fixed && static_cast<i64>(tick) - s.birth >= static_cast<i64>(s.life)) {
            ++ended_;
            continue;
        }
        if (keep != k)
            live_[keep] = s;
        ++keep;
    }
    live_.resize(keep);
    state_.resize(keep);
    for (usize k = 0; k < keep; ++k)
        state_[k] = state_at(live_[k], tick);
}

void Sources::describe(TextBuf* t) const {
    u32 by[kSourceTypes] = {};
    double watts = 0;
    for (usize k = 0; k < live_.size(); ++k) {
        ++by[live_[k].type];
        watts += static_cast<double>(live_[k].rated_uw) / 1e6;
    }
    char line[240];
    snprintf(line, sizeof line,
             "sources: %u at tick %llu (%u steady, %u daylight, %u gusty), rated %.0f W in all; %llu begun, %llu "
             "ended",
             count(), static_cast<unsigned long long>(tick_), by[0], by[1], by[2], watts,
             static_cast<unsigned long long>(begun_), static_cast<unsigned long long>(ended_));
    t->append(line);
}

} // namespace nova2
