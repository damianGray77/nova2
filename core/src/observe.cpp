#include "nova2/observe.h"

#include <cmath>   // ceil
#include <cstdio>  // snprintf
#include <cstdlib> // qsort
#include <cstring> // memcmp
#include <intrin.h> // _mm_prefetch

#include "nova2/compress.h"
#include "nova2/hash.h"
#include "nova2/io.h"
#include "nova2/place.h"
#include "nova2/world.h"

namespace nova2 {

namespace {

u64 pair_key(u64 origin, u32 group) { return mix64(origin * kGolden ^ (static_cast<u64>(group) + 0x51ED27ull)); }

u32 load32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

// The intersection of two sorted, distinct runs of numbers.
u32 common(const u64* a, u32 na, const u64* b, u32 nb) {
    u32 i = 0, j = 0, c = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j]) {
            ++i;
        } else if (a[i] > b[j]) {
            ++j;
        } else {
            ++c;
            ++i;
            ++j;
        }
    }
    return c;
}

} // namespace

void Observer::init(const ObserverSettings& s) {
    s_ = s;
    programs_.clear();
    prog_.clear();
    free_prog_.clear();
    dead_senders_.clear();
    sends = sends_port0 = 0;
    lineages_.clear();
    lin_.clear();
    free_lin_.clear();
    finished_lineages.clear();
    shallow_lineages = shallow_births = 0;
    genomes_.clear();
    gen_.clear();
    bank_.clear();
    free_gen_.clear();
    bank_dead_ = kept_n_ = held_n_ = 0;
    forget_at_ = groups_ever_ = forgotten_ = 0;
    genebank.clear();
    genebank_bytes.open_memory(1);
    genebank_bytes.add_many(kGenomeFileTag, kGenomeTagBytes);
    groups_.clear();
    grams_.clear();
    run_lists_.clear();
    run_pool_.clear();
    run_pool_.push(0); // offset 0: no block
    gramless_.clear();
    listed_.clear();
    lineage_groups_.clear();
    colonies_.clear();
    colony_clocks_.clear();
    births.clear();
    deaths.clear();
    samples.clear();
    last_ = Sample{};
    taken_ = 0;
    turnover.clear();
    windows.clear();
    ground.clear();
    ground_windows.clear();
    lead_due_.clear();
    total_births = total_faithful = total_deaths = scavenger = 0;
    written = region_copies = program_copies = near_copies = image_copies = handovers = 0;
    for (u32 i = 0; i < kDeathCauses; ++i)
        deaths_by_cause[i] = 0;
    for (u32 i = 0; i < kBirthCauses; ++i)
        births_by_cause[i] = 0;
    cold_starts = 0;
    recvs = overruns = overruns_unknown = reader_births = injection_births = 0;
    program_samples = reader_program_samples = injections_seen = 0;
    injected_programs = 0;
    for (u32 i = 0; i < kReaches; ++i)
        reached[i] = 0;
    over_.clear();
    over_bytes_.clear();
    injected_.clear();
    dead_talkers_.clear();
    cascade_watch.init(0);
    tally5 = WatchTotals{};
    respawn_log.clear();
    joinings.clear();
    event_log.clear();
    fail_seen_.clear();
    carried_births.clear();
    calls_rows.clear();
    lin_held_.clear();
    col_held_.clear();
    last_cut_.clear();
    last_trip_.clear();
    patch_seen_.clear();
    strait_seen_.clear();
    adj_begin_.clear();
    adj_.clear();
    adj_respawns_ = ~0u;
    boot_writer_.clear();
    volts.clear();
    volt_places = 0;
    volt_sum_.clear();
    records_dir_[0] = 0; // the records in memory, until records_to says otherwise
}

void Observer::records_path(const char* kind, char* out, usize n) const {
    snprintf(out, n, "%s/seed-%llu.%s", records_dir_, static_cast<unsigned long long>(records_seed_), kind);
}

bool Observer::records_to(const char* folder, u64 seed, double flush_seconds, bool carry_on) {
    if (strlen(folder) >= sizeof records_dir_ - 32)
        return false;
    snprintf(records_dir_, sizeof records_dir_, "%s", folder);
    records_seed_ = seed;
    records_flush_ = flush_seconds;
    if (carry_on)
        return true; // load opens them, cut back to its save
    char path[sizeof records_dir_];
    records_path("samples", path, sizeof path);
    bool ok = samples.stream().open_file(path, sizeof(Sample), 0, flush_seconds);
    records_path("volts", path, sizeof path);
    ok = volts.stream().open_file(path, sizeof(PlaceVolts), 0, flush_seconds) && ok;
    records_path("windows", path, sizeof path);
    ok = windows.stream().open_file(path, sizeof(TurnoverWindow), 0, flush_seconds) && ok;
    records_path("ground", path, sizeof path);
    ok = ground_windows.stream().open_file(path, sizeof(GroundWindow), 0, flush_seconds) && ok;
    records_path("genomes", path, sizeof path);
    ok = genebank.stream().open_file(path, sizeof(GenebankRow), 0, flush_seconds) && ok;
    records_path("genome_bytes", path, sizeof path);
    ok = genebank_bytes.open_file(path, 1, 0, flush_seconds) && ok;
    genebank_bytes.add_many(kGenomeFileTag, kGenomeTagBytes);
    records_path("lineages", path, sizeof path);
    ok = finished_lineages.stream().open_file(path, sizeof(LineageRec), 0, flush_seconds) && ok;
    return ok;
}

bool Observer::flush_records() {
    const bool a = samples.stream().flush(), b = volts.stream().flush(), c = windows.stream().flush();
    const bool d = genebank.stream().flush(), e = genebank_bytes.flush(), g = finished_lineages.stream().flush();
    const bool h = ground_windows.stream().flush();
    // The shallow lineages' tally: small, so written whole each time.
    bool t = true;
    if (records_dir_[0]) {
        char path[sizeof records_dir_];
        records_path("lineage_tally", path, sizeof path);
        FILE* f = fopen(path, "wb");
        t = f != nullptr;
        if (f) {
            fprintf(f, "lineages,births\n%llu,%llu\n", static_cast<unsigned long long>(shallow_lineages),
                    static_cast<unsigned long long>(shallow_births));
            t = fclose(f) == 0;
        }
    }
    return a && b && c && d && e && g && h && t;
}

bool Observer::records_ok() const {
    return samples.stream().ok() && volts.stream().ok() && windows.stream().ok() && genebank.stream().ok() &&
           genebank_bytes.ok() && finished_lineages.stream().ok() && ground_windows.stream().ok();
}

void Observer::grams_of(const u8* g, u32 len, Array<u64>* out) {
    out->clear();
    for (u32 i = 0; i + 3 <= len; ++i)
        out->push(static_cast<u64>(g[i]) | static_cast<u64>(g[i + 1]) << 8 | static_cast<u64>(g[i + 2]) << 16);
    sort_u64(out->data(), out->size());
    usize n = 0;
    for (usize i = 0; i < out->size(); ++i)
        if (n == 0 || (*out)[n - 1] != (*out)[i])
            (*out)[n++] = (*out)[i];
    out->resize(n);
}

double Observer::similarity(const u8* a, u32 na, const u8* b, u32 nb) {
    Array<u64> ga, gb;
    grams_of(a, na, &ga);
    grams_of(b, nb, &gb);
    if (ga.empty() || gb.empty())
        return na == nb && (na == 0 || memcmp(a, b, na) == 0) ? 1.0 : 0.0;
    const u32 c = common(ga.data(), static_cast<u32>(ga.size()), gb.data(), static_cast<u32>(gb.size()));
    const usize larger = ga.size() > gb.size() ? ga.size() : gb.size();
    return static_cast<double>(c) / static_cast<double>(larger);
}

// Greedy grouping (13.2): the first group whose reference shares enough of
// the genome's 3-byte runs, or a new group with this genome as its reference.
// A genome under 3 bytes has no runs, and joins a group whose reference has
// none either and is the same bytes.
//
// The index finds the same group as a scan of every group in order, without
// one. A genome of n runs joins a group only if they share at least
// need = ceil(threshold × n) runs (the larger set has n or more), so the
// group holds at least one of any n - need + 1 of the genome's runs. The
// groups on the lists of its n - need + 1 rarest runs are therefore every
// group it could join; they're checked in group order, as the scan would.
u32 Observer::assign_group(const u8* g, u32 len, u32 genome) {
    grams_of(g, len, &scratch_);
    const u32 n = static_cast<u32>(scratch_.size());
    if (n == 0) {
        for (usize i = 0; i < gramless_.size(); ++i) {
            const GenomeRec& ref = gen_[groups_[gramless_[i]].genome];
            if (ref.len == len && (len == 0 || memcmp(bank_.data() + ref.at, g, len) == 0))
                return gramless_[i];
        }
        return new_group(0, genome);
    }
    const u32 need = static_cast<u32>(ceil(s_.group_threshold * n));
    if (need == 0) { // a threshold of 0 or less (its key's range starts at 0.01): any group with runs
        for (u32 k = 0; k < groups_.size(); ++k)
            if (groups_[k].grams_n > 0)
                return k;
        return new_group(n, genome);
    }
    // Its runs, rarest first: each list's length, then the run's place in
    // scratch_.
    rarity_.clear();
    for (u32 i = 0; i < n; ++i) {
        const RunList* l = run_lists_.find(scratch_[i]);
        rarity_.push(static_cast<u64>(l ? l->n : 0) << 32 | i);
    }
    sort_u64(rarity_.data(), rarity_.size());
    candidates_.clear();
    const u32 rarest = need <= n ? n - need + 1 : 1;
    for (u32 j = 0; j < rarest; ++j) {
        const RunList* l = run_lists_.find(scratch_[static_cast<u32>(rarity_[j])]);
        if (!l)
            continue;
        for (u32 b = l->head; b; b = run_pool_[b]) {
            const u32* on = run_pool_.data() + b + 3;
            for (u32 i = 0, used = run_pool_[b + 2]; i < used; ++i)
                if (!listed_[on[i]]) {
                    listed_[on[i]] = 1;
                    candidates_.push(on[i]);
                }
        }
    }
    for (usize i = 0; i < candidates_.size(); ++i)
        listed_[candidates_[i]] = 0;
    sort_u64(candidates_.data(), candidates_.size());
    for (usize i = 0; i < candidates_.size(); ++i) {
        const u32 k = static_cast<u32>(candidates_[i]);
        const GroupRec& gr = groups_[k];
        if (gr.grams_n < need) // they can't share more runs than it has
            continue;
        const u32 c = common(scratch_.data(), n, grams_.data() + gr.grams_at, gr.grams_n);
        const u32 larger = n > gr.grams_n ? n : gr.grams_n;
        if (static_cast<double>(c) >= s_.group_threshold * larger)
            return k;
    }
    return new_group(n, genome);
}

// A new group, with the genome being added as its reference: its runs are
// scratch_'s first n.
u32 Observer::new_group(u32 n, u32 genome) {
    const u32 k = static_cast<u32>(groups_.size());
    ++groups_ever_;
    GroupRec gr;
    gr.genome = genome; // the genome being kept
    gr.grams_at = static_cast<u32>(grams_.size());
    gr.grams_n = n;
    gr.own_runs = 0;
    gr.count = 0;
    grams_.append(scratch_.data(), n);
    groups_.push(gr);
    listed_.push(0);
    if (n == 0)
        gramless_.push(k);
    for (u32 i = 0; i < n; ++i)
        list_run(scratch_[i], k);
    return k;
}

// Puts a group at the end of a run's list.
void Observer::list_run(u64 run, u32 group) {
    RunList& l = run_lists_.get_or_insert(run); // a new one is zeros: empty
    if (l.tail == 0 || run_pool_[l.tail + 2] == run_pool_[l.tail + 1]) {
        u32 cap = l.tail == 0 ? 2 : run_pool_[l.tail + 1] * 2;
        if (cap > 256)
            cap = 256;
        const u32 b = static_cast<u32>(run_pool_.size());
        // Room grows by doubling, as a push's does: resize alone reserves
        // just what it's asked for, so each new block copied the whole pool.
        if (b + 3 + cap > run_pool_.capacity())
            run_pool_.reserve(b + 3 + cap > 2 * run_pool_.capacity() ? b + 3 + cap : 2 * run_pool_.capacity());
        run_pool_.resize(b + 3 + cap); // zeros: no next block, none used
        run_pool_[b + 1] = cap;
        if (l.tail)
            run_pool_[l.tail] = b;
        else
            l.head = b;
        l.tail = b;
    }
    run_pool_[l.tail + 3 + run_pool_[l.tail + 2]++] = group;
    ++l.n;
}

// A birth's genome: one more birth with a genome held or kept, or a new
// genome, held (build-plan-observe.md 3.2). Its group comes when it is kept.
u32 Observer::add_genome(const u8* g, u32 len, u64 hash, u32 place, u64 tick, u64 origin, u64 parent_genome,
                         u32 generation) {
    bool inserted = false;
    u32& found = genomes_.get_or_insert(hash, &inserted);
    if (!inserted) {
        GenomeRec& r = gen_[found];
        ++r.count;
        if (r.group != kNoGroup)
            ++groups_[r.group].count;
        return found;
    }
    GenomeRec r;
    memset(&r, 0, sizeof r);
    r.hash = hash;
    r.at = static_cast<u32>(bank_.size());
    r.len = len;
    r.group = kNoGroup;
    r.place = place;
    r.first_tick = tick;
    r.origin = origin;
    r.count = 1;
    r.alloc_min = ~0u;
    r.alloc_max = 0;
    r.parent_genome = parent_genome;
    r.generation = generation;
    r.state = GenomeState::held;
    r.kept_at = ~0ull;
    bank_.append(g, len);
    u32 slot;
    if (free_gen_.size()) {
        slot = free_gen_.back();
        free_gen_.pop();
        gen_[slot] = r;
    } else {
        slot = static_cast<u32>(gen_.size());
        gen_.push(r);
    }
    found = slot;
    ++held_n_;
    return slot;
}

// A genome kept (13.2): it joins or founds a group, goes to the genebank's
// records, and counts towards its lineage's groups, as new while `full` if
// the place of the birth that kept it is crowded (stage 5).
void Observer::keep(u32 slot, u64 tick, bool full) {
    if (gen_[slot].state != GenomeState::held)
        return;
    const u32 group = assign_group(bank_.data() + gen_[slot].at, gen_[slot].len, slot);
    GenomeRec& r = gen_[slot];
    r.state = GenomeState::kept;
    r.kept_at = tick;
    r.group = group;
    groups_[group].count += r.count;
    --held_n_;
    ++kept_n_;
    GenebankRow row;
    memset(&row, 0, sizeof row);
    row.hash = r.hash;
    row.parent_genome = r.parent_genome;
    row.origin = r.origin;
    row.first_tick = r.first_tick;
    row.kept_at = tick;
    row.bytes_at = genebank_bytes.rows();
    row.len = r.len;
    row.group = group;
    row.place = r.place;
    row.generation = r.generation;
    // Its bytes: its difference from the genome it came from, if that one
    // is kept and in memory (so it has a row, and its bytes are to hand),
    // or a whole copy (genome_code.h).
    const GenomeRec* base = nullptr;
    if (r.parent_genome && s_.genome_key_every > 1)
        if (const u32* bi = genomes_.find(r.parent_genome))
            if (*bi != slot && gen_[*bi].state == GenomeState::kept)
                base = &gen_[*bi];
    entry_.clear();
    const u64 here = genebank.size();
    r.steps = static_cast<u8>(genome_entry(bank_.data() + r.at, r.len, base ? bank_.data() + base->at : nullptr,
                                           base ? base->len : 0, base ? base->steps : 0,
                                           base ? here - base->bank_row : 0, s_.genome_key_every, &entry_));
    r.bank_row = static_cast<u32>(here);
    genebank.push(row);
    genebank_bytes.add_many(entry_.data(), entry_.size());
    if (const u32* li = lineages_.find(r.origin)) {
        bool new_pair = false;
        lineage_groups_.get_or_insert(pair_key(r.origin, group), &new_pair);
        if (new_pair) {
            ++lin_[*li].groups;
            if (full)
                ++lin_[*li].groups_full;
        }
    }
}

// The genomes a program holds: its own, and its forebears' up to
// keep_generations - 2 back. With every genome kept at birth, none.
void Observer::hold(const ProgramRec& r) {
    const u32 k = s_.keep_generations;
    if (k <= 1)
        return;
    if (u32* g = genomes_.find(r.genome))
        ++gen_[*g].holders;
    for (u32 j = 0; j + 2 < k; ++j)
        if (r.line[j])
            if (u32* g = genomes_.find(r.line[j]))
                ++gen_[*g].holders;
}

void Observer::release(const ProgramRec& r) {
    const u32 k = s_.keep_generations;
    if (k <= 1)
        return;
    u64 hashes[kMaxKeepGenerations];
    u32 n = 0;
    hashes[n++] = r.genome;
    for (u32 j = 0; j + 2 < k; ++j)
        if (r.line[j])
            hashes[n++] = r.line[j];
    for (u32 i = 0; i < n; ++i) {
        const u32* g = genomes_.find(hashes[i]);
        if (!g)
            continue;
        const u32 slot = *g;
        if (gen_[slot].holders && --gen_[slot].holders == 0 && gen_[slot].state == GenomeState::held)
            forget_genome(slot);
    }
}

// A genome no line can keep any more: its slot and bytes go. The bank is
// compacted once forgotten bytes are most of it.
void Observer::forget_genome(u32 slot) {
    GenomeRec& r = gen_[slot];
    genomes_.erase(r.hash);
    bank_dead_ += r.len;
    r.state = GenomeState::free;
    r.hash = 0;
    r.len = 0;
    --held_n_;
    free_gen_.push(slot);
    if (bank_dead_ < (1u << 20) || bank_dead_ * 2 < bank_.size())
        return;
    Array<u8> live;
    live.reserve(bank_.size() - static_cast<usize>(bank_dead_));
    for (usize i = 0; i < gen_.size(); ++i) {
        GenomeRec& g = gen_[i];
        if (g.state == GenomeState::free)
            continue;
        const u32 at = static_cast<u32>(live.size());
        live.append(bank_.data() + g.at, g.len);
        g.at = at;
    }
    bank_.clear();
    bank_.append(live.data(), live.size());
    bank_dead_ = 0;
}

// The forgetting pass (observe.h). What goes is no loss to the record: every
// kept genome is a row of the genebank's file from the tick it was kept. A
// genome let go that turns up again is met as new, held and kept afresh, and
// a group let go likewise founded afresh; so with forgetting on the groups a
// lineage counts (LineageRec::groups, and stage 5's groups_full) are those
// new to what is remembered, not new for all time.
void Observer::forget_extinct() {
    const u32 old_n = static_cast<u32>(groups_.size());
    // The kept genomes no program holds: gone. The rest, a group each.
    Array<u32> members;  // per group: its genomes left
    Array<u32> first;    // per group: the first of them, by slot
    members.resize(old_n);
    first.resize(old_n);
    for (u32 i = 0; i < gen_.size(); ++i) {
        GenomeRec& r = gen_[i];
        if (r.state != GenomeState::kept)
            continue;
        if (r.holders == 0) {
            genomes_.erase(r.hash);
            bank_dead_ += r.len;
            r.state = GenomeState::free;
            r.hash = 0;
            r.len = 0;
            r.group = kNoGroup;
            --kept_n_;
            ++forgotten_;
            free_gen_.push(i);
            continue;
        }
        if (members[r.group]++ == 0)
            first[r.group] = i;
    }
    // The groups left, in order, numbered afresh; each one's reference is a
    // genome still kept (its runs stay the first reference's: they are what
    // the group is).
    Array<u32> renumber; // old number -> new, or kNoGroup
    renumber.resize(old_n);
    Array<GroupRec> groups;
    Array<u64> grams;
    for (u32 k = 0; k < old_n; ++k) {
        if (members[k] == 0) {
            renumber[k] = kNoGroup;
            continue;
        }
        GroupRec g = groups_[k];
        const GenomeRec& ref = gen_[g.genome];
        if (ref.state != GenomeState::kept || ref.group != k) {
            g.genome = first[k];
            g.own_runs = 1; // its runs are a genome's that has gone
        }
        const u32 at = static_cast<u32>(grams.size());
        grams.append(grams_.data() + g.grams_at, g.grams_n);
        g.grams_at = at;
        renumber[k] = static_cast<u32>(groups.size());
        groups.push(g);
    }
    groups_.clear();
    groups_.append(groups.data(), groups.size());
    grams_.clear();
    grams_.append(grams.data(), grams.size());
    // The index of runs, built again from the groups left, in their order.
    index_groups();
    // Everything that names a group by its number: the kept genomes, the
    // programs, and the pairs of lineage and group, which are counted again
    // from the genomes and programs left.
    lineage_groups_.clear();
    for (u32 i = 0; i < gen_.size(); ++i) {
        GenomeRec& r = gen_[i];
        if (r.state != GenomeState::kept)
            continue;
        r.group = renumber[r.group];
        lineage_groups_.get_or_insert(pair_key(r.origin, r.group));
    }
    for (usize i = 0; i < programs_.slot_count(); ++i) {
        if (!programs_.slot_used(i))
            continue;
        ProgramRec& r = prog_[programs_.slot_value(i)];
        if (r.group == kNoGroup)
            continue;
        r.group = r.group < old_n ? renumber[r.group] : kNoGroup;
        if (r.group != kNoGroup)
            lineage_groups_.get_or_insert(pair_key(r.origin, r.group));
    }
    for (usize i = 0; i < dead_senders_.slot_count(); ++i)
        if (dead_senders_.slot_used(i)) {
            ProgramRec& r = dead_senders_.slot_value(i);
            if (r.group != kNoGroup)
                r.group = r.group < old_n ? renumber[r.group] : kNoGroup;
        }
    // The bank, without the bytes of what went.
    if (bank_dead_) {
        Array<u8> live;
        live.reserve(bank_.size() - static_cast<usize>(bank_dead_));
        for (usize i = 0; i < gen_.size(); ++i) {
            GenomeRec& g = gen_[i];
            if (g.state == GenomeState::free)
                continue;
            const u32 at = static_cast<u32>(live.size());
            live.append(bank_.data() + g.at, g.len);
            g.at = at;
        }
        bank_.clear();
        bank_.append(live.data(), live.size());
        bank_dead_ = 0;
    }
    forget_at_ = kept_n_ * 2 > s_.forget_floor ? kept_n_ * 2 : s_.forget_floor;
}

// The groups' index, from the groups and their runs, in the groups' order:
// each run's list of the groups that have it, and the groups with no runs.
// A forgetting pass builds it so, and so does a load (a save leaves it out:
// build-plan-gate5.md 11); between the two, new groups join it in order, so
// it is the same table either way.
void Observer::index_groups() {
    run_lists_.clear();
    run_pool_.clear();
    run_pool_.push(0); // offset 0: no block
    gramless_.clear();
    listed_.clear();
    listed_.resize(groups_.size());
    for (u32 k = 0; k < groups_.size(); ++k) {
        const GroupRec g = groups_[k];
        if (g.grams_n == 0)
            gramless_.push(k);
        for (u32 i = 0; i < g.grams_n; ++i)
            list_run(grams_[g.grams_at + i], k);
    }
}

// Every group's runs, on loading: its reference's, worked out from that
// genome's bytes, or its own from the save (`own`, 3 bytes a run, the groups
// with own_runs in order). False if they aren't what the groups say.
bool Observer::runs_again(const Array<u8>& own) {
    grams_.clear();
    usize at = 0;
    for (u32 k = 0; k < groups_.size(); ++k) {
        GroupRec& g = groups_[k];
        const u32 here = static_cast<u32>(grams_.size());
        if (g.own_runs) {
            if (g.grams_n > (own.size() - at) / 3)
                return false;
            for (u32 i = 0; i < g.grams_n; ++i, at += 3)
                grams_.push(static_cast<u64>(own[at]) | static_cast<u64>(own[at + 1]) << 8 |
                            static_cast<u64>(own[at + 2]) << 16);
        } else {
            const GenomeRec& ref = gen_[g.genome];
            grams_of(bank_.data() + ref.at, ref.len, &scratch_);
            if (scratch_.size() != g.grams_n)
                return false;
            grams_.append(scratch_.data(), scratch_.size());
        }
        if (g.grams_at != here) // as the groups were numbered, so their runs lie
            return false;
    }
    return at == own.size();
}

bool Observer::index_from(Array<GenomeRec>& gen, Array<u8>& bank, Array<GroupRec>& groups) {
    gen_ = static_cast<Array<GenomeRec>&&>(gen);
    bank_ = static_cast<Array<u8>&&>(bank);
    groups_ = static_cast<Array<GroupRec>&&>(groups);
    for (usize k = 0; k < groups_.size(); ++k) {
        GroupRec& g = groups_[k];
        g.own_runs = 0; // a save from before the flag: the bytes there were never set
        if (g.genome >= gen_.size() || gen_[g.genome].at > bank_.size() ||
            gen_[g.genome].len > bank_.size() - gen_[g.genome].at)
            return false;
    }
    Array<u8> none;
    if (!runs_again(none))
        return false;
    index_groups();
    return true;
}

// Row `row` of the genebank: its genome, from the second file's entries.
namespace {

bool bank_row_of(void* ctx, u64 i, u64* bytes_at, u32* len) {
    const Observer& o = *static_cast<const Observer*>(ctx);
    GenebankRow r;
    if (!o.genebank.stream().get_row(i, &r))
        return false;
    *bytes_at = r.bytes_at;
    *len = r.len;
    return true;
}

bool bank_bytes_of(void* ctx, u64 at, u64 n, u8* out) {
    return static_cast<const Observer*>(ctx)->genebank_bytes.get_many(at, n, out);
}

} // namespace

bool Observer::kept_genome(u64 row, Array<u8>* out, u32* steps) const {
    GenomeFile f;
    f.ctx = const_cast<Observer*>(this);
    f.rows = genebank.size();
    f.bytes = genebank_bytes.rows();
    f.row = bank_row_of;
    f.read = bank_bytes_of;
    return genome_read(f, row, out, steps);
}

// A record of one of a lineage's dead members kept (by > 0) or let go (by <
// 0) for a boot or an arrival that may bring it back (3.3).
void Observer::remember(u64 origin, i32 by) {
    const u32* li = lineages_.find(origin);
    if (!li)
        return;
    LineageRec& L = lin_[*li];
    if (by > 0) {
        L.remembered += static_cast<u32>(by);
        return;
    }
    const u32 less = static_cast<u32>(-by);
    L.remembered = L.remembered > less ? L.remembered - less : 0;
    finish_lineage(*li);
}

// A lineage finished (3.3): extinct, with no record of a member left for a
// boot or an arrival to bring it back. One that never copied faithfully and
// founded no colony is let go: nothing counts it by its slot (its stages
// are none, and no colony points to it). It is written to
// finished_lineages if it went lineage_generations generations, its first
// program the first (max_depth counts from 0); a shorter one, most of them
// a single program that never bred, is only counted.
void Observer::finish_lineage(u32 li) {
    const LineageRec& L = lin_[li];
    if (L.alive || L.remembered || L.first_copy != ~0ull || L.first_colony)
        return;
    if (L.max_depth + 1 >= s_.lineage_generations) {
        finished_lineages.push(L);
    } else {
        ++shallow_lineages;
        shallow_births += L.births;
    }
    lineages_.erase(L.origin);
    LineageRec gone{};
    gone.first_copy = gone.first_viable = gone.extinct_at = ~0ull; // origin 0: a free slot
    lin_[li] = gone;
    free_lin_.push(li);
}

ObserverMemory Observer::memory() const {
    ObserverMemory m;
    m.genomes = genomes_.bytes() + gen_.bytes() + bank_.bytes() + free_gen_.bytes() + genebank.bytes() +
                genebank_bytes.bytes();
    m.groups = groups_.bytes() + grams_.bytes() + run_lists_.bytes() + run_pool_.bytes() + gramless_.bytes() +
               rarity_.bytes() + candidates_.bytes() + listed_.bytes();
    m.lineages =
        lineages_.bytes() + lin_.bytes() + lineage_groups_.bytes() + colonies_.bytes() + colony_clocks_.bytes() +
        free_lin_.bytes() + finished_lineages.bytes();
    m.programs = programs_.bytes() + prog_.bytes() + free_prog_.bytes() + dead_senders_.bytes() + sent_live_.bytes() +
                 boot_writer_.bytes();
    m.history = births.bytes() + deaths.bytes() + samples.bytes() + turnover.bytes() + windows.bytes() +
                volts.bytes() + volt_sum_.bytes() + ground.bytes() + ground_windows.bytes();
    m.other = scratch_.bytes() + ratios_.bytes() + walk_cross_.bytes() + walk_hijack_.bytes() + lead_keys_.bytes() +
              lead_due_.bytes() + over_.bytes() + over_bytes_.bytes() + injected_.bytes() +
              dead_talkers_.bytes();
    return m;
}

const ProgramRec* Observer::program(u64 uid) const {
    const u32* i = programs_.find(uid);
    return i ? &prog_[*i] : nullptr;
}

const LineageRec* Observer::lineage(u64 origin) const {
    const u32* i = lineages_.find(origin);
    return i ? &lin_[*i] : nullptr;
}

const GenomeRec* Observer::genome(u64 hash) const {
    const u32* i = genomes_.find(hash);
    return i ? &gen_[*i] : nullptr;
}

const ColonyRec* Observer::colony(u64 origin, u32 place) const {
    const LineageRec* L = lineage(origin);
    if (!L)
        return nullptr;
    for (u32 c = L->first_colony; c; c = colonies_[c - 1].next)
        if (colonies_[c - 1].place == place)
            return &colonies_[c - 1];
    return nullptr;
}

ColonyRec* Observer::colony_of(LineageRec& L, u32 place) {
    for (u32 c = L.first_colony; c; c = colonies_[c - 1].next)
        if (colonies_[c - 1].place == place)
            return &colonies_[c - 1];
    return nullptr;
}

// The copying ladder: how far a birth got towards a copy of itself. `near`:
// a program wrote 8 bytes or more, at least half like its own genome. A
// hand-over is on no rung: the parent wrote nothing into it.
void Observer::ladder(Place& p, const BirthFact& f, bool near, bool handover) {
    if (handover) {
        ++handovers;
        return;
    }
    if (f.genome_len == 0)
        return; // nothing written: an empty spawn into leftover memory
    ++written;
    if (f.genome_len < 8)
        return;
    const u8* g = p.os.fact_bytes.data() + f.genome_at;
    if (f.cause != BirthCause::thread) {
        // A copy of the reset region's code: its bytes are found there.
        const u8* reg = p.mem.bytes() + p.mem.region_base();
        const u32 rn = p.mem.region_size();
        for (u32 i = 0; i + f.genome_len <= rn; ++i)
            if (reg[i] == g[0] && memcmp(reg + i, g, f.genome_len) == 0) {
                ++region_copies;
                break;
            }
        return;
    }
    // A program that wrote a genome of its own into its child.
    ++program_copies;
    if (near)
        ++near_copies;
}

namespace {

// A lineage's members' ticks alive, brought up to `tick` before its members
// change (LineageRec::member_ticks).
void count_members(LineageRec& L, u64 tick) {
    if (tick > L.counted_to) {
        L.member_ticks += static_cast<u64>(L.alive) * (tick - L.counted_to);
        L.counted_to = tick;
    }
}

} // namespace

// Reading input (build-plan-gate5.md 2, gap 1): its RECVs that returned
// bytes come to one or more a member a day, over its life so far.
bool Observer::reads_input(const LineageRec& L, u64 now) const {
    if (!L.recvs)
        return false;
    const u64 ticks = L.member_ticks + static_cast<u64>(L.alive) * (now > L.counted_to ? now - L.counted_to : 0);
    return L.recvs * s_.day >= ticks;
}

// An overrun to watch, in its place among the others: by place, then
// address. Those it overlaps go: their bytes aren't as written any more.
void Observer::watch(const OverrunRec& in, const u8* bytes) {
    usize lo = 0, hi = over_.size();
    while (lo < hi) {
        const usize mid = (lo + hi) / 2;
        const OverrunRec& m = over_[mid];
        if (m.place < in.place || (m.place == in.place && m.addr < in.addr))
            lo = mid + 1;
        else
            hi = mid;
    }
    // No two overlap, so only the one before can reach in; and those from
    // here on that start inside it.
    usize first = lo, last = lo;
    if (first > 0 && over_[first - 1].place == in.place && over_[first - 1].addr + over_[first - 1].len > in.addr)
        --first;
    while (last < over_.size() && over_[last].place == in.place && over_[last].addr < in.addr + in.len)
        ++last;
    OverrunRec r = in;
    r.bytes_at = static_cast<u32>(over_bytes_.size());
    over_bytes_.append(bytes, in.len);
    if (first == last) {
        over_.push(r);
        for (usize j = over_.size() - 1; j > first; --j)
            over_[j] = over_[j - 1];
    } else if (last - first > 1) {
        const usize gone = last - first - 1;
        for (usize j = last; j < over_.size(); ++j)
            over_[j - gone] = over_[j];
        over_.resize_unfilled(over_.size() - gone);
    }
    over_[first] = r;
}

// The RECVs that returned bytes (RecvFact), after the tick's births and
// before its deaths: each to its reader's lineage; and the overruns, by
// what they reached, watched from now if another lineage's message wrote
// them.
void Observer::ingest_recvs(const Os& os, u64 now) {
    (void)now;
    usize k = 0; // the overruns' stretches come in their RECVs' order
    for (usize i = 0; i < os.recvs.size(); ++i) {
        const RecvFact& f = os.recvs[i];
        ++recvs;
        const u32* pi = programs_.find(f.uid);
        const ProgramRec* me = pi ? &prog_[*pi] : nullptr;
        if (me)
            if (const u32* li = lineages_.find(me->origin))
                ++lin_[*li].recvs;
        if (!f.past)
            continue;
        ++overruns;
        // Whose message: a living program's, or a dead sender's still
        // remembered. An idle run's SEND has no program (0).
        bool known = false;
        Talker from{};
        if (f.sender) {
            if (const u32* si = programs_.find(f.sender)) {
                from.origin = prog_[*si].origin;
                from.genome = prog_[*si].genome;
                known = true;
            } else if (const Talker* dt = dead_talkers_.find(f.sender)) {
                from = *dt;
                known = true;
            } else if (const ProgramRec* ds = dead_senders_.find(f.sender)) {
                from.origin = ds->origin;
                from.genome = ds->genome;
                known = true;
            }
            if (!known)
                ++overruns_unknown;
        }
        bool hit[kReaches] = {};
        while (k < os.overruns.size() && os.overruns[k].recv < i)
            ++k;
        for (; k < os.overruns.size() && os.overruns[k].recv == i; ++k) {
            const OverrunFact& o = os.overruns[k];
            Reach r = kReachFree;
            if (o.stack) {
                r = kReachStack;
            } else if (o.owner == f.uid) {
                r = kReachOwn;
            } else if (o.owner) {
                const u32* oi = programs_.find(o.owner);
                r = me && oi && prog_[*oi].origin == me->origin ? kReachKin : kReachOther;
            }
            hit[r] = true;
            if (me && known && from.origin != me->origin) {
                OverrunRec w{};
                w.tick = o.tick;
                w.receiver = f.uid;
                w.receiver_origin = me->origin;
                w.sender = f.sender;
                w.sender_origin = from.origin;
                w.sender_genome = from.genome;
                w.place = o.place;
                w.addr = o.addr;
                w.len = o.len;
                watch(w, os.fact_bytes.data() + o.bytes_at);
            }
        }
        for (u32 r = 0; r < kReaches; ++r)
            reached[r] += hit[r] ? 1 : 0;
    }
}

namespace {

// Whether a core or a memory device at the place is flaky (gap 12).
bool place_flaky(const Place& p) {
    for (u32 c = 0; c < p.core_count(); ++c)
        if (p.noise.core_flaky(c) != kFlakyOne)
            return true;
    for (u32 d = 0; d < p.mem.device_count(); ++d)
        if (p.noise.memory_flaky(d) != kFlakyOne)
            return true;
    return false;
}

// The speed the place's cores have lost to wear at its voltage, on average
// over its cores, in millionths: a core's worn speed against the speed it
// would have unworn (worn ground, gap 1). A core unworn there, or one that
// would be off there unworn, has lost none.
u64 slowing_ppm(const Place& p) {
    const u32 n = p.core_count();
    if (!n)
        return 0;
    const i32 mv = p.env.voltage_mv;
    u64 sum = 0;
    for (u32 c = 0; c < n; ++c) {
        const i32 worn = p.cores[c].worn_mv;
        if (worn <= 0)
            continue;
        CorePower k = p.cores[c].kind == DeviceKind::e_core ? p.power().e : p.power().p;
        const u32 fresh = core_speed(k, mv);
        if (!fresh)
            continue;
        k.threshold += worn;
        const u32 slow = core_speed(k, mv);
        sum += slow >= fresh ? 0 : static_cast<u64>(fresh - slow) * 1000000 / fresh;
    }
    return sum / n;
}

// The least maximum of the place's devices: past it, a rise heads for a
// trip (the cascades).
i32 least_maximum(const Place& p) {
    const PowerSettings& w = p.power();
    i32 m = w.memory_maximum;
    if (p.os.disk_size())
        m = w.disk_maximum < m ? w.disk_maximum : m;
    m = w.network_maximum < m ? w.network_maximum : m;
    for (u32 c = 0; c < p.core_count(); ++c) {
        const i32 k = p.cores[c].kind == DeviceKind::e_core ? w.e.maximum : w.p.maximum;
        m = k < m ? k : m;
    }
    return m;
}

// A hold's clock at a sample: begun, or ended into its place's window.
void hold_check(HoldClock& k, u64 origin, u32 place, bool holds, u64 now, Array<PlaceGround>& ground) {
    if (k.origin != origin || k.place != place) {
        k = HoldClock{}; // a slot used again
        k.origin = origin;
        k.place = place;
    }
    if (holds && k.since == ~0ull) {
        k.since = now;
    } else if (!holds && k.since != ~0ull) {
        if (place < ground.size())
            ++ground[place].ended[age_bin(now - k.since)];
        k.since = ~0ull;
    }
}

} // namespace

void Observer::ingest(Place& p) {
    const bool full = p.mem.free_bytes() < p.mem.size() / 10; // stage 5's "crowded"
    const Os& os = p.os;
    const u32 place_id = p.id();
    while (turnover.size() <= place_id)
        turnover.push(PlaceTurnover{});
    while (boot_writer_.size() <= place_id)
        boot_writer_.push(ProgramRec{});
    PlaceTurnover& T = turnover[place_id];
    while (volt_sum_.size() <= place_id)
        volt_sum_.push(VoltSum{});
    VoltSum& v = volt_sum_[place_id];
    const i32 mv = p.env.voltage_mv;
    v.low = v.ticks == 0 || mv < v.low ? mv : v.low;
    v.sum += mv;
    ++v.ticks;
    // The ground measure reads the place's counters as they stand after
    // this tick: core-ticks lost to cut-outs and to heat trips, and the
    // ticks its reset region's memory was off.
    const u64 cut = p.off_ticks[0] + p.off_ticks[1], heat = p.heat_ticks[0] + p.heat_ticks[1];
    while (ground.size() <= place_id)
        ground.push(PlaceGround{});
    PlaceGround& G = ground[place_id];
    if (!G.begun) {
        G.begin(p.env.tick, cut, heat, p.region_off_ticks);
        G.worked0 = G.whole ? 0 : p.worked20;
    }
    // Gate 5's watching, each tick (watch.h): the cores' slowing by wear, a
    // device failed and waiting for its visit, and a flaky part there.
    const bool flaky = place_flaky(p);
    ++tally5.flaky_ticks[flaky ? 1 : 0];
    if (p.failed_devices()) {
        ++G.failed;
        ++tally5.scarred_ticks;
    }
    // Devices failed this tick, for the events timeline (a place first
    // seen counts nothing it had before).
    if (fail_seen_.size() < (static_cast<usize>(place_id) + 1) * kHeatKinds) {
        const usize from = fail_seen_.size();
        fail_seen_.resize((static_cast<usize>(place_id) + 1) * kHeatKinds);
        for (usize k = from; k < fail_seen_.size(); ++k)
            fail_seen_[k] = ~0ull;
    }
    for (u32 k = 0; k < kHeatKinds; ++k) {
        u64& seen = fail_seen_[static_cast<usize>(place_id) * kHeatKinds + k];
        for (u64 j = seen; seen != ~0ull && j < p.failures[k] && j < seen + 8; ++j)
            log_event(p.env.tick ? p.env.tick - 1 : 0, place_id, EventKind::failure, static_cast<u8>(k));
        seen = p.failures[k];
    }
    G.slow_sum += slowing_ppm(p);
    for (usize i = 0; i < os.cold_starts.size(); ++i) {
        T.cold_start();
        ++cold_starts;
    }
    // Births first: a program born and dead in the same tick is born first.
    for (usize i = 0; i < os.births.size(); ++i) {
        const BirthFact& f = os.births[i];
        if (s_.keep_facts)
            births.push(f);
        ++total_births;
        ++births_by_cause[static_cast<u8>(f.cause) < kBirthCauses ? static_cast<u8>(f.cause) : 0];
        T.birth();
        u64 genome = f.genome_hash;
        bool scavenged = false;
        if (f.genome_len == 0 && genomes_.contains(f.image_hash)) {
            genome = f.image_hash; // nothing written, and the code is a known genome's: a corpse's (13.3)
            scavenged = true;
            ++scavenger;
        }
        // A parent the current carried the bytes of is marked (12.6).
        const u64 parent_uid = parent_of(f.parent_uid);
        ProgramRec r{};
        r.uid = f.uid;
        r.parent = parent_uid;
        r.carried = carried_by_current(f.parent_uid);
        r.place = f.place;
        r.born = f.tick;
        r.genome = genome;
        r.image = f.image_hash;
        r.base = f.base;
        r.wlo = f.wlo;
        r.size = f.size;
        // Born from an arrival: by its prompt run, or by a static run in its
        // landed bytes, which names its sender too (13.2).
        r.arrived = f.cause == BirthCause::arrival || (f.cause == BirthCause::static_bytes && f.parent_uid);
        bool parent_faithful = false;
        u64 parent_genome = 0;
        const u32* pi = parent_uid ? programs_.find(parent_uid) : nullptr;
        // A boot's parent is the boot block's last writer (13.2), which may
        // have died before the cold start: then the place kept its record. An
        // arrival's is the program that sent it, at another place (13.3), and
        // so is a static run's in the bytes it landed (13.2).
        ProgramRec par{};
        bool has_parent = pi != nullptr;
        if (pi) {
            par = prog_[*pi];
        } else if (f.cause == BirthCause::boot && parent_uid && boot_writer_[place_id].uid == parent_uid) {
            par = boot_writer_[place_id];
            has_parent = true;
        } else if (r.arrived && parent_uid) {
            if (const ProgramRec* ds = dead_senders_.find(parent_uid)) { // it sent, then died
                par = *ds;
                has_parent = true;
            }
        }
        // A hand-over: born in the allocation its parent was born in. The
        // parent gave its own body away, and copied nothing; the written
        // extent it carries was written for the parent, long before. (Only a
        // thread's SPAWN hands over: a boot's run copies.)
        const bool handover = pi && f.cause == BirthCause::thread && f.base == par.base;
        if (has_parent) {
            r.origin = par.origin;
            r.depth = par.depth + 1;
            // Faithful: the parent's genome, at least min_genome bytes of it. An
            // empty spawn copies nothing, and a byte or two written into a small
            // gift inside the parent's page lets the child run the parent's code
            // in place (the page is shared): neither is a copy.
            const u32 len = scavenged ? f.size : f.genome_len;
            r.faithful = !handover && genome == par.genome && len >= s_.min_genome;
            r.fdepth = r.faithful ? par.fdepth + 1 : 0;
            if (!handover && f.image_hash == par.image)
                ++image_copies;
            parent_faithful = par.faithful;
            parent_genome = par.genome;
            r.line[0] = par.genome; // its forebears' genomes, for keeping (3.2)
            for (u32 j = 1; j + 1 < kMaxKeepGenerations; ++j)
                r.line[j] = par.line[j - 1];
        } else {
            r.origin = r.uid; // a new lineage (13.2)
        }
        // A near copy: a program wrote 8 bytes or more, at least half like
        // its own genome (the same genome is a whole one).
        bool near = false;
        if (has_parent && !handover && f.cause == BirthCause::thread && f.genome_len >= 8) {
            if (genome == parent_genome) {
                near = true;
            } else {
                const GenomeRec* pg = this->genome(parent_genome);
                near = pg && similarity(os.fact_bytes.data() + f.genome_at, f.genome_len, bank_.data() + pg->at,
                                        pg->len) >= 0.5;
            }
        }
        r.copy = r.faithful || (near && f.genome_len >= s_.min_genome);
        u32 g;
        if (scavenged) {
            g = *genomes_.find(genome);
            ++gen_[g].count;
            if (gen_[g].group != kNoGroup)
                ++groups_[gen_[g].group].count;
        } else {
            g = add_genome(os.fact_bytes.data() + f.genome_at, f.genome_len, genome, f.place, f.tick, r.origin,
                           has_parent ? par.genome : 0, r.depth);
        }
        GenomeRec& gr = gen_[g];
        if (f.size < gr.alloc_min)
            gr.alloc_min = f.size;
        if (f.size > gr.alloc_max)
            gr.alloc_max = f.size;
        if (has_parent && !handover && f.image_hash == par.image)
            ++gr.image_copies;
        ladder(p, f, near, handover);

        bool fresh = false;
        u32& li = lineages_.get_or_insert(r.origin, &fresh);
        if (fresh) {
            LineageRec L{};
            L.origin = r.origin;
            L.place = f.place;
            L.cause = f.cause;
            L.born = f.tick;
            L.first_copy = L.first_viable = L.extinct_at = ~0ull;
            L.counted_to = f.tick;
            if (free_lin_.size()) {
                li = free_lin_.back();
                free_lin_.pop();
                lin_[li] = L;
            } else {
                li = static_cast<u32>(lin_.size());
                lin_.push(L);
            }
        }
        LineageRec& L = lin_[li];
        count_members(L, f.tick);
        // Gate 5's: births by size; those a parent made here, at a place
        // with a flaky part or none, and the mutations among them; and the
        // current's births, with where their bytes were held.
        ++tally5.births_by_size[size_bin(f.size)];
        if (has_parent && !handover && !r.arrived) {
            ++tally5.flaky_births[flaky ? 1 : 0];
            tally5.flaky_mutations[flaky ? 1 : 0] += genome != par.genome ? 1 : 0;
        }
        if (r.carried)
            carried_births.push(CarriedRec{f.tick, r.origin, f.place, has_parent ? par.place : f.place, L.place});
        reader_births += reads_input(L, f.tick) ? 1 : 0;
        // Born of a program found injected (9.8), and of the sender's kind:
        // its genome, or at least half like it.
        if (pi && !handover && f.genome_len) {
            if (const u64* sg = injected_.find(par.uid)) {
                const GenomeRec* g5 = this->genome(*sg);
                if (genome == *sg || (g5 && similarity(os.fact_bytes.data() + f.genome_at, f.genome_len,
                                                       bank_.data() + g5->at, g5->len) >= 0.5))
                    ++injection_births;
            }
        }
        if (L.alive == 0)
            L.extinct_at = ~0ull; // a boot brings a lineage back from its writer's past (10.5)
        ++L.alive;
        ++L.births;
        // Where it was born: the lineage's home, or a colony (13.3), founded
        // by its first member there. Only a birth made there counts as a
        // copy there: an arrival's was made elsewhere.
        const bool made_here = r.copy && !r.arrived;
        G.births_copies += made_here ? 1 : 0;
        if (f.place == L.place) {
            ++L.home_alive;
            G.holder_births += made_here && li < lin_held_.size() && lin_held_[li].since != ~0ull &&
                                       lin_held_[li].origin == L.origin
                                   ? 1
                                   : 0;
            L.home_copies += made_here ? 1 : 0;
            L.home_faithful += r.faithful ? 1 : 0;
        } else {
            ColonyRec* C = colony_of(L, f.place);
            if (!C) {
                ColonyRec n{};
                n.origin = r.origin;
                n.place = f.place;
                n.from = has_parent ? par.place : f.place;
                n.colonist = r.arrived && has_parent && !r.carried ? r.uid : 0;
                n.first = f.tick;
                n.ended = ~0ull;
                n.next = L.first_colony;
                colonies_.push(n);
                L.first_colony = static_cast<u32>(colonies_.size());
                ++L.colonies;
                C = &colonies_.back();
            }
            if (C->alive == 0) {
                // The current's are no colonists (12.6): counted apart.
                C->foundings += r.arrived && !r.carried ? 1 : 0;
                C->drifted += r.carried ? 1 : 0;
                if (C->by_current() && r.arrived && has_parent && !r.carried) {
                    C->colonist = r.uid; // the current's had died out: a colonist founds it
                    C->first = f.tick;
                }
                C->ended = ~0ull;
            }
            ++C->alive;
            ++C->births;
            C->alive_copies += made_here ? 1 : 0;
            const usize ci = static_cast<usize>(C - colonies_.data());
            G.holder_births += made_here && ci < col_held_.size() && col_held_[ci].since != ~0ull &&
                                       col_held_[ci].origin == C->origin
                                   ? 1
                                   : 0;
        }
        if (r.depth > L.max_depth)
            L.max_depth = r.depth;
        if (r.fdepth > L.max_fdepth)
            L.max_fdepth = r.fdepth;
        if (r.copy)
            ++L.alive_copies;
        if (r.faithful) {
            ++L.faithful;
            ++L.alive_faithful;
            ++total_faithful;
            if (L.first_copy == ~0ull)
                L.first_copy = f.tick;
            if (parent_faithful && L.first_viable == ~0ull)
                L.first_viable = f.tick;
        }
        // Keeping (13.2; build-plan-observe.md 3.2): with keep_generations 1,
        // every genome at its first birth; else the genome of the forebear
        // this birth makes keep_generations generations deep, itself the
        // first. Its lineage counts the group then.
        const u32 kg = s_.keep_generations;
        if (kg <= 1) {
            keep(g, f.tick, full);
        } else if (kg <= kMaxKeepGenerations && r.line[kg - 2]) {
            if (const u32* a = genomes_.find(r.line[kg - 2]))
                keep(*a, f.tick, full);
        }
        r.group = gen_[g].group;
        if (r.group != kNoGroup) {
            bool new_pair = false;
            lineage_groups_.get_or_insert(pair_key(r.origin, r.group), &new_pair);
            if (new_pair) {
                ++L.groups;
                if (full)
                    ++L.groups_full;
            }
        }
        u32 slot;
        if (free_prog_.size()) {
            slot = free_prog_.back();
            free_prog_.pop();
            prog_[slot] = r;
        } else {
            slot = static_cast<u32>(prog_.size());
            prog_.push(r);
        }
        programs_.get_or_insert(r.uid) = slot;
        hold(r);
    }
    // Then the SENDs (a program born this tick may have sent), before the
    // deaths (and one that died may have).
    for (usize i = 0; i < os.sends.size(); ++i) {
        const SendFact& s = os.sends[i];
        ++sends;
        if (s.port != 0) {
            if (const u32* pi = s.uid ? programs_.find(s.uid) : nullptr)
                prog_[*pi].sent_other = true;
            continue;
        }
        ++sends_port0;
        if (const u32* pi = s.uid ? programs_.find(s.uid) : nullptr)
            prog_[*pi].sent0 = true;
    }
    // The current copied bytes a program held (12.6): what they make
    // elsewhere is its lineage's, so its record is kept as a sender's is.
    for (usize i = 0; i < os.drifts.size(); ++i)
        if (const u32* pi = programs_.find(os.drifts[i].uid))
            prog_[*pi].sent0 = true;
    ingest_recvs(os, p.env.tick);
    for (usize i = 0; i < os.deaths.size(); ++i) {
        const DeathFact& d = os.deaths[i];
        if (s_.keep_facts)
            deaths.push(d);
        ++total_deaths;
        ++deaths_by_cause[static_cast<u8>(d.cause) < kDeathCauses ? static_cast<u8>(d.cause) : 0];
        const u32* pi = programs_.find(d.uid);
        T.death(pi ? prog_[*pi].born : ~0ull, d.cause);
        if (!pi)
            continue;
        const u32 slot = *pi;
        // Gate 5's: what the killer and the watchdog end (gap 14), by size,
        // and whether of the place's leading lineage.
        if (d.cause == DeathCause::oom || d.cause == DeathCause::watchdog) {
            const u32 k = d.cause == DeathCause::oom ? 0 : 1;
            log_event(p.env.tick ? p.env.tick - 1 : 0, place_id, k ? EventKind::watchdog : EventKind::kill, 0);
            const ProgramRec& pr = prog_[slot];
            ++tally5.ended[k][size_bin(pr.size)];
            tally5.ended_leader[k] += G.lead.lineage && G.lead.lineage == pr.origin ? 1 : 0;
            tally5.leader_share[k] +=
                T.starters ? static_cast<double>(G.lead.lineage_programs) / static_cast<double>(T.starters) : 0;
        }
        if (d.uid == os.boot_writer()) {
            // The boot block's last writer: a boot may need it, so it holds
            // its genomes as the living do (3.2), in place of the one before.
            if (boot_writer_[place_id].uid) {
                release(boot_writer_[place_id]);
                remember(boot_writer_[place_id].origin, -1);
            }
            boot_writer_[place_id] = prog_[slot];
            hold(prog_[slot]);
            remember(prog_[slot].origin, 1);
        }
        const u32* li = lineages_.find(prog_[slot].origin);
        if (li) {
            const ProgramRec& pr = prog_[slot];
            LineageRec& L = lin_[*li];
            count_members(L, d.tick);
            if (pr.faithful && L.alive_faithful)
                --L.alive_faithful;
            if (pr.copy && L.alive_copies)
                --L.alive_copies;
            if (L.alive && --L.alive == 0)
                L.extinct_at = d.tick;
            const bool made_here = pr.copy && !pr.arrived;
            if (pr.place == L.place) {
                L.home_alive -= L.home_alive ? 1 : 0;
                L.home_copies -= made_here && L.home_copies ? 1 : 0;
                L.home_faithful -= pr.faithful && L.home_faithful ? 1 : 0;
            } else if (ColonyRec* C = colony_of(L, pr.place)) {
                C->alive_copies -= made_here && C->alive_copies ? 1 : 0;
                if (C->alive && --C->alive == 0)
                    C->ended = d.tick;
            }
        }
        if (prog_[slot].sent_other) {
            Talker& k = dead_talkers_.get_or_insert(d.uid); // something it sent may still be read
            k.origin = prog_[slot].origin;
            k.genome = prog_[slot].genome;
        }
        if (prog_[slot].sent0) {
            dead_senders_.get_or_insert(d.uid) = prog_[slot]; // something it sent may still arrive
            hold(prog_[slot]);
            remember(prog_[slot].origin, 1);
        }
        release(prog_[slot]); // the living program's hold: a record it left holds on its own
        if (const u32* fl = lineages_.find(prog_[slot].origin))
            finish_lineage(*fl); // let go, if this was its last and nothing can bring it back
        free_prog_.push(slot);
        programs_.erase(d.uid);
        if (!injected_.empty())
            injected_.erase(d.uid);
    }
    // p.env.tick already counts the tick just done (Place::tick ends with it).
    TurnoverWindow win;
    if (T.end_tick(p.env.tick, os.live_programs(), full, s_.turnover, place_id, &win)) {
        if (win.starters)
            windows.push(win);
        GroundWindow gw{};
        if (G.close(win, s_.turnover.window, p.core_count(), cut, heat, p.region_off_ticks, &gw)) {
            gw.worked = p.worked20 - G.worked0;
            gw.births_copies = G.births_copies;
            gw.starters_copies = G.starters_copies;
            gw.threads = G.threads;
            gw.top_threads = G.top_threads;
            gw.slow_ppm = static_cast<u32>(G.slow_sum / s_.turnover.window);
            gw.failed = G.failed;
            for (u32 a = 0; a < kAgeBins; ++a) {
                gw.held[a] = G.held[a];
                gw.ended[a] = G.ended[a];
            }
            gw.holder_programs = G.holder_programs;
            gw.holder_births = G.holder_births;
            gw.worn = gw.slow_ppm >= 100000 || G.failed > 0;
            gw.flaky = G.flaky;
            ground_windows.push(gw);
        }
        G.worked0 = p.worked20;
        G.births_copies = 0;
        G.slow_sum = 0;
        G.failed = G.holder_births = 0;
        for (u32 a = 0; a < kAgeBins; ++a)
            G.ended[a] = 0;
        // The next window starts now. Who leads the place then goes in its
        // row, worked out once every place's facts for the tick are in
        // (set_leads).
        lead_due_.push(place_id);
    }
}

void Observer::respawned(u32 place, u64 tick, u32 incarnation, double x, double y, bool moved, u32 left_mass) {
    respawn_log.push(RespawnRec{tick, x, y, place, incarnation, left_mass, moved});
    cascade_watch.respawned(place);
    if (place >= boot_writer_.size() || !boot_writer_[place].uid)
        return;
    const ProgramRec gone = boot_writer_[place];
    boot_writer_[place] = ProgramRec{};
    release(gone);
    remember(gone.origin, -1);
    if (const u32* fl = lineages_.find(gone.origin))
        finish_lineage(*fl);
}

// Every lineage's hold of its home and every colony's of its place, as
// stage 4's hold and a colony's settling have it (13.3): a hold begun, or
// one ended, by its age, into its place's window under way.
void Observer::holds_at_sample(World& w) {
    const u64 now = w.tick_count();
    while (lin_held_.size() < lin_.size())
        lin_held_.push(HoldClock{});
    while (col_held_.size() < colonies_.size())
        col_held_.push(HoldClock{});
    for (usize i = 0; i < lin_.size(); ++i) {
        const LineageRec& L = lin_[i];
        const bool holds = L.origin && L.home_alive >= stages_.members && L.place < w.place_count() &&
                           lineage_holds(L.home_alive, L.home_copies, w.place(L.place).os.live_programs(), stages_);
        hold_check(lin_held_[i], L.origin, L.place, holds, now, ground);
    }
    for (usize i = 0; i < colonies_.size(); ++i) {
        const ColonyRec& C = colonies_[i];
        const bool holds = C.alive >= stages_.members && C.place < w.place_count() &&
                           lineage_holds(C.alive, C.alive_copies, w.place(C.place).os.live_programs(), stages_);
        hold_check(col_held_[i], C.origin, C.place, holds, now, ground);
    }
}

bool Observer::place_hold(bool colony, u32 index, u64 since) {
    Array<HoldClock>& a = colony ? col_held_ : lin_held_;
    if (index >= a.size() || a[index].since == ~0ull || since == ~0ull)
        return false;
    a[index].since = since;
    return true;
}

// As windows begin at the places whose window just ended: the holds there
// by age, the holders' programs, and whether a part there is flaky.
void Observer::holds_as_windows_begin(World& w) {
    const u64 now = w.tick_count() + 1; // the sample about to be taken
    Array<u8> due;
    due.resize(ground.size());
    for (usize k = 0; k < lead_due_.size(); ++k) {
        const u32 q = lead_due_[k];
        if (q >= ground.size() || q >= w.place_count())
            continue;
        due[q] = 1;
        PlaceGround& G = ground[q];
        for (u32 a = 0; a < kAgeBins; ++a)
            G.held[a] = 0;
        G.holder_programs = 0;
        G.flaky = place_flaky(w.place(q));
    }
    for (usize i = 0; i < lin_held_.size() && i < lin_.size(); ++i) {
        const HoldClock& k = lin_held_[i];
        if (k.since == ~0ull || k.origin != lin_[i].origin || k.place >= due.size() || !due[k.place])
            continue;
        ++ground[k.place].held[age_bin(now - k.since)];
        ground[k.place].holder_programs += lin_[i].home_alive;
    }
    for (usize i = 0; i < col_held_.size() && i < colonies_.size(); ++i) {
        const HoldClock& k = col_held_[i];
        if (k.since == ~0ull || k.origin != colonies_[i].origin || k.place >= due.size() || !due[k.place])
            continue;
        ++ground[k.place].held[age_bin(now - k.since)];
        ground[k.place].holder_programs += colonies_[i].alive;
    }
}

// The stiffness of the source feeding a place: the nearest by power links
// (the lowest numbered of those as near), its sag at its rated load. 0
// with no sources.
i32 Observer::feeding_sag(World& w, u32 place) {
    if (!w.powered() || place >= w.place_count())
        return 0;
    const u32 n = w.place_count();
    Array<i32> sag;
    sag.resize(n);
    Array<u8> has;
    has.resize(n);
    for (u32 k = 0; k < w.sources().count(); ++k) {
        const Source& s = w.sources().source(k);
        if (s.place < n && !has[s.place]) {
            has[s.place] = 1;
            sag[s.place] = s.sag;
        }
    }
    Array<u8> seen;
    seen.resize(n);
    Array<u32> queue;
    queue.push(place);
    seen[place] = 1;
    for (usize q = 0; q < queue.size(); ++q) {
        const u32 v = queue[q];
        if (has[v])
            return sag[v];
        if (v + 1 >= adj_begin_.size())
            continue;
        for (u32 j = adj_begin_[v]; j < adj_begin_[v + 1]; ++j)
            if (!seen[adj_[j]]) {
                seen[adj_[j]] = 1;
                queue.push(adj_[j]);
            }
    }
    return 0;
}

// A fact onto the events log. Past kEventsKept and a quarter more, the
// oldest go, leaving the latest kEventsKept.
void Observer::log_event(u64 tick, u32 place, EventKind kind, u8 detail) {
    event_log.push(EventRec{tick, place, static_cast<u8>(kind), detail, 0});
    if (event_log.size() < kEventsKept + kEventsKept / 4)
        return;
    const usize drop = event_log.size() - kEventsKept;
    for (usize i = 0; i < kEventsKept; ++i)
        event_log[i] = event_log[i + drop];
    event_log.resize(kEventsKept);
}

void Observer::after_tick(World& w) {
    const u32 n = w.place_count();
    const u64 t = w.tick_count();
    const Map& m = w.map();
    // The proximity neighbours: the power links, as the map has them now.
    if (adj_respawns_ != m.respawns() || adj_begin_.size() != n + 1) {
        // From the list of links itself: the map keeps each place's list
        // only where the solve needs it.
        adj_begin_.clear();
        adj_begin_.resize(n + 1);
        for (u32 k = 0; k < m.power_link_count(); ++k) {
            const PowerLink& l = m.power_link(k);
            if (l.a < n && l.b < n) {
                ++adj_begin_[l.a + 1];
                ++adj_begin_[l.b + 1];
            }
        }
        for (u32 i = 0; i < n; ++i)
            adj_begin_[i + 1] += adj_begin_[i];
        adj_.clear();
        adj_.resize(adj_begin_[n]);
        Array<u32> fill;
        fill.resize(n);
        for (u32 k = 0; k < m.power_link_count(); ++k) {
            const PowerLink& l = m.power_link(k);
            if (l.a < n && l.b < n) {
                adj_[adj_begin_[l.a] + fill[l.a]++] = l.b;
                adj_[adj_begin_[l.b] + fill[l.b]++] = l.a;
            }
        }
        adj_respawns_ = m.respawns();
    }
    // The cascades (12.2): each place's voltage, and whether its devices
    // cut out or tripped this tick; strikes and swells there. A place
    // first seen goes off at nothing it counted before.
    cin_.resize(n);
    const bool swells = w.events().on() && w.events().any(PatchType::swell);
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        CascadeInput& c = cin_[i];
        c.mv = p.env.voltage_mv;
        c.maximum = least_maximum(p);
        const u64 cut = p.cutouts[0] + p.cutouts[1] + p.memory_cutouts + p.disk_cutouts + p.network_cutouts;
        u64 trip = 0;
        for (u32 k = 0; k < kHeatKinds; ++k)
            trip += p.trips[k];
        if (i >= last_cut_.size()) {
            last_cut_.push(cut);
            last_trip_.push(trip);
        }
        c.cut = cut > last_cut_[i];
        c.trip = trip > last_trip_[i];
        last_cut_[i] = cut;
        last_trip_[i] = trip;
        c.struck = w.struck(i);
        c.swelled = swells && w.events().swell_at(m.x(i), m.y(i)) > 1.0;
    }
    const u32 before = cascade_watch.step(t, cin_.data(), n, adj_begin_.data(), adj_.data());
    for (usize k = before; k < cascade_watch.cascades.size(); ++k)
        cascade_watch.cascades[k].sag = feeding_sag(w, cascade_watch.cascades[k].root);
    // Patches formed (12.3): those there now that weren't at the last tick,
    // each named by its type, its start and its slot.
    if (w.events().on()) {
        Array<u64> now_there;
        for (u32 k = 0; k < w.events().count(); ++k) {
            const Patch& q = w.events().patch(k);
            const u64 key = (q.start << 8) ^ (static_cast<u64>(q.slot & 31) << 3) ^ q.type;
            now_there.push(key);
            bool seen = false;
            for (usize j = 0; j < patch_seen_.size() && !seen; ++j)
                seen = patch_seen_[j] == key;
            if (!seen)
                ++tally5.patches[q.type & 7];
        }
        patch_seen_.clear();
        for (usize j = 0; j < now_there.size(); ++j)
            patch_seen_.push(now_there[j]);
    }
    // Straits parting and joining again (gap 13), as the engine counts them:
    // a strait's spell ending in a tick. A respawn builds the list of straits
    // again from the map (Links::relink), perhaps in another order, so each
    // is known by its two landmasses, not its place in the list (Damian,
    // 2026-11-23, "fix"). One new to the list, or gone from it, has neither
    // parted nor joined: a strait is made where its spells put it.
    if (w.links().straits()) {
        const Links& L = w.links();
        const u32 ns = L.strait_count();
        bool same = strait_seen_.size() == ns;
        for (u32 k = 0; same && k < ns; ++k)
            same = strait_seen_[k].ma == L.strait(k).ma && strait_seen_[k].mb == L.strait(k).mb;
        if (!same) {
            // The list built again: each strait's last state carried over by
            // its landmasses, a new one's taken as it is.
            strait_next_.clear();
            for (u32 k = 0; k < ns; ++k) {
                const Strait& s = L.strait(k);
                StraitSeen e{s.ma, s.mb, s.parted() ? 1u : 0u};
                for (usize i = 0; i < strait_seen_.size(); ++i)
                    if (strait_seen_[i].ma == s.ma && strait_seen_[i].mb == s.mb) {
                        e.parted = strait_seen_[i].parted;
                        break;
                    }
                strait_next_.push(e);
            }
            strait_seen_.clear();
            for (u32 k = 0; k < ns; ++k)
                strait_seen_.push(strait_next_[k]);
        }
        for (u32 k = 0; k < ns; ++k) {
            const Strait& s = L.strait(k);
            const u32 parted = s.parted() ? 1 : 0;
            if (strait_seen_[k].parted && !parted)
                joinings.push(JoinRec{t, k, s.ma, s.mb});
            if (!strait_seen_[k].parted && parted)
                log_event(t, k, EventKind::parted, 0);
            strait_seen_[k].parted = parted;
        }
    }
}

void Observer::set_leads(World& w) {
    holds_as_windows_begin(w);
    for (usize k = 0; k < lead_due_.size(); ++k) {
        if (lead_due_[k] >= w.place_count() || lead_due_[k] >= ground.size())
            continue;
        PlaceGround& G = ground[lead_due_[k]];
        const Os& os = w.place(lead_due_[k]).os;
        lead_of(os, &G.lead);
        // And the reviewer's measures as the window begins: the programs
        // born as copies made there, the programs' threads, and the most
        // any one program has.
        G.starters_copies = G.threads = G.top_threads = 0;
        for (u32 i = 0; i < os.program_capacity(); ++i) {
            const Program& g = os.program(kFirstProgramHolder + i);
            if (!g.alive)
                continue;
            const u32* slot = programs_.find(g.uid);
            G.starters_copies += slot && prog_[*slot].copy && !prog_[*slot].arrived ? 1 : 0;
            G.threads += g.threads;
            G.top_threads = g.threads > G.top_threads ? g.threads : G.top_threads;
        }
        const u32* li = G.lead.lineage ? lineages_.find(G.lead.lineage) : nullptr;
        G.lead_reads = li && reads_input(lin_[*li], w.tick_count());
    }
    lead_due_.clear();
}

namespace {

// Of keys[0..n), sorted, each a name << 1 with its low bit set for a copy:
// the name with the most entries (the lowest, of two with the same), its
// entries and the copies among them, the most entries any other name has,
// and how many names there are. False if there are none.
bool most_of(const u64* keys, usize n, u64* name, u32* entries, u32* copies, u32* second, u32* names) {
    *name = 0;
    *entries = *copies = *second = *names = 0;
    for (usize i = 0; i < n;) {
        const u64 k = keys[i] >> 1;
        u32 e = 0, c = 0;
        for (; i < n && keys[i] >> 1 == k; ++i) {
            ++e;
            c += static_cast<u32>(keys[i] & 1);
        }
        ++*names;
        if (e > *entries) {
            *second = *entries;
            *name = k;
            *entries = e;
            *copies = c;
        } else if (e > *second) {
            *second = e;
        }
    }
    return n != 0;
}

} // namespace

void Observer::lead_of(const Os& os, GroundLead* out) {
    *out = GroundLead{};
    u64 name = 0;
    u32 names = 0;
    // By lineage: every program has one.
    lead_keys_.clear();
    for (u32 i = 0; i < os.program_capacity(); ++i) {
        const Program& g = os.program(kFirstProgramHolder + i);
        const u32* slot = g.alive ? programs_.find(g.uid) : nullptr;
        if (slot)
            lead_keys_.push(prog_[*slot].origin << 1 | (prog_[*slot].copy && !prog_[*slot].arrived ? 1u : 0u));
    }
    sort_u64(lead_keys_.data(), lead_keys_.size());
    if (most_of(lead_keys_.data(), lead_keys_.size(), &name, &out->lineage_programs, &out->lineage_copies,
                &out->lineage_second, &names))
        out->lineage = name;
    // By genome group: its genome's group now, kept since its birth or not.
    lead_keys_.clear();
    for (u32 i = 0; i < os.program_capacity(); ++i) {
        const Program& g = os.program(kFirstProgramHolder + i);
        const u32* slot = g.alive ? programs_.find(g.uid) : nullptr;
        if (!slot)
            continue;
        const ProgramRec& r = prog_[*slot];
        const u32* at = genomes_.find(r.genome);
        if (at && gen_[*at].group != kNoGroup)
            lead_keys_.push(static_cast<u64>(gen_[*at].group) << 1 | (r.copy && !r.arrived ? 1u : 0u));
    }
    sort_u64(lead_keys_.data(), lead_keys_.size());
    if (most_of(lead_keys_.data(), lead_keys_.size(), &name, &out->group_programs, &out->group_copies,
                &out->group_second, &out->groups)) {
        out->group = static_cast<u32>(name);
        out->group_ref = gen_[groups_[out->group].genome].hash;
    }
}

// The records of dead senders that nothing refers to any more: no message
// on the network from them, no arrival from them waiting in a port 0 queue,
// no prompt run of one running, and no bytes they landed still listed, nor
// a static run in them running (13.2). A block of the current's refers to
// the program that held its bytes in the same way (12.6).
void Observer::forget_senders(World& w) {
    if (dead_senders_.empty())
        return;
    sent_live_.clear();
    const Network& net = w.net();
    for (u32 i = 0; i < net.message_slots(); ++i) {
        const Message& g = net.message_slot(i);
        if (g.live && g.sender && (g.kind == MsgKind::current || (g.kind == MsgKind::data && g.port == 0)))
            sent_live_.get_or_insert(g.sender) = 1;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Place& p = w.place(i);
        for (u32 k = 0; k < p.region.queued(); ++k)
            if (p.region.waiting(k).cause == BirthCause::arrival && p.region.waiting(k).sender)
                sent_live_.get_or_insert(p.region.waiting(k).sender) = 1;
        for (u32 k = 0; k < p.region.landing_count(); ++k)
            if (parent_of(p.region.landing(k).sender))
                sent_live_.get_or_insert(parent_of(p.region.landing(k).sender)) = 1;
        for (u32 c = 0; c < p.core_count(); ++c) {
            const Core& k = p.cores[c];
            if (k.idle_run && parent_of(k.run_parent) &&
                (k.run_cause == BirthCause::arrival || k.run_cause == BirthCause::static_bytes))
                sent_live_.get_or_insert(parent_of(k.run_parent)) = 1;
        }
    }
    Array<u64> gone;
    for (usize s = 0; s < dead_senders_.slot_count(); ++s)
        if (dead_senders_.slot_used(s) && !sent_live_.contains(dead_senders_.slot_key(s)))
            gone.push(dead_senders_.slot_key(s));
    for (usize i = 0; i < gone.size(); ++i) {
        if (const ProgramRec* ds = dead_senders_.find(gone[i])) {
            const ProgramRec r = *ds;
            release(r); // its genomes, unless something else holds them
            remember(r.origin, -1);
        }
        dead_senders_.erase(gone[i]);
    }
}

// The dead senders to comms ports that nothing refers to any more: no
// message of theirs on the network, and none waiting in an inbox.
void Observer::forget_talkers(World& w) {
    if (dead_talkers_.empty())
        return;
    sent_live_.clear();
    const Network& net = w.net();
    for (u32 i = 0; i < net.message_slots(); ++i) {
        const Message& g = net.message_slot(i);
        if (g.live && g.kind == MsgKind::data && g.port != 0 && g.sender)
            sent_live_.get_or_insert(g.sender) = 1;
    }
    for (u32 i = 0; i < w.place_count(); ++i) {
        const Os& os = w.place(i).os;
        for (u32 k = 0; k < os.inbox_count(); ++k)
            if (os.inbox_sender(k))
                sent_live_.get_or_insert(os.inbox_sender(k)) = 1;
    }
    Array<u64> gone;
    for (usize s = 0; s < dead_talkers_.slot_count(); ++s)
        if (dead_talkers_.slot_used(s) && !sent_live_.contains(dead_talkers_.slot_key(s)))
            gone.push(dead_talkers_.slot_key(s));
    for (usize i = 0; i < gone.size(); ++i)
        dead_talkers_.erase(gone[i]);
}

namespace {

// Where a thread runs: its core's instruction pointer, or the one saved at
// its stack's base while it is off a core (A.3).
u32 thread_ip(const Place& p, const Thread& th) {
    return th.core ? p.cores[th.core - 1].x.r.ip : load32(p.mem.bytes() + th.stack + 16);
}

} // namespace

// The sampled views of one place (13.1): where each of its threads runs.
// It reads the place and the observer's records and changes neither, so
// the places are walked side by side, a lane each (ratio_lane).
void Observer::walk_threads(const Place& p, u64* cross_owner, u64* hijack) const {
    const Memory& m = p.mem;
    u64 cross = 0, hij = 0;
    const u32 top = p.os.thread_top();
    for (u32 t = 0; t < top; ++t) {
        // What the threads a little further on will need is asked for now:
        // a thread's record and saved registers, then where it runs and
        // that address's tables, then the allocation's record. Each is a
        // wait for the host's memory, and this way they overlap. Hints to
        // the host's cache and nothing else (as Os::ask_for_ready).
        if (t + 8 < top) {
            _mm_prefetch(reinterpret_cast<const char*>(&p.os.thread(t + 8)), _MM_HINT_T0);
            const u32 s = p.os.saved_at()[t + 8];
            if (s < m.size())
                _mm_prefetch(reinterpret_cast<const char*>(m.bytes() + s), _MM_HINT_T0);
        }
        if (t + 4 < top && p.os.thread(t + 4).state != ThreadState::free)
            m.ask_for(thread_ip(p, p.os.thread(t + 4)));
        if (t + 2 < top && p.os.thread(t + 2).state != ThreadState::free)
            m.ask_for_record(thread_ip(p, p.os.thread(t + 2)));
        const Thread& th = p.os.thread(t);
        if (th.state == ThreadState::free)
            continue;
        const u32 ip = thread_ip(p, th);
        const u32 id = m.alloc_at(ip);
        if (!id || m.record(id).holder != th.program) {
            ++cross; // running code in an allocation its program doesn't hold (13.3)
            continue;
        }
        // Hijack (a heuristic, 13.3): its code no longer hashes to its own
        // genome, and now hashes to another lineage's.
        const Alloc& a = m.record(id);
        if (a.whi <= a.wlo)
            continue;
        const ProgramRec* me = program(p.os.program(th.program).uid);
        if (!me)
            continue;
        const u64 h = hash_bytes(m.bytes() + a.base + a.wlo, a.whi - a.wlo, kGenomeDomain);
        if (h == me->genome)
            continue;
        const GenomeRec* other = genome(h);
        if (other && other->origin != me->origin)
            ++hij;
    }
    *cross_owner = cross;
    *hijack = hij;
}

// The overruns watched, at a sample: those no longer as written go, and a
// thread running in one of the rest is an injection (9.8). Its program is
// marked with the sender's genome, for the births it makes (injection_births).
u64 Observer::find_injections(World& w) {
    if (over_.empty())
        return 0;
    Array<u8> kept;
    usize n = 0;
    for (usize i = 0; i < over_.size(); ++i) {
        OverrunRec r = over_[i];
        if (r.place >= w.place_count())
            continue;
        const Memory& m = w.place(r.place).mem;
        if (r.addr >= m.size() || r.len > m.size() - r.addr ||
            memcmp(m.bytes() + r.addr, over_bytes_.data() + r.bytes_at, r.len) != 0)
            continue;
        const u32 at = static_cast<u32>(kept.size());
        kept.append(over_bytes_.data() + r.bytes_at, r.len);
        r.bytes_at = at;
        over_[n++] = r;
    }
    over_.resize_unfilled(n);
    over_bytes_.clear();
    over_bytes_.append(kept.data(), kept.size());
    u64 found = 0;
    for (usize i = 0; i < n;) {
        usize j = i;
        while (j < n && over_[j].place == over_[i].place)
            ++j;
        const Place& p = w.place(over_[i].place);
        for (u32 t = 0; t < p.os.thread_top(); ++t) {
            const Thread& th = p.os.thread(t);
            if (th.state == ThreadState::free)
                continue;
            const u32 ip = thread_ip(p, th);
            // The last of the place's overruns that starts at or before it.
            usize lo = i, hi = j;
            while (lo < hi) {
                const usize mid = (lo + hi) / 2;
                if (over_[mid].addr <= ip)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo == i || ip - over_[lo - 1].addr >= over_[lo - 1].len)
                continue;
            ++found;
            bool fresh = false;
            u64& g = injected_.get_or_insert(p.os.program(th.program).uid, &fresh);
            if (fresh) {
                g = over_[lo - 1].sender_genome;
                ++injected_programs;
            }
        }
        i = j;
    }
    return found;
}

// One lane of a sample's work on the places: every ratio_lanes_-th place
// from its own number on, its compressibility estimate with the lane's own
// tables, and its threads' sampled views.
void Observer::ratio_lane(void* ctx, u32 lane) {
    Observer& o = *static_cast<Observer*>(ctx);
    World& w = *o.ratio_world_;
    for (u32 i = lane; i < w.place_count(); i += o.ratio_lanes_) {
        const Memory& m = w.place(i).mem;
        o.ratios_[i] =
            estimate_compressibility(m.bytes() + m.region_base(), m.region_size(), &o.compress_[lane]).ratio;
        o.walk_threads(w.place(i), &o.walk_cross_[i], &o.walk_hijack_[i]);
    }
}

void Observer::sample(World& w, bool clocked) {
    forget_senders(w);
    forget_talkers(w);
    // An open-ended run lets the extinct go (observe.h), here between two
    // ticks' ingests, each time the kept genomes have doubled.
    if (s_.forget && s_.keep_generations >= 2 && kept_n_ >= (forget_at_ ? forget_at_ : s_.forget_floor))
        forget_extinct();
    Sample s{};
    s.tick = w.tick_count();
    // The places' compressibility and their threads' sampled views first, on
    // the world's host threads.
    if (ratios_.size() != w.place_count()) {
        ratios_.resize(w.place_count());
        walk_cross_.resize(w.place_count());
        walk_hijack_.resize(w.place_count());
    }
    ratio_world_ = &w;
    ratio_lanes_ = min_of(min_of(w.pool().threads(), kCompressLanes), w.place_count());
    if (ratio_lanes_ > 1) {
        w.pool().run(ratio_lanes_, ratio_lane, this);
    } else { // one host thread, or a world whose threads haven't been started
        ratio_lanes_ = 1;
        ratio_lane(this, 0);
    }
    double ratio = 0;
    for (u32 i = 0; i < w.place_count(); ++i) {
        Place& p = w.place(i);
        s.programs += p.os.live_programs();
        s.threads += p.os.live_threads();
        s.threads_started += p.os.threads_started;
        s.idle_runs += p.region.started;
        s.idle_faults += p.region.faulted;
        s.idle_caps += p.region.capped;
        p.region.flush_paths(); // none wait between ticks; this makes sure of it
        s.paths_new += p.region.paths_new;
        s.idle_sends += p.os.idle_sends;
        s.run_alloc += p.os.run_calls[kAlloc];
        s.run_alloc_ok += p.os.run_alloc_ok;
        s.run_spawn += p.os.run_calls[kSpawn];
        s.run_spawn_ok += p.os.run_spawn_ok;
        s.run_cpy += p.region.run_cpy;
        s.run_cpy_own += p.region.run_cpy_own;
        s.instructions += p.sched.instructions + p.region.instructions;
        ratio += ratios_[i];
        // The sampled views (13.1): where each thread runs (walk_threads).
        s.cross_owner += walk_cross_[i];
        s.hijack += walk_hijack_[i];
        // Gate 4's.
        for (u32 k = 0; k < kHeatKinds; ++k)
            s.trips[k] += p.trips[k];
        s.cutouts += p.cutouts[0] + p.cutouts[1];
        s.lv_handlers += p.os.lv_handlers;
        s.lv_raised += p.sched.lv_raised;
        s.lv_taken += p.sched.lv_taken;
        s.lv_slept += p.lv_slept;
        s.lv_outlived += p.lv_outlived;
        s.power_drawn += static_cast<i64>(p.draw.total());
        s.temp_mean += p.temp;
        s.temp_high = i == 0 || p.temp > s.temp_high ? p.temp : s.temp_high;
        // Gate 5's: the calls programs' threads make.
        for (u32 k = 0; k < kCallCount; ++k) {
            s.calls[k] += p.os.calls[k];
            s.calls_ok[k] += p.os.calls_ok[k];
        }
        s.sends_other += p.os.sends_other;
        s.sends_other_ok += p.os.sends_other_ok;
        s.spawns_thread += p.os.spawns_thread;
        s.spawns_thread_ok += p.os.spawns_thread_ok;
        s.spawns_program += p.os.spawns_program;
        s.spawns_program_ok += p.os.spawns_program_ok;
        for (u32 k = 0; k < kHeatKinds; ++k) {
            s.failures += p.failures[k];
            s.replaced += p.replaced[k];
        }
        s.visits += p.visits;
    }
    // Gate 5's world.
    s.respawns = w.respawns;
    s.link_cuts = w.links().cut + w.links().patch_cut;
    s.link_worn = w.links().worn;
    s.mended = w.links().mended;
    s.strikes = w.strikes;
    s.partings = w.links().partings;
    // Reading input, overruns and injections (build-plan-gate5.md 2, gap 1).
    s.recvs = recvs;
    s.overruns = overruns;
    for (u32 k = 0; k < kReaches; ++k)
        s.reached[k] = reached[k];
    s.overruns_unknown = overruns_unknown;
    s.reader_births = reader_births;
    s.injection_births = injection_births;
    if (recvs) {
        for (usize i = 0; i < lin_.size(); ++i) {
            const LineageRec& L = lin_[i];
            if (L.origin && L.alive && reads_input(L, s.tick)) {
                ++s.readers;
                s.reader_programs += L.alive;
            }
        }
    }
    s.injections = find_injections(w);
    program_samples += s.programs;
    reader_program_samples += s.reader_programs;
    injections_seen += s.injections;
    s.temp_mean = w.place_count() ? s.temp_mean / static_cast<i64>(w.place_count()) : 0;
    if (w.powered()) {
        const Sources& src = w.sources();
        s.sources = src.count();
        for (u32 k = 0; k < src.count(); ++k) {
            s.sources_swell += src.state(k).swell > kCapacityOne ? 1 : 0;
            s.sources_out += src.state(k).capacity < kCapacityOne / 10 ? 1 : 0;
        }
        const Grid& g = w.grid();
        s.power_given = g.given_uw;
        s.power_lost = static_cast<i64>(g.links_uw + g.drains_uw + g.inside_uw);
        s.solves = g.solves;
        s.solve_sweeps = g.sweeps;
        s.solves_capped = g.capped;
    }
    s.heat_out = w.heat_out_uw;
    s.region_ratio = w.place_count() ? ratio / w.place_count() : 0;
    s.births = total_births;
    s.faithful = total_faithful;
    for (u32 i = 0; i < kDeathCauses; ++i)
        s.deaths[i] = deaths_by_cause[i];
    s.scavenger = scavenger;
    s.genomes = kept_n_; // kept; and those held, not yet kept (build-plan-observe.md 3.2)
    s.genomes_held = held_n_;
    s.groups = groups_.size();
    s.lineages = lineages_ever(); // those let go too (3.3)
    s.written = written;
    s.region_copies = region_copies;
    s.program_copies = program_copies;
    s.near_copies = near_copies;
    s.image_copies = image_copies;
    s.handovers = handovers;
    s.cold_starts = cold_starts;
    s.boots = births_by_cause[static_cast<u8>(BirthCause::boot)];
    const NetCounts& nc = w.net().counts();
    s.net_sent = nc.sent;
    s.net_delivered = nc.delivered;
    s.net_pings = nc.pings;
    s.net_full = nc.full;
    s.net_stranded = nc.stranded;
    s.net_lost = nc.lost;
    s.net_cut = nc.cut;
    s.colonies = colonies_.size() - current_colonies(); // the current's aren't spread (12.6)
    s.colonists = births_by_cause[static_cast<u8>(BirthCause::arrival)];
    while (colony_clocks_.size() < colonies_.size())
        colony_clocks_.push(LineageClock{});
    for (u32 i = 0; i < colonies_.size(); ++i) {
        const ColonyRec& C = colonies_[i];
        s.colonies_alive += C.alive && !C.by_current() ? 1 : 0;
        const u32 programs = C.place < w.place_count() ? w.place(C.place).os.live_programs() : 0;
        s.settled += colony_settles(C, s.tick, programs, stages_, &colony_clocks_[i]) ? 1 : 0;
    }
    last_ = s;
    ++taken_;
    // Gate 5's watching at a sample: the places' holds (worn ground), and
    // the calls as each turnover window ends, era by era.
    holds_at_sample(w);
    if (clocked && s_.turnover.window && s.tick % s_.turnover.window == 0) {
        CallsRow c{};
        c.tick = s.tick;
        c.program_samples = program_samples;
        memcpy(c.calls, s.calls, sizeof c.calls);
        calls_rows.push(c);
    }
    // Its row, at every write_every-th sample by the clock, and at any
    // sample asked for (a seed's last): the sample as it stands.
    if (!clocked || s_.write_every <= 1 || s.tick % (static_cast<u64>(s_.sample_every) * s_.write_every) == 0) {
        samples.push(s);
        // Each place's voltage since the row before: as it is now if no tick
        // was ingested since (the observer was just attached).
        if (volt_places != w.place_count()) {
            // The first row (a world's places don't change): voltages start
            // here, and the rows before it, if any, are given none.
            NOVA2_ASSERT(volts.empty());
            volt_places = w.place_count();
            for (u32 k = 0; k + 1 < samples.size(); ++k) // rows before it: none of their voltages kept
                for (u32 i = 0; i < volt_places; ++i)
                    volts.push(PlaceVolts{0, 0});
        }
        for (u32 i = 0; i < volt_places; ++i) {
            const i32 now = w.place(i).env.voltage_mv;
            PlaceVolts pv{static_cast<i16>(now), static_cast<i16>(now)};
            if (i < volt_sum_.size() && volt_sum_[i].ticks) {
                VoltSum& v = volt_sum_[i];
                pv.mean = static_cast<i16>(v.sum / v.ticks);
                pv.low = static_cast<i16>(v.low);
                v = VoltSum{};
            }
            volts.push(pv);
        }
    }
    // The records' files are written every records_flush_ seconds: a sample
    // is often enough to look at the clock.
    samples.stream().tick();
    volts.stream().tick();
    windows.stream().tick();
    ground_windows.stream().tick();
}

} // namespace nova2

namespace nova2 {

void PlaceTurnover::death(u64 born, DeathCause cause) {
    ++deaths;
    const u8 c = static_cast<u8>(cause);
    ++deaths_by_cause[c < kDeathCauses ? c : 0];
    // Born before the window started, and alive since: one of its starters.
    if (born != ~0ull && born < start)
        ++starters_died;
}

bool PlaceTurnover::end_tick(u64 done, u32 live, bool full, const TurnoverSettings& s, u32 place, TurnoverWindow* out) {
    program_ticks += live;
    if (s.window == 0 || done % s.window != 0)
        return false;
    TurnoverWindow w{};
    w.place = place;
    w.start = start;
    w.starters = starters;
    w.starters_died = starters_died;
    w.births = births;
    w.deaths = deaths;
    w.crowded = crowded;
    w.cold = cold;
    w.counted = starters > 0 && starters >= s.members;
    w.turned = w.counted && 2ull * starters_died >= starters && 2ull * births >= starters_died;
    if (w.counted) {
        ++windows;
        if (w.turned)
            ++turned;
        if (crowded) {
            ++crowded_windows;
            if (w.turned)
                ++crowded_turned;
        }
        if (cold) {
            ++cold_windows;
            cold_turned += w.turned ? 1 : 0;
            crowded_cold_turned += w.turned && crowded ? 1 : 0;
        }
        if (w.turned) {
            streak = 0;
        } else if (++streak > longest) {
            longest = streak;
            longest_end = done;
        }
    } else {
        streak = 0; // too few programs to say: the run of frozen windows is broken
    }
    *out = w;
    // The next window starts now, with the programs alive after this tick.
    start = done;
    starters = live;
    starters_died = births = deaths = 0;
    crowded = full;
    cold = false;
    return true;
}

Ground ground_of(u32 cores, u64 window, u64 cut, u64 heat, u64 dark, bool* both) {
    const u64 core_time = static_cast<u64>(cores) * window;
    const bool hot = 4 * heat > core_time;
    const bool dim = 4 * cut > core_time || 4 * dark > window;
    if (both)
        *both = hot && dim;
    if (hot && (!dim || heat >= cut))
        return Ground::hot;
    return dim ? Ground::dim : Ground::fair;
}

namespace {

// A recorded window's place, start and ground, for root_grounds.
struct RootKey {
    u64 start;
    u32 place;
    u8 ground;
};

int cmp_root(const void* a, const void* b) {
    const RootKey& x = *static_cast<const RootKey*>(a);
    const RootKey& y = *static_cast<const RootKey*>(b);
    if (x.place != y.place)
        return x.place < y.place ? -1 : 1;
    return x.start < y.start ? -1 : x.start > y.start ? 1 : 0;
}

} // namespace

void root_grounds(const Records<GroundWindow>& windows, const Array<CascadeRec>& cascades, u64 window,
                  Array<u8>* out) {
    out->resize(0);
    out->resize(cascades.size());
    out->fill(kRootNone);
    if (!window || !cascades.size() || !windows.size())
        return;
    Array<RootKey> keys;
    keys.resize(windows.size());
    for (usize k = 0; k < windows.size(); ++k) {
        const GroundWindow g = windows[k];
        keys[k] = RootKey{g.start, g.place, g.both ? kRootBoth : static_cast<u8>(g.ground)};
    }
    qsort(keys.data(), keys.size(), sizeof(RootKey), cmp_root);
    for (usize c = 0; c < cascades.size(); ++c) {
        const CascadeRec& r = cascades[c];
        // The first window of the root's that began at or after the
        // cascade's tick; the one before it is the last begun before.
        usize lo = 0, hi = keys.size();
        while (lo < hi) {
            const usize mid = lo + (hi - lo) / 2;
            const RootKey& m = keys[mid];
            if (m.place < r.root || (m.place == r.root && m.start < r.start))
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == 0)
            continue;
        const RootKey& k = keys[lo - 1];
        if (k.place == r.root && r.start <= k.start + window)
            (*out)[c] = k.ground;
    }
}

void PlaceGround::begin(u64 done, u64 cut, u64 heat, u64 dark) {
    begun = true;
    // After the place's first tick its counters hold that tick's alone, so
    // its first window is watched whole and counts from nothing. Joined any
    // later, what the window under way had lost before isn't known.
    whole = done <= 1;
    cut0 = whole ? 0 : cut;
    heat0 = whole ? 0 : heat;
    dark0 = whole ? 0 : dark;
}

bool PlaceGround::close(const TurnoverWindow& w, u64 window, u32 cores, u64 cut, u64 heat, u64 dark,
                        GroundWindow* out) {
    GroundWindow g{};
    g.start = w.start;
    g.cut = cut - cut0;
    g.heat = heat - heat0;
    g.dark = static_cast<u32>(dark - dark0);
    g.place = w.place;
    g.cores = cores;
    g.starters = w.starters;
    g.births = w.births;
    g.lead = lead;
    g.lead_reads = lead_reads;
    g.ground = ground_of(cores, window, g.cut, g.heat, g.dark, &g.both);
    const bool watched = whole;
    // The next window starts now, watched from its first tick.
    cut0 = cut;
    heat0 = heat;
    dark0 = dark;
    whole = true;
    if (!watched)
        return false;
    const u32 k = static_cast<u32>(g.ground);
    ++windows[k];
    starters[k] += g.starters;
    births[k] += g.births;
    both += g.both ? 1 : 0;
    *out = g;
    return true;
}

void place_lands(const Observer& o, u32 places, PlaceLands* out) {
    PlaceLands& L = *out;
    const Array<RespawnRec>& rs = o.respawn_log; // in tick order, as they came
    L.at.clear();
    L.at.resize(places + 1);
    L.at.fill(0);
    for (usize k = 0; k < rs.size(); ++k)
        if (rs[k].place < places)
            ++L.at[rs[k].place + 1];
    for (u32 p = 0; p < places; ++p)
        L.at[p + 1] += L.at[p];
    L.tick.clear();
    L.tick.resize(L.at[places]);
    L.left.clear();
    L.left.resize(L.at[places]);
    Array<u32> fill;
    fill.assign(L.at.data(), places);
    for (usize k = 0; k < rs.size(); ++k) {
        const RespawnRec& r = rs[k];
        if (r.place >= places)
            continue;
        const u32 j = fill[r.place]++;
        L.tick[j] = r.tick;
        L.left[j] = r.left_mass;
    }
}

u32 land_then(const PlaceLands& L, const Map& m, u32 place, u64 tick) {
    // Its first respawn after the tick took it from where it stood then.
    if (place + 1 < L.at.size())
        for (u32 j = L.at[place]; j < L.at[place + 1]; ++j)
            if (L.tick[j] > tick)
                return L.left[j];
    return m.landmass(place);
}

// Gate 5's measure (observe.h). A group leads with more programs than any
// other group, and stage 4's numbers of them: the hold a lineage is judged
// by, asked of a group.
bool group_leads(const GroundWindow& g, const StageCriteria& c) {
    const GroundLead& l = g.lead;
    return l.group_ref != 0 && l.group_programs > l.group_second &&
           lineage_holds(l.group_programs, l.group_copies, g.starters, c);
}

void tally_eras(const Records<GroundWindow>& rows, u64 stage4, u64 end, u64 era, const StageCriteria& c,
                EraTally* out, Array<FirstLead>* firsts) {
    *out = EraTally{};
    if (firsts)
        firsts->clear();
    const bool staged = stage4 != ~0ull && era != 0 && end > stage4;
    out->eras = staged ? static_cast<u32>((end - stage4) / era) : 0;
    out->late = out->eras / 2;
    const u32 first_late = out->eras - out->late;
    Array<u8> has; // per era: a first lead came in it
    has.resize(out->eras);
    HashMap<u8> led; // the groups that have led, by their reference
    for (u64 i = 0; i < rows.size(); ++i) {
        const GroundWindow g = rows[i];
        ++out->windows;
        if (!group_leads(g, c))
            continue;
        ++out->led;
        out->led_readers += g.lead_reads ? 1 : 0;
        bool fresh = false;
        led.get_or_insert(g.lead.group_ref, &fresh) = 1;
        if (!fresh)
            continue;
        ++out->first_leads;
        FirstLead f{};
        f.tick = g.start;
        f.group_ref = g.lead.group_ref;
        f.place = g.place;
        f.programs = g.lead.group_programs;
        f.copies = g.lead.group_copies;
        f.era = kNoEra;
        if (stage4 == ~0ull || g.start < stage4) {
            ++out->first_before;
        } else if (staged && (g.start - stage4) / era < out->eras) {
            f.era = static_cast<u32>((g.start - stage4) / era);
            f.late = f.era >= first_late;
            has[f.era] = 1;
        }
        if (firsts)
            firsts->push(f);
    }
    for (u32 k = 0; k < out->eras; ++k) {
        out->changed += has[k];
        out->late_changed += k >= first_late ? has[k] : 0;
    }
}

u32 Observer::current_colonies() const {
    u32 n = 0;
    for (usize i = 0; i < colonies_.size(); ++i)
        n += colonies_[i].by_current() ? 1 : 0;
    return n;
}

bool lineage_holds(u32 alive, u32 copies, u32 place_programs, const StageCriteria& c) {
    if (!alive || alive < c.members || !place_programs)
        return false;
    if (static_cast<double>(copies) < c.copies * alive)
        return false;
    return static_cast<double>(alive) >= c.share * place_programs;
}

bool colony_settles(const ColonyRec& C, u64 now, u32 place_programs, const StageCriteria& c, LineageClock* k) {
    if (!C.colonist || !lineage_holds(C.alive, C.alive_copies, place_programs, c))
        k->held_since = ~0ull;
    else if (k->held_since == ~0ull)
        k->held_since = now;
    if (k->established == ~0ull && k->held_since != ~0ull && now - k->held_since >= c.span)
        k->established = now;
    return k->established != ~0ull;
}

u32 lineage_stage(const LineageRec& L, u64 now, u32 place_programs, const StageCriteria& c, LineageClock* k) {
    if (!lineage_holds(L.home_alive, L.home_copies, place_programs, c))
        k->held_since = ~0ull;
    else if (k->held_since == ~0ull)
        k->held_since = now;
    if (L.first_copy == ~0ull)
        return 0;
    if (L.first_viable == ~0ull)
        return 1;
    if (L.max_depth < c.generations)
        return 2;
    if (k->established == ~0ull) {
        if (k->held_since == ~0ull || now - k->held_since < c.span)
            return 3;
        k->established = now;
        k->groups_then = L.groups_full;
    }
    // Stage 5: adapting once established. Groups it made before then, while
    // it was taking its place, don't count.
    return L.groups_full - k->groups_then >= c.groups ? 5 : 4;
}

// ---------------------------------------------------------------------------
// The observer's memory, to a file and back (build-plan-viewer.md step 9)
// ---------------------------------------------------------------------------

namespace {

// "NOV2OBS2": samples, voltages and windows became record streams
// (build-plan-observe.md step 3), saved as below.
// "NOV2OBS3": 3 adds forgetting's settings and counts.
// "NOV2OBS4": 4 adds the ground measure (build-plan-gate4.md step 1), at the
// end.
// "NOV2OBS5": 5 has gate 4's seventh cause of death, heat, and its readings
// in each sample (steps 7 and 8). A sample's row grew, so the saves and
// record files of the builds before it aren't read (design.md 2: no
// backward compatibility in beta); the build that made them reads them.
// "NOV2OBS6": 6 has who led each place as a window began in the ground
// record's rows (build-plan-gate5.md 2), so those rows grew too.
// "NOV2OBS7": 7 has the tally of the shallow lineages let go without a row
// (ObserverSettings::lineage_generations), and that setting among its words.
// "NOV2OBS8": 8 has gate 5's (build-plan-gate5.md step 1): the day among
// its words, a sample's calls and readings, a lineage's RECVs and members'
// ticks, a ground row's reader, and at the end the overruns watched.
// "NOV2OBS9": 9 has the storage changes of 2026-11-20 (build-plan-gate5.md
// 11): samples written every write_every-th, so the latest sample and the
// count of them are saved; the kept genomes' bytes as differences; and the
// groups' index left out, built again on loading.
// "NOV2OBSA": A has the reviewer's measures (build-plan-gate4.md 11): five
//             more fields in a place's ground and in each ground window, and
//             SPAWN counted two ways in a sample.
// "NOV2OBSB": B has gate 5's two causes of death (build-plan-gate5.md step
//             10), "out of memory" and "watchdog", in every count by cause.
// "NOV2OBSC": C has gate 5's watching (build-plan-gate5.md step 11; watch.h):
//             a program's size at birth, worn ground and holds in a place's
//             ground and each ground window, and the watch's own tables.
// "NOV2OBSD": D has gate 5's world in each sample and the events log for
//             the views (build-plan-gate5.md step 13).
// "NOV2OBSE": E keeps each strait's last state by its two landmasses, not
//             its place in the list, so a respawn's rebuilt list makes no
//             partings or joinings (build-plan-gate5.md 11, 2026-11-23).
// "NOV2OBSF": F has the landmass a place stood on before each respawn, in
//             its RespawnRec (the same, 2026-11-23).
constexpr u64 kObserverTag = 0x4653424F32564F4Eull;

// A record stream into a save: whether its rows are in a file, its row's
// size and how many rows it has; and in memory, the rows themselves (in a
// file they are there already, once flushed).
void put_stream(FileOut& f, const RecordStream& s) {
    f.put_u64(s.in_file() ? 1 : 0);
    f.put_u64(s.row_size());
    f.put_u64(s.rows());
    if (!s.in_file())
        f.put(s.memory_rows(), static_cast<usize>(s.rows() * s.row_size()));
}

// And back: a stream saved in a file is opened at `path` (it must be given),
// cut back to the rows the save counted; one saved in memory is read back
// into memory, or into a new file at `path` if one is given.
bool get_stream(FileIn& f, RecordStream& s, const char* path, double flush_seconds) {
    u64 in_file = 0, row = 0, rows = 0;
    if (!f.get_u64(&in_file) || !f.get_u64(&row) || !f.get_u64(&rows) || row != s.row_size() || row == 0)
        return false;
    if (in_file)
        return path && s.open_file(path, static_cast<u32>(row), rows, flush_seconds);
    if (rows > f.left() / row)
        return false;
    if (path) {
        if (!s.open_file(path, static_cast<u32>(row), 0, flush_seconds))
            return false;
    } else {
        s.open_memory(static_cast<u32>(row));
    }
    // In one read, as put_stream wrote them: the file's hash is taken a
    // put at a time, so a read must match the put.
    Array<u8> all;
    all.resize_unfilled(static_cast<usize>(rows * row));
    if (!f.get(all.data(), all.size()))
        return false;
    for (u64 i = 0; i < rows; ++i)
        s.add(all.data() + i * row);
    return s.ok();
}

// The settings an observer's memory was gathered under, as numbers: one
// read back under others would go on wrongly.
constexpr u32 kSettingsWords = 12;
void settings_words(const ObserverSettings& s, u64 out[kSettingsWords]) {
    out[0] = s.sample_every;
    memcpy(&out[1], &s.group_threshold, 8);
    out[2] = s.keep_facts ? 1 : 0;
    out[3] = s.min_genome;
    out[4] = s.turnover.window;
    out[5] = s.turnover.members;
    out[6] = s.keep_generations;
    out[7] = s.forget ? 1 : 0;
    out[8] = s.forget_floor;
    out[9] = s.lineage_generations;
    out[10] = s.day;
    out[11] = s.write_every;
}

} // namespace

void Observer::save(FileOut& f) const {
    f.put_u64(kObserverTag);
    u64 words[kSettingsWords];
    settings_words(s_, words);
    f.put(words, sizeof words);
    // The totals.
    const u64 totals[] = {total_births, total_faithful,  total_deaths, scavenger,   written, region_copies,
                          program_copies, near_copies,   image_copies, sends,       sends_port0, handovers,
                          cold_starts,    volt_places};
    f.put(totals, sizeof totals);
    f.put(deaths_by_cause, sizeof deaths_by_cause);
    f.put(births_by_cause, sizeof births_by_cause);
    // What it has gathered.
    f.put_table(births);
    f.put_table(deaths);
    put_stream(f, samples.stream());
    f.put_table(turnover);
    put_stream(f, windows.stream());
    put_stream(f, volts.stream());
    f.put_table(volt_sum_);
    // The programs alive, and the dead ones still remembered.
    programs_.save(f);
    f.put_table(prog_);
    f.put_table(free_prog_);
    dead_senders_.save(f);
    f.put_table(boot_writer_);
    // The family tree, and the colonies.
    lineages_.save(f);
    f.put_table(lin_);
    lineage_groups_.save(f);
    f.put_table(colonies_);
    f.put_table(colony_clocks_);
    // The genebank and its groups. The groups' index is left out (Damian,
    // 2026-11-20: it was over half of a long run's save): a load works it
    // out again from the groups' references (runs_again, index_groups). Only
    // the runs of a group whose first reference was let go are saved, 3
    // bytes a run.
    genomes_.save(f);
    f.put_table(gen_);
    f.put_table(bank_);
    f.put_table(groups_);
    Array<u8> own;
    for (usize k = 0; k < groups_.size(); ++k) {
        if (!groups_[k].own_runs)
            continue;
        for (u32 i = 0; i < groups_[k].grams_n; ++i) {
            const u64 run = grams_[groups_[k].grams_at + i];
            own.push(static_cast<u8>(run));
            own.push(static_cast<u8>(run >> 8));
            own.push(static_cast<u8>(run >> 16));
        }
    }
    f.put_table(own);
    // The genebank's slots and counts, and the kept genomes' records.
    f.put_table(free_gen_);
    const u64 bank_counts[] = {bank_dead_, kept_n_, held_n_, forget_at_, groups_ever_, forgotten_};
    f.put(bank_counts, sizeof bank_counts);
    put_stream(f, genebank.stream());
    put_stream(f, genebank_bytes);
    // The lineages let go (3.3), and their free slots.
    f.put_table(free_lin_);
    put_stream(f, finished_lineages.stream());
    const u64 shallow[] = {shallow_lineages, shallow_births};
    f.put(shallow, sizeof shallow);
    // The ground measure.
    f.put_table(ground);
    put_stream(f, ground_windows.stream());
    // Gate 5's totals, and the overruns watched.
    const u64 g5[] = {recvs,         overruns,         overruns_unknown,    reader_births,     injection_births,
                      reached[0],    reached[1],       reached[2],          reached[3],        reached[4],
                      program_samples, reader_program_samples, injections_seen, taken_, injected_programs};
    f.put(g5, sizeof g5);
    f.put_table(over_);
    f.put_table(over_bytes_);
    injected_.save(f);
    dead_talkers_.save(f);
    // The latest sample: with write_every over 1 it may be in no row.
    Array<Sample> latest;
    latest.push(last_);
    f.put_table(latest);
    // Gate 5's watching (watch.h).
    f.put_table(cascade_watch.places);
    f.put_table(cascade_watch.cascades);
    f.put(&tally5, sizeof tally5);
    f.put_table(respawn_log);
    f.put_table(joinings);
    f.put_table(event_log);
    f.put_table(fail_seen_);
    f.put_table(carried_births);
    f.put_table(calls_rows);
    f.put_table(lin_held_);
    f.put_table(col_held_);
    f.put_table(last_cut_);
    f.put_table(last_trip_);
    f.put_table(patch_seen_);
    f.put_table(strait_seen_);
}

bool Observer::load(FileIn& f) {
    u64 tag = 0, words[kSettingsWords], saved[kSettingsWords];
    settings_words(s_, words);
    if (!f.get_u64(&tag) || tag != kObserverTag || !f.get(saved, sizeof saved) ||
        memcmp(saved, words, sizeof words) != 0)
        return false;
    u64 totals[14];
    if (!f.get(totals, sizeof totals) || !f.get(deaths_by_cause, sizeof deaths_by_cause) ||
        !f.get(births_by_cause, sizeof births_by_cause))
        return false;
    total_births = totals[0];
    total_faithful = totals[1];
    total_deaths = totals[2];
    scavenger = totals[3];
    written = totals[4];
    region_copies = totals[5];
    program_copies = totals[6];
    near_copies = totals[7];
    image_copies = totals[8];
    sends = totals[9];
    sends_port0 = totals[10];
    handovers = totals[11];
    cold_starts = totals[12];
    volt_places = static_cast<u32>(totals[13]);
    // The records, in their files (records_to, carried on) or in memory.
    char ps[sizeof records_dir_], pv[sizeof records_dir_], pw[sizeof records_dir_];
    records_path("samples", ps, sizeof ps);
    records_path("volts", pv, sizeof pv);
    records_path("windows", pw, sizeof pw);
    const bool files = records_dir_[0] != 0;
    Array<u8> own_runs; // of the groups whose first reference was let go
    const bool got =
        f.get_table(&births) && f.get_table(&deaths) &&
        get_stream(f, samples.stream(), files ? ps : nullptr, records_flush_) && f.get_table(&turnover) &&
        get_stream(f, windows.stream(), files ? pw : nullptr, records_flush_) &&
        get_stream(f, volts.stream(), files ? pv : nullptr, records_flush_) && f.get_table(&volt_sum_) &&
        programs_.load(f) &&
        f.get_table(&prog_) && f.get_table(&free_prog_) && dead_senders_.load(f) && f.get_table(&boot_writer_) &&
        lineages_.load(f) && f.get_table(&lin_) && lineage_groups_.load(f) && f.get_table(&colonies_) &&
        f.get_table(&colony_clocks_) && genomes_.load(f) && f.get_table(&gen_) && f.get_table(&bank_) &&
        f.get_table(&groups_) && f.get_table(&own_runs);
    char pg[sizeof records_dir_], pb[sizeof records_dir_];
    records_path("genomes", pg, sizeof pg);
    records_path("genome_bytes", pb, sizeof pb);
    char pl[sizeof records_dir_];
    records_path("lineages", pl, sizeof pl);
    u64 bank_counts[6];
    bool got_bank = got && f.get_table(&free_gen_) && f.get(bank_counts, sizeof bank_counts) &&
                    get_stream(f, genebank.stream(), files ? pg : nullptr, records_flush_) &&
                    get_stream(f, genebank_bytes, files ? pb : nullptr, records_flush_) &&
                    f.get_table(&free_lin_) &&
                    get_stream(f, finished_lineages.stream(), files ? pl : nullptr, records_flush_);
    u64 shallow[2] = {0, 0};
    got_bank = got_bank && f.get(shallow, sizeof shallow);
    shallow_lineages = shallow[0];
    shallow_births = shallow[1];
    // The ground measure.
    char pd[sizeof records_dir_];
    records_path("ground", pd, sizeof pd);
    if (got_bank)
        got_bank = f.get_table(&ground) && get_stream(f, ground_windows.stream(), files ? pd : nullptr, records_flush_);
    u64 g5[15] = {};
    Array<Sample> latest;
    if (got_bank)
        got_bank = f.get(g5, sizeof g5) && f.get_table(&over_) && f.get_table(&over_bytes_) && injected_.load(f) &&
                   dead_talkers_.load(f) && f.get_table(&latest) && latest.size() == 1;
    if (got_bank)
        last_ = latest[0];
    // Gate 5's watching.
    if (got_bank)
        got_bank = f.get_table(&cascade_watch.places) && f.get_table(&cascade_watch.cascades) &&
                   f.get(&tally5, sizeof tally5) && f.get_table(&respawn_log) && f.get_table(&joinings) &&
                   f.get_table(&event_log) && f.get_table(&fail_seen_) &&
                   f.get_table(&carried_births) && f.get_table(&calls_rows) && f.get_table(&lin_held_) &&
                   f.get_table(&col_held_) && f.get_table(&last_cut_) && f.get_table(&last_trip_) &&
                   f.get_table(&patch_seen_) && f.get_table(&strait_seen_);
    adj_begin_.clear();
    adj_.clear();
    adj_respawns_ = ~0u;
    taken_ = g5[13];
    injected_programs = g5[14];
    recvs = g5[0];
    overruns = g5[1];
    overruns_unknown = g5[2];
    reader_births = g5[3];
    injection_births = g5[4];
    for (u32 i = 0; i < kReaches; ++i)
        reached[i] = g5[5 + i];
    program_samples = g5[10];
    reader_program_samples = g5[11];
    injections_seen = g5[12];
    for (usize i = 0; got_bank && i < over_.size(); ++i)
        if (over_[i].bytes_at > over_bytes_.size() || over_[i].len > over_bytes_.size() - over_[i].bytes_at)
            return false;
    if (got_bank) {
        bank_dead_ = bank_counts[0];
        kept_n_ = bank_counts[1];
        held_n_ = bank_counts[2];
        forget_at_ = bank_counts[3];
        groups_ever_ = bank_counts[4];
        forgotten_ = bank_counts[5];
    }
    // What is worked out afresh at each use, as init leaves it.
    listed_.resize(0);
    listed_.resize(groups_.size());
    sent_live_.clear();
    rarity_.clear();
    candidates_.clear();
    scratch_.clear();
    // The tables must agree with each other: every index in range.
    if (!got_bank || programs_.size() + free_prog_.size() != prog_.size() ||
        lineages_.size() + free_lin_.size() != lin_.size() ||
        genomes_.size() + free_gen_.size() != gen_.size() || genomes_.size() != kept_n_ + held_n_ ||
        colony_clocks_.size() > colonies_.size() ||
        (volt_places && volts.size() != samples.size() * volt_places))
        return false;
    for (usize i = 0; i < gen_.size(); ++i) {
        const GenomeRec& g = gen_[i];
        if (g.state == GenomeState::free)
            continue;
        if (g.state != GenomeState::held && g.state != GenomeState::kept)
            return false;
        if ((g.state == GenomeState::kept) != (g.group != kNoGroup) ||
            (g.group != kNoGroup && g.group >= groups_.size()) || g.at > bank_.size() || g.len > bank_.size() - g.at)
            return false;
    }
    for (usize i = 0; i < free_gen_.size(); ++i)
        if (free_gen_[i] >= gen_.size() || gen_[free_gen_[i]].state != GenomeState::free)
            return false;
    for (usize i = 0; i < free_lin_.size(); ++i)
        if (free_lin_[i] >= lin_.size() || lin_[free_lin_[i]].origin != 0)
            return false;
    for (usize i = 0; i < groups_.size(); ++i)
        if (groups_[i].genome >= gen_.size() || gen_[groups_[i].genome].state != GenomeState::kept)
            return false;
    // The kept genomes' rows, for the genomes kept after them.
    for (usize i = 0; i < gen_.size(); ++i)
        if (gen_[i].state == GenomeState::kept && gen_[i].bank_row >= genebank.size())
            return false;
    // The groups' runs and their index, worked out again.
    if (!runs_again(own_runs))
        return false;
    index_groups();
    return true;
}

} // namespace nova2
