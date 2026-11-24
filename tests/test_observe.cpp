// The observers (nova2/observe.h; design.md 13; build-plan-gate1.md step 10):
// the facts log reconstructing the family tree exactly; generation depth and
// lineages; the Replicator detector's stages; genome groups and similarity;
// Scavenger and cross-owner execution; samples; and watching changing nothing.
#include <cstring>

#include "check.h"
#include "nova2/hash.h"
#include "nova2/hashmap.h"
#include "nova2/io.h"
#include "nova2/observe.h"
#include "nova2/world.h"

using namespace nova2;

namespace {

const char* const kSmall = "world.places = 4\n"
                           "memory.size = 32768\n"
                           "disk.size = 65536\n"
                           "hw.cores.max = 4\n"
                           "os.strict_share = 0\n"
                           "region.retry = 3\n"
                           "noise.p_floor = 1e-4\n"
                           "noise.e_floor = 1e-4\n";

// A.5's 14-byte replicator on a lax place (test_os.cpp traces it).
const u8 kLax[14] = {encode(kMov, kImm), 14,     encode(kSto, kB),   encode(kSys, kImm), kAlloc,
                     encode(kMov, kC),   encode(kSto, kB), encode(kCpy, kImm), 14,  encode(kMov, kD),
                     encode(kSys, kImm), kSpawn, encode(kJmp, kImm), static_cast<u8>(-12)};

bool build(World& w, Config& cfg, const char* extra = "", bool seed_life = true) {
    Config::ParseError pe;
    if (!cfg.parse(kSmall, &pe))
        return false;
    Config over; // the extra settings replace the small world's
    if (!over.parse(extra, &pe))
        return false;
    for (usize i = 0; i < over.count(); ++i)
        cfg.set(over.key(i), over.value(i));
    TextBuf why;
    if (!w.init(cfg, &why))
        return false;
    for (u32 i = 0; seed_life && i < w.place_count(); ++i) // a test world (R7)
        memcpy(w.place(i).mem.bytes() + w.place(i).mem.region_base(), kLax, 14);
    return true;
}

// Every birth's parent is born earlier in the log (or there is none), and
// walking the log gives each living program the observer's depth and lineage.
void test_family_tree() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(4);
    for (u32 t = 0; t < 40; ++t)
        w.tick();
    CHECK(obs.births.size() > 50 && obs.total_births == obs.births.size());
    HashMap<u32> depth;  // uid -> generation, from the log alone
    HashMap<u64> origin; // uid -> lineage origin, from the log alone
    bool ordered = true;
    for (usize i = 0; i < obs.births.size(); ++i) {
        const BirthFact& f = obs.births[i];
        const u32* pd = f.parent_uid ? depth.find(f.parent_uid) : nullptr;
        if (f.parent_uid && !pd)
            ordered = false;
        depth.get_or_insert(f.uid) = pd ? *pd + 1 : 0;
        origin.get_or_insert(f.uid) = pd ? *origin.find(f.parent_uid) : f.uid;
    }
    CHECK(ordered);
    u32 living = 0, agree = 0, deep = 0;
    for (usize i = 0; i < obs.births.size(); ++i) {
        const ProgramRec* r = obs.program(obs.births[i].uid);
        if (!r)
            continue; // dead since
        ++living;
        agree += *depth.find(r->uid) == r->depth && *origin.find(r->uid) == r->origin ? 1 : 0;
        deep += r->depth >= 3 ? 1 : 0;
    }
    CHECK(living > 0 && agree == living && deep > 0);
    // Deaths in the log are of programs born in it.
    bool known = true;
    for (usize i = 0; i < obs.deaths.size(); ++i)
        known = known && depth.contains(obs.deaths[i].uid);
    CHECK(known);
}

// The replicator's lineage: its origin is a birth from static, its copies are
// faithful, and it reaches stages 1 and 2 and several generations.
void test_lineages_and_stages() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(2);
    for (u32 t = 0; t < 40; ++t)
        w.tick();
    u32 replicating = 0, viable = 0, deep = 0, faithful_lines = 0;
    for (u32 i = 0; i < obs.lineage_count(); ++i) {
        const LineageRec& L = obs.lineage_at(i);
        if (L.first_copy != ~0ull) {
            ++replicating;
            CHECK(L.cause == BirthCause::static_bytes); // the region's copy started it
        }
        viable += L.first_viable != ~0ull ? 1 : 0;
        deep += L.max_depth >= 5 ? 1 : 0;
        // The faithful counts never pass the totals, and the replicator's own
        // copies make long faithful lines (the origin itself isn't faithful).
        CHECK(L.alive_faithful <= L.alive && L.max_fdepth <= L.max_depth && L.max_fdepth <= L.faithful);
        CHECK(L.alive_faithful <= L.alive_copies && L.alive_copies <= L.alive); // a faithful birth is a copy
        // Nothing arrives from elsewhere: every member is at its home, and the
        // lineage has no colony (13.3).
        CHECK(L.home_alive == L.alive && L.home_copies == L.alive_copies && L.home_faithful == L.alive_faithful);
        CHECK(L.colonies == 0 && L.first_colony == 0);
        faithful_lines += L.max_fdepth >= 4 && L.alive_faithful >= 1 ? 1 : 0;
    }
    CHECK(replicating >= 2 && viable >= 2 && deep >= 2 && faithful_lines >= 2);
    // The replicator's 14 bytes are a genome in the bank, born many times.
    const GenomeRec* g = nullptr;
    for (u32 i = 0; i < obs.genome_count(); ++i)
        if (obs.genome_at(i).len == 14 && memcmp(obs.genome_bytes(obs.genome_at(i)), kLax, 14) == 0)
            g = &obs.genome_at(i);
    CHECK(g != nullptr && g->count > 20);
    CHECK(obs.total_faithful > 20 && obs.total_faithful <= obs.total_births);
    CHECK(g->image_copies <= g->count && g->alloc_min == 14 && g->alloc_max == 14);
}

// A genome shorter than observe.min_genome is never faithful: the same world
// with the bar above the replicator's 14 bytes has copies but no faithful ones.
void test_min_genome() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    ObserverSettings os;
    os.min_genome = 15;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    for (u32 t = 0; t < 40; ++t)
        w.tick();
    CHECK(obs.total_births > 20 && obs.total_faithful == 0);
    for (u32 i = 0; i < obs.lineage_count(); ++i) {
        const LineageRec& L = obs.lineage_at(i);
        CHECK(L.first_copy == ~0ull && L.max_fdepth == 0 && L.alive_faithful == 0);
        CHECK(L.alive_copies == 0); // near copies need min_genome too
    }
}

// A birth into the allocation its parent was born in is a hand-over: the
// parent's own body, nothing copied. It carries the parent's written extent,
// so its genome matches, but it is never faithful, a copy, or on the ladder.
void test_handover() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    Place& p = w.place(0);
    const u32 run = 1;
    const u32 a = p.mem.alloc(run, 14, 2, 0);
    memcpy(p.mem.bytes() + a, kLax, 14);
    p.mem.note_write(a, 14, run);
    CHECK(p.os.place_by_hand(run, a, a, 0) != 0);
    const BirthFact parent = p.os.births[0];
    obs.ingest(p);
    p.os.clear_facts();
    const u64 written0 = obs.written; // the parent's own birth is on the ladder
    // The same block, the same bytes: a hand-over.
    BirthFact f = parent;
    f.uid = parent.uid + 1000;
    f.parent_uid = parent.uid;
    f.cause = BirthCause::thread;
    f.genome_at = static_cast<u32>(p.os.fact_bytes.size());
    p.os.fact_bytes.append(kLax, 14);
    p.os.births.push(f);
    obs.ingest(p);
    p.os.clear_facts();
    CHECK(obs.handovers == 1 && obs.total_faithful == 0 && obs.written == written0 && obs.image_copies == 0);
    const ProgramRec* r = obs.program(f.uid);
    CHECK(r && !r->faithful && !r->copy && r->depth == 1);
    CHECK(obs.lineage(parent.uid)->alive_copies == 0 && obs.lineage(parent.uid)->first_copy == ~0ull);
    // The same bytes in another block: a faithful copy.
    f.uid = parent.uid + 1001;
    f.base = parent.base + 1024;
    f.genome_at = static_cast<u32>(p.os.fact_bytes.size());
    p.os.fact_bytes.append(kLax, 14);
    p.os.births.push(f);
    obs.ingest(p);
    p.os.clear_facts();
    CHECK(obs.handovers == 1 && obs.total_faithful == 1 && obs.near_copies == 1 && obs.image_copies == 1);
    CHECK(obs.lineage(parent.uid)->alive_copies == 1);
}

void test_groups() {
    u8 a[14], b[14], c[14];
    memcpy(a, kLax, 14);
    memcpy(b, kLax, 14);
    b[9] ^= 0x40; // one byte changed: three of twelve runs go
    for (u32 i = 0; i < 14; ++i)
        c[i] = static_cast<u8>(200 + i);
    CHECK(Observer::similarity(a, 14, a, 14) == 1.0);
    const double near = Observer::similarity(a, 14, b, 14);
    CHECK(near > 0.74 && near < 0.76); // 9 / 12
    CHECK(Observer::similarity(a, 14, c, 14) == 0.0);
    CHECK(Observer::similarity(a, 2, a, 2) == 1.0 && Observer::similarity(a, 2, c, 2) == 0.0); // too short: equality
    // In a world, the replicator's genome group holds most of its births.
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    for (u32 t = 0; t < 30; ++t)
        w.tick();
    CHECK(obs.group_count() >= 1 && obs.group_count() <= obs.genome_count());
}

// The groups' index (observe.h) groups as greedy grouping's plain scan does:
// each new genome joins the first group, in order, whose reference shares
// enough of its distinct 3-byte runs, or starts a new one. Genomes are fed
// as births: mutants of earlier ones (bytes changed, put in and taken out),
// fresh ones, and some under 3 bytes, at four thresholds. The scan here is
// written apart from the observer's, and every genome's group must match.
u64 next_rand(u64* s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

u32 runs_of(const u8* g, u32 len, u32* out) { // distinct and sorted, out has len entries
    u32 n = 0;
    for (u32 i = 0; i + 3 <= len; ++i)
        out[n++] = static_cast<u32>(g[i]) | static_cast<u32>(g[i + 1]) << 8 | static_cast<u32>(g[i + 2]) << 16;
    for (u32 i = 1; i < n; ++i) // insertion sort: genomes here are short
        for (u32 j = i; j > 0 && out[j - 1] > out[j]; --j) {
            const u32 t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    u32 d = 0;
    for (u32 i = 0; i < n; ++i)
        if (d == 0 || out[d - 1] != out[i])
            out[d++] = out[i];
    return d;
}

// What the scan saw: distinct genomes, groups, and genomes that joined a
// group past the first.
struct GroupIndexSeen {
    u32 genomes = 0, groups = 0, joined_later = 0;
};

GroupIndexSeen group_index_at(double threshold, u32 births) {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    ObserverSettings s;
    s.group_threshold = threshold;
    s.keep_facts = false;
    s.keep_generations = 1; // every genome kept at its birth: the index sees each one
    Observer obs;
    obs.init(s);
    Place& p = w.place(0);
    const u32 kMax = 64;
    Array<u8> all;      // every genome fed, kMax bytes a slot
    Array<u32> lens;
    Array<u8> refs;     // the scan's groups' references, kMax bytes a slot
    Array<u32> ref_lens;
    Array<u32> expect;  // each new genome's group, by the scan
    u32 joined_later = 0;
    HashMap<u8> seen;
    u64 rng = 0x9E3779B97F4A7C15ull ^ static_cast<u64>(threshold * 1000);
    u8 g[kMax];
    u32 a[kMax], b[kMax];
    for (u32 i = 0; i < births; ++i) {
        u32 len;
        const u64 r = next_rand(&rng);
        if (lens.empty() || r % 16 == 0) { // fresh
            len = static_cast<u32>(next_rand(&rng) % kMax);
            for (u32 k = 0; k < len; ++k)
                g[k] = static_cast<u8>(next_rand(&rng));
        } else if (r % 16 == 1) { // under 3 bytes
            len = static_cast<u32>(next_rand(&rng) % 3);
            for (u32 k = 0; k < len; ++k)
                g[k] = static_cast<u8>(next_rand(&rng) % 4);
        } else { // a mutant of an earlier genome, often one of the last 8
            const u32 fed = static_cast<u32>(lens.size());
            const u32 back = fed < 8 ? fed : 8;
            const u32 from = r % 4 == 0 ? static_cast<u32>(next_rand(&rng) % fed)
                                        : fed - 1 - static_cast<u32>(next_rand(&rng) % back);
            len = lens[from];
            memcpy(g, all.data() + from * kMax, len);
            const u32 edits = 1 + static_cast<u32>(next_rand(&rng) % 4);
            for (u32 e = 0; e < edits; ++e) {
                const u32 kind = static_cast<u32>(next_rand(&rng) % 3);
                const u32 at = len ? static_cast<u32>(next_rand(&rng) % len) : 0;
                if (kind == 0 && len) {
                    g[at] = static_cast<u8>(next_rand(&rng));
                } else if (kind == 1 && len < kMax) {
                    memmove(g + at + 1, g + at, len - at);
                    g[at] = static_cast<u8>(next_rand(&rng));
                    ++len;
                } else if (len) {
                    memmove(g + at, g + at + 1, len - at - 1);
                    --len;
                }
            }
        }
        all.append(g, kMax);
        lens.push(len);
        const u64 h = hash_bytes(g, len, kGenomeDomain);
        // The scan, for a genome not seen before.
        bool fresh = false;
        seen.get_or_insert(h, &fresh);
        if (fresh) {
            const u32 n = runs_of(g, len, a);
            u32 k = 0;
            for (; k < ref_lens.size(); ++k) {
                const u8* ref = refs.data() + k * kMax;
                const u32 m = runs_of(ref, ref_lens[k], b);
                if (n == 0 || m == 0) {
                    if (n == 0 && m == 0 && ref_lens[k] == len && memcmp(ref, g, len) == 0)
                        break;
                    continue;
                }
                u32 c = 0;
                for (u32 x = 0, y = 0; x < n && y < m;) {
                    if (a[x] < b[y]) {
                        ++x;
                    } else if (a[x] > b[y]) {
                        ++y;
                    } else {
                        ++c;
                        ++x;
                        ++y;
                    }
                }
                if (static_cast<double>(c) >= threshold * (n > m ? n : m))
                    break;
            }
            if (k == ref_lens.size()) {
                refs.append(g, kMax);
                ref_lens.push(len);
            } else if (k > 0) {
                ++joined_later;
            }
            expect.push(k);
        }
        // The observer, as a birth with no parent.
        BirthFact f{};
        f.tick = i;
        f.uid = 1000 + i;
        f.cause = BirthCause::thread;
        f.size = len;
        f.genome_hash = h;
        f.image_hash = hash_bytes(g, len, 0x1A6Eull); // never a genome's: not a scavenger
        f.genome_at = static_cast<u32>(p.os.fact_bytes.size());
        f.genome_len = len;
        p.os.fact_bytes.append(g, len);
        p.os.births.push(f);
        obs.ingest(p);
        p.os.clear_facts();
    }
    CHECK(obs.genome_count() == expect.size() && obs.group_count() == ref_lens.size());
    u32 wrong = 0;
    for (u32 i = 0; i < expect.size() && i < obs.genome_count(); ++i)
        wrong += obs.genome_at(i).group != expect[i];
    CHECK(wrong == 0);
    GroupIndexSeen seen_by_scan;
    seen_by_scan.genomes = static_cast<u32>(expect.size());
    seen_by_scan.groups = static_cast<u32>(ref_lens.size());
    seen_by_scan.joined_later = joined_later;
    return seen_by_scan;
}

void test_group_index() {
    // The setting's default, and looser: many groups, and many genomes
    // joining a group past the first.
    GroupIndexSeen s = group_index_at(0.8, 4000);
    CHECK(s.groups > 100 && s.joined_later > 500);
    s = group_index_at(0.5, 3000);
    CHECK(s.groups > 20 && s.joined_later > 500);
    // Every run shared: the rarest run's list alone holds the candidates.
    s = group_index_at(1.0, 2000);
    CHECK(s.groups > 100);
    // One run shared: every run's list.
    s = group_index_at(0.01, 2000);
    CHECK(s.groups > 1 && s.joined_later > 0);
}

// A birth into a dead program's unchanged code is a Scavenger; a thread
// running in an allocation its program doesn't hold is cross-owner.
void test_scavenger_and_cross_owner() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Observer obs;
    ObserverSettings one;
    one.keep_generations = 1; // every genome kept at its birth: a dead program's is remembered
    obs.init(one);
    w.watch(&obs);
    Place& p = w.place(0);
    const u32 run = 1;
    const u32 a = p.mem.alloc(run, 14, 2, 0);
    memcpy(p.mem.bytes() + a, kLax, 14);
    p.mem.note_write(a, 14, run);
    const u32 id = p.os.place_by_hand(run, a, a, 0);
    CHECK(id != 0);
    obs.ingest(p);
    p.os.clear_facts();
    CHECK(obs.genome_count() == 1);
    u32 t = 0;
    while (p.os.thread(t).id != id)
        ++t;
    p.os.end_thread(t, DeathCause::exit); // it dies, its code left in place
    obs.ingest(p);
    p.os.clear_facts();
    const u32 b = p.mem.alloc(run, 14, 2, 0);
    CHECK(b == a); // first fit: the corpse's slot, bytes and all
    CHECK(p.os.place_by_hand(run, b, b, 0) != 0); // nothing written: no genome of its own
    obs.ingest(p);
    p.os.clear_facts();
    CHECK(obs.scavenger == 1 && obs.genome_count() == 1);

    // Cross-owner: its thread's IP moved into someone else's allocation.
    const u32 theirs = p.mem.alloc(2, 64, 2, 0);
    u32 t2 = 0;
    while (p.os.thread(t2).state == ThreadState::free)
        ++t2;
    u8* saved = p.mem.bytes() + p.os.thread(t2).stack + 16;
    for (int k = 0; k < 4; ++k)
        saved[k] = static_cast<u8>(theirs >> (8 * k));
    obs.sample(w);
    CHECK(obs.samples.size() == 1 && obs.samples[0].cross_owner == 1);
}

// Helpers for the scavenger test below: a lax replicator placed by hand, a
// program ended, and the facts handed to the observer.
u32 place_lax(Place& p, u32 run) {
    const u32 a = p.mem.alloc(run, 14, 2, 0);
    memcpy(p.mem.bytes() + a, kLax, 14);
    p.mem.note_write(a, 14, run);
    return a;
}

void end_program(Place& p, u32 id) {
    u32 t = 0;
    while (p.os.thread(t).id != id)
        ++t;
    p.os.end_thread(t, DeathCause::exit);
}

void ingest_clear(Observer& obs, Place& p) {
    obs.ingest(p);
    p.os.clear_facts();
}

// The scavenger, keeping genomes whose lines last 3 generations (13.2): a birth
// into a dead program's code is named only while the observers still hold
// its genome, here for another carrier alive; once no carrier is left, a
// birth into the same code is one into unknown leftovers.
void test_scavenger_kept() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Observer obs;
    obs.init(ObserverSettings{}); // keep_generations 3
    w.watch(&obs);
    Place& p = w.place(0);
    const u32 run = 1;
    const u32 a = place_lax(p, run);
    const u32 ida = p.os.place_by_hand(run, a, a, 0);
    const u32 c = place_lax(p, run);
    const u32 idc = p.os.place_by_hand(run, c, c, 0);
    CHECK(ida && idc);
    ingest_clear(obs, p);
    CHECK(obs.genomes_held() == 1 && obs.genomes_kept() == 0 && obs.group_count() == 0);
    end_program(p, ida); // one carrier dies; the other holds the genome
    ingest_clear(obs, p);
    const u32 b = p.mem.alloc(run, 14, 2, 0);
    CHECK(b == a);
    const u32 idb = p.os.place_by_hand(run, b, b, 0);
    ingest_clear(obs, p);
    CHECK(obs.scavenger == 1 && obs.genomes_held() == 1);
    end_program(p, idc); // every carrier gone: the genome is forgotten
    end_program(p, idb);
    ingest_clear(obs, p);
    CHECK(obs.genomes_held() == 0 && obs.genomes_kept() == 0);
    const u32 d = p.mem.alloc(run, 14, 2, 0);
    CHECK(d == a || d == c); // a corpse's slot, its bytes unchanged
    CHECK(memcmp(p.mem.bytes() + d, kLax, 14) == 0 && p.os.place_by_hand(run, d, d, 0) != 0);
    ingest_clear(obs, p);
    CHECK(obs.scavenger == 1); // not named: nothing remembers that code
}

// Births and deaths made up as facts, for the keeping tests: genome i is
// 16 bytes of its own, hashed as kKeepHash + i.
const u64 kKeepHash = 0x6E0A0000ull;

void keep_birth(Observer& obs, Place& p, u64 uid, u64 parent, u32 genome, BirthCause cause = BirthCause::thread) {
    u8 g[16];
    for (u32 j = 0; j < 16; ++j)
        g[j] = static_cast<u8>(genome * 37 + j * 11 + (genome >> 3) * j);
    BirthFact f{};
    f.tick = p.env.tick;
    f.uid = uid;
    f.parent_uid = parent;
    f.cause = cause;
    f.base = static_cast<u32>(uid * 64);
    f.size = 16;
    f.genome_hash = kKeepHash + genome;
    f.image_hash = kKeepHash + genome + 0x10000;
    f.genome_at = static_cast<u32>(p.os.fact_bytes.size());
    f.genome_len = 16;
    p.os.fact_bytes.append(g, 16);
    p.os.births.push(f);
    ++p.env.tick;
    ingest_clear(obs, p);
}

void keep_death(Observer& obs, Place& p, u64 uid) {
    DeathFact d{};
    d.tick = p.env.tick;
    d.uid = uid;
    d.cause = DeathCause::exit;
    p.os.deaths.push(d);
    ++p.env.tick;
    ingest_clear(obs, p);
}

const GenomeRec* kept_genome(const Observer& obs, u32 genome) { return obs.genome(kKeepHash + genome); }

bool is_kept(const Observer& obs, u32 genome) {
    const GenomeRec* g = kept_genome(obs, genome);
    return g && g->state == GenomeState::kept && g->group != kNoGroup;
}

bool is_held(const Observer& obs, u32 genome) {
    const GenomeRec* g = kept_genome(obs, genome);
    return g && g->state == GenomeState::held && g->group == kNoGroup;
}

// The genebank's row n: its genome, the genome it came from, its
// generation, and its bytes as the observers hold them.
bool bank_row(const Observer& obs, u64 n, u32 genome, u32 parent, u32 generation) {
    if (n >= obs.genebank.size())
        return false;
    const GenebankRow r = obs.genebank[n];
    Array<u8> bytes; // from the record's entries (genome_code.h)
    if (r.len != 16 || !obs.kept_genome(n, &bytes) || bytes.size() != 16)
        return false;
    const GenomeRec* g = kept_genome(obs, genome);
    return r.hash == kKeepHash + genome && r.parent_genome == (parent ? kKeepHash + parent : 0) &&
           r.generation == generation && g && g->bank_row == n && memcmp(obs.genome_bytes(*g), bytes.data(), 16) == 0;
}

// Keeping (13.2; build-plan-observe.md step 4): at 1, every genome at its
// first birth, as before; at 3, a genome when a carrier has a grandchild,
// a line of mutants generation by generation, each with the genome it came
// from; a line that ends sooner forgotten when its last carrier's children
// are gone; a line carried on by an arrival from a dead sender; and all of
// it read back from a save.
void test_keep_generations() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Place& p = w.place(0);
    {
        Observer one;
        ObserverSettings s;
        s.keep_generations = 1;
        one.init(s);
        keep_birth(one, p, 1, 0, 1);
        CHECK(is_kept(one, 1) && one.genomes_kept() == 1 && one.genomes_held() == 0 && one.group_count() == 1);
        CHECK(bank_row(one, 0, 1, 0, 0));
    }
    Observer obs;
    ObserverSettings s;
    CHECK(s.keep_generations == 3);
    obs.init(s);
    // One genome, copied: kept at the grandchild, not before.
    keep_birth(obs, p, 1, 0, 1);
    keep_birth(obs, p, 2, 1, 1);
    CHECK(is_held(obs, 1) && obs.genomes_kept() == 0 && obs.genomes_held() == 1 && obs.group_count() == 0);
    CHECK(obs.program(2)->group == kNoGroup);
    keep_birth(obs, p, 3, 2, 1);
    CHECK(is_kept(obs, 1) && obs.genomes_kept() == 1 && obs.genomes_held() == 0 && obs.group_count() == 1);
    CHECK(obs.genebank.size() == 1 && bank_row(obs, 0, 1, 0, 0) && obs.genebank[0].kept_at == p.env.tick - 1);
    CHECK(obs.program(3)->group == kept_genome(obs, 1)->group && obs.lineage(1)->groups == 1);
    // A line of mutants: each generation kept two generations on, with the
    // genome it came from.
    keep_birth(obs, p, 10, 0, 10);
    keep_birth(obs, p, 11, 10, 11);
    keep_birth(obs, p, 12, 11, 12);
    CHECK(is_kept(obs, 10) && is_held(obs, 11) && is_held(obs, 12));
    keep_birth(obs, p, 13, 12, 13);
    keep_birth(obs, p, 14, 13, 14);
    CHECK(is_kept(obs, 11) && is_kept(obs, 12) && is_held(obs, 13) && is_held(obs, 14));
    CHECK(bank_row(obs, 1, 10, 0, 0) && bank_row(obs, 2, 11, 10, 1) && bank_row(obs, 3, 12, 11, 2));
    CHECK(obs.genomes_kept() == 4 && obs.genomes_held() == 2);
    // A line that ends at a child: held while either lives, then forgotten.
    keep_birth(obs, p, 20, 0, 20);
    keep_birth(obs, p, 21, 20, 21);
    keep_death(obs, p, 20);
    CHECK(is_held(obs, 20) && is_held(obs, 21)); // the child holds its parent's
    const u32 slots = obs.genome_count();
    keep_death(obs, p, 21);
    CHECK(!kept_genome(obs, 20) && !kept_genome(obs, 21) && obs.genomes_held() == 2);
    // Its lineage, extinct, never copied and with no record left, is let go.
    // It went two generations, short of the three a row takes
    // (lineage_generations): it is counted in the tally, with its births.
    CHECK(s.lineage_generations == 3 && !obs.lineage(20) && obs.finished_lineages.size() == 0 &&
          obs.shallow_lineages == 1 && obs.shallow_births == 2 && obs.lineages_ever() == 3);
    // Every carrier of the mutants gone: the kept stay, the held go.
    for (u64 uid = 10; uid <= 14; ++uid)
        keep_death(obs, p, uid);
    CHECK(is_kept(obs, 10) && is_kept(obs, 11) && is_kept(obs, 12) && !kept_genome(obs, 13) &&
          !kept_genome(obs, 14) && obs.genomes_held() == 0 && obs.genomes_kept() == 4);
    // The mutants' lineage went five generations: it gets its row. 1 copied: it stays.
    CHECK(!obs.lineage(10) && obs.finished_lineages.size() == 1 && obs.finished_lineages.back().origin == 10 &&
          obs.finished_lineages.back().births == 5 && obs.finished_lineages.back().max_depth == 4 &&
          obs.shallow_lineages == 1 && obs.lineage(1));
    // A forgotten genome born again is new, in a freed slot.
    const u32 lineage_slots = obs.lineage_count();
    keep_birth(obs, p, 22, 0, 21);
    CHECK(obs.lineage_count() == lineage_slots && obs.lineages_ever() == 4); // in the slot lineage 20 left
    CHECK(is_held(obs, 21) && kept_genome(obs, 21)->generation == 0 && obs.genome_count() == slots);
    keep_death(obs, p, 22);
    // An arrival from a sender that died: the sender's record holds its
    // genome, and the line the arrival carries on keeps it.
    keep_birth(obs, p, 40, 0, 40);
    SendFact sf{};
    sf.uid = 40;
    sf.port = 0;
    p.os.sends.push(sf);
    ingest_clear(obs, p);
    keep_death(obs, p, 40);
    CHECK(is_held(obs, 40) && obs.lineage(40) && obs.lineage(40)->remembered == 1); // not let go: it may return
    keep_birth(obs, p, 41, 40, 40, BirthCause::arrival);
    CHECK(obs.program(41) && obs.program(41)->depth == 1 && obs.program(41)->line[0] == kKeepHash + 40);
    keep_birth(obs, p, 42, 41, 40);
    CHECK(is_kept(obs, 40));

    // Saved and read back, the observer goes on as one never saved: held
    // genomes, free slots and the genebank with it.
    keep_birth(obs, p, 50, 0, 50);
    keep_birth(obs, p, 51, 50, 51);
    keep_birth(obs, p, 60, 0, 60);
    keep_death(obs, p, 60);
    FileOut out;
    CHECK(out.open("test_observe_keep.obs"));
    obs.save(out);
    CHECK(out.close());
    Observer back;
    back.init(s);
    FileIn in;
    CHECK(in.open("test_observe_keep.obs") && back.load(in));
    CHECK(back.genomes_kept() == obs.genomes_kept() && back.genomes_held() == obs.genomes_held() &&
          back.genome_count() == obs.genome_count() && back.genebank.size() == obs.genebank.size());
    CHECK(back.shallow_lineages == obs.shallow_lineages && back.shallow_births == obs.shallow_births &&
          obs.shallow_lineages >= 2 && back.finished_lineages.size() == obs.finished_lineages.size());
    CHECK(back.lineage_count() == obs.lineage_count() && back.lineages_ever() == obs.lineages_ever() &&
          !back.lineage(20) && back.lineage(40) && back.lineage(40)->remembered == 1);
    const u64 tick = p.env.tick;
    keep_birth(obs, p, 52, 51, 52);
    keep_birth(obs, p, 70, 0, 70);
    p.env.tick = tick;
    keep_birth(back, p, 52, 51, 52);
    keep_birth(back, p, 70, 0, 70);
    CHECK(is_kept(back, 50) && is_held(back, 51) && is_held(back, 70) && !kept_genome(back, 60));
    bool same = back.genebank.size() == obs.genebank.size() && back.genome_count() == obs.genome_count();
    for (u64 i = 0; same && i < obs.genebank.size(); ++i) {
        const GenebankRow x = back.genebank[i], y = obs.genebank[i];
        same = memcmp(&x, &y, sizeof x) == 0;
    }
    CHECK(same);
    // With lineage_generations 1 every lineage let go gets its row, as before
    // the tally, and one of three generations gets its row under the usual
    // 3; and a save made under another number is refused.
    {
        Observer every;
        ObserverSettings s0;
        s0.lineage_generations = 1;
        every.init(s0);
        keep_birth(every, p, 80, 0, 80);
        keep_birth(every, p, 81, 80, 81);
        keep_death(every, p, 80);
        keep_death(every, p, 81);
        CHECK(!every.lineage(80) && every.finished_lineages.size() == 1 && every.shallow_lineages == 0 &&
              every.finished_lineages.back().births == 2 && every.lineages_ever() == 1);
        FileIn in0;
        CHECK(in0.open("test_observe_keep.obs") && !every.load(in0));
        Observer usual;
        usual.init(ObserverSettings{});
        keep_birth(usual, p, 90, 0, 90);
        keep_birth(usual, p, 91, 90, 91);
        keep_birth(usual, p, 92, 91, 92);
        for (u64 uid = 90; uid <= 92; ++uid)
            keep_death(usual, p, uid);
        CHECK(!usual.lineage(90) && usual.finished_lineages.size() == 1 && usual.shallow_lineages == 0 &&
              usual.finished_lineages.back().max_depth == 2 && usual.lineages_ever() == 1);
    }
    // A save under other keeping is refused.
    Observer other;
    ObserverSettings s2;
    s2.keep_generations = 2;
    other.init(s2);
    FileIn in2;
    CHECK(in2.open("test_observe_keep.obs") && !other.load(in2));
    printf("  observe: genomes kept at a carrier's grandchild, a mutant line kept generation by generation with "
           "its parents, short lines forgotten and their slots reused, a dead sender's arrival carrying a line "
           "on, and the whole read back from a save\n");
}

// A birth with a genome given byte for byte, for the forgetting test: two
// genomes alike enough to share a group.
void bytes_birth(Observer& obs, Place& p, u64 uid, u64 parent, u64 hash, const u8* g, u32 len) {
    BirthFact f{};
    f.tick = p.env.tick;
    f.uid = uid;
    f.parent_uid = parent;
    f.cause = BirthCause::thread;
    f.base = static_cast<u32>(uid * 64);
    f.size = len;
    f.genome_hash = hash;
    f.image_hash = hash + 0x10000;
    f.genome_at = static_cast<u32>(p.os.fact_bytes.size());
    f.genome_len = len;
    p.os.fact_bytes.append(g, len);
    p.os.births.push(f);
    ++p.env.tick;
    ingest_clear(obs, p);
}

// Forgetting (observe.forget; build-plan-observe.md 3.5), for a run with no
// end set: a kept genome with no carrier or descendant of one left goes at
// the next pass, and its group with it; the groups left are numbered afresh
// and still found by their runs; what went and comes back is new; a lineage
// doesn't count a group it still holds twice; and all of it is read back
// from a save. With forgetting off, nothing kept ever goes.
void test_forget() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Place& p = w.place(0);
    u8 x[32], x2[32];
    for (u32 j = 0; j < 32; ++j)
        x[j] = x2[j] = static_cast<u8>(200 - 5 * j);
    x2[31] = 7; // 29 of its 30 runs are x's: the same group
    const u64 hx = kKeepHash + 1000, hx2 = kKeepHash + 1001;
    for (u32 pass = 0; pass < 2; ++pass) {
        Observer obs;
        ObserverSettings s;
        s.forget = pass == 1;
        s.forget_floor = 4;
        obs.init(s);
        // A line of mutants, kept generation by generation, then x, copied.
        keep_birth(obs, p, 10, 0, 10);
        keep_birth(obs, p, 11, 10, 11);
        keep_birth(obs, p, 12, 11, 12);
        keep_birth(obs, p, 13, 12, 13);
        keep_birth(obs, p, 14, 13, 14);
        bytes_birth(obs, p, 1, 0, hx, x, 32);
        bytes_birth(obs, p, 2, 1, hx, x, 32);
        bytes_birth(obs, p, 3, 2, hx, x, 32);
        CHECK(is_kept(obs, 10) && is_kept(obs, 11) && is_kept(obs, 12) && obs.genomes_kept() == 4);
        const u32 groups = obs.group_count();
        const u32 x_group = obs.genome(hx)->group;
        CHECK(groups >= 2 && x_group == groups - 1 && obs.groups_ever() == groups);
        CHECK(obs.program(3)->group == x_group && obs.lineage(1)->groups == 1);
        // The mutants die out, every one; x lives.
        for (u64 uid = 10; uid <= 14; ++uid)
            keep_death(obs, p, uid);
        obs.sample(w);
        if (pass == 0) { // off: all four stay, as the gates' runs count them
            CHECK(is_kept(obs, 10) && is_kept(obs, 11) && is_kept(obs, 12) && obs.genomes_kept() == 4 &&
                  obs.group_count() == groups && obs.genomes_forgotten() == 0);
            continue;
        }
        CHECK(!kept_genome(obs, 10) && !kept_genome(obs, 11) && !kept_genome(obs, 12));
        CHECK(obs.genomes_kept() == 1 && obs.genomes_forgotten() == 3 && obs.genebank.size() == 4);
        CHECK(obs.group_count() == 1 && obs.groups_ever() == groups);
        const GenomeRec* gx = obs.genome(hx);
        CHECK(gx && gx->state == GenomeState::kept && gx->group == 0 && memcmp(obs.genome_bytes(*gx), x, 32) == 0);
        CHECK(obs.program(3)->group == 0 && obs.program(1)->group == kNoGroup); // 1 was born before x was kept
        // The group left is found by its runs: a near copy of x joins it.
        bytes_birth(obs, p, 5, 3, hx2, x2, 32);
        bytes_birth(obs, p, 6, 5, hx2, x2, 32);
        bytes_birth(obs, p, 7, 6, hx2, x2, 32);
        CHECK(obs.genome(hx2) && obs.genome(hx2)->state == GenomeState::kept && obs.genome(hx2)->group == 0);
        CHECK(obs.group_count() == 1 && obs.lineage(1)->groups == 1); // no new group, for the world or the lineage
        // A genome that went and comes back is new: held, kept afresh, a
        // second row in the genebank, its group founded again.
        keep_birth(obs, p, 30, 0, 10);
        CHECK(is_held(obs, 10) && kept_genome(obs, 10)->count == 1);
        keep_birth(obs, p, 31, 30, 10);
        keep_birth(obs, p, 32, 31, 10);
        CHECK(is_kept(obs, 10) && kept_genome(obs, 10)->group == 1 && obs.group_count() == 2 &&
              obs.groups_ever() == groups + 1 && obs.genebank.size() == 6);
        // Under the floor of kept genomes no pass runs; at it, one does, and
        // nothing living goes.
        obs.sample(w);
        CHECK(obs.genomes_kept() == 3 && obs.genomes_forgotten() == 3);
        keep_birth(obs, p, 50, 0, 50);
        keep_birth(obs, p, 51, 50, 50);
        keep_birth(obs, p, 52, 51, 50);
        CHECK(obs.genomes_kept() == 4);
        obs.sample(w);
        CHECK(obs.genomes_kept() == 4 && obs.genomes_forgotten() == 3 && obs.group_count() == 3);
        // Saved and read back, it goes on as the one never saved; and a save
        // made with forgetting on is refused with it off.
        FileOut out;
        CHECK(out.open("test_observe_forget.obs"));
        obs.save(out);
        CHECK(out.close());
        Observer back;
        back.init(s);
        FileIn in;
        CHECK(in.open("test_observe_forget.obs") && back.load(in));
        CHECK(back.genomes_kept() == 4 && back.genomes_forgotten() == 3 && back.group_count() == 3 &&
              back.groups_ever() == obs.groups_ever());
        const u64 tick = p.env.tick;
        for (u32 k = 0; k < 2; ++k) {
            Observer& o = k ? back : obs;
            p.env.tick = tick;
            for (u64 uid = 30; uid <= 32; ++uid)
                keep_death(o, p, uid);
            for (u64 uid = 50; uid <= 52; ++uid)
                keep_death(o, p, uid);
            for (u64 g = 0; g < 5; ++g) { // five more kept, to the next pass
                keep_birth(o, p, 100 + 3 * g, 0, 100 + static_cast<u32>(g));
                keep_birth(o, p, 101 + 3 * g, 100 + 3 * g, 100 + static_cast<u32>(g));
                keep_birth(o, p, 102 + 3 * g, 101 + 3 * g, 100 + static_cast<u32>(g));
            }
            o.sample(w);
        }
        CHECK(obs.genomes_forgotten() == 5 && back.genomes_forgotten() == 5 && !kept_genome(back, 10) &&
              !kept_genome(back, 50) && back.genomes_kept() == obs.genomes_kept() && obs.genomes_kept() == 7 &&
              back.group_count() == obs.group_count() && back.genebank.size() == obs.genebank.size());
        bool same = back.genome_count() == obs.genome_count();
        for (u32 i = 0; same && i < obs.genome_count(); ++i) {
            const GenomeRec &a = obs.genome_at(i), &b = back.genome_at(i);
            same = a.state == b.state && a.hash == b.hash && a.group == b.group && a.count == b.count;
        }
        CHECK(same);
        Observer other;
        ObserverSettings off;
        off.forget_floor = 4;
        other.init(off);
        FileIn in2;
        CHECK(in2.open("test_observe_forget.obs") && !other.load(in2));
    }
    printf("  observe: with forgetting on, three extinct kept genomes and their groups left memory at a pass, the "
           "group left was numbered afresh and still took a near copy, a genome that came back was kept as new, "
           "and a save read back went on the same; with it off all four stayed\n");
}

// Samples come every N ticks, with sane region measures; watching changes nothing.
void test_samples_and_no_effect() {
    World a, b;
    Config ca, cb;
    CHECK(build(a, ca));
    CHECK(build(b, cb));
    Observer obs;
    ObserverSettings s;
    s.sample_every = 5;
    obs.init(s);
    a.watch(&obs);
    a.start_threads(3);
    b.start_threads(1);
    for (u32 t = 0; t < 30; ++t) {
        a.tick();
        b.tick();
    }
    CHECK(a.hash() == b.hash()); // watched and unwatched: the same world
    CHECK(obs.samples.size() == 6 && obs.samples.back().tick == 30);
    const Sample& last = obs.samples.back();
    CHECK(last.region_ratio > 0.05 && last.region_ratio < 1.2);
    CHECK(last.paths_new > 0 && last.idle_runs >= last.paths_new);
    CHECK(last.births == obs.total_births && last.programs > 0 && last.threads_started > 0);
    // Static alone: idle runs and paths, no births needed.
    World c;
    Config cc;
    CHECK(build(c, cc, "", false));
    Observer o2;
    o2.init(s);
    c.watch(&o2);
    for (u32 t = 0; t < 10; ++t)
        c.tick();
    CHECK(o2.samples.size() == 2 && o2.samples.back().idle_runs > 0 && o2.samples.back().paths_new > 0);
    CHECK(o2.samples.back().region_ratio > 0.9); // static barely compresses
}

// The turnover measure (build-plan-gate2.md step 1), on programs of known
// lifetimes placed by hand: cohorts born together and dying together.
struct Cohort {
    u32 n;
    u64 born, dies; // the ticks they're born and die in
};

u32 run_cohorts(const Cohort* c, u32 nc, u64 ticks, u64 full_at, const TurnoverSettings& s, PlaceTurnover* T,
                TurnoverWindow* got, u32 room) {
    u32 n = 0, live = 0;
    for (u64 t = 0; t < ticks; ++t) {
        for (u32 i = 0; i < nc; ++i)
            if (c[i].born == t) {
                for (u32 k = 0; k < c[i].n; ++k)
                    T->birth();
                live += c[i].n;
            }
        for (u32 i = 0; i < nc; ++i)
            if (c[i].dies == t) {
                for (u32 k = 0; k < c[i].n; ++k)
                    T->death(c[i].born, DeathCause::exit);
                live -= c[i].n;
            }
        TurnoverWindow w;
        if (T->end_tick(t + 1, live, t + 1 == full_at, s, 3, &w) && n < room)
            got[n++] = w;
    }
    return n;
}

void test_turnover_windows() {
    TurnoverSettings s;
    s.window = 100;
    s.members = 20;
    // 40 born at 0 who die at 150; 30 born at 120 who die at 350; 10 born at
    // 360 who die at 450. The place is full as the window at 200 starts.
    const Cohort c[3] = {{40, 0, 150}, {30, 120, 350}, {10, 360, 450}};
    PlaceTurnover T;
    TurnoverWindow got[8];
    const u32 n = run_cohorts(c, 3, 600, 200, s, &T, got, 8);
    CHECK(n == 6);
    // [0, 100): nobody alive at its start.
    CHECK(got[0].start == 0 && got[0].starters == 0 && got[0].births == 40 && !got[0].counted);
    // [100, 200): all 40 starters die, and 30 are born: it turns over.
    CHECK(got[1].start == 100 && got[1].starters == 40 && got[1].starters_died == 40 && got[1].births == 30 &&
          got[1].deaths == 40 && got[1].counted && got[1].turned && !got[1].crowded);
    // [200, 300): crowded, and nobody dies: frozen.
    CHECK(got[2].starters == 30 && got[2].starters_died == 0 && got[2].crowded && got[2].counted && !got[2].turned);
    // [300, 400): all 30 die but only 10 are born: emptying, not turning over.
    CHECK(got[3].starters == 30 && got[3].starters_died == 30 && got[3].births == 10 && got[3].counted &&
          !got[3].turned && !got[3].crowded);
    // [400, 500): 10 starters, too few to count; [500, 600): none.
    CHECK(got[4].starters == 10 && got[4].starters_died == 10 && !got[4].counted);
    CHECK(got[5].starters == 0 && !got[5].counted && got[5].place == 3);
    CHECK(T.windows == 3 && T.turned == 1 && T.crowded_windows == 1 && T.crowded_turned == 0);
    CHECK(T.longest == 2 && T.longest_end == 400 && T.streak == 0); // broken by the window too small to count
    // Programs alive over the run: 40 x 150 + 30 x 230 + 10 x 90 ticks, so the
    // mean life (program-ticks over deaths) is their lifetimes' mean, 172.5.
    CHECK(T.program_ticks == 13800 && T.deaths_by_cause[0] == 80 && T.deaths == 0);
    // A death whose birth the observer never saw counts, but not as a starter's.
    PlaceTurnover U;
    U.end_tick(100, 25, false, s, 0, &got[0]);
    U.death(~0ull, DeathCause::fault);
    U.death(50, DeathCause::fault);
    U.end_tick(200, 23, false, s, 0, &got[0]);
    CHECK(got[0].starters == 25 && got[0].deaths == 2 && got[0].starters_died == 1 && U.deaths_by_cause[1] == 2);

    // A steady place: one born and one dying each tick, each living 100
    // ticks. Every window of 200 that counts turns over.
    s.window = 200;
    PlaceTurnover V;
    u32 live = 0, turned = 0, counted = 0;
    for (u64 t = 0; t < 2000; ++t) {
        V.birth();
        ++live;
        if (t >= 100) {
            V.death(t - 100, DeathCause::exit);
            --live;
        }
        TurnoverWindow w;
        if (V.end_tick(t + 1, live, false, s, 0, &w) && w.counted) {
            ++counted;
            turned += w.turned && w.starters == 100 && w.starters_died == 100 && w.births == 200 ? 1 : 0;
        }
    }
    CHECK(counted == 9 && turned == 9 && V.windows == 9 && V.turned == 9 && V.longest == 0);
    // Steady from tick 100: its program-ticks there over its deaths, 100.
    CHECK(V.program_ticks == 5050 + 1900ull * 100 && V.deaths_by_cause[0] == 1900);
    // Off: no windows at all.
    s.window = 0;
    PlaceTurnover Z;
    CHECK(!Z.end_tick(200, 30, false, s, 0, &got[0]) && Z.program_ticks == 30);
}

// In a test world, every window the observer finishes agrees with the facts
// log: its starters, their deaths, its births and deaths.
void test_turnover_world() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    os.turnover.window = 10;
    os.turnover.members = 1;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(4);
    for (u32 t = 0; t < 60; ++t)
        w.tick();
    HashMap<u64> born, died; // uid -> tick
    for (usize i = 0; i < obs.births.size(); ++i)
        born.get_or_insert(obs.births[i].uid) = obs.births[i].tick;
    for (usize i = 0; i < obs.deaths.size(); ++i)
        died.get_or_insert(obs.deaths[i].uid) = obs.deaths[i].tick;
    CHECK(obs.windows.size() > 8 && obs.turnover.size() == w.place_count());
    u32 agree = 0, flags = 0;
    for (usize k = 0; k < obs.windows.size(); ++k) {
        const TurnoverWindow& x = obs.windows[k];
        const u64 end = x.start + os.turnover.window;
        u32 starters = 0, starters_died = 0, births = 0, deaths = 0;
        for (usize i = 0; i < obs.births.size(); ++i) {
            const BirthFact& f = obs.births[i];
            if (f.place != x.place)
                continue;
            births += f.tick >= x.start && f.tick < end ? 1 : 0;
            const u64* d = died.find(f.uid);
            if (f.tick < x.start && (!d || *d >= x.start)) {
                ++starters;
                starters_died += d && *d < end ? 1 : 0;
            }
        }
        for (usize i = 0; i < obs.deaths.size(); ++i)
            deaths +=
                obs.deaths[i].place == x.place && obs.deaths[i].tick >= x.start && obs.deaths[i].tick < end ? 1 : 0;
        agree += x.starters == starters && x.starters_died == starters_died && x.births == births &&
                         x.deaths == deaths
                     ? 1
                     : 0;
        flags += x.counted == (x.starters >= os.turnover.members) &&
                         x.turned == (x.counted && 2 * x.starters_died >= x.starters && 2 * x.births >= x.starters_died)
                     ? 1
                     : 0;
    }
    CHECK(agree == obs.windows.size());
    CHECK(flags == obs.windows.size()); // counted and turned as defined
    // Every birth and death lands in some place's record.
    u64 deaths = 0, counted = 0;
    for (u32 i = 0; i < obs.turnover.size(); ++i) {
        for (u32 c = 0; c < kDeathCauses; ++c)
            deaths += obs.turnover[i].deaths_by_cause[c];
        counted += obs.turnover[i].windows;
    }
    CHECK(deaths == obs.total_deaths && deaths > 0 && counted > 0);
}

// The ground measure's bookkeeping (build-plan-gate4.md step 1), by hand.
void test_ground_windows() {
    const Ground fair = Ground::fair, hot = Ground::hot, dim = Ground::dim;
    bool both = true;
    // 4 cores and windows of 100 ticks: 400 core-ticks. Over a quarter, not
    // at it, of the core time or of the window.
    CHECK(ground_of(4, 100, 0, 0, 0, &both) == fair && !both);
    CHECK(ground_of(4, 100, 100, 100, 25, &both) == fair && !both);
    CHECK(ground_of(4, 100, 101, 0, 0, &both) == dim && !both);
    CHECK(ground_of(4, 100, 0, 101, 0, &both) == hot && !both);
    CHECK(ground_of(4, 100, 0, 0, 26, &both) == dim && !both); // its reset region's memory off, alone
    // Hot and dim at once: the larger loss of core time names it, hot if
    // they are the same.
    CHECK(ground_of(4, 100, 150, 200, 0, &both) == hot && both);
    CHECK(ground_of(4, 100, 200, 150, 0, &both) == dim && both);
    CHECK(ground_of(4, 100, 150, 150, 0, &both) == hot && both);
    CHECK(ground_of(4, 100, 0, 150, 30, &both) == hot && both);

    // A place's record, from its counters as each window ends. Begun after
    // its first tick, in which all 4 of its cores were cut out.
    PlaceGround G;
    G.begin(1, 4, 0, 0);
    TurnoverWindow w{};
    GroundWindow g;
    w.place = 2;
    w.births = 7;
    CHECK(G.close(w, 100, 4, 120, 0, 0, &g) && g.ground == dim && g.cut == 120 && g.heat == 0 && g.place == 2 &&
          g.cores == 4 && g.starters == 0 && g.births == 7 && g.start == 0);
    w.start = 100;
    w.starters = 30;
    w.births = 0;
    CHECK(G.close(w, 100, 4, 130, 250, 0, &g) && g.ground == hot && g.cut == 10 && g.heat == 250 && g.starters == 30 &&
          g.start == 100);
    w.start = 200;
    w.starters = 50;
    CHECK(G.close(w, 100, 4, 130, 250, 0, &g) && g.ground == fair && g.cut == 0 && g.heat == 0 && g.starters == 50);
    CHECK(G.windows[0] == 1 && G.windows[1] == 1 && G.windows[2] == 1 && G.starters[0] == 50 && G.starters[1] == 30 &&
          G.starters[2] == 0 && G.births[2] == 7 && G.both == 0);
    // Joined part-way through a window: that one isn't recorded, and the
    // next counts from its own start.
    PlaceGround J;
    J.begin(250, 1000, 0, 40);
    CHECK(!J.close(w, 100, 4, 1100, 0, 60, &g) && J.windows[0] + J.windows[1] + J.windows[2] == 0);
    w.start = 300;
    CHECK(J.close(w, 100, 4, 1100, 0, 90, &g) && g.ground == dim && g.cut == 0 && g.dark == 30 && g.start == 300);
    CHECK(J.windows[2] == 1 && J.starters[2] == 50);
    // Who led the place as a window began rides in its row (GroundLead): no
    // one, until the observer sets it for the window under way.
    PlaceGround L;
    L.begin(1, 0, 0, 0);
    w.start = 0;
    CHECK(L.close(w, 100, 4, 0, 0, 0, &g) && g.lead.group == kNoGroup && g.lead.group_ref == 0 &&
          g.lead.lineage == 0 && g.lead.group_programs == 0 && g.lead.lineage_programs == 0);
    L.lead.group = 3;
    L.lead.group_ref = 0xABCDull;
    L.lead.group_programs = 21;
    L.lead.group_copies = 12;
    L.lead.group_second = 5;
    L.lead.groups = 2;
    L.lead.lineage = 77;
    L.lead.lineage_programs = 30;
    L.lead.lineage_copies = 14;
    L.lead.lineage_second = 1;
    w.start = 100;
    CHECK(L.close(w, 100, 4, 0, 0, 0, &g) && g.start == 100 && g.lead.group == 3 && g.lead.group_ref == 0xABCDull &&
          g.lead.group_programs == 21 && g.lead.group_copies == 12 && g.lead.group_second == 5 &&
          g.lead.groups == 2 && g.lead.lineage == 77 && g.lead.lineage_programs == 30 &&
          g.lead.lineage_copies == 14 && g.lead.lineage_second == 1);
}

// A test world with places held hot, dim and fair by hand (R7;
// build-plan-gate4.md step 1): place 0 is left alone; place 1's cores are
// all off by a heat trip for 40 ticks of each 100; place 2's are cut out
// for 30; place 3's reset region's memory is off for 26. Nothing heats
// before gate 4's heat is built, and the steady supply cuts nothing out, so
// the test moves the places' counters itself. Each window is named as
// expected, and starts with the programs the place had then.
void hold_ground(World& w, u64 t) {
    const u64 at = t % 100;
    if (at < 40)
        w.place(1).heat_ticks[0] += w.place(1).core_count();
    if (at < 30)
        w.place(2).off_ticks[0] += w.place(2).core_count();
    if (at < 26)
        w.place(3).region_off_ticks += 1;
}

void test_ground_world() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    os.turnover.window = 100;
    os.turnover.members = 1;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(4);
    const u32 places = w.place_count();
    CHECK(places == 4);
    const u32 kWindows = 5;
    u32 live[kWindows + 1][4] = {}; // each place's programs as each window starts
    for (u64 t = 0; t < kWindows * 100; ++t) {
        hold_ground(w, t);
        w.tick();
        if ((t + 1) % 100 == 0)
            for (u32 i = 0; i < places; ++i)
                live[(t + 1) / 100][i] = w.place(i).os.live_programs();
    }
    const Ground want[4] = {Ground::fair, Ground::hot, Ground::dim, Ground::dim};
    CHECK(obs.ground_windows.size() == kWindows * places && obs.ground.size() == places);
    u32 named = 0, counted = 0, lost = 0;
    u64 programs = 0;
    for (usize k = 0; k < obs.ground_windows.size(); ++k) {
        const GroundWindow g = obs.ground_windows[k];
        const u32 i = g.place, n = w.place(i).core_count();
        named += g.ground == want[i] && !g.both && g.cores == n ? 1 : 0;
        u32 births = 0;
        for (usize b = 0; b < obs.births.size(); ++b)
            births += obs.births[b].place == i && obs.births[b].tick >= g.start && obs.births[b].tick < g.start + 100
                          ? 1
                          : 0;
        counted += g.start % 100 == 0 && g.starters == live[g.start / 100][i] && g.births == births ? 1 : 0;
        lost += g.cut == (i == 2 ? 30ull * n : 0) && g.heat == (i == 1 ? 40ull * n : 0) && g.dark == (i == 3 ? 26u : 0u)
                    ? 1
                    : 0;
        programs += g.starters;
    }
    CHECK(named == obs.ground_windows.size());
    CHECK(counted == obs.ground_windows.size());
    CHECK(lost == obs.ground_windows.size());
    CHECK(programs > 0); // the replicator lives there
    for (u32 i = 0; i < places; ++i) {
        const PlaceGround& G = obs.ground[i];
        const u32 k = static_cast<u32>(want[i]);
        CHECK(G.windows[k] == kWindows && G.windows[0] + G.windows[1] + G.windows[2] == kWindows && G.both == 0);
    }

    // Saved and read back, the record is as it was.
    FileOut out;
    CHECK(out.open("test_observe_ground.obs"));
    obs.save(out);
    CHECK(out.close());
    Observer back;
    back.init(os);
    FileIn in;
    CHECK(in.open("test_observe_ground.obs") && back.load(in));
    bool same = back.ground.size() == obs.ground.size() && back.ground_windows.size() == obs.ground_windows.size();
    for (usize k = 0; same && k < obs.ground_windows.size(); ++k) {
        const GroundWindow x = back.ground_windows[k], y = obs.ground_windows[k];
        same = memcmp(&x, &y, sizeof x) == 0;
    }
    for (u32 i = 0; same && i < places; ++i)
        same = memcmp(&back.ground[i], &obs.ground[i], sizeof(PlaceGround)) == 0;
    CHECK(same);

    // A save by a build before this one (its tag an E or less) isn't read:
    // the rows of the samples and of the ground record have grown since a
    // 5, the straits' states are kept by their landmasses since a D, and a
    // respawn's record has the landmass it left since an E (observe.cpp,
    // "NOV2OBS6", "NOV2OBSE" and "NOV2OBSF").
    Array<u8> bytes;
    CHECK(read_file("test_observe_ground.obs", &bytes));
    CHECK(bytes.size() > 16 && bytes[7] == 'F');
    bytes[7] = 'E';
    CHECK(write_file("test_observe_ground_old.obs", bytes.data(), bytes.size()));
    Observer old;
    old.init(os);
    FileIn in2;
    CHECK(in2.open("test_observe_ground_old.obs") && !old.load(in2));
    printf("  observe: places held hot, dim and fair by hand named so window by window, with their programs; the "
           "record read back from a save, and a save of the build before this one refused\n");
}

// Who leads a place now, counted here from the observer's records of the
// programs alive in its OS, one against every other (GroundLead says what
// is counted).
GroundLead count_lead(World& w, const Observer& obs, u32 place) {
    static_assert(sizeof(GroundLead) == 48, "a ground row's lead has no padding: its rows go to files as they are");
    const Os& os = w.place(place).os;
    Array<u64> lines;
    Array<u32> groups;
    Array<u8> copies;
    for (u32 i = 0; i < os.program_capacity(); ++i) {
        const Program& g = os.program(kFirstProgramHolder + i);
        const ProgramRec* r = g.alive ? obs.program(g.uid) : nullptr;
        if (!r)
            continue;
        const GenomeRec* gr = obs.genome(r->genome);
        lines.push(r->origin);
        groups.push(gr ? gr->group : kNoGroup);
        copies.push(r->copy && !r->arrived ? 1 : 0);
    }
    GroundLead l;
    const usize n = lines.size();
    for (usize a = 0; a < n; ++a) {
        u32 same = 0, made = 0;
        for (usize b = 0; b < n; ++b)
            if (lines[b] == lines[a]) {
                ++same;
                made += copies[b];
            }
        if (same > l.lineage_programs || (same == l.lineage_programs && lines[a] < l.lineage)) {
            l.lineage = lines[a];
            l.lineage_programs = same;
            l.lineage_copies = made;
        }
    }
    for (usize a = 0; a < n; ++a) {
        if (groups[a] == kNoGroup)
            continue;
        u32 same = 0, made = 0;
        bool first = true;
        for (usize b = 0; b < n; ++b) {
            if (groups[b] != groups[a])
                continue;
            ++same;
            made += copies[b];
            first = first && b >= a;
        }
        l.groups += first ? 1 : 0;
        if (same > l.group_programs || (same == l.group_programs && groups[a] < l.group)) {
            l.group = groups[a];
            l.group_programs = same;
            l.group_copies = made;
        }
    }
    // The most any other has.
    for (usize a = 0; a < n; ++a) {
        u32 line = 0, group = 0;
        for (usize b = 0; b < n; ++b) {
            line += lines[b] == lines[a] ? 1 : 0;
            group += groups[b] == groups[a] ? 1 : 0;
        }
        if (lines[a] != l.lineage && line > l.lineage_second)
            l.lineage_second = line;
        if (groups[a] != kNoGroup && groups[a] != l.group && group > l.group_second)
            l.group_second = group;
    }
    if (l.group != kNoGroup)
        l.group_ref = obs.group_reference(l.group).hash;
    return l;
}

// Who led each place as each window began (build-plan-gate5.md 2: the
// columns gate 5's measure reads gate 4's run by): every row's lead is the
// count made here as that window started, in a world where the replicator
// and its mutants crowd four places under noise.
void test_ground_lead() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    os.turnover.window = 100;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(4);
    const u32 places = w.place_count(), kWindows = 60;
    Array<GroundLead> want; // by window, then place: who led as it began
    want.resize(static_cast<usize>(kWindows + 1) * places);
    for (u32 i = 0; i < places; ++i)
        want[i] = GroundLead{}; // the first window begins with no one
    for (u64 t = 1; t <= kWindows * 100; ++t) {
        w.tick();
        if (t % 100 == 0)
            for (u32 i = 0; i < places; ++i)
                want[static_cast<usize>(t / 100) * places + i] = count_lead(w, obs, i);
    }
    CHECK(obs.ground_windows.size() == static_cast<usize>(kWindows) * places);
    u32 right = 0, led = 0, copied = 0, shared = 0, tied = 0, most = 0, lines2 = 0, unkept = 0;
    for (usize k = 0; k < obs.ground_windows.size(); ++k) {
        const GroundWindow g = obs.ground_windows[k];
        const GroundLead& l = g.lead;
        const GroundLead& x = want[static_cast<usize>(g.start / 100) * places + g.place];
        right += memcmp(&l, &x, sizeof l) == 0 ? 1 : 0;
        // What a row must hold whatever the world: a leader's programs are
        // among the place's, its copies among its programs, and no other
        // has more.
        CHECK(l.lineage_programs <= g.starters && l.group_programs <= g.starters);
        CHECK(l.lineage_copies <= l.lineage_programs && l.group_copies <= l.group_programs);
        CHECK(l.lineage_second <= l.lineage_programs && l.group_second <= l.group_programs);
        CHECK((l.group == kNoGroup) == (l.group_programs == 0) && (l.group == kNoGroup) == (l.group_ref == 0));
        CHECK((l.lineage == 0) == (g.starters == 0) && (l.groups >= 2) == (l.group_second > 0));
        led += l.group != kNoGroup ? 1 : 0;
        copied += l.group_copies ? 1 : 0;
        shared += l.groups >= 2 ? 1 : 0;
        tied += l.group_programs && l.group_second == l.group_programs ? 1 : 0;
        lines2 += l.lineage_second ? 1 : 0;
        unkept += l.group_programs < g.starters ? 1 : 0;
        most = l.group_programs > most ? l.group_programs : most;
    }
    CHECK(right == obs.ground_windows.size());
    CHECK(led > 0 && copied > 0 && shared > 0 && most >= 20);
    printf("  observe: who led each place as each window began, %u place-windows each as counted from the records "
           "of its living programs: %u led by a group (the most programs %u), %u with copies made there among "
           "them, %u with two groups or more there, %u with two groups level, %u with a second lineage, %u with "
           "programs of no kept genome\n",
           static_cast<u32>(obs.ground_windows.size()), led, most, copied, shared, tied, lines2, unkept);
}

// The same measure under the supply itself: a source whose nights leave it
// a fiftieth of its capacity, so the voltage falls below the cores'
// thresholds and at times the memory's, and comes back by day. Each window
// agrees with the place's own devices, read tick by tick: the cores not
// working and the reset region's memory off.
void test_ground_supply() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "supply.sag = 50\nsupply.day = 1600\nsupply.night = 0.02\n"));
    Observer obs;
    ObserverSettings os;
    os.turnover.window = 100;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    const u32 places = w.place_count(), kTicks = 1600;
    u64 cut[8] = {};
    u32 dark[8] = {};
    u32 right = 0, dims = 0, fairs = 0, darks = 0;
    for (u32 t = 1; t <= kTicks; ++t) {
        w.tick();
        for (u32 i = 0; i < places; ++i) {
            const Place& p = w.place(i);
            for (u32 c = 0; c < p.core_count(); ++c)
                cut[i] += p.cores[c].working ? 0 : 1;
            dark[i] += p.memory_state[p.mem.region_device()].on ? 0 : 1;
        }
        if (t % 100)
            continue;
        for (u32 i = 0; i < places; ++i) {
            const GroundWindow g = obs.ground_windows[static_cast<u64>(t / 100 - 1) * places + i];
            bool both = false;
            const Ground want = ground_of(w.place(i).core_count(), 100, cut[i], 0, dark[i], &both);
            right += g.place == i && g.start == t - 100 && g.cut == cut[i] && g.heat == 0 && g.dark == dark[i] &&
                             g.ground == want && !g.both
                         ? 1
                         : 0;
            dims += g.ground == Ground::dim ? 1 : 0;
            fairs += g.ground == Ground::fair ? 1 : 0;
            darks += g.dark ? 1 : 0;
            cut[i] = 0;
            dark[i] = 0;
        }
    }
    CHECK(obs.ground_windows.size() == static_cast<usize>(kTicks / 100) * places);
    CHECK(right == (kTicks / 100) * places);
    CHECK(dims > 0 && fairs > 0 && darks > 0);
    printf("  observe: under a source with deep nights, %u place-windows dim and %u fair, %u with the reset region's "
           "memory off, each as the place's devices were\n",
           dims, fairs, darks);
}

// Each place's voltage per sample (build-plan-gate2.md step 8), under a
// varying supply with drop-outs: its mean and lowest over the sample's
// ticks, against the voltages read tick by tick; and cold starts and births
// from boots in the samples, against the observer's own totals.
void test_volts() {
    World w;
    Config cfg;
    CHECK(build(w, cfg,
                "supply.sag = 50\nsupply.day = 600\nsupply.night = 0.2\nsupply.swing = 0.25\n"
                "supply.swing_period = 200\nsupply.dropouts = 1000\nsupply.dropout.min = 5\n"
                "supply.dropout.max = 120\nsupply.blackout_share = 0.5\n"));
    Observer obs;
    ObserverSettings os;
    os.sample_every = 10;
    obs.init(os);
    w.watch(&obs);
    w.start_threads(2);
    const u32 places = w.place_count(), kTicks = 1200;
    i64 sum[8] = {};
    i32 low[8] = {};
    u32 right = 0, dips = 0;
    for (u32 t = 1; t <= kTicks; ++t) {
        w.tick();
        for (u32 i = 0; i < places; ++i) {
            const i32 mv = w.place(i).env.voltage_mv;
            low[i] = (t - 1) % 10 == 0 || mv < low[i] ? mv : low[i];
            sum[i] += mv;
        }
        if (t % 10)
            continue;
        for (u32 i = 0; i < places; ++i) {
            const PlaceVolts v = obs.volt(t / 10 - 1, i);
            right += v.mean == sum[i] / 10 && v.low == low[i] ? 1 : 0;
            dips += v.low < v.mean ? 1 : 0;
            sum[i] = 0;
        }
    }
    CHECK(obs.volt_places == places && obs.volts.size() == static_cast<usize>(kTicks / 10) * places);
    CHECK(right == (kTicks / 10) * places);
    CHECK(dips > 0);
    const Sample& last = obs.samples.back();
    CHECK(last.cold_starts == obs.cold_starts && last.boots == obs.births_by_cause[static_cast<u8>(BirthCause::boot)]);
}

// The origin test's stages, on records made by hand and in a test world.
// The copying ladder in a world where the region holds a replicator: idle
// runs ALLOC, CPY into what they hold and SPAWN; their copies are the
// region's code; programs then copy themselves, faithfully.
void test_ladder() {
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(4);
    for (u32 t = 0; t < 30; ++t)
        w.tick();
    obs.sample(w);
    const Sample& s = obs.samples.back();
    CHECK(s.run_alloc >= s.run_alloc_ok && s.run_alloc_ok > 0);
    CHECK(s.run_spawn >= s.run_spawn_ok && s.run_spawn_ok > 0);
    CHECK(s.run_cpy >= s.run_cpy_own && s.run_cpy_own > 0);
    CHECK(s.written > 0 && s.region_copies > 0); // the runs copied the region's replicator
    CHECK(s.program_copies > 0 && s.near_copies > 0 && s.near_copies <= s.program_copies);
    CHECK(s.faithful <= s.near_copies); // a faithful copy is a near one
    CHECK(s.written <= s.births);
}

void test_stages() {
    StageCriteria c;
    c.generations = 5;
    c.span = 50;
    c.share = 0.1;
    c.members = 10;
    c.copies = 0.5;
    c.groups = 2;
    LineageRec L{};
    L.first_copy = L.first_viable = L.extinct_at = ~0ull;
    L.born = 100;
    L.alive = L.home_alive = 10;
    L.alive_copies = L.home_copies = 5;
    LineageClock k;
    CHECK(lineage_stage(L, 100, 50, c, &k) == 0 && k.held_since == 100); // no copy yet, but its clock runs
    L.first_copy = 110;
    CHECK(lineage_stage(L, 110, 50, c, &k) == 1 && k.held_since == 100);
    L.first_viable = 120;
    CHECK(lineage_stage(L, 120, 50, c, &k) == 2);
    L.max_depth = 5;
    // One check that fails ends the hold, and the next starts afresh.
    CHECK(lineage_stage(L, 130, 200, c, &k) == 3 && k.held_since == ~0ull); // 10 of 200: under a tenth
    CHECK(lineage_stage(L, 140, 50, c, &k) == 3 && k.held_since == 140);
    CHECK(lineage_stage(L, 189, 50, c, &k) == 3); // held for 49 ticks of 50
    // Groups made while it was taking its place don't count for stage 5.
    L.groups_full = 4;
    CHECK(lineage_stage(L, 190, 50, c, &k) == 4 && k.established == 190 && k.groups_then == 4);
    L.groups_full = 5;
    CHECK(lineage_stage(L, 200, 50, c, &k) == 4);
    L.groups_full = 6;
    CHECK(lineage_stage(L, 210, 50, c, &k) == 5);
    // Established is for good: a broken hold later doesn't take stage 4 or 5
    // away, though the hold's clock stops.
    CHECK(lineage_stage(L, 220, 200, c, &k) == 5 && k.held_since == ~0ull && k.established == 190);
    // Too few members, too few of them born as copies, or none: no hold.
    L.home_alive = L.home_copies = 9;
    LineageClock k2;
    CHECK(!lineage_holds(9, 9, 50, c) && lineage_stage(L, 240, 50, c, &k2) == 3 && k2.held_since == ~0ull);
    CHECK(!lineage_holds(20, 9, 50, c)); // 9 of 20 born as copies
    CHECK(lineage_holds(20, 10, 50, c) && !lineage_holds(20, 10, 201, c) && !lineage_holds(20, 10, 0, c));
    CHECK(!lineage_holds(0, 0, 50, c)); // dead: it didn't establish
    // Stage 4 holds the place the lineage began in (13.3): members elsewhere
    // don't count.
    L.alive = L.alive_copies = 40;
    LineageClock k3;
    CHECK(lineage_stage(L, 250, 50, c, &k3) == 3 && k3.held_since == ~0ull);
    // A colony settles by the same hold, and only one founded by a colonist.
    ColonyRec C{};
    C.colonist = 7;
    C.alive = 20;
    C.alive_copies = 10;
    LineageClock k4;
    CHECK(!colony_settles(C, 300, 50, c, &k4) && k4.held_since == 300);
    CHECK(!colony_settles(C, 349, 50, c, &k4));
    CHECK(colony_settles(C, 350, 50, c, &k4) && k4.established == 350);
    C.alive_copies = 9;
    CHECK(colony_settles(C, 360, 50, c, &k4) && k4.held_since == ~0ull); // settled for good
    C.alive_copies = 10;
    C.colonist = 0;
    LineageClock k5;
    CHECK(!colony_settles(C, 300, 50, c, &k5) && !colony_settles(C, 400, 50, c, &k5) && k5.held_since == ~0ull);

    // A replicator in a test world reaches stage 4 under short criteria,
    // checked every tick.
    World w;
    Config cfg;
    CHECK(build(w, cfg));
    Observer obs;
    obs.init(ObserverSettings{});
    w.watch(&obs);
    w.start_threads(4);
    Array<LineageClock> clocks;
    u32 best = 0;
    for (u32 t = 0; t < 120; ++t) {
        w.tick();
        while (clocks.size() < obs.lineage_count())
            clocks.push(LineageClock{});
        for (u32 i = 0; i < obs.lineage_count(); ++i) {
            const LineageRec& r = obs.lineage_at(i);
            const u32 s = lineage_stage(r, w.tick_count(), w.place(r.place).os.live_programs(), c, &clocks[i]);
            best = s > best ? s : best;
        }
    }
    CHECK(best >= 4);
}

// Stage 5's count (13.3; build-plan-observe.md step 4): a lineage's groups
// made while its place is crowded. With every genome kept at its birth,
// each new kind counts as it is born; keeping those whose lines last 3
// generations, each counts when it is kept, two generations on, and the
// kinds whose lines haven't lasted yet don't.
u64 groups_full_after(u32 keep_generations) {
    World w;
    Config cfg;
    if (!build(w, cfg, "world.places = 1\n", false))
        return ~0ull;
    Place& p = w.place(0);
    while (p.mem.free_bytes() >= p.mem.size() / 10 && p.mem.alloc(99, 256, 2, 0)) {
    }
    if (p.mem.free_bytes() >= p.mem.size() / 10)
        return ~0ull;
    Observer obs;
    ObserverSettings s;
    s.keep_generations = keep_generations;
    obs.init(s);
    for (u32 i = 1; i <= 5; ++i) // a line of five kinds, each a mutant of the one before
        keep_birth(obs, p, i, i - 1, i);
    const LineageRec* L = obs.lineage(1);
    return L && L->groups == L->groups_full ? L->groups_full : ~0ull;
}

void test_stage5_count() {
    CHECK(groups_full_after(1) == 5);
    CHECK(groups_full_after(3) == 3);
}

} // namespace

// The genebank's two files held in memory, for genome_read.
struct MemBank {
    Array<u64> at;
    Array<u32> len;
    Array<u8> bytes;
};

bool mem_row(void* ctx, u64 i, u64* bytes_at, u32* len) {
    const MemBank& b = *static_cast<const MemBank*>(ctx);
    if (i >= b.at.size())
        return false;
    *bytes_at = b.at[static_cast<usize>(i)];
    *len = b.len[static_cast<usize>(i)];
    return true;
}

bool mem_read(void* ctx, u64 at, u64 n, u8* out) {
    const MemBank& b = *static_cast<const MemBank*>(ctx);
    if (at > b.bytes.size() || n > b.bytes.size() - at)
        return false;
    memcpy(out, b.bytes.data() + at, static_cast<usize>(n));
    return true;
}

GenomeFile mem_file(MemBank& b) {
    GenomeFile f;
    f.ctx = &b;
    f.rows = b.at.size();
    f.bytes = b.bytes.size();
    f.row = mem_row;
    f.read = mem_read;
    return f;
}

// g's entry against a base, read back: the bytes it took, or 0 if it didn't
// come back as g. `steps`: 1 if it went as a difference, 0 whole.
u32 entry_round(const u8* g, u32 n, const u8* base, u32 nb, u32* steps) {
    Array<u8> e, back;
    *steps = genome_entry(g, n, base, nb, 0, 1, 16, &e);
    u32 s = 0;
    u64 far = 0;
    usize body = 0;
    if (!genome_entry_head(e.data(), e.size(), &s, &far, &body) || s != *steps || far != (s ? 1u : 0u))
        return 0;
    if (s == 0)
        return e.size() - body == n && (n == 0 || memcmp(e.data() + body, g, n) == 0) ? static_cast<u32>(e.size()) : 0;
    if (!genome_patch(e.data() + body, e.size() - body, base, nb, n, &back) || back.size() != n ||
        (n && memcmp(back.data(), g, n) != 0))
        return 0;
    return static_cast<u32>(e.size());
}

u32 lcg(u64* state) {
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<u32>(*state >> 33);
}

// The kept genomes' bytes as differences (genome_code.h; build-plan-gate5.md
// 11): numbers, each kind of change by hand, changes at random, lines with
// their whole copies, and what damage to one entry spoils.
void test_genome_code() {
    // Numbers.
    const u64 nums[] = {0, 1, 127, 128, 300, 16383, 16384, 0xFFFFFFFFull, ~0ull};
    Array<u8> b;
    for (u32 i = 0; i < 9; ++i)
        put_varint(&b, nums[i]);
    usize at = 0;
    bool nums_back = true;
    for (u32 i = 0; i < 9; ++i) {
        u64 v = 0;
        nums_back = nums_back && get_varint(b.data(), b.size(), &at, &v) && v == nums[i];
    }
    u64 none = 0;
    CHECK(nums_back && at == b.size() && b.size() == 1 + 1 + 1 + 2 + 2 + 2 + 3 + 5 + 10);
    CHECK(!get_varint(b.data(), b.size(), &at, &none)); // past the end
    // Each kind of change to a genome of 64 bytes, by hand.
    u8 base[64], g[96];
    for (u32 j = 0; j < 64; ++j)
        base[j] = static_cast<u8>(j * 7 + 3);
    u32 steps = 0;
    CHECK(entry_round(base, 64, base, 64, &steps) == 3 && steps == 1); // the same: steps, back, copy 64
    memcpy(g, base, 64);
    g[20] ^= 0x55;
    CHECK(entry_round(g, 64, base, 64, &steps) == 7 && steps == 1); // one byte changed
    memcpy(g, base, 30);
    g[30] = 1, g[31] = 2, g[32] = 3;
    memcpy(g + 33, base + 30, 34);
    CHECK(entry_round(g, 67, base, 64, &steps) == 9 && steps == 1); // three bytes put in
    memcpy(g, base, 40);
    memcpy(g + 40, base + 45, 19);
    CHECK(entry_round(g, 59, base, 64, &steps) == 6 && steps == 1); // five taken out
    memcpy(g, base, 64);
    for (u32 j = 54; j < 64; ++j)
        g[j] = static_cast<u8>(200 + j);
    CHECK(entry_round(g, 64, base, 64, &steps) == 15 && steps == 1); // its last ten changed
    memcpy(g, base, 64);
    g[5] ^= 1;
    g[50] ^= 1;
    const u32 two = entry_round(g, 64, base, 64, &steps);
    CHECK(two == 11 && steps == 1); // two changes far apart
    memcpy(g, base, 64);
    memcpy(g + 64, base + 32, 32);
    const u32 doubled = entry_round(g, 96, base, 64, &steps);
    CHECK(doubled > 0 && doubled <= 40 && steps == 1); // its second half again: the tail fresh
    for (u32 j = 0; j < 64; ++j)
        g[j] = static_cast<u8>(base[j] + 1 + j);
    CHECK(entry_round(g, 64, base, 64, &steps) == 65 && steps == 0); // nothing alike: whole
    CHECK(entry_round(g, 0, base, 64, &steps) == 1 && steps == 0);   // empty: whole, one byte
    CHECK(entry_round(g, 64, nullptr, 0, &steps) == 65 && steps == 0); // no base
    CHECK(entry_round(g, 2, base, 0, &steps) == 3 && steps == 0);      // an empty base
    // Changes at random: always the genome again, and never longer than whole.
    u64 state = 0xC0DE5EEDull;
    u32 right = 0, as_difference = 0;
    u64 whole_bytes = 0, entry_bytes = 0;
    Array<u8> rb, rg;
    const u32 kRounds = 3000;
    for (u32 round = 0; round < kRounds; ++round) {
        const u32 nb = lcg(&state) % 200;
        rb.clear();
        for (u32 j = 0; j < nb; ++j)
            rb.push(static_cast<u8>(lcg(&state)));
        rg.clear();
        rg.append(rb.data(), rb.size());
        if (round % 10 == 9) { // no kin of the base
            rg.clear();
            for (u32 j = lcg(&state) % 200; j > 0; --j)
                rg.push(static_cast<u8>(lcg(&state)));
        }
        for (u32 edits = lcg(&state) % 5; edits > 0 && round % 10 != 9; --edits) {
            const u32 where = rg.size() ? lcg(&state) % static_cast<u32>(rg.size()) : 0;
            const u32 many = 1 + lcg(&state) % 10;
            const u32 kind = lcg(&state) % 3;
            Array<u8> next;
            next.append(rg.data(), where);
            if (kind != 1) // changed or put in: fresh bytes
                for (u32 j = 0; j < many; ++j)
                    next.push(static_cast<u8>(lcg(&state)));
            const u32 drop = kind == 2 ? 0 : many; // changed or taken out: the old bytes go
            if (where + drop < rg.size())
                next.append(rg.data() + where + drop, rg.size() - where - drop);
            rg.clear();
            rg.append(next.data(), next.size());
        }
        const u32 took = entry_round(rg.data(), static_cast<u32>(rg.size()), rb.data(), nb, &steps);
        right += took && took <= rg.size() + 1 ? 1 : 0;
        as_difference += steps;
        whole_bytes += rg.size() + 1;
        entry_bytes += took;
    }
    CHECK(right == kRounds && as_difference > kRounds * 7 / 10 && entry_bytes * 3 < whole_bytes);
    // A line of 50, each a point mutant of the one before: a whole copy at
    // the 1st, 17th, 33rd and 49th, differences between, each read back.
    MemBank bank;
    bank.bytes.append(kGenomeFileTag, kGenomeTagBytes);
    Array<u8> truth; // every genome, 40 bytes each
    Array<u8> row_steps;
    u8 cur[40], prev[40];
    for (u32 j = 0; j < 40; ++j)
        cur[j] = static_cast<u8>(j * 5 + 1);
    for (u32 k = 0; k < 50; ++k) {
        memcpy(prev, cur, 40);
        if (k)
            cur[(k * 13) % 40] ^= static_cast<u8>(0x40 + k);
        bank.at.push(bank.bytes.size());
        bank.len.push(40);
        row_steps.push(static_cast<u8>(genome_entry(cur, 40, k ? prev : nullptr, 40, k ? row_steps[k - 1] : 0, 1,
                                                    16, &bank.bytes)));
        truth.append(cur, 40);
    }
    // And a second child of row 5, 45 rows back.
    memcpy(cur, truth.data() + 5 * 40, 40);
    cur[0] ^= 0xFF;
    bank.at.push(bank.bytes.size());
    bank.len.push(40);
    row_steps.push(static_cast<u8>(genome_entry(cur, 40, truth.data() + 5 * 40, 40, row_steps[5], 45, 16,
                                                &bank.bytes)));
    truth.append(cur, 40);
    GenomeFile file = mem_file(bank);
    u32 read_right = 0;
    bool steps_right = row_steps[50] == 6;
    Array<u8> out;
    for (u32 k = 0; k < 51; ++k) {
        u32 st = 99;
        read_right += genome_read(file, k, &out, &st) && out.size() == 40 &&
                              memcmp(out.data(), truth.data() + k * 40, 40) == 0 && st == row_steps[k]
                          ? 1
                          : 0;
        if (k < 50)
            steps_right = steps_right && row_steps[k] == k % 16;
    }
    CHECK(read_right == 51 && steps_right && bank.bytes.size() < 8 + 51 * 41 / 3);
    CHECK(!genome_read(file, 51, &out)); // no such row
    // Damage to row 20's entry (4 on from row 16's whole copy): it and the
    // rows built on it, 21 to 31, can't be read or come back as other bytes,
    // which their hashes show; every other row reads as before, the second
    // child of row 5 among them.
    bank.bytes[static_cast<usize>(bank.at[20]) + 3] ^= 0x21;
    u32 spoiled = 0, sound = 0;
    for (u32 k = 0; k < 51; ++k) {
        const bool ok = genome_read(file, k, &out) && out.size() == 40 &&
                        hash_bytes(out.data(), 40) == hash_bytes(truth.data() + k * 40, 40);
        if (k >= 20 && k <= 31)
            spoiled += ok ? 0 : 1;
        else
            sound += ok ? 1 : 0;
    }
    CHECK(spoiled == 12 && sound == 39);
    // A line that never comes to a whole copy, or runs off the file: refused.
    bank.bytes[static_cast<usize>(bank.at[20]) + 3] ^= 0x21;
    bank.bytes[static_cast<usize>(bank.at[16])] = 1; // row 16 claims to be a difference too
    CHECK(!genome_read(file, 18, &out) && genome_read(file, 15, &out) && genome_read(file, 40, &out));
    printf("  observe: kept genomes as differences: %u changes at random each read back, %u%% of them as a "
           "difference, in %llu%% of the bytes whole copies take; a line of 50 point mutants with a whole copy "
           "every 16th in %llu bytes (2,050 whole); damage to one entry spoiled the 12 genomes up to the next whole "
           "copy and no other\n",
           kRounds, as_difference * 100 / kRounds, static_cast<unsigned long long>(entry_bytes * 100 / whole_bytes),
           static_cast<unsigned long long>(bank.bytes.size() - 8));
}

// The same through the observers: a line of mutants kept generation by
// generation is recorded as differences with a whole copy every 16th, a
// genome with no parent or nothing like its parent whole, each read back
// from the record; and a save read back goes on with the same entries.
void test_genome_differences() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Place& p = w.place(0);
    Observer obs, whole;
    ObserverSettings s;
    s.keep_generations = 1; // every genome kept at its first birth
    CHECK(s.genome_key_every == 16);
    obs.init(s);
    ObserverSettings s1 = s;
    s1.genome_key_every = 1;
    whole.init(s1);
    u8 tag[kGenomeTagBytes];
    CHECK(obs.genebank_bytes.rows() == kGenomeTagBytes && obs.genebank_bytes.get_many(0, kGenomeTagBytes, tag) &&
          memcmp(tag, kGenomeFileTag, kGenomeTagBytes) == 0);
    u8 g[48];
    for (u32 j = 0; j < 48; ++j)
        g[j] = static_cast<u8>(j * 9 + 2);
    Array<u8> truth;
    const u64 tick = p.env.tick;
    for (u32 k = 0; k < 40; ++k) {
        if (k)
            g[(k * 13) % 48] ^= static_cast<u8>(0x40 + k);
        bytes_birth(obs, p, 100 + k, k ? 99 + k : 0, 0x77000000ull + k, g, 48);
        truth.append(g, 48);
    }
    p.env.tick = tick;
    for (u32 k = 0; k < 40; ++k)
        bytes_birth(whole, p, 100 + k, k ? 99 + k : 0, 0x77000000ull + k, truth.data() + k * 48, 48);
    // A second child of the 6th; a child of the 1st with nothing of it; and
    // a program with no parent.
    memcpy(g, truth.data() + 5 * 48, 48);
    g[47] ^= 0x0F;
    bytes_birth(obs, p, 200, 105, 0x77000100ull, g, 48);
    truth.append(g, 48);
    for (u32 j = 0; j < 48; ++j)
        g[j] = static_cast<u8>(truth[j] + 1 + j);
    bytes_birth(obs, p, 201, 100, 0x77000101ull, g, 48);
    truth.append(g, 48);
    g[0] ^= 1;
    bytes_birth(obs, p, 202, 0, 0x77000102ull, g, 48);
    truth.append(g, 48);
    CHECK(obs.genebank.size() == 43 && whole.genebank.size() == 40);
    Array<u8> out;
    u32 right = 0, whole_right = 0;
    bool steps_right = true;
    for (u32 k = 0; k < 43; ++k) {
        u32 st = 99;
        right += obs.kept_genome(k, &out, &st) && out.size() == 48 &&
                 memcmp(out.data(), truth.data() + k * 48, 48) == 0;
        const u32 want = k < 40 ? k % 16 : k == 40 ? 6u : 0u;
        steps_right = steps_right && st == want && obs.genome(obs.genebank[k].hash)->steps == want &&
                      obs.genome(obs.genebank[k].hash)->bank_row == k;
    }
    for (u32 k = 0; k < 40; ++k) {
        u32 st = 99;
        whole_right += whole.kept_genome(k, &out, &st) && st == 0 && out.size() == 48 &&
                       memcmp(out.data(), truth.data() + k * 48, 48) == 0;
    }
    CHECK(right == 43 && steps_right && whole_right == 40);
    CHECK(whole.genebank_bytes.rows() == 8 + 40 * 49 && obs.genebank_bytes.rows() < 8 + 43 * 49 / 3);
    CHECK(obs.genebank[40].parent_genome == 0x77000005ull && !obs.kept_genome(43, &out));
    // Saved and read back: the record reads the same, and a birth after it
    // is recorded the same by both.
    FileOut fo;
    CHECK(fo.open("test_observe_differences.obs"));
    obs.save(fo);
    CHECK(fo.close());
    Observer back;
    back.init(s);
    FileIn in;
    CHECK(in.open("test_observe_differences.obs") && back.load(in));
    memcpy(g, truth.data() + 39 * 48, 48);
    g[7] ^= 0x33;
    const u64 then = p.env.tick;
    bytes_birth(obs, p, 300, 139, 0x77000200ull, g, 48);
    p.env.tick = then;
    bytes_birth(back, p, 300, 139, 0x77000200ull, g, 48);
    truth.append(g, 48);
    bool same = back.genebank.size() == 44 && obs.genebank.size() == 44 &&
                back.genebank_bytes.rows() == obs.genebank_bytes.rows();
    for (u64 i = 0; same && i < obs.genebank_bytes.rows(); ++i) {
        u8 x = 0, y = 1;
        same = obs.genebank_bytes.get(i, &x) && back.genebank_bytes.get(i, &y) && x == y;
    }
    u32 st = 99;
    CHECK(same && back.kept_genome(43, &out, &st) && st == 8 && memcmp(out.data(), g, 48) == 0);
    printf("  observe: a line of 40 mutants recorded as differences with a whole copy every 16th, in %llu bytes "
           "where whole copies took %llu; each read back, a save too\n",
           static_cast<unsigned long long>(obs.genebank_bytes.rows()),
           static_cast<unsigned long long>(8 + 44 * 49));
}

// Writing every second sample (observe.write_every; build-plan-gate5.md 11):
// sampling is the same; the rows written are the samples at every second
// sample's tick as they stand, a place's voltage row covers both samples'
// ticks, a last sample asked for is always written, and a save taken
// between two written rows goes on to the same rows.
void test_write_every() {
    const char* extra = "supply.sag = 50\nsupply.day = 600\nsupply.night = 0.2\nsupply.swing = 0.25\n"
                        "supply.swing_period = 200\nsupply.dropouts = 1000\nsupply.dropout.min = 5\n"
                        "supply.dropout.max = 120\nsupply.blackout_share = 0.5\n";
    World a, b;
    Config ca, cb;
    CHECK(build(a, ca, extra) && build(b, cb, extra));
    Observer one, two;
    ObserverSettings s;
    s.sample_every = 10;
    one.init(s);
    ObserverSettings s2 = s;
    s2.write_every = 2;
    two.init(s2);
    a.watch(&one);
    b.watch(&two);
    a.start_threads(2);
    b.start_threads(2);
    const u32 places = a.place_count();
    bool last_same = true;
    Observer back; // two, saved at tick 250: between its rows of ticks 240 and 260
    World c;
    Config cc;
    CHECK(build(c, cc, extra));
    for (u32 t = 1; t <= 505; ++t) {
        a.tick();
        b.tick();
        if (t > 250)
            c.tick();
        const Sample x = one.last_sample(), y = two.last_sample();
        last_same = last_same && memcmp(&x, &y, sizeof x) == 0 && one.samples_taken() == two.samples_taken();
        if (t == 250) {
            ByteWriter world, counters;
            b.save(world);
            b.save_counters(counters);
            ByteReader win(world.data(), world.size());
            CHECK(c.load(win));
            FileOut fo;
            CHECK(fo.open("test_observe_write_every.obs"));
            two.save(fo);
            CHECK(fo.close());
            back.init(s2);
            c.watch(&back);
            ByteReader cin(counters.data(), counters.size());
            CHECK(c.load_counters(cin));
            FileIn in;
            CHECK(in.open("test_observe_write_every.obs") && back.load(in));
            Observer other; // and not under another number
            other.init(s);
            FileIn in2;
            CHECK(in2.open("test_observe_write_every.obs") && !other.load(in2));
            c.start_threads(2);
        }
    }
    CHECK(a.hash() == b.hash() && b.hash() == c.hash() && last_same);
    CHECK(one.samples_taken() == 50 && one.samples.size() == 50 && two.samples.size() == 25 &&
          back.samples.size() == 25 && back.samples_taken() == 50);
    u32 rows_same = 0, volts_right = 0, back_same = 0;
    for (u32 i = 0; i < 25; ++i) {
        const Sample x = one.samples[2 * i + 1], y = two.samples[i], z = back.samples[i];
        rows_same += x.tick == 20ull * (i + 1) && memcmp(&x, &y, sizeof x) == 0 ? 1 : 0;
        back_same += memcmp(&y, &z, sizeof y) == 0 ? 1 : 0;
        for (u32 k = 0; k < places; ++k) {
            const PlaceVolts v1 = one.volt(2 * i, k), v2 = one.volt(2 * i + 1, k), v = two.volt(i, k);
            const PlaceVolts vb = back.volt(i, k);
            const i32 low = v1.low < v2.low ? v1.low : v2.low, mean = (v1.mean + v2.mean) / 2;
            volts_right += v.low == low && v.mean >= mean - 1 && v.mean <= mean + 1 && vb.low == v.low &&
                                   vb.mean == v.mean
                               ? 1
                               : 0;
        }
    }
    CHECK(rows_same == 25 && back_same == 25 && volts_right == 25 * places);
    CHECK(two.volts.size() == 25ull * places && one.volts.size() == 50ull * places);
    // What is worked out at a sample is the same: the windows, the totals.
    CHECK(one.windows.size() == two.windows.size() && one.ground_windows.size() == two.ground_windows.size() &&
          one.total_births == two.total_births && one.program_samples == two.program_samples &&
          back.program_samples == two.program_samples && back.total_births == two.total_births);
    // A last sample, asked for at tick 505: written by both.
    one.sample(a);
    two.sample(b);
    const Sample x = one.samples.back(), y = two.samples.back();
    CHECK(one.samples.size() == 51 && two.samples.size() == 26 && x.tick == 505 && memcmp(&x, &y, sizeof x) == 0 &&
          two.volts.size() == 26ull * places);
    printf("  observe: writing every second sample: 50 samples taken either way and the latest always the same; 25 "
           "rows written, each the sample at its tick, each place's voltage row over both samples' ticks; a save "
           "between two rows went on to the same; a last sample asked for was written\n");
}

// A save leaves the groups' index out, and a load works it out again
// (build-plan-gate5.md 11). A group is its first genome's runs for good, so
// when forgetting has let that genome go the runs can't be had from the
// reference the group has now: the save holds them. Here x founds a group,
// x2 joins it, x goes; x3 is like x (27 of its 30 runs) and not like x2 (22
// of 30), so it joins only a group that still has x's runs, and it does in
// the observer read back from a save as in the one never saved.
void test_index_rebuilt() {
    World w;
    Config cfg;
    CHECK(build(w, cfg, "world.places = 1\n", false));
    Place& p = w.place(0);
    u8 x[32], x2[32], x3[32];
    for (u32 j = 0; j < 32; ++j)
        x[j] = x2[j] = x3[j] = static_cast<u8>(200 - 5 * j);
    x2[0] = 1, x2[1] = 2, x2[29] = 3, x2[30] = 4, x2[31] = 5; // 25 of x's runs
    x3[10] = 9;
    const u64 hx = kKeepHash + 2000, hx2 = kKeepHash + 2001, hx3 = kKeepHash + 2002;
    CHECK(Observer::similarity(x, 32, x2, 32) > 0.82 && Observer::similarity(x, 32, x3, 32) > 0.89 &&
          Observer::similarity(x2, 32, x3, 32) < 0.75);
    Observer obs;
    ObserverSettings s;
    s.forget = true;
    s.forget_floor = 2;
    obs.init(s);
    bytes_birth(obs, p, 1, 0, hx, x, 32);
    bytes_birth(obs, p, 2, 1, hx, x, 32);
    bytes_birth(obs, p, 3, 2, hx, x, 32);
    bytes_birth(obs, p, 5, 3, hx2, x2, 32);
    bytes_birth(obs, p, 6, 5, hx2, x2, 32);
    bytes_birth(obs, p, 7, 6, hx2, x2, 32);
    bytes_birth(obs, p, 8, 7, hx2, x2, 32);
    CHECK(obs.genome(hx) && obs.genome(hx2) && obs.genome(hx)->state == GenomeState::kept &&
          obs.genome(hx2)->state == GenomeState::kept && obs.genome(hx2)->group == obs.genome(hx)->group &&
          obs.group_count() == 1 && obs.group_reference(0).hash == hx);
    // x's carriers die, and those with x in the generations before them.
    const u64 gone[] = {1, 2, 3, 5, 6};
    for (u32 i = 0; i < 5; ++i)
        keep_death(obs, p, gone[i]);
    obs.sample(w);
    CHECK(!obs.genome(hx) && obs.genomes_forgotten() == 1 && obs.group_count() == 1 &&
          obs.group_reference(0).hash == hx2);
    FileOut out;
    CHECK(out.open("test_observe_index.obs"));
    obs.save(out);
    CHECK(out.close());
    Observer back;
    back.init(s);
    FileIn in;
    CHECK(in.open("test_observe_index.obs") && back.load(in));
    CHECK(back.group_count() == 1 && back.group_reference(0).hash == hx2);
    const u64 tick = p.env.tick;
    for (u32 k = 0; k < 2; ++k) {
        Observer& o = k ? back : obs;
        p.env.tick = tick;
        bytes_birth(o, p, 20, 8, hx3, x3, 32);
        bytes_birth(o, p, 21, 20, hx3, x3, 32);
        bytes_birth(o, p, 22, 21, hx3, x3, 32);
        // And a genome like nothing kept founds the second group in both.
        keep_birth(o, p, 30, 0, 77);
        keep_birth(o, p, 31, 30, 77);
        keep_birth(o, p, 32, 31, 77);
        CHECK(o.genome(hx3) && o.genome(hx3)->state == GenomeState::kept && o.genome(hx3)->group == 0 &&
              o.group_count() == 2 && is_kept(o, 77) && kept_genome(o, 77)->group == 1);
    }
    // Saved again, the two are the same file: what a load works out is what
    // the run that never stopped has.
    FileOut a, b;
    CHECK(a.open("test_observe_index_a.obs") && b.open("test_observe_index_b.obs"));
    obs.save(a);
    back.save(b);
    CHECK(a.close() && b.close());
    Array<u8> fa, fb;
    CHECK(read_file("test_observe_index_a.obs", &fa) && read_file("test_observe_index_b.obs", &fb) &&
          fa.size() == fb.size() && memcmp(fa.data(), fb.data(), fa.size()) == 0);
    printf("  observe: a save without the groups' index, read back, grouped the next genomes as the observer never "
           "saved did: one like a group's first genome, since let go, joined it by the runs the save kept, and the "
           "two saved again to the same %llu bytes\n",
           static_cast<unsigned long long>(fa.size()));
}

void suite_observe() {
    test_stages();
    test_ladder();
    test_family_tree();
    test_lineages_and_stages();
    test_min_genome();
    test_handover();
    test_groups();
    test_group_index();
    test_scavenger_and_cross_owner();
    test_scavenger_kept();
    test_keep_generations();
    test_forget();
    test_index_rebuilt();
    test_genome_code();
    test_genome_differences();
    test_write_every();
    test_stage5_count();
    test_samples_and_no_effect();
    test_turnover_windows();
    test_turnover_world();
    test_ground_windows();
    test_ground_world();
    test_ground_supply();
    test_ground_lead();
    test_volts();
}
