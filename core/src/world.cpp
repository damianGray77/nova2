#include "nova2/world.h"

#include <cmath>
#include <cstring> // memcpy, memchr
#include <intrin.h> // __rdtsc: the network step's cost and gate 5's, for reports

#include "nova2/archive.h"
#include "nova2/hash.h"
#include "nova2/observe.h"
#include "nova2/purposes.h"
#include "nova2/rng.h"

namespace nova2 {

namespace {

constexpr u64 kIdBits = 20; // a place id fits a deal key's low 20 bits (world.places is at most 10^6)
constexpr u64 kIdMask = (1ull << kIdBits) - 1;

// Each place's rank in a field's order, lowest value first, ties by id.
void field_ranks(const Map& map, u64 seed, Field f, const MapSettings& ms, Array<u32>* rank) {
    Array<double> v;
    map.field_values(seed, f, ms, &v);
    const u32 n = map.place_count();
    Array<u64> keys;
    for (u32 i = 0; i < n; ++i)
        keys.push((order_key(v[i]) & ~kIdMask) | i);
    sort_u64(keys.data(), n);
    rank->resize(n);
    for (u32 k = 0; k < n; ++k)
        (*rank)[static_cast<u32>(keys[k] & kIdMask)] = k;
}

// Sorts both key lists; then the place at position k of `to` takes from the
// drawn place at position k of `from` (each key's low bits its place).
void pair_up(Array<u64>& to, Array<u64>& from) {
    sort_u64(to.data(), to.size());
    sort_u64(from.data(), from.size());
    for (usize k = 0; k < to.size(); ++k) {
        to[k] &= kIdMask;
        from[k] &= kIdMask;
    }
}

u32 e_count(const PlaceHw& h) {
    u32 e = 0;
    for (u32 c = 0; c < h.cores; ++c)
        e += h.e[c];
    return e;
}

u32 latency_sum(const PlaceHw& h) {
    u32 s = 0;
    for (u32 d = 0; d < h.devices && d < kMaxMemDevices; ++d)
        s += h.latency[d];
    return s;
}

// The values drawn for every place, dealt out by the fields (7.3; world.h).
void deal_by_fields(const Map& map, u64 seed, const MapSettings& ms, Array<PlaceHw>& hw) {
    const u32 n = static_cast<u32>(hw.size());
    NOVA2_ASSERT(n <= kIdMask + 1);
    Array<PlaceHw> drawn;
    drawn.assign(hw.data(), n);
    Array<u32> rank;
    Array<u64> to, from;
    // The core counts, by their field.
    field_ranks(map, seed, Field::cores, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].cores) << kIdBits) | i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        hw[static_cast<u32>(to[k])].cores = drawn[static_cast<u32>(from[k])].cores;
    // The E cores' patterns among the places with the same core count, in
    // order of E cores, by theirs.
    field_ranks(map, seed, Field::e_cores, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(hw[i].cores) << 40) | (static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].cores) << 40) | (static_cast<u64>(e_count(drawn[i])) << kIdBits) | i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        memcpy(hw[static_cast<u32>(to[k])].e, drawn[static_cast<u32>(from[k])].e, kMaxCores);
    // The memory device counts, by theirs.
    field_ranks(map, seed, Field::devices, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].devices) << kIdBits) | i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        hw[static_cast<u32>(to[k])].devices = drawn[static_cast<u32>(from[k])].devices;
    // Each place's set of latencies, among the places with the same device
    // count, in order of their sum, by theirs.
    field_ranks(map, seed, Field::latency, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(hw[i].devices) << 44) | (static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].devices) << 44) | (static_cast<u64>(latency_sum(drawn[i])) << kIdBits) |
                  i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        memcpy(hw[static_cast<u32>(to[k])].latency, drawn[static_cast<u32>(from[k])].latency,
               sizeof hw[0].latency);
    // The page sizes, by theirs.
    field_ranks(map, seed, Field::page, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].page_size) << kIdBits) | i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        hw[static_cast<u32>(to[k])].page_size = drawn[static_cast<u32>(from[k])].page_size;
    // Strict or lax, by theirs (lax first).
    field_ranks(map, seed, Field::strict, ms, &rank);
    to.clear();
    from.clear();
    for (u32 i = 0; i < n; ++i) {
        to.push((static_cast<u64>(rank[i]) << kIdBits) | i);
        from.push((static_cast<u64>(drawn[i].strict ? 1 : 0) << kIdBits) | i);
    }
    pair_up(to, from);
    for (u32 k = 0; k < n; ++k)
        hw[static_cast<u32>(to[k])].strict = drawn[static_cast<u32>(from[k])].strict;
}

} // namespace

World::~World() {
    clear();
    delete[] chunk_hits_;
}

void World::clear() {
    for (usize i = 0; i < places_.size(); ++i)
        delete places_[i];
    places_.clear();
}

bool World::init(Config& cfg, TextBuf* why) {
    clear();
    read_world_settings(cfg, &ws_);
    read_map_settings(cfg, &ms_);
    read_net_settings(cfg, &ns_);
    read_source_settings(cfg, &ss_);
    read_solve_settings(cfg, &gs_);
    read_link_settings(cfg, &ls_);
    read_event_settings(cfg, &es_);
    read_current_settings(cfg, &cs_);
    if (cs_.strength > 0 && cs_.block > ns_.message_max)
        cfg.fail("current.block", "a block is a message: at most net.message_max bytes");
    if (!cfg.ok())
        return false;
    current_.init(cs_, ws_.seed);
    drift = CurrentCounts{};
    // Whether links can fail here (links.h's rule): the network then saves
    // the failed links it routes around.
    ns_.outages = ms_.kind != MapKind::none &&
                  (ls_.cuts > 0 || (ss_.on && ws_.wear.rate > 0 && ls_.endurance > 0));
    // The map first (6.1), then every place's values, drawn and, with the
    // fields, dealt out by them (7.3).
    {
        TextBuf reason;
        if (!map_.build(ws_.seed, ws_.places, ms_, &reason) || !net_.init(&map_, ns_, ws_.seed, &reason)) {
            if (why)
                why->append(reason.view());
            return false;
        }
        dark_.resize(0);
        dark_.resize(ws_.places);
    }
    Array<PlaceHw> hw;
    hw.resize(ws_.places);
    for (u32 i = 0; i < ws_.places; ++i)
        draw_hardware(ws_, i, &hw[i]);
    if (ms_.fields && map_.placed())
        deal_by_fields(map_, ws_.seed, ms_, hw);
    // Visits follow density (7.8): a place's mean interval is the world's ×
    // (its density ÷ the average)^(−tie). Its density is the map's density
    // to the power of its rank among the places by the density field, as
    // the map lays land out (map.cpp): 1 at the sparsest, map.density at
    // the densest.
    if (ws_.visit.mean && ws_.visit.density_tie != 0 && ms_.fields && map_.placed() && ws_.places > 1) {
        Array<u32> rank;
        field_ranks(map_, ws_.seed, Field::density, ms_, &rank);
        Array<double> dens;
        dens.resize(ws_.places);
        const double top = ms_.density > 1 ? ms_.density : 1;
        double sum = 0;
        for (u32 i = 0; i < ws_.places; ++i) {
            dens[i] = pow(top, static_cast<double>(rank[i]) / (ws_.places - 1));
            sum += dens[i];
        }
        for (u32 i = 0; i < ws_.places; ++i)
            hw[i].visits = pow(dens[i] * ws_.places / sum, -ws_.visit.density_tie);
    }
    // The cooling devices (7.7): at cooler.share of the places, those the
    // coolers' field puts highest; with no fields, each place by a draw.
    if (ws_.heat.on && ws_.heat.cooler_share > 0) {
        if (ms_.fields && map_.placed()) {
            Array<u32> rank;
            field_ranks(map_, ws_.seed, Field::coolers, ms_, &rank);
            const u32 count = static_cast<u32>(ws_.heat.cooler_share * ws_.places + 0.5);
            for (u32 i = 0; i < ws_.places; ++i)
                hw[i].cooler = rank[i] + count >= ws_.places;
        } else {
            for (u32 i = 0; i < ws_.places; ++i) {
                Rng r(ws_.seed, i, kPurposeCooler, Rng::kInitTick);
                hw[i].cooler = r.chance(ws_.heat.cooler_share);
            }
        }
    }
    // Kept for a respawned place's values (7.3), and its records' reader.
    hw0_.assign(hw.data(), hw.size());
    respawns_on_ = ws_.visit.mean > 0 && ws_.visit.respawn_share <= 1;
    first_respawn_.resize(0);
    first_respawn_.resize(ws_.places);
    first_respawn_.fill(~0ull);
    struck_.resize(0);
    struck_.resize(ws_.places);
    respawns = respawns_moved = torn_programs = 0;
    for (u32 i = 0; i < ws_.places; ++i) {
        Place* p = new Place;
        places_.push(p);
        TextBuf reason;
        if (!p->init(i, ws_, hw[i], &reason)) {
            if (why) {
                why->append("place ");
                why->append_u64(i);
                why->append(": ");
                why->append(reason.view());
            }
            return false;
        }
    }
    // With a map, each OS has the network for its calls, and knows its data
    // neighbours (A.4).
    if (net_.active())
        for (u32 i = 0; i < ws_.places; ++i) {
            places_[i]->os.attach_net(&net_);
            places_[i]->env.neighbours = map_.degree(i);
        }
    hashes_.resize(0);
    hashes_.resize(places_.size());
    tick_ = 0;
    if (pool_.threads() == 0)
        pool_.start(1);
    build_heat();
    heat_out_uw = 0;
    solve_cycles = heat_cycles = 0;
    // Gate 5's events, before the first solve, which takes the patches of
    // tick 0.
    strikes = 0;
    {
        TextBuf reason;
        if (!events_.init(es_, &map_, ws_.seed, &reason)) {
            if (why)
                why->append(reason.view());
            return false;
        }
    }
    // The sources and the solve (gate 4). The world starts with the voltages
    // the sources give with nothing drawn, solved until they hold still.
    if (ss_.on) {
        TextBuf reason;
        if (!sources_.init(ss_, &map_, ws_.seed, &reason) || !grid_.init(gs_, &map_, &reason)) {
            if (why)
                why->append(reason.view());
            return false;
        }
        links_.init(ls_, ws_.wear.rate, &map_, &grid_, ws_.seed);
        power_step(100000);
        grid_.solves = grid_.sweeps = grid_.capped = 0;
        solve_cycles = 0;
        for (u32 i = 0; i < ws_.places; ++i) {
            Place& p = *places_[i];
            p.env.voltage_mv = p.given_mv;
            p.mv_lowest = p.given_mv;
        }
    } else {
        links_.init(ls_, ws_.wear.rate, &map_, nullptr, ws_.seed);
    }
    return true;
}

void World::begin_at(u64 tick) {
    tick_ = tick;
    for (u32 i = 0; i < places_.size(); ++i)
        places_[i]->env.tick = tick;
    // The straits where their spells put them at the tick, before the first
    // solve, as a run's tick has them (inputs).
    if (links_.on()) {
        links_.straits_due(tick, false);
        links_.sync();
    }
    if (!ss_.on)
        return;
    // The voltages those sources give with nothing drawn, as init's.
    power_step(100000);
    grid_.solves = grid_.sweeps = grid_.capped = 0;
    solve_cycles = 0;
    for (u32 i = 0; i < places_.size(); ++i) {
        Place& p = *places_[i];
        p.env.voltage_mv = p.given_mv;
        p.mv_lowest = p.given_mv;
    }
}

void World::respawn_at(u32 i, u64 tick) {
    tick_ = tick;
    respawn(i);
}

// Appendix C's steps 1 to 3: the sources' states at this tick, each place's
// conductance from its last tick (G = P / V^2), and the solve. Each place is
// handed its voltage for the tick.
void World::power_step(u32 cap) {
    const u64 c0 = __rdtsc();
    sources_.seek(tick_);
    grid_.begin();
    for (u32 i = 0; i < places_.size(); ++i) {
        const Place& p = *places_[i];
        grid_.load(i, load_conductance(p.draw.total(), p.env.voltage_mv));
    }
    // The patches over the sources (12.3), each source by its landing
    // point: a swell patch raises its voltage, a brownout lowers its
    // capacity.
    events_.seek(tick_);
    const bool swell = events_.any(PatchType::swell), brown = events_.any(PatchType::brownout);
    for (u32 k = 0; k < sources_.count(); ++k) {
        const SourceState& st = sources_.state(k);
        const Source& s = sources_.source(k);
        i64 uv = st.uv;
        u64 g = st.g;
        if (swell)
            uv = static_cast<i64>(static_cast<double>(uv) * events_.swell_at(s.x, s.y) + 0.5);
        if (brown)
            g = static_cast<u64>(static_cast<double>(g) * events_.brownout_at(s.x, s.y));
        grid_.source(s.place, uv, g);
    }
    // A storm's strikes (12.2): a source of one tick at each place one
    // lands on, in place order.
    if (events_.any(PatchType::storm))
        for (u32 i = 0; i < places_.size(); ++i)
            if (events_.strike(i, tick_, events_.storm_at(map_.x(i), map_.y(i)))) {
                grid_.source(i, events_.strike_uv(), events_.strike_g());
                ++strikes;
                if (i < struck_.size())
                    struck_[i] = 1;
            }
    grid_.solve(cap);
    for (u32 i = 0; i < places_.size(); ++i)
        places_[i]->given_mv = grid_.mv(i);
    solve_cycles += __rdtsc() - c0;
}

// Appendix C's step 7, the places' half (heat.h): every place steps from
// the temperatures the tick began with. Into it: what its devices passed
// it, and its links' and sources' losses. Out of it: to the outside at its
// cooling rate, more with a cooling device working, and to each neighbour
// over land.
void World::heat_places() {
    const u64 c0 = __rdtsc();
    const u32 n = static_cast<u32>(places_.size());
    const u8* gone = links_.down_count() ? links_.power_down() : nullptr; // a failed link carries no heat
    // A heat wave (12.3) raises the temperature its places cool toward.
    // Leakage and wear stay counted from the world's outside temperature.
    const bool wave = events_.any(PatchType::heatwave);
    i64 out = 0;
    for (u32 i = 0; i < n; ++i) {
        const Place& p = *places_[i];
        const HeatLaw& law = p.heat;
        const u64 outside = cool_rate_[i] + ((law.cooler_rate * p.cooler_work) >> 20);
        const i64 toward =
            law.outside + (wave ? static_cast<i64>(events_.heat_at(map_.x(i), map_.y(i)) * kDegree) : 0);
        const i64 left = heat_flow(outside, p.temp, toward);
        i64 net = p.passed_uw + (ss_.on ? static_cast<i64>(grid_.place_heat(i)) : 0) - left;
        u64 rate = outside;
        for (u32 e = heat_begin_[i]; e < heat_begin_[i + 1]; ++e) {
            if (gone && gone[heat_link_[e]])
                continue;
            net -= heat_flow(heat_rate_[e], p.temp, places_[heat_other_[e]]->temp);
            rate += heat_rate_[e];
        }
        temp_next_[i] = heat_step_net(p.temp, net, rate, law.place_capacity);
        out += left;
    }
    for (u32 i = 0; i < n; ++i)
        places_[i]->set_temp(temp_next_[i]);
    heat_out_uw = out;
    heat_cycles += __rdtsc() - c0;
}

void World::tick_one(void* ctx, u32 k) {
    World* w = static_cast<World*>(ctx);
    const u32 i = w->heaviest_[k];
    const u64 c0 = __rdtsc();
    w->deferred_[i] = w->places_[i]->tick_begin(w->defer_cells_) ? 1 : 0;
    w->place_cycles_[i] = __rdtsc() - c0;
}

namespace {

// A tick's cycles in steps of a quarter of a doubling, 0 to 255.
u32 weight_step(u64 c) {
    if (c < 4)
        return static_cast<u32>(c);
    unsigned long msb;
    _BitScanReverse64(&msb, c);
    return msb * 4 + static_cast<u32>((c >> (msb - 2)) & 3);
}

} // namespace

// The order the host threads take the places in: heaviest first by their
// last tick's time, to a quarter of a doubling, and by place within that.
// The longest ticks start first and the run ends on short ones, so the
// barrier comes sooner. Each place's tick is its own (tick_one): no order
// changes a result.
void World::order_heaviest() {
    const u32 n = place_count();
    if (place_cycles_.size() != n) {
        place_cycles_.resize(n);
        place_cycles_.fill(0);
        heaviest_.resize(n);
    }
    u32 at[256] = {};
    for (u32 i = 0; i < n; ++i)
        ++at[weight_step(place_cycles_[i])];
    u32 from = 0;
    for (u32 s = 256; s-- > 0;) {
        const u32 count = at[s];
        at[s] = from;
        from += count;
    }
    for (u32 i = 0; i < n; ++i)
        heaviest_[at[weight_step(place_cycles_[i])]++] = i;
}

void World::set_fade_spread(u64 chunk, u64 defer) {
    chunk_cells_ = chunk < 64 ? 64 : (chunk + 63) & ~63ull;
    defer_cells_ = defer ? defer : 1;
}

// The places whose ticks stopped before a big fade's cells: those cells in
// chunks, one task each, over the host threads; then each place's tick
// finished from there, its chunks' hits joined in order (Place::tick_end).
// Only each task's own chunk, or place, is written: a fade's cells are
// distinct, its chunks of one device set their bits with interlocked
// operations when they may run at once, and the rest of a place's tick is
// what its task would have done in the run before, on its own state.
void World::fade_spread() {
    const u32 n = place_count();
    if (!n || !memchr(deferred_.data(), 1, n))
        return;
    chunks_.resize(0);
    spread_.resize(0);
    spread_from_.resize(0);
    const bool wide = pool_.threads() > 1;
    for (u32 i = 0; i < n; ++i) {
        if (!deferred_[i])
            continue;
        spread_.push(i);
        spread_from_.push(static_cast<u32>(chunks_.size()));
        const Array<FadeJob>& jobs = places_[i]->noise.fade_jobs();
        for (u32 j = 0; j < jobs.size(); ++j) {
            const FadeJob& f = jobs[j];
            const u8 shared = wide && f.upto - f.from > chunk_cells_ ? 1 : 0;
            for (u64 k = f.from; k < f.upto; k += chunk_cells_)
                chunks_.push(FadeChunk{i, j, k, f.upto - k > chunk_cells_ ? k + chunk_cells_ : f.upto, shared, 0});
            spread_cells += f.upto - f.from;
        }
    }
    const u32 count = static_cast<u32>(chunks_.size());
    spread_from_.push(count);
    if (count > hit_lists_) {
        delete[] chunk_hits_;
        hit_lists_ = count > 2 * hit_lists_ ? count : 2 * hit_lists_;
        chunk_hits_ = new Array<u32>[hit_lists_];
    }
    for (u32 c = 0; c < count; ++c)
        chunk_hits_[c].resize(0);
    spread_fades += spread_.size();
    spread_chunks += count;
    pool_.run(count, chunk_one, this);
    pool_.run(static_cast<u32>(spread_.size()), end_one, this);
}

void World::chunk_one(void* ctx, u32 c) {
    World* w = static_cast<World*>(ctx);
    FadeChunk& k = w->chunks_[c];
    const Place& p = *w->places_[k.place];
    k.region = p.fade_chunk(p.noise.fade_jobs()[k.job], k.k0, k.k1, &w->chunk_hits_[c], k.shared != 0) ? 1 : 0;
}

void World::end_one(void* ctx, u32 k) {
    World* w = static_cast<World*>(ctx);
    const u32 from = w->spread_from_[k], to = w->spread_from_[k + 1];
    bool region = false;
    for (u32 c = from; c < to; ++c)
        region = region || w->chunks_[c].region;
    w->places_[w->spread_[k]]->tick_end(region, w->chunk_hits_ + from, to - from);
}

// A neighbour's reading for a SENSE from place `from` (A.4, gap 4), taken
// at the barrier its request lands at: every reading but the id is 0 if
// either place's network device is off.
u32 World::neighbour_reading(u32 from, u32 nb, u32 which) const {
    if (which == kNeighbourId)
        return nb;
    const Place& p = *places_[nb];
    if (!places_[from]->network_state.on || !p.network_state.on)
        return 0;
    // Nor over a failed link, or a parted strait's, which carries nothing (11.6).
    if (links_.down_count())
        for (u32 j = 0; j < map_.degree(from); ++j) {
            const u32 k = map_.link_at(from, j);
            if (map_.other(k, from) == nb && links_.data_down()[k])
                return 0;
        }
    switch (which) {
    case kNeighbourVoltage:
        return static_cast<u32>(p.env.voltage_mv);
    case kNeighbourTemperature:
        return static_cast<u32>(p.env.temperature_dc);
    case kNeighbourIdleCores: {
        u32 n = 0;
        for (u32 c = 0; c < p.core_count(); ++c)
            n += p.cores[c].working && !p.cores[c].thread ? 1 : 0;
        return n;
    }
    default: // kNeighbourFreeMemory
        return p.mem.free_bytes();
    }
}

// The barrier's network step (net.h), at the tick just run.
void World::network_step() {
    // The SENDs and PINGs every place let go this tick, in place order, ahead
    // of the barrier's arrivals (gap 7).
    for (u32 i = 0; i < places_.size(); ++i) {
        Os& os = places_[i]->os;
        for (u32 k = 0; k < os.outbox_count(); ++k) {
            const Os::OutMsg& o = os.outbox(k);
            net_.post(i, o.dst, o.end, o.ping ? MsgKind::ping : MsgKind::data, o.port, os.outbox_bytes() + o.at,
                      o.size, o.sender, o.sent, o.tag_t, o.tag_id, o.tag_seq);
        }
        os.clear_outbox();
    }
    for (usize i = 0; i < places_.size(); ++i)
        dark_[i] = places_[i]->network_state.on ? 0 : 1;
    // The current's blocks, copied and queued behind them (12.6).
    if (drifting())
        current_step();
    net_.step(tick_, dark_.data(), links_.on() ? links_.data_down() : nullptr);
    // The out-of-memory killer, before arrivals join port 0 queues and RECVs
    // are delivered (appendix C 6).
    if (killing())
        killer_step();
    // What reached its port: an arrival for port 0 joins the reset region's
    // queue, with its sender, or is dropped if there's no room (10.4); a
    // message for a comms port goes to a waiting RECV or the port's inbox
    // (11.2); a PING's echo ends its thread's wait.
    for (u32 i = 0; i < net_.delivered_count(); ++i) {
        const Message& g = net_.delivered(i);
        Place& p = *places_[g.dst];
        if (g.kind == MsgKind::current) {
            // A block of the current's lands in the region, as raw bytes
            // (12.6), unless the memory device the region is on is off or
            // failed.
            const u32 d = p.mem.region_device();
            if (p.memory_worn[d].failed || !p.memory_state[d].on) {
                ++drift.unlanded;
                continue;
            }
            drift.landed_bytes += p.region.drift(net_.data(g), g.size, g.tag_seq, g.sender);
            ++drift.landed;
            continue;
        }
        if (g.kind == MsgKind::echo)
            p.os.ping_reply(g.tag_t, g.tag_id, g.tag_seq, static_cast<u32>(tick_ - g.sent));
        else if (g.port == 0)
            p.region.arrive(net_.data(g), g.size, BirthCause::arrival, g.sender, false, false);
        else
            p.os.deliver(g.port, net_.data(g), g.size, g.src, g.sender);
    }
    for (usize i = 0; i < places_.size(); ++i)
        places_[i]->net_bytes += net_.take_bytes(static_cast<u32>(i));
    // The neighbour SENSEs whose requests land now are read.
    for (u32 i = 0; i < places_.size(); ++i) {
        Os& os = places_[i]->os;
        if (os.asks().empty())
            continue;
        for (usize k = 0; k < os.asks().size(); ++k) {
            const Os::Ask& a = os.asks()[k];
            if (a.lands <= tick_)
                os.answer(a, neighbour_reading(i, a.neighbour, a.which));
        }
        os.drop_asks(tick_);
    }
    // The columns the next barrier's arrivals will ask for, worked out now
    // across the host threads (net.h). Which thread works one out changes
    // nothing.
    const u32 tasks = net_.plan_routes(tick_, pool_.threads());
    if (tasks)
        pool_.run(tasks, route_one, this);
}

void World::route_one(void* ctx, u32 task) { static_cast<World*>(ctx)->net_.route_task(task); }

// The replication current's step at the barrier (current.h; 12.6): each
// place's block of this tick, if its schedule has one, copied from its
// memory and queued down one of its links, behind the tick's SENDs. In
// place order. Nothing here depends on what programs do but the room in
// the queues, the devices' states and the bytes copied.
void World::current_step() {
    current_.seek(tick_);
    const bool gusts = events_.any(PatchType::gust);
    const u8* down = links_.on() ? links_.data_down() : nullptr;
    const u32 block = cs_.block;
    for (u32 i = 0; i < places_.size(); ++i) {
        Place& p = *places_[i];
        const u64 phase = ss_.on ? sources_.phase_of(i) : p.supply.phase();
        const double gust = gusts ? events_.gust_at(map_.x(i), map_.y(i)) : 1;
        if (!current_.copies(i, current_.chance(phase, gust)))
            continue;
        ++drift.drawn;
        if (p.mem.size() < block)
            continue; // (no place is that small)
        const Current::Draw d = current_.draw(i, p.mem.size() - block + 1);
        // Down which link: by the weights of those that point downstream.
        const u32 degree = map_.degree(i);
        double total = 0;
        for (u32 j = 0; j < degree; ++j) {
            const u32 q = map_.other(map_.link_at(i, j), i);
            total += current_.weight(map_.x(i), map_.y(i), map_.x(q), map_.y(q));
        }
        if (!(total > 0)) {
            ++drift.becalmed;
            continue;
        }
        double pick = d.link * total;
        u32 k = kNoLink;
        for (u32 j = 0; j < degree; ++j) {
            const u32 q = map_.other(map_.link_at(i, j), i);
            const double w = current_.weight(map_.x(i), map_.y(i), map_.x(q), map_.y(q));
            if (!(w > 0))
                continue;
            k = map_.link_at(i, j); // (the last one downstream, if rounding leaves the pick past them all)
            if (pick < w)
                break;
            pick -= w;
        }
        const u32 q = map_.other(k, i);
        // It never waits: what stands in its way drops it (12.6).
        if (dark_[i]) {
            ++drift.unlit;
            continue;
        }
        const u32 d0 = p.mem.device_of(d.addr), d1 = p.mem.device_of(d.addr + block - 1);
        if (p.memory_worn[d0].failed || !p.memory_state[d0].on || p.memory_worn[d1].failed ||
            !p.memory_state[d1].on) {
            ++drift.unread;
            continue;
        }
        if (down && down[k]) {
            ++drift.down;
            continue;
        }
        if (dark_[q]) {
            ++drift.dark;
            continue;
        }
        const u32 e = 2 * k + (map_.link(k).a == i ? 0 : 1);
        if (net_.end_room(e) < block) {
            ++drift.full;
            continue;
        }
        // Whose bytes they are, for the observers alone: its record is kept
        // while the block could still make a program (observe.h).
        const u64 holder = p.os.holder_of(d.addr, block);
        net_.post(i, q, e, MsgKind::current, 0, p.mem.bytes() + d.addr, block, holder, tick_, 0, 0, d.spot);
        ++drift.sent;
        drift.sent_bytes += block;
        if (holder) {
            ++drift.held;
            p.os.drifts.push(DriftFact{tick_, holder, i, q});
        }
    }
}

// Heat between places (heat.h): each place's cooling rate from its field
// and its coast, and each land link's rate from its length, from the map
// as it stands.
void World::build_heat() {
    const HeatLaw& law = places_[0]->heat;
    const u32 n = ws_.places;
    cool_rate_.resize(0);
    cool_rate_.resize(n);
    heat_begin_.resize(0);
    heat_begin_.resize(n + 1);
    heat_other_.resize(0);
    heat_link_.resize(0);
    heat_rate_.resize(0);
    temp_next_.resize(0);
    temp_next_.resize(n);
    for (u32 i = 0; i < n; ++i) {
        const double c = static_cast<double>(law.place_cooling) * map_.cooling(i) *
                         (1 + ws_.heat.coast * map_.water_share(i));
        cool_rate_[i] = c <= 0 ? 0 : (c > 9e17 ? static_cast<u64>(9e17) : static_cast<u64>(c + 0.5));
        heat_begin_[i] = static_cast<u32>(heat_other_.size());
        if (!law.on || !law.spread)
            continue;
        for (u32 j = 0; j < map_.power_degree(i); ++j) {
            const u32 k = map_.power_link_at(i, j);
            const PowerLink& l = map_.power_link(k);
            if (l.water > 0 || !(l.length > 0))
                continue; // heat doesn't cross water: it goes into it (5)
            const double r = static_cast<double>(law.spread) * map_.mean_length() / l.length;
            heat_other_.push(map_.power_other(k, i));
            heat_link_.push(k);
            heat_rate_.push(r > 9e17 ? static_cast<u64>(9e17) : static_cast<u64>(r + 0.5));
        }
    }
    heat_begin_[n] = static_cast<u32>(heat_other_.size());
}

// Everything built on the map, built again from nothing on the map as it
// stands (a snapshot's respawns, just loaded): the network, each OS's
// neighbours, heat between places, the solve, the link table, and where
// the sources are.
bool World::on_map() {
    TextBuf reason;
    if (!net_.init(&map_, ns_, ws_.seed, &reason))
        return false;
    for (u32 i = 0; i < places_.size(); ++i) {
        if (net_.active())
            places_[i]->os.attach_net(&net_);
        places_[i]->env.neighbours = net_.active() ? map_.degree(i) : 0;
    }
    build_heat();
    if (ss_.on) {
        if (!grid_.init(gs_, &map_, &reason))
            return false;
        links_.init(ls_, ws_.wear.rate, &map_, &grid_, ws_.seed);
        sources_.reattach();
    } else {
        links_.init(ls_, ws_.wear.rate, &map_, nullptr, ws_.seed);
    }
    return true;
}

// A respawned place's values (7.3): each is the one the world dealt to the
// place whose field value was nearest this place's at its point now. A
// value that came with another is looked for among the places with the
// same other, as it was dealt: the E cores' pattern among the places with
// its core count, the latencies among those with its memory device count.
// With no fields, each is drawn again, from the place's own seed.
void World::respawn_hw(u32 i, PlaceHw* out) const {
    PlaceHw& h = *out;
    const u32 inc = map_.incarnation(i);
    if (!(ms_.fields && map_.placed())) {
        const u64 seed = place_seed(ws_.seed, i, inc);
        draw_hardware(ws_, i, &h, seed);
        h.visits = hw0_[i].visits;
        if (ws_.heat.on && ws_.heat.cooler_share > 0) {
            Rng r(seed, i, kPurposeCooler, Rng::kInitTick);
            h.cooler = r.chance(ws_.heat.cooler_share);
        }
        return;
    }
    h = PlaceHw{};
    const u32 n = static_cast<u32>(hw0_.size());
    Array<u8> only;
    only.resize(n);
    u32 who = map_.nearest_dealt(Field::cores, map_.field_now(Field::cores, i));
    h.cores = hw0_[who].cores;
    for (u32 j = 0; j < n; ++j)
        only[j] = hw0_[j].cores == h.cores ? 1 : 0;
    who = map_.nearest_dealt(Field::e_cores, map_.field_now(Field::e_cores, i), only.data());
    memcpy(h.e, hw0_[who].e, kMaxCores);
    who = map_.nearest_dealt(Field::devices, map_.field_now(Field::devices, i));
    h.devices = hw0_[who].devices;
    for (u32 j = 0; j < n; ++j)
        only[j] = hw0_[j].devices == h.devices ? 1 : 0;
    who = map_.nearest_dealt(Field::latency, map_.field_now(Field::latency, i), only.data());
    memcpy(h.latency, hw0_[who].latency, sizeof h.latency);
    who = map_.nearest_dealt(Field::page, map_.field_now(Field::page, i));
    h.page_size = hw0_[who].page_size;
    who = map_.nearest_dealt(Field::strict, map_.field_now(Field::strict, i));
    h.strict = hw0_[who].strict;
    who = map_.nearest_dealt(Field::coolers, map_.field_now(Field::coolers, i));
    h.cooler = hw0_[who].cooler;
    who = map_.nearest_dealt(Field::density, map_.field_now(Field::density, i));
    h.visits = hw0_[who].visits;
}

// Place i is torn down and built again (7.8; world.h's head).
void World::respawn(u32 i) {
    Place* old = places_[i];
    // What lives there ends. The facts go to the observers with the tick's.
    torn_programs += old->os.live_programs();
    old->teardown();
    // The map: its new point, and the links worked out again. The landmass it
    // stood on is the observers' (a new point may be on another).
    const u32 old_data = map_.link_count(), old_power = map_.power_link_count();
    const u32 left_mass = map_.placed() ? map_.landmass(i) : 0;
    const bool moved = map_.respawn(i);
#ifndef NDEBUG
    {
        // The map's invariants, in the checked build: the Gabriel rule over
        // both link sets, no crossings, every landmass bridged.
        TextBuf broken;
        NOVA2_ASSERT(map_.check_links(false, &broken));
    }
#endif
    // The new place, with what goes with the id: its counters too.
    PlaceHw hw;
    respawn_hw(i, &hw);
    Place* fresh = new Place;
    TextBuf reason;
    const bool built = fresh->init(i, ws_, hw, &reason, map_.incarnation(i));
    NOVA2_ASSERT(built);
    (void)built;
    fresh->region.track_paths(observer_ != nullptr);
    fresh->take_over(*old, tick_);
    {
        ByteWriter kept;
        Archive out(&kept);
        out.carry = true;
        old->counters(out);
        ByteReader in(kept.data(), kept.size());
        Archive back(&in);
        back.carry = true;
        fresh->counters(back);
        NOVA2_ASSERT(back.ok() && in.remaining() == 0);
    }
    delete old;
    places_[i] = fresh;
    // The engine follows the map.
    if (net_.active()) {
        net_.relink(map_.data_moved().data(), old_data, i, tick_);
        fresh->os.attach_net(&net_);
        for (u32 j = 0; j < places_.size(); ++j)
            places_[j]->env.neighbours = map_.degree(j);
    }
    build_heat();
    if (ss_.on)
        grid_.remap();
    links_.relink(map_.data_moved().data(), old_data, map_.power_moved().data(), old_power, tick_, map_.respawns());
    if (ss_.on)
        sources_.reattach();
    ++respawns;
    respawns_moved += moved ? 1 : 0;
    if (first_respawn_[i] == ~0ull)
        first_respawn_[i] = tick_;
    if (observer_)
        observer_->respawned(i, tick_, map_.incarnation(i), map_.placed() ? map_.x(i) : 0,
                             map_.placed() ? map_.y(i) : 0, moved, left_mass);
}

// A tick's inputs (appendix C 1), on one host thread. A place whose visit
// comes this tick with enough of its devices failed is torn down and built
// again, in place order (7.8). Then the links: a visit due at a place this
// tick mends its failed links, then the cuts due land; and the solve's
// parts follow whatever failed or came back since the last solve.
void World::inputs() {
    events_.seek(tick_); // the patches there this tick (12.3)
    links_.straits_due(tick_); // the straits parting or joining at this tick (11.6)
    if (respawns_on_)
        for (u32 i = 0; i < places_.size(); ++i) {
            const Place& p = *places_[i];
            if (p.next_visit == tick_ &&
                static_cast<double>(p.failed_devices()) >= ws_.visit.respawn_share * p.device_count())
                respawn(i);
        }
    if (!links_.on())
        return;
    if (links_.failed_count())
        for (u32 i = 0; i < places_.size(); ++i)
            if (places_[i]->next_visit == tick_)
                links_.visit(i);
    links_.cuts_due(tick_);
    links_.patch_cuts(tick_, events_); // an outage patch's (12.3)
    links_.sync();
}

void World::tick() {
    struck_.fill(0);
    if (respawns_on_ || links_.on() || events_.on()) {
        const u64 c0 = __rdtsc();
        inputs();
        input_cycles += __rdtsc() - c0;
    }
    if (ss_.on)
        power_step(0); // the sources' states and the voltage solve (appendix C 1-3)
    if (links_.wears()) {
        const u64 c0 = __rdtsc();
        links_.wear_step(); // links at their endurance fail (C 4); the rest take the tick's wear (C 8)
        link_wear_cycles += __rdtsc() - c0;
    }
    if (deferred_.size() != places_.size())
        deferred_.resize(places_.size());
    order_heaviest();
    pool_.run(place_count(), tick_one, this); // returns when every place is done, or stopped before a big fade
    fade_spread();                            // and those finished: the barrier
    if (net_.active()) {
        const u64 c0 = __rdtsc();
        network_step();
        net_cycles += __rdtsc() - c0;
    } else if (killing()) {
        killer_step(); // with no network, the barrier's step of it
    }
    if (ws_.heat.on)
        heat_places(); // each place's temperature (appendix C 7)
    // The facts, place by place, to the observer; then they're done with.
    for (usize i = 0; i < places_.size(); ++i) {
        Place& p = *places_[i];
        if (observer_)
            observer_->ingest(p);
        p.births += p.os.births.size();
        p.deaths += p.os.deaths.size();
        p.os.clear_facts();
    }
    // Gate 5's watching of the world as a whole (watch.h): cascades,
    // patches formed and straits joining.
    if (observer_)
        observer_->after_tick(*this);
    // Who leads each place whose window has just ended, once every place's
    // facts are in: a birth at one place can keep a genome that programs at
    // another carry.
    if (observer_ && observer_->leads_due())
        observer_->set_leads(*this);
    ++tick_;
    if (observer_ && observer_->due(tick_))
        observer_->sample(*this, true);
}

// The out-of-memory killer (9.1; build-plan-gate5.md step 10), at the
// barrier, place by place: a place crowded, its free memory under a tenth
// (13.3), at the set number of barriers in a row loses one program, drawn
// by the bytes programs hold there from the place's stream for the tick.
void World::killer_step() {
    for (u32 i = 0; i < places_.size(); ++i) {
        Place& p = *places_[i];
        if (!p.os.crowded_tick(p.mem.free_bytes() < p.mem.size() / 10))
            continue;
        Rng r(ws_.seed, i, kPurposeKiller, tick_);
        const u32 holder = p.os.pick_by_bytes(r.next());
        if (!holder)
            continue; // nothing a program holds: the clock has started again all the same
        p.os.killed_bytes += p.os.bytes_held(holder);
        p.os.killed_threads += p.os.program(holder).threads;
        ++p.os.kills;
        p.os.end_program(holder, DeathCause::oom);
    }
}

void World::watch(Observer* o) {
    observer_ = o;
    for (usize i = 0; i < places_.size(); ++i)
        places_[i]->region.track_paths(o != nullptr);
}

void World::hash_one(void* ctx, u32 i) {
    World* w = static_cast<World*>(ctx);
    w->hashes_[i] = w->places_[i]->hash();
}

u64 World::hash() {
    pool_.run(place_count(), hash_one, this);
    Hasher h(0x776F726C64ull); // "world"
    h.word(ws_.seed);
    h.word(tick_);
    h.word(places_.size());
    if (respawns_on_)
        h.word(map_.respawn_hash()); // who has respawned, and where to (build-plan-gate5.md step 6)
    for (usize i = 0; i < hashes_.size(); ++i)
        h.word(hashes_[i]);
    if (net_.active())
        h.word(net_.hash()); // what's on its way (build-plan-gate3.md step 5)
    if (ss_.on)
        h.word(grid_.hash()); // the voltages the next solve starts from (build-plan-gate4.md step 8)
    if (links_.on())
        h.word(links_.hash()); // the failed and worn links (build-plan-gate5.md step 5)
    return h.finish();
}

void World::save(ByteWriter& out, u32 version) {
    Archive ar(&out, version);
    u64 t = tick_;
    ar.u64_(t);
    if (respawns_on_)
        map_.serialize_respawns(ar); // ahead of the places: a respawned place's shape follows from it
    for (usize i = 0; i < places_.size(); ++i)
        places_[i]->serialize(ar);
    net_.serialize(ar);
    if (ss_.on)
        grid_.serialize(ar); // the sources themselves are a function of the seed and the tick
    if (links_.on())
        links_.serialize(ar);
}

bool World::load(ByteReader& in, u32 version) {
    Archive ar(&in, version);
    ar.u64_(tick_);
    if (ar.ok() && respawns_on_) {
        // The map as the save had it: from the map as set up, the saved
        // respawns. Then every place that has respawned, here or there, is
        // built again as the save's map makes it, and what is built on the
        // map is built again.
        const bool had = map_.respawned() != 0;
        if (had) {
            TextBuf reason;
            if (!map_.build(ws_.seed, ws_.places, ms_, &reason))
                return false;
        }
        map_.serialize_respawns(ar);
        if (!ar.ok())
            return false;
        if (had || map_.respawned()) {
            for (u32 i = 0; i < places_.size(); ++i) {
                const u32 inc = map_.incarnation(i);
                if (!inc && !places_[i]->incarnation)
                    continue;
                PlaceHw hw = hw0_[i];
                if (inc)
                    respawn_hw(i, &hw);
                Place* fresh = new Place;
                TextBuf reason;
                if (!fresh->init(i, ws_, hw, &reason, inc)) {
                    delete fresh;
                    return false;
                }
                fresh->region.track_paths(observer_ != nullptr);
                fresh->timing = places_[i]->timing;
                delete places_[i];
                places_[i] = fresh;
            }
            if (!on_map())
                return false;
        }
    }
    for (usize i = 0; i < places_.size() && ar.ok(); ++i)
        places_[i]->serialize(ar);
    if (ar.ok())
        net_.serialize(ar);
    if (ar.ok() && ss_.on) {
        grid_.serialize(ar);
        // Each place's voltage for a tick is handed over by that tick's
        // solve, so nothing more to restore for the run. The sources as
        // they stood at the last tick run are worked out again for whatever
        // reads them between ticks (the viewer, opened on a snapshot): they
        // are a function of the seed and the tick, and the next tick's
        // step walks on from here to the same sources it would have found.
        if (ar.ok() && tick_)
            sources_.seek(tick_ - 1);
    }
    if (ar.ok() && links_.on())
        links_.serialize(ar);
    // The straits as the last tick run left them: the seed's and the tick's.
    if (ar.ok() && links_.straits()) {
        links_.straits_due(tick_ ? tick_ - 1 : 0, false);
        links_.sync();
    }
    // The patches, the strikes that landed and the current as the last tick
    // run left them, for whatever reads them between ticks (the viewer,
    // opened on a snapshot mid-storm; build-plan-gate5.md step 13), as the
    // sources above: each a function of the seed and the tick, and the next
    // tick seeks its own from here.
    if (ar.ok() && tick_) {
        const u64 last = tick_ - 1;
        if (events_.on()) {
            events_.seek(last);
            struck_.fill(0);
            if (events_.any(PatchType::storm))
                for (u32 i = 0; i < places_.size() && i < struck_.size(); ++i)
                    if (events_.strike(i, last, events_.storm_at(map_.x(i), map_.y(i))))
                        struck_[i] = 1;
        }
        if (current_.on())
            current_.seek(last);
    }
    return ar.ok() && in.remaining() == 0;
}

void World::save_place(u32 i, ByteWriter& out) {
    Archive ar(&out);
    places_[i]->serialize(ar);
}

bool World::load_place(u32 i, ByteReader& in, u32 version) {
    if (i >= places_.size())
        return false;
    Archive ar(&in, version);
    places_[i]->serialize(ar);
    return ar.ok() && in.remaining() == 0;
}

} // namespace nova2
