// Gate 5's determinism and snapshots (build-plan-gate5.md step 12).
//
// - The world is the fuzz suite's tenth (test_fuzz.cpp): 24 places on a
//   landscape, crowded, with gate 3's traffic, the sources on the map and
//   every type of patch over it, devices wearing out within the run and
//   mended at visits, places torn down and built again, links cut, a third
//   of the cores and memory devices flaky, every strait parting and
//   joining, the current with gusts, rowhammer, the killer and the
//   watchdog.
// - It gives the same hash every 400 ticks on 1, 2 and 24 host threads,
//   watched by observers or not, and with its big fades shared out over
//   the host threads (optimization-plan.md pass 10). Resumed from a
//   snapshot taken mid-ramp, mid-storm, with a link cut and messages
//   waiting, with a device failed and waiting for its visit, after a
//   respawn, with a strait parted, with a place's crowded clock running,
//   with the current's blocks on the wire and mid-gust, on 2 host threads
//   unwatched and on 24 watched and sharing its fades out, it ends at the
//   hash of a world never stopped, with the same counts (saved beside the
//   world as an origin run saves them).
// - Gate 4's saves still open: a world saved by gate 4's own last build
//   (format 4, tests/golden/gate4_world.snap: 3 places of static under
//   sources and heat, its settings leaving the seven numbers agreed for
//   gate 4 to its build), carried on 300 ticks here on 1 and on 24 host
//   threads, is the world gate 4's build made carrying it on
//   (tests/golden/gate4_world_300.snap). Its settings read as that build
//   had them: the seven at section 7's placeholders, gate 5 off.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/io.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"
#include "traffic_world.h"

using namespace nova2;

namespace {

typedef unsigned long long ull;

// The fuzz world (test_fuzz.cpp), then its tenth world's settings.
const char* const kWorld = "world.places = 24\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "region.retry = 1\n"
                           "os.slots = 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 3 0 3 0 3 0 3\n"
                           "noise.p_floor = 1e-3\n"
                           "noise.e_floor = 1e-3\n"
                           "noise.cosmic = 1e-5\n"
                           "supply.day = 400\n"
                           "supply.model = sources\n"
                           "heat.on = true\n"
                           "disk.threshold = 900\n"
                           "os.idle_send = true\n"
                           "wear.rate = 1\n"
                           "wear.p.endurance = 500\n"
                           "wear.e.endurance = 500\n"
                           "wear.memory.endurance = 500\n"
                           "wear.disk.endurance = 400\n"
                           "wear.network.endurance = 400\n"
                           "wear.cooler.endurance = 400\n"
                           "wear.weak.share = 0.01\n"
                           "wear.weak.chance = 0.01\n"
                           "disk.block = 8192\n"
                           "disk.block_endurance = 2\n"
                           "disk.worn_flip = 0.05\n"
                           "os.levelling = true\n"
                           "os.levelling_gap = 1\n"
                           "visit.mean = 250\n"
                           "visit.regularity = 0.3\n"
                           "visit.replace_at = 0.8\n"
                           "visit.respawn_share = 0.3\n"
                           "link.cuts = 0.003\n"
                           "net.lifetime = 40\n"
                           "ramp.lead.min = 10\n"
                           "ramp.lead.max = 100\n"
                           "patch.life.min = 100\n"
                           "patch.life.max = 500\n"
                           "patch.storm.rate = 30000\n"
                           "patch.storm.strength.min = 0.005\n"
                           "patch.storm.strength.max = 0.02\n"
                           "patch.swell.rate = 30000\n"
                           "patch.brownout.rate = 30000\n"
                           "patch.heatwave.rate = 30000\n"
                           "patch.outage.rate = 30000\n"
                           "hammer.strength = 0.01\n"
                           "hammer.window = 500\n"
                           "hammer.threshold.fast = 200\n"
                           "hammer.threshold.slow = 600\n"
                           "flaky.share = 0.3\n"
                           "flaky.worn = 10\n"
                           "strait.share = 1\n"
                           "strait.spell.min = 50\n"
                           "strait.spell.max = 400\n"
                           "current.strength = 60\n"
                           "current.season = 700\n"
                           "patch.gust.rate = 30000\n"
                           "os.killer = 10\n"
                           "os.watchdog = 5000\n";

const u64 kTicks = 2000;
const u64 kEvery = 400;

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};
// The fuzz suite's booter, disk writer and reader, handled spinner and
// sleeper (test_fuzz.cpp says what each does).
const u8 kBooter[34] = {encode(kMov, kC),   encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSto, kC),   encode(kMov, kImm), 0,                  encode(kSys, kImm),
                        kDwrite,            encode(kJnz, kImm), 19,                 encode(kMov, kImm),
                        2,                  encode(kSto, kB),   encode(kMov, kImm), 34,
                        encode(kSys, kImm), kAlloc,             encode(kMov, kIp),  encode(kSub, kImm),
                        18,                 encode(kSto, kB),   encode(kCpy, kImm), 34,
                        encode(kMov, kD),   encode(kSys, kImm), kSpawn,             encode(kPush, kA),
                        encode(kMov, kImm), 0,                  encode(kSys, kImm), kSleep,
                        encode(kJmp, kImm), static_cast<u8>(-4)};
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
const u8 kReader[12] = {encode(kMov, kB),   encode(kAdd, kImm), 32, encode(kSto, kB), encode(kMov, kImm), 16,
                        encode(kSto, kC),   encode(kMov, kD),   encode(kSys, kImm), kDread, encode(kJmp, kImm),
                        static_cast<u8>(-3)};
const u8 kHandled[17] = {encode(kMov, kIp),  encode(kAdd, kImm), 14, encode(kSto, kB),  encode(kMov, kIp),
                         encode(kAdd, kImm), 26,                 encode(kSto, kD),  encode(kMov, kImm), 0,
                         encode(kSys, kImm), kSetHandler,        encode(kJmp, kImm), 0, encode(kSto, kAtD),
                         encode(kSys, kImm), kHret};
const u8 kSleeper[6] = {encode(kMov, kImm), 0, encode(kSys, kImm), kSleep, encode(kJmp, kImm),
                        static_cast<u8>(-4)};

bool hand_place(Place& p, const u8* code, u32 len, u32 size = 64, u32 perm = 2) {
    const u32 at = p.mem.alloc(1, size, perm, 0);
    if (!at)
        return false;
    memcpy(p.mem.bytes() + at, code, len);
    return p.os.place_by_hand(1, at, at, 0) != 0;
}

// The world, crowded as the fuzz suite's tenth is (a test world, R7).
bool build(World& w, Config& cfg) {
    Config::ParseError pe;
    if (!cfg.parse(Str(kWorld, strlen(kWorld)), &pe))
        return false;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        printf("  determinism5: %s %s\n", why.c_str(), cfg.value_error());
        return false;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        if (!p.mem.strict())
            memcpy(p.mem.bytes() + p.mem.region_base(), kLax, sizeof kLax);
        hand_place(p, kBooter, sizeof kBooter);
        hand_place(p, kWriter, sizeof kWriter);
        hand_place(p, kReader, sizeof kReader);
        place_traffic(p, w.place_count(), i);
        if (!p.mem.strict())
            hand_place(p, kHandled, sizeof kHandled);
        while (p.mem.free_bytes() + 1000 > p.mem.size() / 10 &&
               hand_place(p, kSleeper, sizeof kSleeper, 512, p.mem.strict() ? 1 : 2)) {
        }
    }
    return true;
}

// The moments a snapshot is taken at, between ticks (the next tick to run
// is w.tick_count()).
enum Moment { kRamp = 0, kStorm, kCutWait, kFailed, kRespawned, kParted, kCrowded, kWire, kGust, kMoments };
const char* const kMomentName[kMoments] = {"mid-ramp",
                                           "mid-storm",
                                           "with a link cut and messages waiting",
                                           "with a device failed and waiting for its visit",
                                           "after a respawn",
                                           "with a strait parted",
                                           "with a place's crowded clock running",
                                           "with the current's blocks on the wire",
                                           "mid-gust"};

// Whether the world is at the moment: a patch whose ramp is rising, and
// rises through the next tick; a storm at its peak that goes on past the
// next tick; a link failed while messages wait for a way on; a device
// failed at a place and not yet replaced; a place respawned in the tick just
// run (`respawned`: the world's respawns before it); a strait parted; a place crowded at the barriers in a row
// before this one; a block of the current part way across a link; a gust
// there that goes on past the next tick.
bool at_moment(World& w, Moment m, u64 respawned) {
    const u64 t = w.tick_count();
    const Events& ev = w.events();
    const u64 et = ev.tick();
    if (m == kRamp || m == kStorm || m == kGust) {
        for (u32 k = 0; k < ev.count(); ++k) {
            const Patch& p = ev.patch(k);
            const u64 lead = p.ramp.lead;
            if (m == kRamp && lead > 0 && p.start < et && et + 1 < p.start + lead)
                return true;
            if (m == kStorm && p.type == static_cast<u8>(PatchType::storm) && !p.ramp.false_alarm &&
                p.start + lead <= et && et + 1 < p.start + lead + p.life)
                return true;
            if (m == kGust && p.type == static_cast<u8>(PatchType::gust) && ev.now(k).level > 0 && p.end() > et + 1)
                return true;
        }
        return false;
    }
    if (m == kCutWait)
        return w.links().failed_count() > 0 && w.net().waiting() > 0;
    if (m == kRespawned)
        return w.respawns > respawned;
    if (m == kParted)
        return w.links().parted_straits() > 0;
    if (m == kWire) {
        for (u32 i = 0; i < w.net().message_slots(); ++i) {
            const Message& g = w.net().message_slot(i);
            if (g.live && g.kind == MsgKind::current && g.arrive >= t)
                return true;
        }
        return false;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        if ((m == kFailed && p.failed_devices() > 0) || (m == kCrowded && p.os.crowded_for() > 0))
            return true;
    }
    return false;
}

// The counts a report gives of gate 5's parts.
struct Sums {
    u64 births = 0, failures = 0, replaced = 0, renewed = 0, visits = 0;
    u64 respawns = 0, moved = 0, torn = 0, strikes = 0;
    u64 cut = 0, mended = 0, patch_cut = 0, partings = 0, joinings = 0;
    u64 drawn = 0, sent = 0, landed = 0;
    u64 kills = 0, dog = 0, hammered = 0, hammer = 0, weak = 0, waited = 0, rerouted = 0;
};

Sums sums(World& w) {
    Sums s;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        s.births += p.births;
        for (u32 k = 0; k < kHeatKinds; ++k) {
            s.failures += p.failures[k];
            s.replaced += p.replaced[k];
            s.renewed += p.renewed[k];
        }
        s.visits += p.visits;
        s.kills += p.os.kills;
        s.dog += p.os.watchdog_faults;
        s.hammered += p.noise.hammered;
        s.hammer += p.noise.hammer;
        s.weak += p.noise.weak;
    }
    s.respawns = w.respawns;
    s.moved = w.respawns_moved;
    s.torn = w.torn_programs;
    s.strikes = w.strikes;
    s.cut = w.links().cut;
    s.mended = w.links().mended;
    s.patch_cut = w.links().patch_cut;
    s.partings = w.links().partings;
    s.joinings = w.links().joinings;
    s.drawn = w.drift.drawn;
    s.sent = w.drift.sent;
    s.landed = w.drift.landed;
    s.waited = w.net().counts().waited;
    s.rerouted = w.net().counts().rerouted;
    return s;
}

bool same_sums(const Sums& a, const Sums& b) { return memcmp(&a, &b, sizeof a) == 0; }

struct Run {
    u64 h[kTicks / kEvery] = {};
    u64 snap_at[kMoments] = {};
    ByteWriter snap[kMoments], counters[kMoments];
    Sums end;
    u64 faded = 0, spread = 0, chunks = 0; // cells faded; fades shared out, and their chunks
};

// A world's fades of 256 cells or more shared out over its host threads in
// chunks of 64 (optimization-plan.md pass 10), and every run of the pool's
// that no test order is set for, the chunks' among them, in reverse.
void spread_fades(World& w) {
    w.set_fade_spread(64, 256);
    w.pool().set_dispatch_reversed(true);
}

// Every fade in its tick, for the runs the shared-out ones are held to:
// a world's own defaults share out fades of 8,192 cells (pass 11).
void fades_in_ticks(World& w) { w.set_fade_spread(64, ~0ull); }

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

// Each moment is looked for from its own tick on, so the snapshots are of
// the world at nine ticks, not the first at which all of them hold.
u64 from_tick(u32 m) { return 100 + 150 * static_cast<u64>(m); }

// Runs the world on `threads` host threads, watched or not, hashing it
// every kEvery ticks; with `snaps`, taking a snapshot, and the counters
// beside it, at the first of each moment; with `spread`, its big fades
// shared out (spread_fades).
void run(Run* r, u32 threads, bool observe, bool snaps, bool spread = false) {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    if (spread)
        spread_fades(w);
    else
        fades_in_ticks(w);
    u64 respawned = 0; // the world's respawns before the last tick run
    while (w.tick_count() < kTicks) {
        for (u32 m = 0; snaps && m < kMoments; ++m)
            if (w.tick_count() > from_tick(m) && !r->snap_at[m] && at_moment(w, static_cast<Moment>(m), respawned)) {
                r->snap_at[m] = w.tick_count();
                snapshot(w, cfg, r->snap[m]);
                w.save_counters(r->counters[m]);
            }
        respawned = w.respawns;
        w.tick();
        if (w.tick_count() % kEvery == 0)
            r->h[w.tick_count() / kEvery - 1] = w.hash();
    }
    r->end = sums(w);
    for (u32 i = 0; i < w.place_count(); ++i)
        r->faded += w.place(i).noise.cells_faded;
    r->spread = w.spread_fades;
    r->chunks = w.spread_chunks;
    if (observe)
        CHECK(obs.total_births == r->end.births);
}

bool same(const Run& a, const Run& b) { return memcmp(a.h, b.h, sizeof a.h) == 0; }

// Opens a snapshot: its settings as snapshot_config reads them, the world
// built from them and loaded. False if any of that fails.
bool open(const u8* data, usize n, Config* cfg, World* w, SnapshotHeader* sh) {
    TextBuf err;
    Config::ParseError pe;
    TextBuf why;
    if (!snapshot_parse(data, n, sh, &err) || !snapshot_config(*sh, cfg, &pe) || !w->init(*cfg, &why))
        return false;
    ByteReader in(data + sh->world_offset, sh->world_size);
    return w->load(in, sh->version);
}

// Loads a snapshot and its counters and runs it on to kTicks: its final
// hash, and its counts. With `spread`, its big fades shared out.
u64 resume(const ByteWriter& snap, const ByteWriter& counters, u32 threads, bool observe, u64* at, Sums* end,
           bool spread = false) {
    Config cfg;
    World w;
    SnapshotHeader sh;
    CHECK(open(snap.data(), snap.size(), &cfg, &w, &sh));
    CHECK(sh.version == kSnapshotVersion);
    ByteReader cin(counters.data(), counters.size());
    CHECK(w.load_counters(cin));
    *at = w.tick_count();
    Observer obs;
    if (observe) {
        obs.init(ObserverSettings{});
        w.watch(&obs);
    }
    w.start_threads(threads);
    if (spread)
        spread_fades(w);
    else
        fades_in_ticks(w);
    while (w.tick_count() < kTicks)
        w.tick();
    *end = sums(w);
    return w.hash();
}

void test_world() {
    static Run one_, two_, many_, watched_, watched_many_, spread_;
    Run* one = &one_;
    run(one, 1, false, true);
    run(&two_, 2, false, false);
    run(&many_, 24, false, false);
    run(&watched_, 1, true, false);
    run(&watched_many_, 24, true, false);
    run(&spread_, 24, true, false, true);
    CHECK(same(*one, two_) && same(*one, many_));
    CHECK(same(*one, watched_) && same(*one, watched_many_) && same(*one, spread_));
    CHECK(same_sums(one->end, two_.end) && same_sums(one->end, many_.end) && same_sums(one->end, watched_.end) &&
          same_sums(one->end, watched_many_.end) && same_sums(one->end, spread_.end));
    CHECK(one->spread == 0 && spread_.faded == one->faded && (one->faded == 0 || spread_.spread > 0));
    CHECK(one->h[0] != one->h[1]); // the world moves
    // It went through what gate 5 adds.
    const Sums& e = one->end;
    CHECK(e.failures > 20 && e.replaced > 10 && e.visits > 60 && e.respawns > 3 && e.strikes > 5);
    CHECK(e.cut > 10 && e.mended > 5 && e.partings > 3 && e.joinings > 3 && e.landed > 100);
    CHECK(e.kills > 0 && e.dog > 0 && e.hammer > 0 && e.waited > 0 && e.births > 100);
    const u64 end = one->h[kTicks / kEvery - 1];
    for (u32 m = 0; m < kMoments; ++m) {
        CHECK(one->snap_at[m] > from_tick(m));
        if (!one->snap_at[m]) {
            printf("  determinism5: the world was never %s\n", kMomentName[m]);
            continue;
        }
        u64 at = 0;
        Sums s;
        CHECK_EQ_U64(resume(one->snap[m], one->counters[m], 2, false, &at, &s), end);
        CHECK(at == one->snap_at[m] && same_sums(s, e));
        CHECK_EQ_U64(resume(one->snap[m], one->counters[m], 24, true, &at, &s, true), end);
        CHECK(same_sums(s, e));
    }
    printf("  determinism5: the fuzz suite's tenth world, %llu ticks: %llu failures, %llu replaced at %llu visits, "
           "%llu respawns (%llu moved), %llu strikes, %llu links cut and %llu mended, straits parted %llu times "
           "and joined %llu, %llu of the current's blocks landed, %llu programs ended out of memory, %llu watchdog "
           "faults, %llu bits flipped by rowhammer, %llu births\n",
           static_cast<ull>(kTicks), static_cast<ull>(e.failures), static_cast<ull>(e.replaced),
           static_cast<ull>(e.visits), static_cast<ull>(e.respawns), static_cast<ull>(e.moved),
           static_cast<ull>(e.strikes), static_cast<ull>(e.cut), static_cast<ull>(e.mended),
           static_cast<ull>(e.partings), static_cast<ull>(e.joinings), static_cast<ull>(e.landed),
           static_cast<ull>(e.kills), static_cast<ull>(e.dog), static_cast<ull>(e.hammer),
           static_cast<ull>(e.births));
    printf("  determinism5: the same hash every %llu ticks on 1, 2 and 24 host threads, watched or not, and with %llu "
           "of its fades (%llu cells faded in all) shared out in %llu chunks; resumed on 2 threads unwatched and on "
           "24 watched and sharing them out from snapshots taken\n",
           static_cast<ull>(kEvery), static_cast<ull>(spread_.spread), static_cast<ull>(one->faded),
           static_cast<ull>(spread_.chunks));
    for (u32 m = 0; m < kMoments; ++m)
        printf("    %s at tick %llu\n", kMomentName[m], static_cast<ull>(one->snap_at[m]));
    printf("  it ended at the same hash with the same counts\n");
}

// Gate 4's saves (the comment at the top).
void test_gate4_saves() {
    Array<u8> a, b;
    char path[1024];
    snprintf(path, sizeof path, "%s/tests/golden/gate4_world.snap", NOVA2_SOURCE_DIR);
    CHECK(read_file(path, &a));
    snprintf(path, sizeof path, "%s/tests/golden/gate4_world_300.snap", NOVA2_SOURCE_DIR);
    CHECK(read_file(path, &b));
    if (a.empty() || b.empty())
        return;
    // The world as gate 4's build has it, not as the suites' fallback would
    // (test_main.cpp): sources and heat where the save doesn't say.
    Config::ParseError fpe;
    CHECK(Config::set_fallback(nullptr, &fpe));
    // The world gate 4's build made, opened here.
    u64 target = 0, target_tick = 0;
    {
        Config cfg;
        World w;
        SnapshotHeader sh;
        CHECK(open(b.data(), b.size(), &cfg, &w, &sh) && sh.version == 4);
        target = w.hash();
        target_tick = w.tick_count();
    }
    u64 start = 0, hashes[2] = {};
    for (u32 k = 0; k < 2; ++k) {
        Config cfg;
        World w;
        SnapshotHeader sh;
        const bool opened = open(a.data(), a.size(), &cfg, &w, &sh);
        CHECK(opened && sh.version == 4);
        if (!opened)
            continue;
        if (k == 0) {
            // Read as its build had it: the placeholders, gate 5 off.
            WorldSettings ws;
            read_world_settings(cfg, &ws);
            CHECK(key_f64(cfg, "heat.p.sink") == 0.55 && key_f64(cfg, "heat.e.capacity") == 1.4 &&
                  key_f64(cfg, "sources.size.max") == 400 && key_f64(cfg, "grid.link_resistance") == 5);
            CHECK(cfg.ok() && ws.wear.rate == 0 && w.powered() && w.heated());
        }
        start = w.tick_count();
        w.start_threads(k == 0 ? 1 : 24);
        while (w.tick_count() < target_tick)
            w.tick();
        hashes[k] = w.hash();
    }
    CHECK(target_tick == start + 300);
    CHECK_EQ_U64(hashes[0], target);
    CHECK_EQ_U64(hashes[1], target);
    // A save that names the seven keeps them.
    SnapshotHeader h;
    h.version = 4;
    const char* const text = "world.places = 3\nheat.p.sink = 0.98\n";
    h.config_text = Str(text, strlen(text));
    Config named;
    Config::ParseError pe;
    CHECK(snapshot_config(h, &named, &pe) && key_f64(named, "heat.p.sink") == 0.98 &&
          key_f64(named, "heat.p.capacity") == 11);
    // A format 5 save is read at this build's defaults: its builds had the
    // placeholders until the agreed numbers went in later the same day, and
    // nothing in the file says which. Only gate 5's offs are filled in.
    h.version = 5;
    Config five;
    CHECK(snapshot_config(h, &five, &pe) && !five.has("heat.p.capacity") && five.has("wear.rate"));
    CHECK(Config::set_fallback(ntest::kFallback, &fpe));
    printf("  determinism5: a world saved by gate 4's own last build (format 4, tick %llu, its settings leaving the "
           "seven agreed\n  numbers to that build), carried on 300 ticks here on 1 and on 24 host threads, is the "
           "world that build made\n  carrying it on; read with section 7's placeholders for the seven, as that "
           "build had them, and gate 5 off\n",
           static_cast<ull>(start));
}

} // namespace

void suite_determinism5() {
    test_world();
    test_gate4_saves();
}
