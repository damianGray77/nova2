// Gate 4's determinism, snapshots and fuzz world (build-plan-gate4.md step
// 8), and gate 3's routes with every delay a tie (build-plan-gate3.md, step
// 5's "still open").
//
// The world is a small landscape with gate 3's traffic, under gate 4's
// power and heat at their hardest: sources that live a few hundred ticks,
// so they grow, fade and go while it runs, with drop-outs and swells as
// often as the settings allow; places that cool badly, so devices trip and
// cool down; cooling devices at half the places; and in every place a
// thread with a low-voltage handler that sleeps on the interrupt.
//
// - Determinism: the same hash every 400 ticks on 1, 2 and 24 host threads,
//   with an observer or without; and resumed from a snapshot taken mid-swell,
//   one mid-trip, one with a source fading, one with a thread's low-voltage
//   bit set and one with a cooling device at work, on another thread count,
//   watched or not, it ends at the hash of a world never stopped, with the
//   same counts (the solve's, heat's and the interrupt's, saved beside the
//   world as an origin run saves them).
// - A snapshot of version 3, made before the supply's model and heat were
//   settings, is still read: as a world with a supply at each place and no
//   heat (snapshot_config).
// - The fuzz world: the same world with a disk writer, a reader and a booter
//   in every place, every place's and the network's invariants checked
//   after every tick, and the power balance: what the sources give the
//   network is what the places draw and the links and drains lose.
// - Routes: with both map delays 0 every hop still takes a tick, so every
//   route goes by the fewest hops and none circles.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

const char* const kWorld = "world.places = 16\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "region.retry = 2\n"
                           "noise.p_floor = 1e-4\n"
                           "noise.e_floor = 1e-4\n"
                           "noise.cosmic = 1e-6\n"
                           "os.idle_send = true\n"
                           "supply.model = sources\n"
                           "heat.on = true\n"
                           "sources.start = 60\n"
                           "sources.life = 500\n"
                           "sources.size.min = 20\n"
                           "sources.size.max = 120\n"
                           "source.steady.dropouts = 1000\n"
                           "source.daylight.dropouts = 1000\n"
                           "source.gusty.dropouts = 1000\n"
                           "source.steady.swells = 1000\n"
                           "source.daylight.swells = 1000\n"
                           "source.gusty.swells = 1000\n"
                           "supply.dropout.min = 5\n"
                           "supply.dropout.max = 60\n"
                           "sources.swell.longest = 60\n"
                           "supply.day = 600\n"
                           "heat.place.cooling = 0.3\n"
                           "heat.place.capacity = 60\n"
                           "cooler.share = 0.5\n"
                           "power.low_voltage_margin = 60\n";

const u64 kTicks = 2000;
const u64 kEvery = 400;

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// test_lowv.cpp's guarded counter: sets a low-voltage handler (B = its
// start + 12, A = 3), then counts in A. The handler sleeps 60 ticks and
// returns.
const u8 kGuarded[18] = {encode(kMov, kB),   encode(kAdd, kImm), 12,     encode(kSto, kB),
                         encode(kMov, kImm), kOnLowVoltage,      encode(kSys, kImm), kSetHandler,
                         encode(kAdd, kImm), 1,                  encode(kJmp, kImm), static_cast<u8>(-2),
                         encode(kMov, kImm), 60,                 encode(kSys, kImm), kSleep,
                         encode(kSys, kImm), kHret};
// DWRITE in a loop, and DREAD in a loop (test_fuzz.cpp's).
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
const u8 kReader[9] = {encode(kMov, kD),   encode(kSto, kB), encode(kAdd, kImm), 32, encode(kSto, kC),
                       encode(kSys, kImm), kDread,           encode(kJmp, kImm), static_cast<u8>(-7)};

void hand_place(Place& p, const u8* code, u32 len) {
    const u32 at = p.mem.alloc(1, 64, 2, 0);
    if (!at)
        return;
    memcpy(p.mem.bytes() + at, code, len);
    p.os.place_by_hand(1, at, at, 0);
}

bool build(World& w, Config& cfg, const char* more, bool crowd) {
    Config::ParseError pe;
    Config over;
    if (!cfg.parse(Str(kWorld, strlen(kWorld)), &pe) || !over.parse(Str(more, strlen(more)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  gate4: %s %s\n", why.c_str(), cfg.value_error());
        return false;
    }
    // A test world (R7).
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        hand_place(p, kGuarded, sizeof kGuarded);
        hand_place(p, kGuarded, sizeof kGuarded);
        hand_place(p, kWriter, sizeof kWriter);
        if (crowd)
            hand_place(p, kReader, sizeof kReader);
        place_traffic(p, w.place_count(), i);
    }
    return true;
}

// The moments a snapshot is taken at, between ticks (the next tick to run
// is w.tick_count()).
enum Moment { kSwell = 0, kTrip, kFading, kLatch, kCooling, kMoments };
const char* const kMomentName[kMoments] = {"mid-swell", "mid-trip", "with a source fading",
                                           "with a thread's low-voltage bit set", "with a cooling device at work"};

// Whether the world is at the moment, and where: a source part way through
// a swell (it began before the next tick and goes on past it); a device
// tripped by heat and not yet cooled; a source in the last tenth of its
// life, giving less each tick; a thread that has taken the low-voltage
// interrupt for the dip it is in; a cooling device working.
bool at_moment(World& w, Moment m, u32* where) {
    const u64 t = w.tick_count();
    if (m == kSwell || m == kFading) {
        const Sources& src = w.sources();
        for (u32 k = 0; k < src.count(); ++k) {
            const Source& s = src.source(k);
            bool yes = false;
            if (m == kSwell) {
                Swell sw;
                yes = src.swell(s, t, &sw) > kCapacityOne && sw.start < t && sw.start + sw.length > t + 1;
            } else if (!s.fixed) {
                const i64 age = static_cast<i64>(t) - s.birth;
                const i64 left = static_cast<i64>(s.life) - age;
                yes = left > 2 && left < static_cast<i64>(s.life / 10) && src.state(k).capacity > 0;
            }
            if (yes) {
                *where = s.place;
                return true;
            }
        }
        return false;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        bool yes = false;
        if (m == kTrip) {
            for (u32 c = 0; c < p.core_count() && !yes; ++c)
                yes = p.core_tripped[c];
            for (u32 d = 0; d < p.mem.device_count() && !yes; ++d)
                yes = p.memory_tripped[d];
            yes = yes || p.disk_tripped || p.network_tripped || p.cooler_tripped;
        } else if (m == kLatch) {
            for (u32 k = 0; k < p.os.thread_top() && !yes; ++k)
                yes = p.os.thread(k).state != ThreadState::free && (p.os.thread(k).pending & kLowVoltageTaken) != 0;
        } else {
            yes = p.has_cooler && p.cooler_work > 0;
        }
        if (yes) {
            *where = i;
            return true;
        }
    }
    return false;
}

// The counts a report gives of gate 4's parts, summed over the places.
struct Sums {
    u64 solves = 0, sweeps = 0, capped = 0;
    u64 trips = 0, tripped = 0, heat_ticks = 0, heat_threads = 0, cooler_cutouts = 0;
    u64 handlers = 0, raised = 0, taken = 0, slept = 0, outlived = 0;
    u64 births = 0, cutouts = 0, cooling = 0;
    i64 temp_total = 0, hottest = 0;
};

Sums sums(World& w) {
    Sums s;
    s.solves = w.grid().solves;
    s.sweeps = w.grid().sweeps;
    s.capped = w.grid().capped;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k) {
            s.trips += p.trips[k];
            s.tripped += p.tripped_ticks[k];
        }
        s.heat_ticks += p.heat_ticks[0] + p.heat_ticks[1];
        s.heat_threads += p.heat_threads;
        s.cooler_cutouts += p.cooler_cutouts;
        s.handlers += p.os.lv_handlers;
        s.raised += p.sched.lv_raised;
        s.taken += p.sched.lv_taken;
        s.slept += p.lv_slept;
        s.outlived += p.lv_outlived;
        s.births += p.births;
        s.cutouts += p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts;
        s.cooling += p.energy.cooling;
        s.temp_total += p.temp_total;
        s.hottest = p.temp_highest > s.hottest ? p.temp_highest : s.hottest;
    }
    return s;
}

struct Run {
    u64 h[kTicks / kEvery] = {};
    u64 snap_at[kMoments] = {};
    u32 snap_place[kMoments] = {};
    ByteWriter snap[kMoments], counters[kMoments];
    Sums end;
    u64 begun = 0, ended = 0;
    u32 sources_end = 0;
};

// The world's threads with a low-voltage handler end at its cut-outs and
// trips like any others, so more are placed by hand every 200 ticks (a test
// world, R7): two a place, before the tick runs.
void guard(World& w) {
    if (w.tick_count() == 0 || w.tick_count() % 200 != 0)
        return;
    for (u32 i = 0; i < w.place_count(); ++i) {
        hand_place(w.place(i), kGuarded, sizeof kGuarded);
        hand_place(w.place(i), kGuarded, sizeof kGuarded);
    }
}

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

void run(Run* r, u32 threads, bool observe, bool snaps) {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "", false));
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    while (w.tick_count() < kTicks) {
        guard(w);
        if (snaps && w.tick_count() > 100)
            for (u32 m = 0; m < kMoments; ++m)
                if (!r->snap_at[m] && at_moment(w, static_cast<Moment>(m), &r->snap_place[m])) {
                    r->snap_at[m] = w.tick_count();
                    snapshot(w, cfg, r->snap[m]);
                    w.save_counters(r->counters[m]);
                }
        w.tick();
        if (w.tick_count() % kEvery == 0)
            r->h[w.tick_count() / kEvery - 1] = w.hash();
    }
    r->end = sums(w);
    r->begun = w.sources().begun();
    r->ended = w.sources().ended();
    r->sources_end = w.sources().count();
    if (observe) {
        // The observer's last sample has what the places counted.
        const Sample& last = obs.samples.back();
        CHECK(obs.total_births == r->end.births && last.tick == kTicks);
        u64 trips = 0;
        for (u32 k = 0; k < kHeatKinds; ++k)
            trips += last.trips[k];
        CHECK(trips == r->end.trips && last.lv_taken == r->end.taken && last.lv_slept == r->end.slept &&
              last.solve_sweeps == r->end.sweeps && last.sources == r->sources_end);
    }
}

bool same(const Run& a, const Run& b) {
    for (u64 i = 0; i < kTicks / kEvery; ++i)
        if (a.h[i] != b.h[i])
            return false;
    return true;
}

bool same_sums(const Sums& a, const Sums& b) {
    return a.solves == b.solves && a.sweeps == b.sweeps && a.capped == b.capped && a.trips == b.trips &&
           a.tripped == b.tripped && a.heat_ticks == b.heat_ticks && a.heat_threads == b.heat_threads &&
           a.cooler_cutouts == b.cooler_cutouts && a.handlers == b.handlers && a.raised == b.raised &&
           a.taken == b.taken && a.slept == b.slept && a.outlived == b.outlived && a.births == b.births &&
           a.cutouts == b.cutouts && a.cooling == b.cooling && a.temp_total == b.temp_total && a.hottest == b.hottest;
}

// Loads a snapshot and its counters and runs it on to kTicks: its final
// hash, and its counts.
u64 resume(const ByteWriter& snap, const ByteWriter& counters, u32 threads, bool observe, u64* at, Sums* end) {
    SnapshotHeader sh;
    TextBuf err;
    CHECK(snapshot_parse(snap.data(), snap.size(), &sh, &err));
    CHECK(sh.version == kSnapshotVersion);
    Config cfg;
    Config::ParseError pe;
    CHECK(snapshot_config(sh, &cfg, &pe));
    World w;
    TextBuf why;
    CHECK(w.init(cfg, &why));
    ByteReader in(snap.data() + sh.world_offset, sh.world_size);
    CHECK(w.load(in));
    ByteReader cin(counters.data(), counters.size());
    CHECK(w.load_counters(cin));
    *at = w.tick_count();
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    // (The snapshot was taken after its tick's threads were placed.)
    for (bool first = true; w.tick_count() < kTicks; first = false) {
        if (!first)
            guard(w);
        w.tick();
    }
    *end = sums(w);
    return w.hash();
}

void test_determinism() {
    Run one_, two_, many_, watched_, watched_many_;
    Run* one = &one_;
    run(one, 1, false, true);
    run(&two_, 2, false, false);
    run(&many_, 24, false, false);
    run(&watched_, 1, true, false);
    run(&watched_many_, 24, true, false);
    CHECK(same(*one, two_) && same(*one, many_));
    CHECK(same(*one, watched_) && same(*one, watched_many_));
    CHECK(same_sums(one->end, two_.end) && same_sums(one->end, many_.end) && same_sums(one->end, watched_many_.end));
    CHECK(one->h[0] != one->h[1]); // the world moves
    // It went through what gate 4 adds.
    const Sums& e = one->end;
    CHECK(e.trips > 0 && e.heat_threads > 0 && e.cooling > 0 && e.raised > 0 && e.taken > 0 && e.slept > 0);
    CHECK(one->begun > 12 && one->ended > 0 && e.births > 100 && e.cutouts > 0);
    const u64 end = one->h[kTicks / kEvery - 1];
    for (u32 m = 0; m < kMoments; ++m) {
        CHECK(one->snap_at[m] > 100);
        if (!one->snap_at[m]) {
            printf("  gate4: the world was never %s\n", kMomentName[m]);
            continue;
        }
        u64 at = 0;
        Sums s;
        CHECK_EQ_U64(resume(one->snap[m], one->counters[m], 2, false, &at, &s), end);
        CHECK(at == one->snap_at[m] && same_sums(s, e));
        CHECK_EQ_U64(resume(one->snap[m], one->counters[m], 24, true, &at, &s), end);
        CHECK(same_sums(s, e));
    }
    typedef unsigned long long ull;
    printf("  gate4: 16 places under sources that come and go (%llu begun, %llu ended, %u at the end), in %llu "
           "ticks: %llu heat trips (%llu threads ended by them), the hottest place at %.1f degrees, %llu "
           "cut-outs, %llu births; the low-voltage interrupt raised %llu times and taken %llu, %llu threads slept "
           "on it and %llu outlived a cut; %.1f sweeps a solve, %llu of %llu solves capped\n",
           static_cast<ull>(one->begun), static_cast<ull>(one->ended), one->sources_end, static_cast<ull>(kTicks),
           static_cast<ull>(e.trips), static_cast<ull>(e.heat_threads), static_cast<double>(e.hottest) / kDegree,
           static_cast<ull>(e.cutouts), static_cast<ull>(e.births), static_cast<ull>(e.raised),
           static_cast<ull>(e.taken), static_cast<ull>(e.slept), static_cast<ull>(e.outlived),
           e.solves ? static_cast<double>(e.sweeps) / e.solves : 0.0, static_cast<ull>(e.capped),
           static_cast<ull>(e.solves));
    printf("  gate4: the same hash every %llu ticks on 1, 2 and 24 host threads, watched or not; and resumed from "
           "snapshots taken %s at tick %llu (place %u), %s at %llu (place %u), %s at %llu (place %u), %s at %llu "
           "(place %u) and %s at %llu (place %u), on 2 and on 24 threads, watched and not, it ended at the same "
           "hash with the same counts\n",
           static_cast<ull>(kEvery), kMomentName[0], static_cast<ull>(one->snap_at[0]), one->snap_place[0],
           kMomentName[1], static_cast<ull>(one->snap_at[1]), one->snap_place[1], kMomentName[2],
           static_cast<ull>(one->snap_at[2]), one->snap_place[2], kMomentName[3],
           static_cast<ull>(one->snap_at[3]), one->snap_place[3], kMomentName[4],
           static_cast<ull>(one->snap_at[4]), one->snap_place[4]);
}

// A snapshot of version 3: a world of gate 3's kind, saved by a build
// before the supply's model and heat were settings, so its text names
// neither (nor the low-voltage margin, which is set to 0 for it: nothing
// raised that interrupt then). The test runner's own defaults for the two
// (test_main.cpp) are set aside here, as the app has none: the build's
// defaults are sources and heat on.
void test_version3() {
    const char* const text = "world.places = 6\nmemory.size = 32768\ndisk.size = 65536\nsupply.sag = 50\n"
                             "supply.day = 300\nsupply.night = 0.2\nsupply.dropouts = 1000\n"
                             "supply.dropout.min = 5\nsupply.dropout.max = 60\n";
    Config::ParseError pe;
    ByteWriter whole;
    u64 end = 0;
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(cfg.parse(Str(text, strlen(text)), &pe));
        if (!w.init(cfg, &why)) {
            printf("  gate4: %s %s\n", why.c_str(), cfg.ok() ? "" : cfg.value_error());
            CHECK(false);
            return;
        }
        CHECK(!w.powered() && !w.heated());
        for (u32 i = 0; i < w.place_count(); ++i)
            if (!w.place(i).mem.strict())
                memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, sizeof kLax);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        // The snapshot as a build of that version wrote it: every table whole.
        ByteWriter out;
        TextBuf saved;
        cfg.write(saved);
        snapshot_begin(w.seed(), w.tick_count(), saved.view(), out);
        w.save(out, 3);
        const u8 three[4] = {3, 0, 0, 0};
        whole.put_bytes(out.data(), 8);
        whole.put_bytes(three, 4);
        whole.put_bytes(out.data() + 12, out.size() - 12);
        snapshot_finish(whole);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        end = w.hash();
    }
    CHECK(Config::set_fallback(nullptr, &pe));
    SnapshotHeader h;
    TextBuf err;
    CHECK(snapshot_parse(whole.data(), whole.size(), &h, &err) && h.version == 3);
    {
        // Read as the snapshot's text alone, it is another world: this
        // build's defaults give it sources and heat, and it doesn't load.
        Config cfg;
        World w;
        TextBuf why;
        CHECK(cfg.parse(h.config_text, &pe));
        const bool built = w.init(cfg, &why);
        if (!built)
            printf("  gate4: %s %s\n", why.c_str(), cfg.ok() ? "" : cfg.value_error());
        CHECK(built && w.powered() && w.heated());
        ByteReader in(whole.data() + h.world_offset, h.world_size);
        CHECK(built && !w.load(in, h.version));
    }
    {
        Config cfg;
        World w;
        TextBuf why;
        CHECK(snapshot_config(h, &cfg, &pe));
        CHECK(w.init(cfg, &why) && !w.powered() && !w.heated());
        ByteReader in(whole.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version) && w.tick_count() == 300);
        for (u32 t = 0; t < 300; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), end);
        // Saved again, it is a version 4 that says what it is.
        ByteWriter again;
        snapshot(w, cfg, again);
        SnapshotHeader h2;
        CHECK(snapshot_parse(again.data(), again.size(), &h2, &err) && h2.version == kSnapshotVersion);
        Config c2;
        CHECK(c2.parse(h2.config_text, &pe) && c2.has("supply.model") && c2.has("heat.on") &&
              c2.has("power.low_voltage_margin"));
    }
    // And a version this build never wrote is refused.
    ByteWriter two;
    const u8 v2[4] = {2, 0, 0, 0};
    two.put_bytes(whole.data(), 8);
    two.put_bytes(v2, 4);
    two.put_bytes(whole.data() + 12, whole.size() - 20);
    snapshot_finish(two);
    CHECK(!snapshot_parse(two.data(), two.size(), &h, &err));
    CHECK(Config::set_fallback(ntest::kFallback, &pe));
    printf("  gate4: a version 3 snapshot (no supply model or heat in its settings) loaded as a world with a supply "
           "at each place and no heat, and ran on 300 ticks to the hash of the world it was taken from; read "
           "without that it didn't load; a version 2 was refused\n");
}

// The fuzz world (build-plan-gate4.md step 8).
void test_fuzz() {
    const u64 ticks = 2500;
    World w;
    Config cfg;
    CHECK(build(w, cfg, "noise.p_floor = 1e-3\nnoise.e_floor = 1e-3\nnoise.cosmic = 1e-5\ndisk.threshold = 900\n",
                true));
    w.start_threads(4);
    TextBuf why;
    u64 checks = 0, broken = 0, capped = 0, balanced = 0, unlit = 0;
    double worst = 0, worst_capped = 0, given_sum = 0;
    i64 coldest = 0x7FFFFFFFFFFFFFFFll;
    for (u64 t = 0; t < ticks && broken == 0; ++t) {
        const u64 capped_before = w.grid().capped;
        guard(w);
        w.tick();
        for (u32 i = 0; i < w.place_count(); ++i) {
            ++checks;
            why.clear();
            const Place& p = w.place(i);
            if (!p.check(&why)) {
                ++broken;
                printf("  gate4: place %u at tick %llu: %s\n", i, static_cast<unsigned long long>(w.tick_count()),
                       why.c_str());
                break;
            }
            coldest = p.temp < coldest ? p.temp : coldest;
            unlit += w.grid().lit(i) ? 0 : 1;
            // A place in a part with no source has no voltage.
            if (!w.grid().lit(i) && w.grid().uv(i) != 0)
                ++broken;
        }
        why.clear();
        if (broken == 0 && !w.net().check(&why)) {
            ++broken;
            printf("  gate4: the network at tick %llu: %s\n", static_cast<unsigned long long>(w.tick_count()),
                   why.c_str());
        }
        // The power balance: what the sources gave the network this tick
        // against what its loads, links and drains took. A solve that
        // stopped at its cap hasn't settled, and is held to less.
        const Grid& g = w.grid();
        const double given = static_cast<double>(g.given_uw);
        const double taken = static_cast<double>(g.loads_uw) + static_cast<double>(g.links_uw) +
                             static_cast<double>(g.drains_uw);
        const double off = fabs(given - taken) / (fabs(given) > 1e6 ? fabs(given) : 1e6);
        if (g.capped > capped_before) {
            ++capped;
            worst_capped = off > worst_capped ? off : worst_capped;
        } else {
            ++balanced;
            worst = off > worst ? off : worst;
        }
        given_sum += given / 1e6;
    }
    const Sums s = sums(w);
    CHECK(broken == 0 && checks == ticks * w.place_count());
    // A settled solve balances to a fiftieth of what the sources give (the
    // tolerance is a tenth of a millivolt a place); a capped one to a
    // fifth.
    CHECK(balanced > 0 && worst < 0.02 && worst_capped < 0.2);
    // Nothing falls below the outside temperature.
    CHECK(coldest >= static_cast<i64>(w.settings().heat.outside) * (kDegree / 10) - 1);
    CHECK(s.trips > 0 && s.cutouts > 0 && s.births > 100 && s.taken > 0);
    const NetCounts& n = w.net().counts();
    CHECK(n.sent > 0 && n.delivered > 0);
    typedef unsigned long long ull;
    printf("  gate4: the fuzz world, %llu ticks checked in %u places (%llu checks) with no invariant broken: %llu "
           "heat trips, %llu cut-outs, %llu births, %llu messages sent and %llu delivered, %llu place-ticks on a "
           "landmass with no source (0 V each); the sources gave %.1f W a tick on average, and the network's "
           "loads, links and drains took the same within %.4f%% in the %llu solves that settled and %.2f%% in the "
           "%llu that stopped at the cap\n",
           static_cast<ull>(ticks), w.place_count(), static_cast<ull>(checks), static_cast<ull>(s.trips),
           static_cast<ull>(s.cutouts), static_cast<ull>(s.births), static_cast<ull>(n.sent),
           static_cast<ull>(n.delivered), static_cast<ull>(unlit), given_sum / ticks, 100 * worst,
           static_cast<ull>(balanced), 100 * worst_capped, static_cast<ull>(capped));
}

// Gate 3's routes with both map delays 0 (build-plan-gate3.md step 5,
// "still open"): every link still takes a tick, so a route's every step is
// a hop nearer its end. Followed from every place to every other on three
// landscapes with a tenth of their places dark, each route arrives in as
// many hops as the fewest there are, and none comes back to a place.
void test_route_ties() {
    u32 routes = 0, longest = 0, unreachable = 0;
    for (u32 trial = 0; trial < 3; ++trial) {
        const u32 n = trial == 2 ? 200 : 60;
        char text[200];
        snprintf(text, sizeof text,
                 "world.places = %u\nworld.seed = %u\nmap.delay.hop = 0\nmap.delay.length = 0\nmemory.size = 16384\n"
                 "disk.size = 16384\n",
                 n, trial + 11);
        Config cfg;
        Config::ParseError pe;
        CHECK(cfg.parse(Str(text, strlen(text)), &pe));
        World w;
        TextBuf why;
        CHECK(w.init(cfg, &why));
        const Map& m = w.map();
        bool all_one = m.link_count() > 0;
        for (u32 k = 0; k < m.link_count(); ++k)
            all_one = all_one && m.link(k).delay == 1;
        CHECK(all_one);
        Array<u8> dark;
        dark.resize(n);
        Rng r(trial, 0, 97, 0);
        for (u32 i = 0; i < n; ++i)
            dark[i] = r.chance(0.1) ? 1 : 0;
        Routes& rt = w.net().routes();
        rt.set_dark(dark.data());
        // The fewest hops from every place to d, by a search of its own.
        Array<u32> hops, queue;
        for (u32 d = 0; d < n; ++d) {
            hops.resize(0);
            hops.resize(n);
            hops.fill(~0u);
            queue.clear();
            if (!dark[d]) {
                hops[d] = 0;
                queue.push(d);
            }
            for (usize q = 0; q < queue.size(); ++q) {
                const u32 v = queue[q];
                for (u32 j = 0; j < m.degree(v); ++j) {
                    const u32 u = m.other(m.link_at(v, j), v);
                    if (!dark[u] && hops[u] == ~0u) {
                        hops[u] = hops[v] + 1;
                        queue.push(u);
                    }
                }
            }
            for (u32 s = 0; s < n; ++s) {
                if (s == d)
                    continue;
                if (dark[s] || hops[s] == ~0u) {
                    CHECK(rt.next(s, d) == ~0u);
                    ++unreachable;
                    continue;
                }
                u32 at = s, steps = 0;
                while (at != d && steps <= n) {
                    const u32 j = rt.next(at, d);
                    if (j == ~0u)
                        break;
                    const u32 to = m.other(m.link_at(at, j), at);
                    if (hops[to] + 1 != hops[at])
                        break; // not a hop nearer
                    at = to;
                    ++steps;
                }
                CHECK(at == d && steps == hops[s]);
                ++routes;
                longest = steps > longest ? steps : longest;
            }
        }
    }
    printf("  gate4: with both map delays 0 every link takes one tick: %u routes on 3 landscapes, a tenth of their "
           "places dark, each followed to its end by the fewest hops (%u at most), none back to a place; %u pairs "
           "with no way\n",
           routes, longest, unreachable);
}

} // namespace

void suite_gate4() {
    test_determinism();
    test_version3();
    test_fuzz();
    test_route_ties();
}
