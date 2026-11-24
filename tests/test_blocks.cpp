// The disk's blocks (os.h, blocks.cpp; design.md 7.4, 7.6, 10.5;
// build-plan-gate5.md step 3): each block's endurance drawn from half to
// twice the disk's; a block's flips starting past its endurance and growing
// as the writing goes on, in the bytes written and nowhere else; the boot
// block passing its endurance failing the disk, whose calls then fail soft
// and whose boot block boots nothing; an offset keeping its block for life
// with levelling off, and a million writes to one offset spread over the
// disk with it on, the OS's copies counted; worlds through a snapshot and on
// one host thread and three; and nothing at all with the endurance at 0.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/keys.h"
#include "nova2/snapshot.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 1\n"
                           "world.map = none\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "disk.latency = 1000\n"
                           "os.strict_share = 0\n"
                           "region.retry = 1000000\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 2\n"
                           "hw.e_share = 0\n"
                           "disk.block_endurance = 1000\n";

bool build_on(World& w, Config& cfg, const char* base, const char* extra, TextBuf* why) {
    Config::ParseError pe;
    if (!cfg.parse(Str(base, strlen(base)), &pe))
        return false;
    Config over;
    if (!over.parse(Str(extra, strlen(extra)), &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    if (!w.init(cfg, why)) {
        printf("  blocks: a world wasn't built: %s %s\n", cfg.ok() ? "" : cfg.value_error(), why->c_str());
        return false;
    }
    return true;
}

bool build(World& w, Config& cfg, const char* extra, TextBuf* why) { return build_on(w, cfg, kSmall, extra, why); }

void run_at(Place& p, i32 mv) {
    p.supplied = true;
    p.given_mv = mv;
    p.os.clear_facts();
    p.tick();
}

bool place_ok(const Place& p) {
    TextBuf why;
    const bool ok = p.check(&why);
    if (!ok)
        printf("  blocks: %s\n", why.c_str());
    return ok;
}

// DWRITE in a loop (test_cutout.cpp's): its offset in D, buffer in B and
// length in C.
const u8 kWriter[5] = {encode(kMov, kD), encode(kSys, kImm), kDwrite, encode(kJmp, kImm), static_cast<u8>(-3)};
// DREAD in a loop, each count it gets kept in D, the next read's offset: so
// D shows what the last read answered.
const u8 kReader[6] = {encode(kMov, kD), encode(kSys, kImm), kDread, encode(kSto, kD),
                       encode(kJmp, kImm), static_cast<u8>(-4)};

Regs regs(Place& p, u32 t) {
    const Thread& th = p.os.thread(t);
    if (th.core)
        return p.cores[th.core - 1].x.r;
    ExecContext x;
    p.os.load(t, x);
    return x.r;
}

// A program placed by hand in an allocation of 1 KB, its buffer 64 bytes
// past its start: B the buffer, C `len`, D `offset`. Its thread's record.
u32 place_io(Place& p, const u8* code, u32 n, u32 offset, u32 len) {
    const u32 at = p.mem.alloc(1, 1024, 2, 0);
    CHECK(at != 0);
    memcpy(p.mem.bytes() + at, code, n);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    u32 t = 0;
    for (; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            break;
    CHECK(t < p.os.thread_capacity());
    ExecContext x;
    p.os.load(t, x);
    x.r.b = at + 64;
    x.r.c = len;
    x.r.d = offset;
    p.os.save(t, x);
    return t;
}

u64 bits_set(const u8* p, u32 n) {
    u64 bits = 0;
    for (u32 i = 0; i < n; ++i)
        for (u32 b = 0; b < 8; ++b)
            bits += (p[i] >> b) & 1;
    return bits;
}

// --- lives ---------------------------------------------------------------------

void test_lives() {
    u32 first[2] = {};
    for (u32 pass = 0; pass < 3; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, pass == 2 ? "world.seed = 77\n" : "", &why));
        const Os& os = w.place(0).os;
        CHECK(os.blocks_on() && os.block_size() == 512 && os.block_count() == 128);
        u32 lo = ~0u, hi = 0, low = 0, high = 0;
        u64 sum = 0;
        for (u32 b = 0; b < os.block_count(); ++b) {
            const u32 life = os.block_life(b);
            lo = life < lo ? life : lo;
            hi = life > hi ? life : hi;
            low += life < 1000 ? 1 : 0;
            high += life > 1000 ? 1 : 0;
            sum += life;
            CHECK(os.block_writes(b) == 0 && os.block_holding(b) == b);
        }
        // Half to twice 1,000, evenly on a log scale: as many under it as over.
        CHECK(lo >= 500 && lo < 560 && hi <= 2000 && hi > 1800);
        CHECK(low > 44 && high > 44);
        if (pass < 2)
            first[pass] = os.block_life(0) * 31 + os.block_life(77);
        else
            CHECK(os.block_life(0) * 31 + os.block_life(77) != first[0]); // another seed, other lives
        CHECK(!os.boot_worn() && !os.disk_failed());
    }
    CHECK(first[0] == first[1]); // the same seed, the same lives
    // A disk that isn't a whole number of blocks has a short last one.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "disk.size = 1300\ndisk.block = 512\n", &why));
    CHECK(w.place(0).os.block_count() == 3);
    printf("  blocks: 128 blocks' endurances drawn from half to twice the disk's, the same for a seed\n");
}

// --- a worn block -------------------------------------------------------------------

// Writes of zeros to bytes [lo, hi) with block `p`'s writes held at `writes`
// before each: the bits that landed wrong over `times` of them.
u64 flips_at(Place& pl, u32 p, u32 lo, u32 hi, u32 writes, u32 times) {
    u64 flips = 0;
    for (u32 i = 0; i < times; ++i) {
        memset(pl.os.disk() + lo, 0, hi - lo);
        pl.os.set_block_writes(p, writes);
        pl.os.wear_blocks(lo, hi - lo);
        flips += bits_set(pl.os.disk() + lo, hi - lo);
    }
    return flips;
}

void test_worn_block() {
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "", &why));
    Place& p = w.place(0);
    Os& os = p.os;
    const u32 blk = 5, lo = blk * 512, hi = lo + 512;
    const u32 life = os.block_life(blk);
    // Up to its endurance, to the write, nothing lands wrong.
    u8 before[512 * 3];
    memcpy(before, os.disk() + lo - 512, sizeof before);
    for (u32 i = 0; i < life; ++i)
        os.wear_blocks(lo, 512);
    CHECK(os.block_writes(blk) == life && os.worn_writes == 0 && os.worn_flips == 0);
    CHECK(memcmp(before, os.disk() + lo - 512, sizeof before) == 0);
    CHECK(os.block_writes_all == life && os.block_writes(blk - 1) == 0 && os.block_writes(blk + 1) == 0);
    // Past it, each bit written lands wrong with a chance of 1 in 1,000 x the
    // writes past / 1,000: over 400 writes of the block's 4,096 bits,
    //   half the endurance past:    819 flips
    //   the endurance past:       1,638
    //   ten times it:            16,384
    // and never more than one bit in two, however far past.
    const u64 worn0 = os.worn_flips;
    const u64 f_half = flips_at(p, blk, lo, hi, life + 500 - 1, 400);
    const u64 f_one = flips_at(p, blk, lo, hi, life + 1000 - 1, 400);
    const u64 f_ten = flips_at(p, blk, lo, hi, life + 10000 - 1, 400);
    const u64 f_far = flips_at(p, blk, lo, hi, life + 3000000 - 1, 400);
    CHECK(f_half > 819 - 110 && f_half < 819 + 110);
    CHECK(f_one > 1638 - 160 && f_one < 1638 + 160);
    CHECK(f_ten > 16384 - 500 && f_ten < 16384 + 500);
    CHECK(f_far > 819200 - 3000 && f_far < 819200 + 3000); // one in two
    CHECK(os.worn_flips - worn0 == f_half + f_one + f_ten + f_far && os.worn_writes == 1600);
    // Only the bytes written flip: a write of 100 bytes in the block's
    // middle leaves the rest of it, and its neighbours, alone.
    memset(os.disk() + lo - 512, 0, 512 * 3);
    os.set_block_writes(blk, life + 200000);
    for (u32 i = 0; i < 20; ++i)
        os.wear_blocks(lo + 200, 100);
    CHECK(bits_set(os.disk() + lo + 200, 100) > 100);
    CHECK(bits_set(os.disk() + lo - 512, 712) == 0 && bits_set(os.disk() + lo + 300, 212 + 512) == 0);
    // A write across three blocks, the middle one worn: a write to each, and
    // flips in the middle one alone.
    memset(os.disk() + lo - 512, 0, 512 * 3);
    const u32 w0 = os.block_writes(blk - 1), w2 = os.block_writes(blk + 1);
    os.set_block_writes(blk, life + 200000);
    os.wear_blocks(lo - 100, 712);
    CHECK(os.block_writes(blk - 1) == w0 + 1 && os.block_writes(blk + 1) == w2 + 1);
    CHECK(os.block_writes(blk) == life + 200001);
    CHECK(bits_set(os.disk() + lo, 512) > 300 && bits_set(os.disk() + lo - 512, 512) == 0 &&
          bits_set(os.disk() + hi, 512) == 0);
    CHECK(place_ok(p));
    printf("  blocks: a block took its %u writes clean; past them, 400 writes of 4,096 bits landed %llu, %llu and "
           "%llu bits wrong at half, once and ten times the endurance past (819, 1,638 and 16,384 expected), and "
           "%llu far past (one in two)\n",
           life, static_cast<unsigned long long>(f_half), static_cast<unsigned long long>(f_one),
           static_cast<unsigned long long>(f_ten), static_cast<unsigned long long>(f_far));
}

// --- the boot block --------------------------------------------------------------

void test_boot_block() {
    // A writer keeps its 64 bytes at offset 0, a write a tick; a reader reads
    // 64 bytes at offset 4096. Block 0 takes 20 to 80 writes; nothing wears
    // by time (wear.rate is 0), and the disk fails all the same.
    World w;
    Config cfg;
    TextBuf why;
    CHECK(build(w, cfg, "disk.block_endurance = 40\nwear.rate = 0\n", &why));
    Place& p = w.place(0);
    const u32 writer = place_io(p, kWriter, sizeof kWriter, 0, 64);
    const u32 reader = place_io(p, kReader, sizeof kReader, 4096, 64);
    const u32 life = p.os.block_life(0);
    CHECK(life >= 20 && life <= 80);
    u64 passed_at = 0;
    while (p.env.tick < 400 && !p.disk_worn.failed) {
        run_at(p, 1000);
        if (!passed_at && p.os.boot_worn())
            passed_at = p.env.tick; // the tick after the write that passed it
        CHECK(place_ok(p));
        // Until then the block's last writer is the writer's program.
        if (!passed_at)
            CHECK(p.os.boot_writer() != 0 || p.os.block_writes(0) == 0);
    }
    // It failed at the device states of the tick after its write passed the
    // endurance: one write past, no more.
    CHECK(passed_at != 0 && p.disk_worn.failed && p.env.tick == passed_at + 1);
    CHECK(p.os.block_writes(0) == life + 1);
    CHECK(p.failures[kHeatDisk] == 1 && p.boot_failures == 1 && p.os.disk_failed() && p.os.boot_writer() == 0);
    CHECK(!p.disk_state.on && p.os.pending_disk() == 0 && p.os.in_flight_bytes() == 0);
    // Its calls fail soft: they answer 0 at once, and nobody waits.
    const u64 ok_w = p.os.calls_ok[kDwrite], ok_r = p.os.calls_ok[kDread];
    const u64 asked_w = p.os.calls[kDwrite], asked_r = p.os.calls[kDread];
    const u32 writes = p.os.block_writes(0);
    for (u32 k = 0; k < 20; ++k) {
        run_at(p, 1000);
        CHECK(p.os.thread(writer).state != ThreadState::waiting_disk);
        CHECK(p.os.thread(reader).state != ThreadState::waiting_disk);
        CHECK(place_ok(p));
    }
    CHECK(p.os.calls[kDwrite] > asked_w + 100 && p.os.calls[kDread] > asked_r + 100);
    CHECK(p.os.calls_ok[kDwrite] == ok_w && p.os.calls_ok[kDread] == ok_r);
    CHECK(regs(p, reader).d == 0 && p.os.block_writes(0) == writes && p.failed_ticks[kHeatDisk] == 20);
    // SENSE reads 0 for it in every reading but its kind.
    const u32 di = p.core_count() + p.mem.device_count();
    CHECK(p.os.reading(0, kDeviceKind, di) == static_cast<u32>(DeviceKind::disk));
    CHECK(p.os.reading(0, kDeviceSpeed, di) == 0 && p.os.reading(0, kDeviceFree, di) == 0);
    // A cold start boots nothing from it; a twin whose disk works boots the
    // block its writer wrote.
    World w2;
    Config cfg2;
    CHECK(build(w2, cfg2, "disk.block_endurance = 0\nwear.rate = 0\n", &why));
    Place& q = w2.place(0);
    place_io(q, kWriter, sizeof kWriter, 0, 64);
    for (u32 k = 0; k < 30; ++k)
        run_at(q, 1000);
    Place* both[2] = {&p, &q};
    for (u32 i = 0; i < 2; ++i) {
        Place& x = *both[i];
        const u64 cold = x.cold_starts;
        for (u32 k = 0; k < 40; ++k)
            run_at(x, 0);
        for (u32 k = 0; k < 40; ++k)
            run_at(x, 1000);
        CHECK(x.cold_starts == cold + 1 && place_ok(x));
    }
    CHECK(p.region.boot_runs == 0 && p.cold_written == 0 && p.disk_worn.failed);
    CHECK(q.region.boot_runs == 1 && q.cold_written == 1);
    printf("  blocks: a boot block of %u writes' endurance failed its disk the tick after write %u; the disk's "
           "calls then answered 0 at once, and a cold start booted nothing\n",
           life, life + 1);
}

// --- levelling --------------------------------------------------------------------

void test_levelling() {
    // Off: an offset keeps its block for life.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "disk.block_endurance = 1000000\n", &why));
        Os& os = w.place(0).os;
        for (u32 i = 0; i < 5000; ++i)
            os.wear_blocks(7 * 512 + 3, 100);
        for (u32 b = 0; b < os.block_count(); ++b)
            CHECK(os.block_holding(b) == b && os.block_writes(b) == (b == 7 ? 5000u : 0u));
        CHECK(os.level_swaps == 0 && os.block_writes_all == 5000);
    }
    // On: a million writes to one offset spread over the disk's 128 blocks,
    // the OS's two copies a swap counted as writes. No block is worn (the
    // endurance is a thousand million), so not a byte changes.
    {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, "disk.block_endurance = 1000000000\nos.levelling = true\n", &why));
        Place& p = w.place(0);
        Os& os = p.os;
        Array<u8> before;
        before.append(os.disk(), os.disk_size());
        const u32 million = 1000000;
        for (u32 i = 0; i < million; ++i)
            os.wear_blocks(3 * 512, 512);
        u64 sum = 0;
        u32 lo = ~0u, hi = 0, moved = 0;
        for (u32 b = 0; b < os.block_count(); ++b) {
            const u32 n = os.block_writes(b);
            sum += n;
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
            moved += os.block_holding(b) != b ? 1 : 0;
        }
        CHECK(sum == million + 2 * os.level_swaps && sum == os.block_writes_all);
        // A swap each time the written block is 100 past the least: about one
        // a hundred writes, and no block more than the gap and a swap's
        // writes from another.
        CHECK(os.level_swaps > 9000 && os.level_swaps < 11000);
        CHECK(hi - lo <= 100 + 2 && lo > 7000);
        CHECK(moved > 100 && os.worn_writes == 0 && os.worn_flips == 0);
        CHECK(memcmp(before.data(), os.disk(), os.disk_size()) == 0);
        CHECK(place_ok(p));
        printf("  blocks: with levelling a million writes to one offset made %llu swaps and %llu block writes, "
               "every one of 128 blocks written %u to %u times\n",
               static_cast<unsigned long long>(os.level_swaps), static_cast<unsigned long long>(sum), lo, hi);
    }
    // What it buys: writing offset 0 until the block holding it passes its
    // endurance, with levelling off and on.
    u64 lasted[2] = {};
    for (u32 on = 0; on < 2; ++on) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg, on ? "disk.block_endurance = 200\nos.levelling = true\n" : "disk.block_endurance = 200\n",
                    &why));
        Os& os = w.place(0).os;
        while (!os.boot_worn() && lasted[on] < 1000000) {
            os.wear_blocks(0, 512);
            ++lasted[on];
        }
        CHECK(os.boot_worn() && place_ok(w.place(0)));
        if (!on)
            CHECK(lasted[0] == os.block_life(0) + 1);
    }
    CHECK(lasted[1] > 20 * lasted[0]);
    printf("  blocks: writing offset 0 alone, the disk's first block passed its endurance after %llu writes with "
           "levelling off and %llu with it on\n",
           static_cast<unsigned long long>(lasted[0]), static_cast<unsigned long long>(lasted[1]));
}

// --- worlds ------------------------------------------------------------------------

const char* const kWorld = "world.places = 4\n"
                           "world.map = ring\n"
                           "memory.size = 32768\n"
                           "disk.size = 4096\n"
                           "disk.latency = 1000\n"
                           "os.strict_share = 0\n"
                           "region.retry = 50\n"
                           "hw.cores.min = 2\n"
                           "hw.cores.max = 2\n"
                           "disk.block_endurance = 60\n"
                           "disk.worn_flip = 0.05\n"
                           "os.levelling_gap = 5\n";

void snapshot(World& w, Config& cfg, ByteWriter& out) {
    TextBuf text;
    cfg.write(text);
    snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
    w.save(out);
    snapshot_finish(out);
}

void place_writers(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        place_io(w.place(i), kWriter, sizeof kWriter, 0, 600);
        place_io(w.place(i), kWriter, sizeof kWriter, 2000, 300);
        place_io(w.place(i), kReader, sizeof kReader, 1024, 64);
    }
}

struct Sums {
    u64 writes = 0, worn = 0, flips = 0, swaps = 0, failed = 0;
};

Sums sums(World& w) {
    Sums s;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        s.writes += p.os.block_writes_all;
        s.worn += p.os.worn_writes;
        s.flips += p.os.worn_flips;
        s.swaps += p.os.level_swaps;
        s.failed += p.boot_failures;
        CHECK(place_ok(p));
    }
    return s;
}

void test_worlds() {
    // Four places with writers on small disks whose blocks take 30 to 120
    // writes: with levelling off and on, the same hash on 1 host thread and
    // 3, and through a snapshot taken with blocks worn and (on) moved.
    for (u32 on = 0; on < 2; ++on) {
        const char* const extra = on ? "os.levelling = true\n" : "";
        u64 hash[2] = {}, mid = 0;
        ByteWriter saved;
        Sums at_save, at_end;
        const u32 first = on ? 100 : 60, rest = on ? 400 : 100;
        for (u32 pass = 0; pass < 2; ++pass) {
            World w;
            Config cfg;
            TextBuf why;
            CHECK(build_on(w, cfg, kWorld, extra, &why));
            place_writers(w);
            w.start_threads(pass == 0 ? 1 : 3);
            for (u32 t = 0; t < first; ++t)
                w.tick();
            if (pass == 0) {
                mid = w.hash();
                snapshot(w, cfg, saved);
                at_save = sums(w);
            } else {
                CHECK_EQ_U64(w.hash(), mid);
            }
            for (u32 t = 0; t < rest; ++t) {
                w.tick();
                if (t % 16 == 0)
                    sums(w); // the invariants, as it goes
            }
            hash[pass] = w.hash();
            at_end = sums(w);
        }
        CHECK_EQ_U64(hash[0], hash[1]);
        CHECK(at_save.writes > 100 && at_end.failed == 4); // every disk's boot block wore out in the end
        CHECK(on ? at_save.swaps > 20 && at_save.worn > 0 && at_save.flips > 0 : at_end.swaps == 0);
        CHECK(at_save.failed > 0 && at_save.failed < 4); // saved with some disks failed and some not
        SnapshotHeader h;
        TextBuf err;
        Config::ParseError pe;
        CHECK(snapshot_parse(saved.data(), saved.size(), &h, &err) && h.version == 6 && h.tick == first);
        World w;
        Config cfg;
        TextBuf why;
        CHECK(snapshot_config(h, &cfg, &pe) && w.init(cfg, &why));
        ByteReader in(saved.data() + h.world_offset, h.world_size);
        CHECK(w.load(in, h.version));
        CHECK_EQ_U64(w.hash(), mid);
        u64 failed = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            failed += w.place(i).disk_worn.failed ? 1 : 0;
            CHECK(w.place(i).os.disk_failed() == w.place(i).disk_worn.failed && place_ok(w.place(i)));
        }
        CHECK(failed == at_save.failed);
        for (u32 t = 0; t < rest; ++t)
            w.tick();
        CHECK_EQ_U64(w.hash(), hash[0]);
        printf("  blocks: four places' disks with levelling %s: %llu block writes, %llu to worn blocks landing "
               "%llu bits wrong, %llu swaps, %llu disks failed by their boot block; the same world on 1 host "
               "thread and 3, and from a snapshot of tick %u, with %llu of the disks failed\n",
               on ? "on" : "off", static_cast<unsigned long long>(at_end.writes),
               static_cast<unsigned long long>(at_end.worn), static_cast<unsigned long long>(at_end.flips),
               static_cast<unsigned long long>(at_end.swaps), static_cast<unsigned long long>(at_end.failed), first,
               static_cast<unsigned long long>(at_save.failed));
    }
}

// --- off ---------------------------------------------------------------------------

void test_off() {
    // With the endurance at 0 blocks don't wear: nothing is counted, nothing
    // flips, no disk fails, and the save's bytes and hash are those of a
    // world that says nothing about blocks, whatever the other settings.
    u64 hash[2] = {};
    usize bytes[2] = {};
    for (u32 pass = 0; pass < 2; ++pass) {
        World w;
        Config cfg;
        TextBuf why;
        CHECK(build(w, cfg,
                    pass ? "disk.block_endurance = 0\ndisk.block = 64\ndisk.worn_flip = 0.5\nos.levelling = true\n"
                           "os.levelling_gap = 1\n"
                         : "disk.block_endurance = 0\n",
                    &why));
        Place& p = w.place(0);
        place_io(p, kWriter, sizeof kWriter, 0, 600);
        for (u32 t = 0; t < 300; ++t)
            run_at(p, 1000);
        CHECK(!p.os.blocks_on() && p.os.block_count() == 0 && !p.os.boot_worn() && !p.disk_worn.failed);
        CHECK(p.os.block_writes_all == 0 && p.os.worn_flips == 0 && p.os.level_swaps == 0);
        CHECK(p.os.calls_ok[kDwrite] > 250 && place_ok(p));
        hash[pass] = w.hash();
        ByteWriter out;
        w.save(out);
        bytes[pass] = out.size();
    }
    CHECK_EQ_U64(hash[0], hash[1]);
    CHECK(bytes[0] == bytes[1]);
    // A snapshot from before gate 5 is read with blocks not wearing.
    SnapshotHeader h;
    h.version = 5;
    const char* const text = "world.places = 3\n";
    h.config_text = Str(text, strlen(text));
    Config cfg;
    Config::ParseError pe;
    CHECK(snapshot_config(h, &cfg, &pe) && cfg.has("disk.block_endurance"));
    WorldSettings ws;
    read_world_settings(cfg, &ws);
    CHECK(cfg.ok() && ws.os.block_endurance == 0 && !ws.os.levelling);
    // And the agreed numbers are the defaults (build-plan-gate5.md 7).
    CHECK(key_find(Str("disk.block"))->def == 512 && key_find(Str("disk.block_endurance"))->def == 10000);
    CHECK(key_find(Str("disk.worn_flip"))->def == 0.001 && key_find(Str("os.levelling"))->def == 0);
    CHECK(key_find(Str("os.levelling_gap"))->def == 100);
}

} // namespace

void suite_blocks() {
    test_lives();
    test_worn_block();
    test_boot_block();
    test_levelling();
    test_worlds();
    test_off();
}
