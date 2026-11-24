// The `nova2` command line (build-plan-gate1.md steps 1 and 9).
//
//   nova2 run [config] [--set key=value]... [--save path]
//   nova2 resume <snapshot> [--set run.key=value]... [--save path]
//   nova2 check <snapshot>...                    saved worlds through the engine's checks
//   nova2 sizes <snapshot>...                    where the bytes of a save go, world or test, by part
//   nova2 origin [config] [--set key=value]...   the origin test (10.6)
//   nova2 samples [config] [--set key=value]...  a run's samples as CSVs, from its records (origin.records)
//   nova2 bench [config] [--set key=value]...    the first benchmark
//   nova2 busy [config] [--set key=value]...     the origin test's loop on a busy test world (R7), to profile
//   nova2 trace <hex> [config] [options]...      one genome alone (trace.cpp)
//   nova2 trace --snapshot <file> [options]...   a place from a place snapshot
//   nova2 version | help
//
// run builds a world from the settings (the config file, then any --set) and
// runs run.ticks ticks on run.threads host threads; resume continues a saved
// world for run.ticks more. Both print what happened and the world's hash.
#include <cstdio>  // printf, fprintf
#include <cstring> // strcmp, strchr, strncmp

#include "nova2/config.h"
#include "nova2/io.h"
#include "nova2/keys.h"
#include "nova2/observe.h"
#include "nova2/pool.h"
#include "nova2/snapshot.h"
#include "run_view.h"
#include "nova2/world.h"

#include "commands.h"

using namespace nova2;

namespace {

const char* const kVersion = "0.1.0";

void print_usage(FILE* out) {
    fprintf(out,
            "nova2 %s\n"
            "usage:\n"
            "  nova2 run [config] [--set key=value]... [--save path]\n"
            "  nova2 resume <snapshot> [--set run.key=value]... [--save path]\n"
            "  nova2 check <snapshot>...                    a saved world's places through the engine's checks\n"
            "  nova2 sizes <snapshot>...                    where the bytes of a save go, world or test, by part\n"
            "  nova2 origin [config] [--set key=value]...   the origin test\n"
            "  nova2 samples [config] [--set key=value]...  a run's samples as CSVs, from its records\n"
            "  nova2 genomes <records folder> <seed>        a seed's kept genomes read whole and checked\n"
            "  nova2 watch [pid]                            opens the window of an origin run that is going\n"
            "  nova2 save [pid]                             has an origin run's seeds save their worlds now\n"
            "  nova2 bench [config] [--set key=value]...    the first benchmark\n"
            "  nova2 busy [config] [--set key=value]...     the origin test's loop on a busy test world, to profile\n"
            "  nova2 taker [config] [--set key=value]...    9.4's test world: a memory-taker in every place\n"
            "  nova2 worlds [config] [--set key=value]...   gate 3's test worlds at full size (worlds.test)\n"
            "  nova2 trace <hex> [config] [--page N] [--strict] [--zero] [--at N] [--copies N]\n"
            "              [--a N] [--steps N] [--turn N] [--quiet] [--set key=value]...\n"
            "                                               one genome alone, instruction by instruction\n"
            "  nova2 trace --snapshot <file> [--place P] [--list] [--program U] [--wake] [--steps N]\n"
            "              [--turn N] [--quiet]             a place from a snapshot (origin.snapshots)\n"
            "  nova2 version\n"
            "  nova2 help\n",
            kVersion);
}

struct Args {
    const char* file = nullptr; // the config (run) or snapshot (resume)
    const char* save = nullptr;
    const char* sets[64];
    u32 set_count = 0;
};

bool parse_args(int argc, char** argv, Args* a) {
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--set") == 0 && i + 1 < argc && a->set_count < 64) {
            a->sets[a->set_count++] = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            a->save = argv[++i];
        } else if (argv[i][0] != '-' && !a->file) {
            a->file = argv[i];
        } else {
            fprintf(stderr, "nova2: don't understand '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

bool apply_sets(Config& cfg, const Args& a, bool run_keys_only) {
    for (u32 i = 0; i < a.set_count; ++i) {
        const char* eq = strchr(a.sets[i], '=');
        if (!eq || eq == a.sets[i]) {
            fprintf(stderr, "nova2: --set wants key=value, not '%s'\n", a.sets[i]);
            return false;
        }
        const Str key(a.sets[i], static_cast<usize>(eq - a.sets[i]));
        if (run_keys_only && !(key.n > 4 && strncmp(key.p, "run.", 4) == 0)) {
            fprintf(stderr, "nova2: a resumed world keeps its settings; only run.* can change\n");
            return false;
        }
        cfg.set(key, Str(eq + 1));
    }
    return true;
}

// Runs the world for run.ticks and reports. 0 on success.
int run_world(World& w, Config& cfg, const Args& a) {
    const u64 ticks = key_u64(cfg, "run.ticks");
    const u32 threads = key_u32(cfg, "run.threads");
    Str unused;
    if (cfg.first_unused(&unused)) {
        fprintf(stderr, "nova2: unknown setting '%.*s'\n", static_cast<int>(unused.n), unused.p);
        return 2;
    }
    if (!cfg.ok()) {
        fprintf(stderr, "nova2: %s\n", cfg.value_error());
        return 2;
    }
    w.start_threads(threads);
    const double t0 = clock_seconds();
    for (u64 t = 0; t < ticks; ++t)
        w.tick();
    const double secs = clock_seconds() - t0;

    u64 instr = 0, idle_instr = 0, births = 0, deaths = 0, programs = 0, threads_live = 0, started = 0, faulted = 0,
        capped = 0, strict = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        instr += p.sched.instructions;
        idle_instr += p.region.instructions;
        births += p.births;
        deaths += p.deaths;
        programs += p.os.live_programs();
        threads_live += p.os.live_threads();
        started += p.region.started;
        faulted += p.region.faulted;
        capped += p.region.capped;
        strict += p.mem.strict() ? 1 : 0;
    }
    const u64 all = instr + idle_instr;
    printf("tick %llu  places %u (%llu strict)  host threads %u\n", static_cast<unsigned long long>(w.tick_count()),
           w.place_count(), static_cast<unsigned long long>(strict), w.pool().threads());
    TextBuf map_line;
    w.map().describe(&map_line);
    printf("%s\n", map_line.c_str());
    if (w.map().placed()) {
        TextBuf power_line;
        w.map().describe_power(&power_line);
        printf("%s\n", power_line.c_str());
    }
    printf("programs %llu  threads %llu  births %llu  deaths %llu\n", static_cast<unsigned long long>(programs),
           static_cast<unsigned long long>(threads_live), static_cast<unsigned long long>(births),
           static_cast<unsigned long long>(deaths));
    printf("idle runs %llu  faulted %llu  capped %llu\n", static_cast<unsigned long long>(started),
           static_cast<unsigned long long>(faulted), static_cast<unsigned long long>(capped));
    printf("instructions %llu (%llu in idle runs)  %.2f s  %.1f M/s\n", static_cast<unsigned long long>(all),
           static_cast<unsigned long long>(idle_instr), secs, secs > 0 ? static_cast<double>(all) / secs / 1e6 : 0.0);
    printf("hash %016llx\n", static_cast<unsigned long long>(w.hash()));

    if (a.save) {
        TextBuf text;
        cfg.write(text);
        ByteWriter out;
        snapshot_begin(w.seed(), w.tick_count(), text.view(), out);
        w.save(out);
        snapshot_finish(out);
        if (!write_file(a.save, out.data(), out.size())) {
            fprintf(stderr, "nova2: cannot write %s\n", a.save);
            return 1;
        }
        printf("saved %s (%llu bytes)\n", a.save, static_cast<unsigned long long>(out.size()));
    }
    return 0;
}

int cmd_run(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, &a))
        return 2;
    Config cfg;
    Config::ParseError pe;
    if (a.file && !cfg.load_file(a.file, &pe)) {
        fprintf(stderr, "nova2: %s: line %u: %s\n", a.file, pe.line, pe.msg);
        return 2;
    }
    if (!apply_sets(cfg, a, false))
        return 2;
    World w;
    TextBuf why;
    if (!w.init(cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg.ok() ? why.c_str() : cfg.value_error());
        return 2;
    }
    return run_world(w, cfg, a);
}

// A config file and --set lines, for the commands that build their own worlds.
bool settings_for(int argc, char** argv, Config* cfg) {
    Args a;
    if (!parse_args(argc, argv, &a) || a.save) {
        fprintf(stderr, "nova2: %s takes a config and --set lines\n", argv[1]);
        return false;
    }
    Config::ParseError pe;
    if (a.file && !cfg->load_file(a.file, &pe)) {
        fprintf(stderr, "nova2: %s: line %u: %s\n", a.file, pe.line, pe.msg);
        return false;
    }
    return apply_sets(*cfg, a, false);
}


// A saved world, read and built. False with the reason printed.
bool open_world(const char* file, const Args& a, World* w, Config* cfg) {
    Array<u8> bytes;
    if (!read_file(file, &bytes)) {
        fprintf(stderr, "nova2: cannot read %s\n", file);
        return false;
    }
    SnapshotHeader h;
    TextBuf err;
    if (!snapshot_parse(bytes.data(), bytes.size(), &h, &err)) {
        fprintf(stderr, "nova2: %s: %s\n", file, err.c_str());
        return false;
    }
    if (h.flags == kSnapshotPlace) {
        fprintf(stderr, "nova2: %s holds one place, not a world: read it with nova2 trace --snapshot\n", file);
        return false;
    }
    Config::ParseError pe;
    if (!snapshot_config(h, cfg, &pe)) {
        fprintf(stderr, "nova2: the snapshot's settings: line %u: %s\n", pe.line, pe.msg);
        return false;
    }
    if (!apply_sets(*cfg, a, true))
        return false;
    key_accept_known(*cfg); // a save of a seed of the origin test carries that test's settings, checked when it began
    TextBuf why;
    if (!w->init(*cfg, &why)) {
        fprintf(stderr, "nova2: %s\n", cfg->ok() ? why.c_str() : cfg->value_error());
        return false;
    }
    ByteReader in(bytes.data() + h.world_offset, h.world_size);
    if (!w->load(in, h.version) || w->tick_count() != h.tick) {
        fprintf(stderr, "nova2: %s: the world section doesn't match its settings\n", file);
        return false;
    }
    return true;
}

int cmd_resume(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, &a) || !a.file) {
        fprintf(stderr, "nova2: resume needs a snapshot\n");
        return 2;
    }
    Config cfg;
    World w;
    if (!open_world(a.file, a, &w, &cfg))
        return 2;
    return run_world(w, cfg, a);
}

// nova2 check <world save>...: every place of each saved world through the
// engine's own checks (Place::check: the memory's tables, the OS's, the reset
// region's, and that every allocation's holder is alive), which a run never
// calls. It also counts the threads whose stack is no longer an allocation of
// their program (given away at SPAWN, or freed): allowed, and counted because
// the OS still keeps such a thread's registers at that address.
int cmd_check(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "nova2: check needs a world's save\n");
        return 2;
    }
    int bad = 0;
    for (int f = 2; f < argc; ++f) {
        Args a;
        Config cfg;
        World w;
        if (!open_world(argv[f], a, &w, &cfg)) {
            ++bad;
            continue;
        }
        u64 programs = 0, threads = 0, strays = 0;
        u32 broken = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            const Place& p = w.place(i);
            TextBuf why;
            if (!p.check(&why)) {
                ++broken;
                printf("%s: place %u: %s\n", argv[f], i, why.c_str());
            }
            programs += p.os.live_programs();
            threads += p.os.live_threads();
            for (u32 t = 0; t < p.os.thread_top(); ++t) {
                const Thread& th = p.os.thread(t);
                if (th.state == ThreadState::free)
                    continue;
                const u32 id = p.mem.alloc_at(th.stack);
                if (!id || p.mem.record(id).base != th.stack || p.mem.record(id).holder != th.program)
                    ++strays;
            }
        }
        printf("%s: tick %llu, %u places, %llu programs, %llu threads: %s; %llu threads whose stack isn't theirs\n",
               argv[f], static_cast<unsigned long long>(w.tick_count()), w.place_count(),
               static_cast<unsigned long long>(programs), static_cast<unsigned long long>(threads),
               broken ? "BROKEN" : "every place passes its checks", static_cast<unsigned long long>(strays));
        bad += broken ? 1 : 0;
    }
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------------------
// nova2 sizes, for a test's save (seed-S-tick-T.test.snap): the bytes each
// part of it takes
// ---------------------------------------------------------------------------

// The parts of a test's save (origin.cpp's save_seed and Observer::save), in
// the order they are written. Most describe themselves: a table is its
// length, a row's size and its rows; a map is its count and three tables; a
// record stream is whether its rows are in a file, a row's size, how many
// rows, and the rows themselves if they are in the save. The blocks of plain
// numbers between them have changed length from build to build, and some
// parts came later, so the walk tries each length, and each later part there
// or not, until the whole file is accounted for to its last byte. So it
// reads the saves of earlier builds too, which nothing else here does.
enum class PartKind : u8 { plain, table, map, stream, result };
struct Part {
    PartKind kind;
    const char* name;
    u32 least, most; // plain: its 8-byte words, from least to most
    bool later;      // a part some builds' saves don't have
    u32 row = 0;     // a table: the size its rows must have (0: any), to tell two tables apart
};

const Part kTestParts[] = {
    {PartKind::plain, "settings, totals, counts by cause", 12, 48, false},
    {PartKind::table, "births, one by one", 0, 0, false},
    {PartKind::table, "deaths, one by one", 0, 0, false},
    {PartKind::stream, "samples", 0, 0, false},
    {PartKind::table, "turnover: each place's open window", 0, 0, false},
    {PartKind::stream, "turnover windows", 0, 0, false},
    {PartKind::stream, "places' voltages, a sample each", 0, 0, false},
    {PartKind::table, "voltages: the sums being gathered", 0, 0, false},
    {PartKind::map, "programs alive or remembered: index", 0, 0, false},
    {PartKind::table, "programs alive or remembered: records", 0, 0, false},
    {PartKind::table, "programs: free slots", 0, 0, false},
    {PartKind::map, "dead senders", 0, 0, false},
    {PartKind::table, "boot blocks' writers", 0, 0, false},
    {PartKind::map, "lineages: index", 0, 0, false},
    {PartKind::table, "lineages: records", 0, 0, false},
    {PartKind::map, "lineages: the groups each has had", 0, 0, false},
    {PartKind::table, "colonies", 0, 0, false},
    {PartKind::table, "colonies' clocks", 0, 0, false},
    {PartKind::map, "genomes: index", 0, 0, false},
    {PartKind::table, "genomes: records", 0, 0, false},
    {PartKind::table, "genomes: their bytes, kept and held", 0, 0, false},
    {PartKind::table, "groups", 0, 0, false},
    // The groups' index: in the saves made before 2026-11-20's storage
    // changes. Since then a load works it out again, and a save holds only
    // the runs of the groups whose first genome was let go.
    {PartKind::table, "groups' index: 3-byte runs", 0, 0, true, 8},
    {PartKind::map, "groups' index: lists by run", 0, 0, true},
    {PartKind::table, "groups' index: the lists' entries", 0, 0, true, 4},
    {PartKind::table, "groups' index: genomes too short", 0, 0, true, 4},
    {PartKind::table, "groups whose first genome is gone: runs", 0, 0, true, 1},
    {PartKind::table, "genomes: free slots", 0, 0, false, 4},
    {PartKind::plain, "genebank's counts", 0, 8, false},
    {PartKind::stream, "genebank: a row a kept genome", 0, 0, false},
    {PartKind::stream, "genebank: the kept genomes' bytes", 0, 0, false},
    {PartKind::table, "lineages: free slots", 0, 0, true},
    {PartKind::stream, "finished lineages", 0, 0, true},
    {PartKind::plain, "shallow lineages' tally", 0, 2, false},
    {PartKind::table, "ground: each place's open window", 0, 0, true},
    {PartKind::stream, "ground windows", 0, 0, true},
    {PartKind::plain, "gate 5's totals", 0, 15, false},
    {PartKind::table, "overruns watched", 0, 0, true},
    {PartKind::table, "overruns watched: their bytes", 0, 0, true},
    {PartKind::map, "programs found injected", 0, 0, true},
    {PartKind::map, "dead senders to comms ports", 0, 0, true},
    {PartKind::table, "the latest sample", 0, 0, true},
    {PartKind::result, "the test's own record of the seed", 0, 0, false},
    {PartKind::table, "stage clocks", 0, 0, false},
    {PartKind::table, "colonies' settling clocks", 0, 0, false},
    {PartKind::plain, "the seed's running counts", 4, 4, false},
};
constexpr u32 kTestPartCount = sizeof kTestParts / sizeof kTestParts[0];

struct TestWalk {
    FILE* f;
    u64 end;                    // where the parts end: the file's size less its hash
    u64 result_bytes;           // the size of the test's own record, from the header
    u64 bytes[kTestPartCount];  // each part's bytes in the save
    u64 rows[kTestPartCount];   // a table's or stream's rows
    u64 row[kTestPartCount];    // and a row's size
    u64 filed[kTestPartCount];  // a stream's bytes in its own file, not in the save
    bool there[kTestPartCount]; // false: a later part this save doesn't have
};

bool words_at(FILE* f, u64 pos, u64* out, u32 n) {
    return _fseeki64(f, static_cast<long long>(pos), SEEK_SET) == 0 && fread(out, 8, n, f) == n;
}

// A table at `pos`: its bytes with its header, or 0 if none fits there.
u64 table_at(const TestWalk& t, u64 pos, u64* rows, u64* row) {
    u64 h[2];
    if (pos + 16 > t.end || !words_at(t.f, pos, h, 2) || h[1] == 0 || h[1] > 65536 || h[0] > (t.end - pos - 16) / h[1])
        return 0;
    *rows = h[0];
    *row = h[1];
    return 16 + h[0] * h[1];
}

bool walk_from(TestWalk& t, u64 pos, u32 i) {
    if (i == kTestPartCount)
        return pos == t.end;
    const Part& p = kTestParts[i];
    t.there[i] = true;
    t.rows[i] = t.row[i] = t.filed[i] = 0;
    if (p.kind == PartKind::plain) {
        for (u32 n = p.least; n <= p.most; ++n) {
            t.bytes[i] = 8ull * n;
            if (pos + t.bytes[i] <= t.end && walk_from(t, pos + t.bytes[i], i + 1))
                return true;
        }
        return false;
    }
    u64 n = 0;
    if (p.kind == PartKind::table) {
        n = table_at(t, pos, &t.rows[i], &t.row[i]);
        if (p.row && t.row[i] != p.row)
            n = 0;
    } else if (p.kind == PartKind::map) {
        // Its count, then its keys, values and used marks.
        u64 at = pos + 8, rows = 0, row = 0;
        bool ok = pos + 8 <= t.end && words_at(t.f, pos, &t.rows[i], 1);
        for (u32 k = 0; ok && k < 3; ++k) {
            const u64 b = table_at(t, at, &rows, &row);
            ok = b != 0;
            at += b;
            t.row[i] += row; // a slot's bytes: key, value and mark
        }
        n = ok ? at - pos : 0;
    } else if (p.kind == PartKind::stream) {
        u64 h[3];
        if (pos + 24 <= t.end && words_at(t.f, pos, h, 3) && h[0] <= 1 && h[1] != 0 && h[1] <= 65536) {
            t.rows[i] = h[2];
            t.row[i] = h[1];
            if (h[0]) {
                n = 24;
                t.filed[i] = h[1] * h[2];
            } else if (h[2] <= (t.end - pos - 24) / h[1]) {
                n = 24 + h[1] * h[2];
            }
        }
    } else {
        n = pos + t.result_bytes <= t.end ? t.result_bytes : 0;
    }
    t.bytes[i] = n;
    if (n && walk_from(t, pos + n, i + 1))
        return true;
    if (p.later) { // an older save, without it
        t.there[i] = false;
        t.bytes[i] = 0;
        return walk_from(t, pos, i + 1);
    }
    return false;
}

// A table's rows read from the save into `out` (its header passed over).
template <class T> bool read_part(FILE* f, u64 pos, u64 rows, u64 row, Array<T>* out) {
    if (row != sizeof(T))
        return false;
    out->resize_unfilled(static_cast<usize>(rows));
    if (_fseeki64(f, static_cast<long long>(pos + 16), SEEK_SET) != 0)
        return false;
    u8* at = reinterpret_cast<u8*>(out->data());
    u64 left = rows * row;
    while (left) { // fread's count is a size_t, but a read of gigabytes at once has failed on some systems
        const usize n = left > (256u << 20) ? (256u << 20) : static_cast<usize>(left);
        if (fread(at, 1, n, f) != n)
            return false;
        at += n;
        left -= n;
    }
    return true;
}

// nova2 sizes <test save> --index: the time a load takes to work the groups'
// index out again (build-plan-gate5.md 11), measured on a save of any build
// by doing it: the genomes, their bytes and the groups are read from the
// save and the index is built from them as Observer::load builds it. A save
// from before 2026-11-20 holds the index too, so there the one built is set
// beside the one saved, the runs and the lists' entries byte for byte.
int index_time(const TestWalk& t, u64 start, const char* path) {
    u32 at_gen = 0, at_bank = 0, at_groups = 0, at_runs = 0, at_lists = 0;
    u64 pos[kTestPartCount];
    u64 p = start;
    for (u32 i = 0; i < kTestPartCount; ++i) {
        pos[i] = p;
        p += t.there[i] ? t.bytes[i] : 0;
        const char* name = kTestParts[i].name;
        if (strcmp(name, "genomes: records") == 0)
            at_gen = i;
        else if (strcmp(name, "genomes: their bytes, kept and held") == 0)
            at_bank = i;
        else if (strcmp(name, "groups") == 0)
            at_groups = i;
        else if (strcmp(name, "groups' index: 3-byte runs") == 0)
            at_runs = i;
        else if (strcmp(name, "groups' index: the lists' entries") == 0)
            at_lists = i;
    }
    FILE* f = fopen(path, "rb");
    if (!f)
        return 2;
    Array<GenomeRec> gen;
    Array<u8> bank;
    Array<GroupRec> groups;
    const double t0 = clock_seconds();
    if (!read_part(f, pos[at_gen], t.rows[at_gen], t.row[at_gen], &gen) ||
        !read_part(f, pos[at_bank], t.rows[at_bank], t.row[at_bank], &bank) ||
        !read_part(f, pos[at_groups], t.rows[at_groups], t.row[at_groups], &groups)) {
        fprintf(stderr, "nova2: its genomes and groups aren't laid out as this build's are\n");
        fclose(f);
        return 1;
    }
    const double t1 = clock_seconds();
    const u64 genomes = gen.size(), ngroups = groups.size();
    Observer o;
    ObserverSettings s;
    o.init(s);
    if (!o.index_from(gen, bank, groups)) {
        fprintf(stderr, "nova2: the groups' runs aren't their first genomes': the index can't be built from them\n");
        fclose(f);
        return 1;
    }
    const double t2 = clock_seconds();
    printf("the groups' index built again from %llu genomes in %llu groups: %.2f s (and %.2f s to read them from the "
           "save); %llu runs of the groups', %llu different runs listed, %llu words of lists: %.0f MB\n",
           static_cast<unsigned long long>(genomes), static_cast<unsigned long long>(ngroups), t2 - t1, t1 - t0,
           static_cast<unsigned long long>(o.group_runs().size()), static_cast<unsigned long long>(o.runs_listed()),
           static_cast<unsigned long long>(o.group_lists().size()),
           static_cast<double>(o.group_runs().size() * 8 + o.group_lists().size() * 4) / (1 << 20));
    int result = 0;
    if (t.there[at_runs] && t.there[at_lists]) {
        Array<u64> runs;
        Array<u32> lists;
        bool same = read_part(f, pos[at_runs], t.rows[at_runs], t.row[at_runs], &runs) &&
                    runs.size() == o.group_runs().size() &&
                    memcmp(runs.data(), o.group_runs().data(), runs.size() * sizeof(u64)) == 0;
        runs.clear();
        same = same && read_part(f, pos[at_lists], t.rows[at_lists], t.row[at_lists], &lists) &&
               lists.size() == o.group_lists().size() &&
               memcmp(lists.data(), o.group_lists().data(), lists.size() * sizeof(u32)) == 0;
        printf("the save holds the index too: the one built is %s\n",
               same ? "the same, the groups' runs and the lists' entries byte for byte" : "NOT THE SAME");
        result = same ? 0 : 1;
    }
    fclose(f);
    return result;
}

int test_sizes(const char* path, bool index) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "nova2: can't open %s\n", path);
        return 2;
    }
    TestWalk t = {};
    t.f = f;
    u64 head[5] = {};
    _fseeki64(f, 0, SEEK_END);
    const u64 size = static_cast<u64>(_ftelli64(f));
    // The header: the tag, the seed, the tick, the size of the test's own
    // record, and the world's counters with their length. The file ends
    // with its hash.
    if (size < 48 || !words_at(f, 0, head, 5) || head[0] != kTestSaveTag || head[4] > size - 48) {
        fprintf(stderr, "nova2: %s is not a test's save\n", path);
        fclose(f);
        return 2;
    }
    t.end = size - 8;
    t.result_bytes = head[3];
    const u64 start = 40 + head[4];
    char tag[9] = {};
    u64 obs_tag = 0;
    words_at(f, start, &obs_tag, 1);
    memcpy(tag, &obs_tag, 8);
    const bool ok = walk_from(t, start, 0);
    fclose(f);
    printf("%s: seed %llu at tick %llu, %llu bytes; the observers' memory is in format %s\n", path,
           static_cast<unsigned long long>(head[1]), static_cast<unsigned long long>(head[2]),
           static_cast<unsigned long long>(size), tag);
    if (!ok) {
        fprintf(stderr, "nova2: its parts can't be followed: it is not laid out as this build's saves are\n");
        return 1;
    }
    printf("  %-40s %14llu  %5.1f%%\n", "header and the world's counters", static_cast<unsigned long long>(start),
           100.0 * static_cast<double>(start) / static_cast<double>(size));
    for (u32 i = 0; i < kTestPartCount; ++i) {
        if (!t.there[i])
            continue;
        printf("  %-40s %14llu  %5.1f%%", kTestParts[i].name, static_cast<unsigned long long>(t.bytes[i]),
               100.0 * static_cast<double>(t.bytes[i]) / static_cast<double>(size));
        if (t.row[i])
            printf("  %llu rows of %llu bytes", static_cast<unsigned long long>(t.rows[i]),
                   static_cast<unsigned long long>(t.row[i]));
        if (t.filed[i])
            printf(", in its own file: %llu bytes there", static_cast<unsigned long long>(t.filed[i]));
        printf("\n");
    }
    return index ? index_time(t, start, path) : 0;
}

// nova2 sizes <world save>...: where the bytes of a save go, world or test, by part of
// the save as this build writes it (archive.h's marks), summed over the
// places, with the live records beside them; and the file's own size, in
// whatever form it was saved.
int cmd_sizes(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "nova2: sizes needs a world's save\n");
        return 2;
    }
    bool index = false; // --index: a test's save, and the time to build its groups' index again
    for (int f = 2; f < argc; ++f)
        index = index || strcmp(argv[f], "--index") == 0;
    for (int f = 2; f < argc; ++f) {
        if (strcmp(argv[f], "--index") == 0)
            continue;
        if (strstr(argv[f], ".test.snap")) { // a test's save: walked, not loaded
            const int r = test_sizes(argv[f], index);
            if (r)
                return r;
            continue;
        }
        Args a;
        Config cfg;
        World w;
        if (!open_world(argv[f], a, &w, &cfg))
            return 2;
        ArchiveSizes s;
        u64 total = 0, programs = 0, threads = 0, records = 0, bytes = 0;
        for (u32 i = 0; i < w.place_count(); ++i) {
            Place& p = w.place(i);
            ByteWriter out;
            Archive ar(&out);
            ar.sizes_to(&s);
            p.serialize(ar);
            total += out.size();
            programs += p.os.live_programs();
            threads += p.os.live_threads();
            bytes += p.mem.size();
            for (u32 r = 1; r < p.mem.record_capacity(); ++r)
                records += p.mem.record(r).holder ? 1 : 0;
        }
        {
            ByteWriter out;
            Archive ar(&out);
            ar.sizes_to(&s);
            w.net().serialize(ar);
            ar.mark("network: other");
            total += out.size();
        }
        if (w.powered()) {
            ByteWriter out;
            Archive ar(&out);
            ar.sizes_to(&s);
            w.grid().serialize(ar);
            ar.mark("grid: voltages");
            total += out.size();
        }
        printf("%s: tick %llu, %u places, %llu bytes of memory; %llu programs, %llu threads, %llu allocations live\n",
               argv[f], static_cast<unsigned long long>(w.tick_count()), w.place_count(),
               static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(programs),
               static_cast<unsigned long long>(threads), static_cast<unsigned long long>(records));
        printf("  saved now in format %u: %llu bytes, %.1f a place\n", kArchiveVersion,
               static_cast<unsigned long long>(total), static_cast<double>(total) / w.place_count());
        for (u32 i = 0; i < s.n; ++i)
            printf("  %-40s %14llu  %5.1f%%  %12.0f a place\n", s.name[i], static_cast<unsigned long long>(s.bytes[i]),
                   total ? 100.0 * static_cast<double>(s.bytes[i]) / static_cast<double>(total) : 0.0,
                   static_cast<double>(s.bytes[i]) / w.place_count());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "version") == 0) {
        printf("nova2 %s\n", kVersion);
        return 0;
    }
    if (strcmp(argv[1], "run") == 0)
        return cmd_run(argc, argv);
    if (strcmp(argv[1], "resume") == 0)
        return cmd_resume(argc, argv);
    if (strcmp(argv[1], "check") == 0)
        return cmd_check(argc, argv);
    if (strcmp(argv[1], "sizes") == 0)
        return cmd_sizes(argc, argv);
    if (strcmp(argv[1], "genomes") == 0)
        return cmd_genomes(argc, argv);
    if (strcmp(argv[1], "watch") == 0 || strcmp(argv[1], "save") == 0)
        return cmd_signal(argc, argv);
    if (strcmp(argv[1], "view") == 0)
        return cmd_view(argc, argv);
    if (strcmp(argv[1], "origin") == 0 || strcmp(argv[1], "bench") == 0 || strcmp(argv[1], "taker") == 0 ||
        strcmp(argv[1], "worlds") == 0 || strcmp(argv[1], "busy") == 0 || strcmp(argv[1], "samples") == 0) {
        Config cfg;
        if (!settings_for(argc, argv, &cfg))
            return 2;
        if (strcmp(argv[1], "samples") == 0)
            return cmd_samples(cfg);
        if (strcmp(argv[1], "busy") == 0)
            return cmd_origin(cfg, true);
        if (argv[1][0] == 't')
            return cmd_taker(cfg);
        if (argv[1][0] == 'w')
            return cmd_worlds(cfg);
        return argv[1][0] == 'o' ? cmd_origin(cfg) : cmd_bench(cfg);
    }
    if (strcmp(argv[1], "trace") == 0) {
        TraceOptions t;
        char* rest[64];
        int rest_argc = 0;
        if (!trace_options(argc, argv, &t, &rest_argc, rest))
            return 2;
        Config cfg;
        if (!settings_for(rest_argc, rest, &cfg))
            return 2;
        return cmd_trace(cfg, t);
    }
    fprintf(stderr, "nova2: unknown command '%s'\n", argv[1]);
    print_usage(stderr);
    return 2;
}
