// The world (nova2/world.h, nova2/place.h; build-plan-gate1.md step 9):
// places built from settings and static; the same seed giving the same
// world on 1, 2 and 24 host threads and in any dispatch order; a snapshot
// and resume landing on the same hash as an unbroken run; another seed
// giving another world; and the settings' constraints refusing a bad world.
#include <cstring>

#include "check.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

// A small, busy test world: six places on a landscape, small memories,
// frequent retries.
const char* const kSmall = "world.places = 6\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "region.retry = 3\n"
                           "noise.cosmic = 1e-6\n"
                           "noise.p_floor = 1e-4\n"
                           "noise.e_floor = 1e-4\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build(World& w, Config& cfg, const char* extra = "") {
    Config::ParseError pe;
    TextBuf text;
    text.append(kSmall);
    text.append(extra);
    if (!cfg.parse(text.view(), &pe))
        return false;
    TextBuf why;
    const bool ok = w.init(cfg, &why);
    if (!ok)
        fprintf(stderr, "  world: %s %s\n", why.c_str(), cfg.value_error());
    // A test world (R7): a replicator in each lax place's region, at core 0's first spot.
    for (u32 i = 0; ok && i < w.place_count(); ++i)
        if (!w.place(i).mem.strict())
            memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
    return ok;
}

struct Trace {
    u64 h[3]; // the hash at ticks 10, 20 and 40
};

Trace run(u32 threads, const u32* order = nullptr) {
    World w;
    Config cfg;
    Trace t{};
    CHECK(build(w, cfg));
    w.start_threads(threads);
    if (order) { // and every other run in reverse, with any fade of 64 cells or more shared out
        w.pool().set_dispatch_order(order, w.place_count());
        w.pool().set_dispatch_reversed(true);
        w.set_fade_spread(64, 64);
    }
    u32 k = 0;
    for (u32 tick = 1; tick <= 40; ++tick) {
        w.tick();
        if (tick == 10 || tick == 20 || tick == 40)
            t.h[k++] = w.hash();
    }
    return t;
}

void test_places() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    CHECK(w.place_count() == 6);
    u32 strict = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        CHECK(p.core_count() >= 2 && p.core_count() <= 4);
        CHECK(p.mem.device_count() >= 2 && p.mem.device_count() <= 4 && p.mem.size() == 32768);
        CHECK(p.os.disk_size() == 65536);
        // The reset region is on the slowest memory, which has the lowest threshold.
        const MemDevice& rd = p.mem.device(p.mem.region_device());
        for (u32 d = 0; d < p.mem.device_count(); ++d)
            CHECK(p.mem.device(d).threshold >= rd.threshold);
        strict += p.mem.strict() ? 1 : 0;
        TextBuf why;
        CHECK(p.mem.check(&why));
    }
    CHECK(strict > 0 && strict < 6); // with this seed, both kinds
    // Memory starts as static: not zeros.
    u32 zero = 0;
    for (u32 i = 0; i < 4096; ++i)
        zero += w.place(1).mem.bytes()[8192 + i] == 0 ? 1 : 0;
    CHECK(zero < 64);
}

void test_thread_counts() {
    const Trace one = run(1);
    const Trace two = run(2);
    const Trace many = run(24);
    const u32 reversed[6] = {5, 4, 3, 2, 1, 0};
    const Trace rev = run(24, reversed);
    bool same = true;
    for (int i = 0; i < 3; ++i)
        same = same && one.h[i] == two.h[i] && one.h[i] == many.h[i] && one.h[i] == rev.h[i];
    CHECK(same);
    CHECK(one.h[0] != one.h[1] && one.h[1] != one.h[2]); // the world moves
}

void test_life_happens() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    w.start_threads(4);
    for (u32 t = 0; t < 40; ++t)
        w.tick();
    u64 births = 0, programs = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        births += w.place(i).births;
        programs += w.place(i).os.live_programs();
        TextBuf why;
        CHECK(w.place(i).mem.check(&why));
    }
    CHECK(births > 20 && programs > 0);
    printf("  world: %llu births, %llu programs alive after 40 ticks\n", static_cast<unsigned long long>(births),
           static_cast<unsigned long long>(programs));
}

void test_snapshot_resume() {
    const Trace whole = run(4);
    // Run 20 ticks, snapshot, and resume in a fresh world.
    World a;
    Config cfg;
    CHECK(build(a, cfg));
    a.start_threads(4);
    for (u32 t = 0; t < 20; ++t)
        a.tick();
    TextBuf text;
    cfg.write(text);
    ByteWriter out;
    snapshot_begin(a.seed(), a.tick_count(), text.view(), out);
    a.save(out);
    snapshot_finish(out);

    // Written to a file as it is made (a big world's save), the snapshot is
    // byte for byte the one built whole, whatever its length's odd bytes.
    for (u32 odd = 0; odd < 8; ++odd) {
        ByteWriter built;
        snapshot_begin(a.seed(), a.tick_count(), text.view(), built);
        a.save(built);
        for (u32 k = 0; k < odd; ++k)
            built.put_u8(static_cast<u8>(k + 1));
        snapshot_finish(built);
        SnapshotFile file;
        CHECK(file.open("test_world_stream.snap", 1000 + 37 * odd));
        snapshot_begin(a.seed(), a.tick_count(), text.view(), file.writer());
        a.save(file.writer());
        for (u32 k = 0; k < odd; ++k)
            file.writer().put_u8(static_cast<u8>(k + 1));
        CHECK(file.close() && file.bytes() == built.size());
        Array<u8> read;
        CHECK(read_file("test_world_stream.snap", &read) && read.size() == built.size() &&
              memcmp(read.data(), built.data(), built.size()) == 0);
    }

    SnapshotHeader h;
    TextBuf err;
    CHECK(snapshot_parse(out.data(), out.size(), &h, &err) && h.tick == 20 && h.seed == a.seed());
    Config cfg2;
    Config::ParseError pe;
    CHECK(cfg2.parse(h.config_text, &pe));
    World b;
    TextBuf why;
    CHECK(b.init(cfg2, &why));
    ByteReader in(out.data() + h.world_offset, h.world_size);
    CHECK(b.load(in) && b.tick_count() == 20);
    CHECK(b.hash() == a.hash() && b.hash() == whole.h[1]);
    b.start_threads(2);
    for (u32 t = 0; t < 20; ++t)
        b.tick();
    CHECK(b.hash() == whole.h[2]); // the same world as the unbroken run
    // A damaged snapshot is refused.
    Array<u8> bad;
    bad.append(out.data(), out.size());
    bad[bad.size() / 2] ^= 1;
    CHECK(!snapshot_parse(bad.data(), bad.size(), &h, &err));
}

// A place snapshot (snapshot.h; nova2 trace --snapshot): one place's state,
// which loads into a world built from the same settings and makes that place
// the same as the one saved.
void test_place_snapshot() {
    World a;
    Config cfg;
    CHECK(build(a, cfg));
    a.start_threads(2);
    for (u32 t = 0; t < 20; ++t)
        a.tick();
    TextBuf text;
    cfg.write(text);
    ByteWriter out;
    snapshot_begin(a.seed(), a.tick_count(), text.view(), out, kSnapshotPlace);
    out.put_u32(2);
    a.save_place(2, out);
    snapshot_finish(out);

    SnapshotHeader h;
    TextBuf err;
    CHECK(snapshot_parse(out.data(), out.size(), &h, &err));
    CHECK(h.flags == kSnapshotPlace && h.place == 2 && h.tick == 20 && h.seed == a.seed());
    Config cfg2;
    Config::ParseError pe;
    CHECK(cfg2.parse(h.config_text, &pe));
    World b;
    TextBuf why;
    CHECK(b.init(cfg2, &why));
    ByteReader in(out.data() + h.world_offset, h.world_size);
    CHECK(b.load_place(2, in));
    CHECK(b.place(2).hash() == a.place(2).hash() && b.place(2).os.live_programs() == a.place(2).os.live_programs());
    CHECK(b.tick_count() == 0); // the world's own tick isn't the place's
    // A truncated section is refused.
    ByteReader cut(out.data() + h.world_offset, h.world_size / 2);
    CHECK(!b.load_place(2, cut));
}

// A save holds only what is in use (snapshot version 5; serialize.cpp), and
// a world loaded from it is the world saved, table for table: a busy world,
// its records taken and freed for 600 ticks, is saved in that form and in
// the form before it (version 4, every table whole) and loaded from both.
// Each loaded world passes the engine's checks; its memory's tables written
// whole are byte for byte the first world's (the granule map, the holders'
// lists, the bitmaps and the free records' order are worked out again on
// loading, not saved); and all three worlds go on to the same hashes and
// counts. The form in use is a small part of the whole one.
void test_compact_save() {
    World a;
    Config cfg;
    CHECK(build(a, cfg));
    a.start_threads(2);
    for (u32 t = 0; t < 600; ++t)
        a.tick();
    u64 programs = 0, freed = 0;
    for (u32 i = 0; i < a.place_count(); ++i) {
        programs += a.place(i).os.live_programs();
        freed += a.place(i).deaths;
    }
    CHECK(programs > 20 && freed > 20); // records in use, and records given back
    ByteWriter now, old;
    a.save(now);
    a.save(old, 4);
    CHECK(now.size() * 4 < old.size());
    World b, c;
    Config cb, cc;
    CHECK(build(b, cb) && build(c, cc));
    // Loaded into worlds that have run, so nothing is left from before.
    for (u32 t = 0; t < 50; ++t) {
        b.tick();
        c.tick();
    }
    ByteReader in_now(now.data(), now.size()), in_old(old.data(), old.size());
    CHECK(b.load(in_now) && c.load(in_old, 4));
    CHECK(b.tick_count() == 600 && c.tick_count() == 600);
    CHECK(b.hash() == a.hash() && c.hash() == a.hash());
    u32 same = 0;
    for (u32 i = 0; i < a.place_count(); ++i) {
        TextBuf why;
        CHECK(b.place(i).check(&why) && c.place(i).check(&why));
        ByteWriter ma, mb, mc;
        Archive wa(&ma, 4), wb(&mb, 4), wc(&mc, 4);
        a.place(i).mem.serialize(wa);
        b.place(i).mem.serialize(wb);
        c.place(i).mem.serialize(wc);
        same += ma.size() == mb.size() && memcmp(ma.data(), mb.data(), ma.size()) == 0 && ma.size() == mc.size() &&
                        memcmp(ma.data(), mc.data(), ma.size()) == 0
                    ? 1
                    : 0;
    }
    CHECK(same == a.place_count());
    // A save of the loaded world is the save it was loaded from.
    ByteWriter again;
    b.save(again);
    CHECK(again.size() == now.size() && memcmp(again.data(), now.data(), now.size()) == 0);
    b.start_threads(1);
    c.start_threads(24);
    // Births and deaths from here on (the counts are for reports, and a save
    // leaves them out, so each world's start where it stands).
    u64 born[3] = {}, died[3] = {};
    World* const worlds[3] = {&a, &b, &c};
    for (u32 k = 0; k < 3; ++k)
        for (u32 i = 0; i < a.place_count(); ++i) {
            born[k] -= worlds[k]->place(i).births;
            died[k] -= worlds[k]->place(i).deaths;
        }
    for (u32 round = 0; round < 4; ++round) {
        for (u32 t = 0; t < 100; ++t) {
            a.tick();
            b.tick();
            c.tick();
        }
        CHECK(b.hash() == a.hash() && c.hash() == a.hash());
    }
    for (u32 k = 0; k < 3; ++k)
        for (u32 i = 0; i < a.place_count(); ++i) {
            born[k] += worlds[k]->place(i).births;
            died[k] += worlds[k]->place(i).deaths;
        }
    CHECK(born[0] > 20 && died[0] > 20 && born[1] == born[0] && born[2] == born[0] && died[1] == died[0] &&
          died[2] == died[0]);
    for (u32 i = 0; i < a.place_count(); ++i) {
        const Place &pa = a.place(i), &pb = b.place(i), &pc = c.place(i);
        CHECK(pb.os.live_threads() == pa.os.live_threads() && pc.os.live_threads() == pa.os.live_threads());
    }
    // A save cut short is refused, not read astray.
    World d;
    Config cd;
    CHECK(build(d, cd));
    ByteReader cut(now.data(), now.size() - now.size() / 3);
    CHECK(!d.load(cut));
    printf("  world: a world of %llu programs saves in %llu bytes with only what is in use, against %llu whole, and "
           "loads from either as the world saved\n",
           static_cast<unsigned long long>(programs), static_cast<unsigned long long>(now.size()),
           static_cast<unsigned long long>(old.size()));
}

void test_seeds_and_constraints() {
    World a, b;
    Config ca, cb;
    CHECK(build(a, ca));
    CHECK(build(b, cb, "world.seed = 2\n"));
    CHECK(a.hash() != b.hash());
    // Constraints between settings refuse the world.
    World c;
    Config cc;
    Config::ParseError pe;
    CHECK(cc.parse("world.places = 1\nhw.cores.min = 6\nhw.cores.max = 3\n", &pe));
    TextBuf why;
    CHECK(!c.init(cc, &why) && strstr(cc.value_error(), "hw.cores.max") != nullptr);
    World d;
    Config cd;
    CHECK(cd.parse("world.places = 1\nmemory.threshold.fast = 900\n", &pe));
    CHECK(!d.init(cd, &why) && strstr(cd.value_error(), "below the cores") != nullptr);
    World e;
    Config ce;
    CHECK(ce.parse("world.places = 1\nos.page_sizes = 64 100\n", &pe));
    CHECK(!e.init(ce, &why) && strstr(ce.value_error(), "os.page_sizes") != nullptr);
    World f;
    Config cf;
    CHECK(cf.parse("world.places = 1\nregion.step = 16\n", &pe));
    TextBuf fwhy;
    CHECK(!f.init(cf, &fwhy) && strstr(fwhy.c_str(), "region.step") != nullptr);
}

} // namespace

void suite_world() {
    test_places();
    test_thread_counts();
    test_life_happens();
    test_snapshot_resume();
    test_place_snapshot();
    test_compact_save();
    test_seeds_and_constraints();
}
