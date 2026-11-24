// Gate 5's measure (build-plan-gate5.md step 1; design.md 14.1): leads, first
// leads and eras.
//
// - By hand: rows of the ground record made here, read by the rule (a group
//   leads with more programs than any other, stage 4's numbers of them), and
//   a run cut into eras from its stage 4, the later half of them late.
// - A world where replicators of different genomes, placed by hand, take a
//   place in turn: each kind's first lead is found where it was placed, a
//   kind that comes back is no first lead, and the eras with change are the
//   ones worked by hand.
// - A world where one program holds every place with many threads: no group
//   leads anywhere, so no era has change.
// - Reading input: receivers placed by hand, each given a message longer
//   than its buffer, are counted by what the bytes past the end belonged
//   to; a thread running bytes one of them wrote is an injection, and the
//   programs it then makes are of the sender's kind. And the calls by
//   kind, as a program placed by hand makes them.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/archive.h"
#include "nova2/cpu.h"
#include "nova2/observe.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

// Two lax places with nothing to mutate a genome: what leads is what was
// placed.
const char* const kQuiet = "world.places = 2\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 0\n"
                           "noise.e_floor = 0\n"
                           "noise.cosmic = 0\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

// The same replicator copying 32 bytes: its 14 with 18 more after the jump,
// never run, that make it another kind. Each `kind` gives bytes no other
// has, so no two share enough 3-byte runs to be one group, and none shares
// enough with the 14-byte one.
constexpr u32 kLong = 32;
void long_replicator(u8 kind, u8* out) {
    memcpy(out, kLax, 14);
    out[1] = out[8] = kLong; // 32 is 2 mod 3, as 14 is: the size asks for "both" (A.5)
    for (u32 i = 14; i < kLong; ++i)
        out[i] = static_cast<u8>(kind * 40 + i);
}

// One program, many threads: it starts a thread at its sleeper again and
// again, for as long as the place has a thread to give, and makes no
// program.
const u8 kCrowd[14] = {encode(kMov, kC),   encode(kAdd, kImm), 8,      encode(kSys, kImm), kSpawn,
                       encode(kJmp, kImm), static_cast<u8>(-5), 0,     encode(kMov, kImm), 120,
                       encode(kSys, kImm), kSleep, encode(kJmp, kImm), static_cast<u8>(-4)};

bool build(World& w, Config& cfg, const char* text) {
    Config::ParseError pe;
    TextBuf why;
    return cfg.parse(text, &pe) && w.init(cfg, &why);
}

GroundWindow row(u64 start, u32 place, u32 starters, u64 ref, u32 programs, u32 copies, u32 second) {
    GroundWindow g{};
    g.start = start;
    g.place = place;
    g.starters = starters;
    g.lead.group_ref = ref;
    g.lead.group = ref ? 0 : kNoGroup;
    g.lead.group_programs = programs;
    g.lead.group_copies = copies;
    g.lead.group_second = second;
    return g;
}

void test_eras_by_hand() {
    StageCriteria c; // 20 members, a tenth of the place, half of them copies
    // The rule, at each of its edges.
    CHECK(group_leads(row(0, 0, 100, 7, 20, 10, 19), c));
    CHECK(!group_leads(row(0, 0, 100, 7, 19, 19, 0), c));   // one short of 20
    CHECK(!group_leads(row(0, 0, 100, 7, 20, 9, 0), c));    // under half born as copies
    CHECK(!group_leads(row(0, 0, 201, 7, 20, 20, 0), c));   // under a tenth of the place's programs
    CHECK(group_leads(row(0, 0, 200, 7, 20, 20, 0), c));    // a tenth exactly
    CHECK(!group_leads(row(0, 0, 100, 7, 30, 30, 30), c));  // level with another: neither leads
    CHECK(!group_leads(row(0, 0, 100, 0, 0, 0, 0), c));     // no group there
    c.members = 5;
    CHECK(group_leads(row(0, 0, 50, 7, 5, 3, 4), c));

    // A seed that reached stage 4 at tick 1,000 and ended at 8,500, in eras
    // of 1,000: seven whole eras, [1000, 2000) to [7000, 8000), and half an
    // era left over. The later half of seven is three, eras 4, 5 and 6; the
    // middle one, era 3, is early.
    c = StageCriteria{};
    Records<GroundWindow> rows;
    rows.push(row(0, 0, 0, 0, 0, 0, 0));           // empty
    rows.push(row(200, 0, 40, 0xA, 30, 20, 0));    // A's first lead, before stage 4
    rows.push(row(200, 1, 40, 0xA, 25, 20, 0));    // A again, elsewhere: led, not first
    rows.push(row(1000, 0, 40, 0xB, 21, 11, 19));  // B: the first tick of era 0
    rows.push(row(1800, 1, 40, 0xC, 30, 30, 0));   // C: era 0 again, which is one era with change
    rows.push(row(2000, 1, 40, 0xD, 10, 10, 0));   // D with 10: not a lead
    rows.push(row(3999, 0, 40, 0xD, 22, 22, 3));   // D's first lead: the last tick of era 2
    rows.push(row(4000, 0, 40, 0xA, 30, 30, 0));   // A back: no first lead in era 3
    rows.push(row(5000, 1, 40, 0xE, 30, 30, 0));   // E: era 4, a late one
    rows.push(row(7999, 1, 40, 0xF, 30, 30, 0));   // F: the last tick of era 6, late
    rows.push(row(8000, 0, 40, 0x10, 30, 30, 0));  // after the last whole era: a first lead in no era
    EraTally t;
    Array<FirstLead> firsts;
    tally_eras(rows, 1000, 8500, 1000, c, &t, &firsts);
    CHECK(t.windows == 11 && t.led == 9 && t.first_leads == 7 && t.first_before == 1);
    CHECK(t.eras == 7 && t.late == 3 && t.early() == 4);
    CHECK(t.changed == 4 && t.late_changed == 2 && t.early_changed() == 2); // eras 0, 2, 4 and 6
    CHECK(firsts.size() == 7);
    static const u64 kRefs[7] = {0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10};
    static const u64 kTicks[7] = {200, 1000, 1800, 3999, 5000, 7999, 8000};
    static const u32 kEras[7] = {kNoEra, 0, 0, 2, 4, 6, kNoEra};
    for (usize i = 0; i < firsts.size() && i < 7; ++i) {
        const FirstLead& f = firsts[i];
        CHECK(f.group_ref == kRefs[i] && f.tick == kTicks[i] && f.era == kEras[i]);
        CHECK(f.late == (f.era == 4 || f.era == 6));
    }
    CHECK(firsts[1].place == 0 && firsts[1].programs == 21 && firsts[1].copies == 11);

    // How many eras a span gives, and how many are late: 16 eras' worth of
    // ticks is 16 eras, 8 late; a tick short is 15, 7 late; under one era,
    // none.
    tally_eras(rows, 1000, 1000 + 16000, 1000, c, &t, nullptr);
    CHECK(t.eras == 16 && t.late == 8);
    tally_eras(rows, 1000, 1000 + 15999, 1000, c, &t, nullptr);
    CHECK(t.eras == 15 && t.late == 7);
    tally_eras(rows, 1000, 1999, 1000, c, &t, nullptr);
    CHECK(t.eras == 0 && t.late == 0 && t.changed == 0 && t.first_leads == 7 && t.first_before == 1);
    // The plan's pass mark counts a seed that ran 16 million ticks past its
    // stage 4, in eras of 2 million: 8 eras, 4 of them late, and so up to
    // 18 million.
    tally_eras(rows, 3000000, 3000000 + 16000000, 2000000, c, &t, nullptr);
    CHECK(t.eras == 8 && t.late == 4);
    tally_eras(rows, 3000000, 3000000 + 17999999, 2000000, c, &t, nullptr);
    CHECK(t.eras == 8 && t.late == 4);
    // A seed that reached no stage 4 has leads and first leads, and no eras.
    tally_eras(rows, ~0ull, 8500, 1000, c, &t, &firsts);
    CHECK(t.eras == 0 && t.led == 9 && t.first_leads == 7 && t.first_before == 7 && firsts.size() == 7);
    CHECK(firsts[3].era == kNoEra && !firsts[3].late);
    // And no eras are asked for with an era of 0.
    tally_eras(rows, 1000, 8500, 0, c, &t, nullptr);
    CHECK(t.eras == 0 && t.first_leads == 7 && t.first_before == 1);
    printf("  gate5: a group leads with more programs than any other, 20 or more, a tenth of the place's and half "
           "born as copies, at each edge of the rule; 11 rows made by hand gave 9 leads and 7 first leads, 4 of 7 "
           "eras with change and 2 of the 3 late ones; 16 million ticks past stage 4 is 8 eras, 4 late\n");
}

// Who leads a place now, worked here from the observer's records of the
// programs alive in its OS: each kept genome's group, one against every
// other, and the rule. The group's reference genome's hash, or 0.
u64 lead_by_hand(World& w, const Observer& obs, u32 place, const StageCriteria& c, u32* programs) {
    const Os& os = w.place(place).os;
    Array<u32> groups;
    Array<u8> copies;
    u32 alive = 0;
    for (u32 i = 0; i < os.program_capacity(); ++i) {
        const Program& g = os.program(kFirstProgramHolder + i);
        if (!g.alive)
            continue;
        ++alive;
        const ProgramRec* r = obs.program(g.uid);
        const GenomeRec* gr = r ? obs.genome(r->genome) : nullptr;
        if (!gr || gr->group == kNoGroup)
            continue;
        groups.push(gr->group);
        copies.push(r->copy && !r->arrived ? 1 : 0);
    }
    u32 best = kNoGroup, most = 0, made = 0, second = 0;
    for (usize a = 0; a < groups.size(); ++a) { // the group with the most
        u32 n = 0, k = 0;
        for (usize b = 0; b < groups.size(); ++b)
            if (groups[b] == groups[a]) {
                ++n;
                k += copies[b];
            }
        if (n > most || (n == most && groups[a] < best)) {
            best = groups[a];
            most = n;
            made = k;
        }
    }
    for (usize a = 0; a < groups.size(); ++a) { // and the most any other has
        u32 n = 0;
        for (usize b = 0; b < groups.size(); ++b)
            n += groups[b] == groups[a] ? 1 : 0;
        if (groups[a] != best && n > second)
            second = n;
    }
    *programs = most;
    if (best == kNoGroup || most <= second || most < c.members || made * 2 < most || most * 10 < alive)
        return 0;
    return obs.group_reference(best).hash;
}

// The hash the observers know a genome by: that of the kept genome with
// these bytes (0: none is kept).
u64 genome_named(const Observer& obs, const u8* bytes, u32 len) {
    for (u32 i = 0; i < obs.genome_count(); ++i) {
        const GenomeRec& g = obs.genome_at(i);
        if (g.state == GenomeState::kept && g.len == len && memcmp(obs.genome_bytes(g), bytes, len) == 0)
            return g.hash;
    }
    return 0;
}

// Replicators of different genomes take a place in turn. Place 0 is given
// the 14-byte replicator; every 2,000 ticks it is cold-started by hand, so
// every program there ends, and given another kind: B, C, the first again,
// then D. Place 1 is left alone. Windows are 200 ticks and eras 1,000, and
// the seed is taken to have reached stage 4 at tick 1,000, so in 10,000
// ticks it has 9 eras, the last 4 of them late.
void test_leads_world() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kQuiet));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 100;
    os.turnover.window = 200;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    const StageCriteria c;
    u8 kinds[5][kLong] = {};
    u32 lens[5] = {14, kLong, kLong, 14, kLong};
    memcpy(kinds[0], kLax, 14);
    long_replicator(1, kinds[1]);
    long_replicator(2, kinds[2]);
    memcpy(kinds[3], kLax, 14);
    long_replicator(3, kinds[4]);
    const u64 kTurn = 2000, kEnd = 10000, kWindow = 200;
    Array<u64> want; // by window, then place: who led as it began, by hand (0: no one)
    want.resize(static_cast<usize>(kEnd / kWindow + 1) * 2);
    u32 led_by_hand = 0;
    for (u64 t = 0; t < kEnd; ++t) {
        if (t % kTurn == 0) {
            Place& p = w.place(0);
            if (t)
                p.cold_start();
            // At each core's next start spot, where its next idle run begins
            // (but not over one just placed).
            const u32 k = static_cast<u32>(t / kTurn);
            u32 placed[8], n = 0;
            for (u32 core = 0; core < p.core_count() && n < 8; ++core) {
                const u32 spot = p.region.state(core).spot;
                bool clear = spot + lens[k] <= p.mem.region_size();
                for (u32 j = 0; j < n; ++j)
                    clear = clear && (spot >= placed[j] + lens[k] || placed[j] >= spot + lens[k]);
                if (!clear)
                    continue;
                memcpy(p.mem.bytes() + p.mem.region_base() + spot, kinds[k], lens[k]);
                placed[n++] = spot;
            }
            CHECK(n > 0);
        }
        w.tick();
        if ((t + 1) % kWindow == 0)
            for (u32 i = 0; i < 2; ++i) {
                u32 n = 0;
                const u64 ref = lead_by_hand(w, obs, i, c, &n);
                want[static_cast<usize>((t + 1) / kWindow) * 2 + i] = ref;
                led_by_hand += ref && t + 1 < kEnd ? 1 : 0;
            }
    }
    // Every row's lead is the one worked by hand as its window began.
    CHECK(obs.ground_windows.size() == static_cast<usize>(kEnd / kWindow) * 2);
    u32 agree = 0, led = 0;
    for (usize i = 0; i < obs.ground_windows.size(); ++i) {
        const GroundWindow g = obs.ground_windows[i];
        const u64 by_hand = want[static_cast<usize>(g.start / kWindow) * 2 + g.place];
        const bool leads = group_leads(g, c);
        agree += (leads ? g.lead.group_ref : 0) == by_hand ? 1 : 0;
        led += leads ? 1 : 0;
        CHECK(!leads || g.place == 0); // place 1 was left alone: nothing leads it
    }
    CHECK(agree == obs.ground_windows.size() && led == led_by_hand && led >= 30);
    // The four kinds, by the hashes the observers know them by.
    const u64 a = genome_named(obs, kinds[0], 14), b = genome_named(obs, kinds[1], kLong),
              cc = genome_named(obs, kinds[2], kLong), d = genome_named(obs, kinds[4], kLong);
    CHECK(a && b && cc && d && a != b && a != cc && a != d && b != cc && b != d && cc != d);
    EraTally t;
    Array<FirstLead> firsts;
    tally_eras(obs.ground_windows, 1000, kEnd, 1000, c, &t, &firsts);
    // By hand: the first kind leads from the first windows, before stage 4;
    // B's first lead comes in [2200, 3000), era 1; C's in [4200, 5000), era
    // 3; the first kind's return at 6,000 is no first lead; D's comes in
    // [8200, 9000), era 7, a late one. So 2 of the 5 early eras have change
    // and 1 of the 4 late ones.
    CHECK(t.windows == obs.ground_windows.size() && t.led == led);
    CHECK(firsts.size() == 4 && t.first_leads == 4 && t.first_before == 1);
    CHECK(t.eras == 9 && t.late == 4 && t.changed == 3 && t.late_changed == 1 && t.early_changed() == 2);
    if (firsts.size() == 4) {
        CHECK(firsts[0].group_ref == a && firsts[0].tick < 1000 && firsts[0].era == kNoEra);
        CHECK(firsts[1].group_ref == b && firsts[1].tick >= 2200 && firsts[1].tick < 3000 && firsts[1].era == 1);
        CHECK(firsts[2].group_ref == cc && firsts[2].tick >= 4200 && firsts[2].tick < 5000 && firsts[2].era == 3);
        CHECK(firsts[3].group_ref == d && firsts[3].tick >= 8200 && firsts[3].tick < 9000 && firsts[3].era == 7);
        CHECK(!firsts[1].late && !firsts[2].late && firsts[3].late);
        for (u32 i = 0; i < 4; ++i) {
            const FirstLead& f = firsts[i];
            CHECK(f.place == 0 && f.programs >= c.members && f.copies * 2 >= f.programs);
        }
    }
    // The first kind led again after its return, in the late eras, which is
    // no change.
    u32 back = 0;
    for (usize i = 0; i < obs.ground_windows.size(); ++i) {
        const GroundWindow g = obs.ground_windows[i];
        back += g.start >= 6200 && g.start < 8000 && group_leads(g, c) && g.lead.group_ref == a ? 1 : 0;
    }
    CHECK(back >= 5);
    // The reviewer's measures (build-plan-gate4.md 11) in this world. Its
    // replicators SPAWN at a fresh allocation each time, so every SPAWN that
    // got anything made a program; and each row's counts stand inside one
    // another: copies among the starters and the births, the leading
    // lineage's copies among the starters', the largest program's threads
    // among the place's, and the core time worked within what the cores had.
    u64 made = 0, copy_births = 0, copy_starters = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Os& o = w.place(i).os;
        CHECK(o.spawns_thread_ok == 0 && o.spawns_program_ok == o.calls_ok[kSpawn]);
        CHECK(o.spawns_thread + o.spawns_program <= o.calls[kSpawn]);
        made += o.spawns_program_ok;
    }
    CHECK(made > 1000);
    for (usize i = 0; i < obs.ground_windows.size(); ++i) {
        const GroundWindow g = obs.ground_windows[i];
        CHECK(g.births_copies <= g.births && g.starters_copies <= g.starters);
        CHECK(g.starters_copies >= g.lead.lineage_copies && g.top_threads <= g.threads);
        CHECK(g.threads >= g.starters || g.starters == 0);
        CHECK(g.worked <= (static_cast<u64>(g.cores) * os.turnover.window - g.cut - g.heat) << 20);
        CHECK(g.starters == 0 || g.worked > 0);
        copy_births += g.births_copies;
        copy_starters += g.starters_copies;
    }
    CHECK(copy_births > 1000 && copy_starters > 1000);
    printf("  gate5: four kinds of replicator given a place in turn: %u of its %u place-windows led, each as "
           "worked by hand from the living programs' records; first leads at ticks %llu, %llu, %llu and %llu, the "
           "kind that came back leading %u windows more without one; change in 2 of 5 early eras and 1 of 4 late "
           "ones\n",
           led, static_cast<u32>(obs.ground_windows.size()),
           firsts.size() == 4 ? static_cast<unsigned long long>(firsts[0].tick) : 0ull,
           firsts.size() == 4 ? static_cast<unsigned long long>(firsts[1].tick) : 0ull,
           firsts.size() == 4 ? static_cast<unsigned long long>(firsts[2].tick) : 0ull,
           firsts.size() == 4 ? static_cast<unsigned long long>(firsts[3].tick) : 0ull, back);
}

// One program holds every place, with as many threads as the place gives
// it: late worlds thinned this way at gate 3 (seed 15). Nothing leads, so
// no era has change.
void test_one_program_world() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kQuiet));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 100;
    os.turnover.window = 200;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        const u32 at = p.mem.alloc(1, 64, 2, 0);
        CHECK(at != 0);
        memcpy(p.mem.bytes() + at, kCrowd, sizeof kCrowd);
        CHECK(p.os.place_by_hand(1, at, at, 0) != 0);
    }
    u32 most_threads = 0, most_programs = 0;
    for (u64 t = 0; t < 4000; ++t) {
        w.tick();
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Os& o = w.place(i).os;
            most_threads = o.live_threads() > most_threads ? o.live_threads() : most_threads;
            most_programs = o.live_programs() > most_programs ? o.live_programs() : most_programs;
        }
    }
    const StageCriteria c;
    EraTally t;
    Array<FirstLead> firsts;
    tally_eras(obs.ground_windows, 1000, 4000, 500, c, &t, &firsts);
    CHECK(most_threads >= 100 && most_programs < c.members);
    CHECK(t.windows == 40 && t.led == 0 && t.first_leads == 0 && firsts.empty());
    CHECK(t.eras == 6 && t.late == 3 && t.changed == 0 && t.late_changed == 0);
    // The reviewer's measures (build-plan-gate4.md 11) in this world, by
    // hand. Every SPAWN is at the program's own code, so each asked for a
    // thread and none for a program; the hundred and more that got one are
    // the threads. Its ground rows from the second window on: one starter,
    // not a copy; every thread the one program's; no births. And the
    // spawner alone keeps a core busy all tick, so the place's cores worked
    // a core's whole window or more, and no more than they had.
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Os& o = w.place(i).os;
        CHECK(o.spawns_program == 0 && o.spawns_program_ok == 0);
        CHECK(o.spawns_thread == o.calls[kSpawn] && o.spawns_thread_ok == o.calls_ok[kSpawn]);
        CHECK(o.spawns_thread_ok >= 100 && o.spawns_thread > o.spawns_thread_ok);
    }
    const Sample& last = obs.last_sample();
    CHECK(last.spawns_thread == last.calls[kSpawn] && last.spawns_thread_ok == last.calls_ok[kSpawn] &&
          last.spawns_program == 0 && last.spawns_thread_ok >= 200);
    u32 top = 0, rows = 0;
    for (usize i = 0; i < obs.ground_windows.size(); ++i) {
        const GroundWindow g = obs.ground_windows[i];
        const u64 had = (static_cast<u64>(g.cores) * 200 - g.cut - g.heat) << 20;
        CHECK(g.worked <= had && g.births_copies == 0 && g.starters_copies == 0);
        if (g.start == 0) {
            CHECK(g.threads == 0 && g.top_threads == 0); // placed by hand after the window began
            continue;
        }
        ++rows;
        CHECK(g.starters == 1 && g.threads == g.top_threads && g.threads >= 1);
        CHECK(g.worked >= 190ull << 20);
        top = g.top_threads > top ? g.top_threads : top;
    }
    CHECK(rows == 38 && top >= 100 && top <= most_threads);
    printf("  gate5: one program in each place with up to %u threads, and never more than %u programs in a place: "
           "none of %llu place-windows led, no first lead, change in 0 of 6 eras; every SPAWN counted as asking "
           "for a thread, its windows as begun by one program with up to %u threads, no copies among its "
           "starters or births, its cores worked a core's whole window or more\n",
           most_threads, most_programs, static_cast<unsigned long long>(t.windows), top);
}

// A program's bytes, an instruction at a time (test_send.cpp's).
struct Code {
    u8 b[128] = {};
    u32 n = 0;
    Code& op(u8 o, u8 od) {
        b[n++] = encode(o, od);
        return *this;
    }
    Code& imm(u8 o, i32 v) {
        b[n++] = encode(o, kImm);
        b[n++] = static_cast<u8>(v);
        return *this;
    }
    Code& sys(u8 call) { return imm(kSys, call); }
    Code& set(u8 r, i32 v) { return imm(kMov, v).op(kSto, r); } // register r (not A) = v, through A
    // Register r = the address `at` bytes into the program's allocation.
    Code& addr(u8 r, u32 at) {
        const i32 off = static_cast<i32>(at) - static_cast<i32>(n);
        CHECK(off >= -128 && off <= 127);
        return op(kMov, kIp).imm(kAdd, off).op(kSto, r);
    }
    Code& done() { return imm(kMov, 0).sys(kSleep); } // a wait for an interrupt that never comes
    // A RECV on `port` into the buffer at B, at most 40 bytes, for as long as it takes.
    Code& recv(u32 port) { return set(kC, 40).set(kD, -1).imm(kMov, static_cast<i32>(port)).sys(kRecv).done(); }
};

// Two lax places of 1,024-byte pages, so allocations of 96 bytes lie ten to
// a page and a program may write anywhere in a page it holds part of (9.3);
// nothing mutates, and no idle run does anything (their regions are filled
// with PUSH A, which faults at once).
const char* const kPacked = "world.places = 2\n"
                            "memory.size = 32768\n"
                            "disk.size = 65536\n"
                            "hw.cores.min = 2\n"
                            "hw.cores.max = 2\n"
                            "os.strict_share = 0\n"
                            "os.page_sizes = 1024\n"
                            "region.retry = 1000000\n"
                            "noise.p_floor = 0\n"
                            "noise.e_floor = 0\n"
                            "noise.cosmic = 0\n";

constexpr u32 kSlot = 96;  // an allocation: a receiver's buffer is its last 8 bytes
constexpr u32 kPast = 32;  // the bytes a 40-byte message writes past it

// A program placed by hand in a fresh, zeroed allocation of kSlot bytes, its
// thread starting at its first byte. The thread's record; the allocation in *base.
u32 put(Place& p, const Code& c, u32* base) {
    const u32 at = p.mem.alloc(1, kSlot, 2, 0);
    CHECK(at != 0);
    memset(p.mem.bytes() + at, 0, kSlot);
    memcpy(p.mem.bytes() + at, c.b, c.n);
    const u32 id = p.os.place_by_hand(1, at, at, 0);
    CHECK(id != 0);
    *base = at;
    for (u32 t = 0; t < p.os.thread_capacity(); ++t)
        if (p.os.thread(t).state != ThreadState::free && p.os.thread(t).id == id)
            return t;
    CHECK(false);
    return 0;
}

u64 uid_of(const Place& p, u32 t) { return p.os.program(p.os.thread(t).program).uid; }

void fill_regions(World& w) {
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        memset(p.mem.bytes() + p.mem.region_base(), encode(kPush, kA), p.mem.region_size());
    }
}

// Receivers placed by hand at place 0, each waiting in RECV with its buffer
// the last 8 bytes of an allocation and a max length of 40, so a 40-byte
// message runs 32 bytes past the end. What lies there:
// - one's own second allocation;
// - another lineage's program, V, a sleeper: its code becomes the message's,
//   a replicator, which its thread then runs (an injection), making programs
//   of the sender's kind;
// - a program of its own lineage: K, which made a copy of itself, C, in the
//   next allocation; and past C, free memory. Both wait on one port;
// - a thread's stack: this receiver's buffer ends its own stack, and the
//   next thread's lies after it.
// The sender, S, sleeps at place 1: the messages are delivered by hand in
// its name, one in the name of a program nobody knows. One more receiver
// takes a message that fits; and one message waits in an inbox across a
// save, to be read in the loaded world with its sender still named.
void test_reading_input() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, kPacked));
    fill_regions(w);
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    Place& p0 = w.place(0);
    Place& p1 = w.place(1);
    CHECK(!p0.mem.strict() && p0.mem.page_size() == 1024);

    // The message: 8 bytes that fit, then the genome G: two instructions
    // that do nothing, and A.5's replicator copying 32 bytes from where its
    // thread began (C), which is where G starts.
    u8 msg[40], *G = msg + 8;
    for (u32 i = 0; i < 40; ++i)
        msg[i] = static_cast<u8>(0xE0 + i % 16);
    const u8 kRep[18] = {encode(kMov, kImm), 0,      encode(kMov, kImm), 0,      encode(kMov, kImm), kPast,
                         encode(kSto, kB),   encode(kSys, kImm), kAlloc, encode(kMov, kC),   encode(kSto, kB),
                         encode(kCpy, kImm), kPast,  encode(kMov, kD),   encode(kSys, kImm), kSpawn,
                         encode(kJmp, kImm), static_cast<u8>(-12)};
    memcpy(G, kRep, sizeof kRep);

    // Place 0, in the order its allocations lie.
    u32 own_base = 0, other_base = 0, v_base = 0, stack_base = 0, w_base = 0, k_base = 0, short_base = 0;
    Code own;
    own.addr(kB, kSlot - 8).recv(3);
    const u32 t_own = put(p0, own, &own_base);
    const u32 extra = p0.mem.alloc(1, kSlot, 2, 0); // its second allocation, given to it by hand
    CHECK(extra == own_base + kSlot);
    p0.mem.give(p0.mem.alloc_at(extra), p0.os.thread(t_own).program);
    Code other;
    other.addr(kB, kSlot - 8).recv(4);
    const u32 t_other = put(p0, other, &other_base);
    Code sleeper; // V: SLEEP 2, again; it wakes at its fifth byte
    sleeper.imm(kMov, 2).sys(kSleep).imm(kJmp, -4);
    const u32 t_v = put(p0, sleeper, &v_base);
    CHECK(v_base == other_base + kSlot);
    Code on_stack; // its buffer's address comes in A
    on_stack.op(kSto, kB).recv(7);
    const u32 t_stack = put(p0, on_stack, &stack_base);
    Code waits;
    waits.done();
    const u32 t_w = put(p0, waits, &w_base);
    CHECK(p0.os.thread(t_w).stack == p0.os.thread(t_stack).stack + 64);
    {
        ExecContext x;
        p0.os.load(t_stack, x);
        x.r.a = p0.os.thread(t_stack).stack + 64 - 8;
        p0.os.save(t_stack, x);
    }
    Code kin; // K: a copy of itself in a new allocation, started at its RECV; then its own RECV
    kin.set(kB, 2).set(kC, 0).imm(kMov, kSlot).sys(kAlloc).addr(kB, 0).imm(kCpy, kSlot).op(kMov, kD);
    const u32 enter = kin.n + 4; // past the ADD and the SPAWN
    kin.imm(kAdd, static_cast<i32>(enter)).sys(kSpawn);
    CHECK(kin.n == enter);
    kin.addr(kB, kSlot - 8).recv(5);
    const u32 t_k = put(p0, kin, &k_base);
    // Place 1: the sender, its genome G written as a parent writes a child's
    // (so the observers know it by G), asleep; and the program whose calls
    // are counted.
    const u32 s_at = p1.mem.alloc(1, kSlot, 2, 0);
    CHECK(s_at != 0);
    memset(p1.mem.bytes() + s_at, 0, kSlot);
    memcpy(p1.mem.bytes() + s_at, G, kPast);
    p1.mem.note_write(s_at, kPast, 1);
    Code asleep;
    asleep.done();
    memcpy(p1.mem.bytes() + s_at + 40, asleep.b, asleep.n);
    CHECK(p1.os.place_by_hand(1, s_at, s_at + 40, 0) != 0);
    const u64 sender = p1.os.program(p1.mem.record(p1.mem.alloc_at(s_at)).holder).uid;
    // The calls: an ALLOC that works and a FREE that can't; a SEND of 4
    // bytes to its own place's port 3, let go, and one of no bytes to port
    // 0, which fails; a PING of its own place, echoed; SETCLOCK and SENSE,
    // which always answer; and the SLEEP it ends in (the waits a SEND and
    // a PING make are not calls).
    Code caller;
    caller.set(kB, 2).imm(kMov, 32).sys(kAlloc).imm(kMov, 0).sys(kFree);
    caller.set(kD, 4).set(kB, 3).imm(kMov, 1).sys(kSend).set(kD, 0).set(kB, 0).imm(kMov, 1).sys(kSend);
    caller.imm(kMov, 5).sys(kSetClock).imm(kMov, 0).sys(kSense).imm(kMov, 1).sys(kPing).done();
    u32 caller_base = 0;
    put(p1, caller, &caller_base);
    // And a second sender, T, which SENDs the message to place 0's port 10
    // and ends: when the message is read, its sender is dead.
    Code talker;
    talker.addr(kC, 40).set(kD, 40).set(kB, 10).imm(kMov, 0).sys(kSend).sys(kExit);
    u32 talker_base = 0;
    const u32 t_talker = put(p1, talker, &talker_base);
    memcpy(p1.mem.bytes() + talker_base + 40, msg, 40);
    const u64 talker_uid = uid_of(p1, t_talker);

    for (u32 t = 0; t < 20; ++t)
        w.tick();
    // K's copy, C, lies after it, and both wait; so does every receiver.
    u32 t_c = ~0u;
    for (u32 t = 0; t < p0.os.thread_top(); ++t)
        if (p0.os.thread(t).state == ThreadState::waiting_recv && t != t_own && t != t_other && t != t_stack &&
            t != t_k)
            t_c = t;
    CHECK(t_c != ~0u && p0.os.live_programs() == 7);
    const u32 c_base = k_base + kSlot;
    CHECK(t_c != ~0u && p0.mem.record(p0.mem.alloc_at(c_base)).holder == p0.os.thread(t_c).program);
    CHECK(p0.mem.alloc_at(c_base + kSlot) == 0); // free memory after it
    const u32 ts[4] = {t_own, t_other, t_stack, t_k};
    for (u32 i = 0; i < 4; ++i)
        CHECK(p0.os.thread(ts[i]).state == ThreadState::waiting_recv);
    // The calls so far, as counted at each place: place 0's seven RECVs wait.
    CHECK(p0.os.calls[kRecv] == 5 && p0.os.calls_ok[kRecv] == 0 && p0.os.calls[kAlloc] == 1 &&
          p0.os.calls_ok[kAlloc] == 1 && p0.os.calls[kSpawn] == 1 && p0.os.calls_ok[kSpawn] == 1);
    const u64 want_calls[kCallCount] = {1, 1, 0, 0, 1, 2, 1, 0, 0, 0, 1, 3, 0, 0, 0, 1};
    const u64 want_ok[kCallCount] = {1, 0, 0, 0, 1, 2, 1, 0, 0, 0, 1, 2, 0, 0, 0, 1};
    for (u32 k = 0; k < kCallCount; ++k)
        CHECK(p1.os.calls[k] == want_calls[k] && p1.os.calls_ok[k] == want_ok[k]);
    CHECK(p1.os.sends_other == 2 && p1.os.sends_other_ok == 2);
    // T is dead, and its message waits at place 0.
    CHECK(obs.program(talker_uid) == nullptr && p0.os.inbox_count() == 1 && p0.os.inbox_sender(0) == talker_uid);

    // The messages, by hand: S's to each, but one of the two on port 5 from
    // a program nobody knows; and 6 bytes, which fit, to a receiver placed
    // now, once it waits. Placed with it, a receiver on port 10, which
    // takes T's message at once: an overrun into free memory, from a dead
    // sender whose lineage is still known.
    Code fits;
    fits.addr(kB, kSlot - 8).recv(8);
    const u32 spacer = p0.mem.alloc(1, kSlot, 2, 0); // so the allocation after C stays free
    const u32 t_short = put(p0, fits, &short_base);
    Code tenth;
    tenth.addr(kB, kSlot - 8).recv(10);
    u32 tenth_base = 0;
    put(p0, tenth, &tenth_base);
    CHECK(spacer == c_base + kSlot && short_base == c_base + 2 * kSlot && p0.mem.free(1, spacer));
    CHECK(p0.mem.alloc_at(tenth_base + kSlot) == 0);
    for (u32 t = 0; t < 5; ++t)
        w.tick();
    CHECK(p0.os.thread(t_short).state == ThreadState::waiting_recv);
    CHECK(obs.recvs == 1 && obs.overruns == 1 && obs.overruns_unknown == 0 && obs.reached[kReachFree] == 1 &&
          obs.watched_overruns() == 1 && p0.os.inbox_count() == 0);
    p0.os.deliver(3, msg, 40, 1, sender);
    p0.os.deliver(4, msg, 40, 1, sender);
    p0.os.deliver(5, msg, 40, 1, sender);
    p0.os.deliver(5, msg, 40, 1, 0xDEADull);
    p0.os.deliver(7, msg, 40, 1, sender);
    p0.os.deliver(8, msg, 6, 1, sender);
    CHECK(p0.os.recvs.size() == 6 && p0.os.overruns.size() == 5);
    w.tick(); // the observers take the facts in
    CHECK(obs.recvs == 7 && obs.overruns == 6 && obs.overruns_unknown == 1);
    CHECK(obs.reached[kReachOwn] == 1 && obs.reached[kReachKin] == 1 && obs.reached[kReachOther] == 1 &&
          obs.reached[kReachStack] == 1 && obs.reached[kReachFree] == 2);
    CHECK(obs.watched_overruns() == 5); // S's four and T's; not the stranger's
    CHECK(memcmp(p0.mem.bytes() + v_base, G, kPast) == 0 && memcmp(p0.mem.bytes() + extra, G, kPast) == 0);
    CHECK(p0.os.calls_ok[kRecv] == 7 && p0.os.calls[kRecv] == 7);
    const LineageRec* Lk = obs.lineage(uid_of(p0, t_k));
    CHECK(Lk && Lk->recvs == 2 && Lk->alive == 2 && obs.reads_input(*Lk, w.tick_count()));
    const LineageRec* Lv = obs.lineage(uid_of(p0, t_v));
    CHECK(Lv && Lv->recvs == 0 && !obs.reads_input(*Lv, w.tick_count()));

    // V wakes in G and copies it, again and again. Sample by sample, its
    // thread is found there; and the programs it makes from then on are of
    // the sender's kind.
    const u64 v_uid = uid_of(p0, t_v);
    u64 samples_after = 0, found = 0;
    for (u32 t = 0; t < 64; ++t) {
        w.tick();
        if (w.tick_count() % os.sample_every == 0) {
            const Sample s = obs.samples.back();
            ++samples_after;
            found += s.injections;
            CHECK(s.injections == 1 && s.recvs == 7 && s.overruns == 6 && s.reached[kReachStack] == 1);
            CHECK(s.calls[kRecv] == 7 && s.calls_ok[kRecv] == 7 && s.sends_other == 2 && s.sends_other_ok == 2);
            CHECK(s.readers == 6 && s.reader_programs == 7); // every receiver's lineage, K's with two programs
        }
    }
    CHECK(samples_after >= 6 && found == samples_after && obs.injections_seen == found);
    CHECK(obs.injected_programs == 1); // found at every sample, one program
    const ProgramRec* vr = obs.program(v_uid);
    const ProgramRec* sr = obs.program(sender);
    CHECK(vr && sr && sr->genome != 0 && vr->origin != sr->origin);
    u64 made = 0; // V's own children, by the facts: each with the sender's genome
    for (usize i = 0; i < obs.births.size(); ++i)
        if (obs.births[i].parent_uid == v_uid) {
            ++made;
            CHECK(obs.births[i].genome_hash == sr->genome);
        }
    CHECK(made >= 2 && obs.injection_births >= 1 && obs.injection_births <= made);
    CHECK(obs.samples.back().injection_births == obs.injection_births);
    // Nothing wrote over the five, and no reader has had a child since it first read.
    CHECK(obs.watched_overruns() == 5 && obs.reader_births == 0);

    // A message nobody waits for joins its port's inbox, and its sender's
    // name is kept beside the world: a world loaded from a save, with its
    // counters, gives the name to the RECV that takes the message; without
    // the counters, the message is nobody's.
    p0.os.deliver(9, msg, 40, 1, sender);
    ByteWriter snap, counters;
    w.save(snap);
    w.save_counters(counters);
    for (u32 pass = 0; pass < 2; ++pass) {
        World b;
        Config cfg2;
        CHECK(build(b, cfg2, kPacked));
        ByteReader in(snap.data(), snap.size());
        CHECK(b.load(in));
        Observer fresh; // it knows none of the programs: a named sender is one it can't place
        ObserverSettings fs;
        fs.sample_every = 10;
        fresh.init(fs);
        b.watch(&fresh);
        if (pass == 0) {
            ByteReader cin(counters.data(), counters.size());
            CHECK(b.load_counters(cin));
            CHECK(b.place(0).os.calls[kRecv] == 7 && b.place(1).os.sends_other == 2);
        }
        b.start_threads(2);
        Code late;
        late.addr(kB, kSlot - 8).recv(9);
        u32 late_base = 0;
        put(b.place(0), late, &late_base);
        // The place is full of V's line by now: its turn may take a while.
        for (u32 t = 0; t < 4000 && fresh.recvs == 0; ++t)
            b.tick();
        CHECK(fresh.recvs == 1 && fresh.overruns_unknown == (pass == 0 ? 1u : 0u));
    }
    printf("  gate5: five RECVs given 40 bytes for a buffer of 8 counted as overruns into the reader's own memory, "
           "its kin's, another lineage's, a stack and free memory, a sixth that fit as none, and a seventh from a "
           "sender already dead as one from a known lineage; the sleeper whose code one wrote over was found "
           "running it at each of %llu samples, and made %llu programs with the sender's genome, %llu of them "
           "counted as born of an injection; a message waiting in an inbox "
           "kept its sender's name across a save; and a program's calls were counted by kind as it made them\n",
           static_cast<unsigned long long>(samples_after), static_cast<unsigned long long>(made),
           static_cast<unsigned long long>(obs.injection_births));
}

} // namespace

void suite_gate5() {
    test_eras_by_hand();
    test_leads_world();
    test_one_program_world();
    test_reading_input();
}
