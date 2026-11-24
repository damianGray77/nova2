#include "watch_report.h"

#include <cstdio>
#include <cstdlib> // qsort

#include "nova2/map.h"
#include "nova2/place.h"
#include "nova2/world.h"

namespace nova2 {

namespace {

using ull = unsigned long long;

int cmp_u64(const void* a, const void* b) {
    const u64 x = *static_cast<const u64*>(a), y = *static_cast<const u64*>(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

// Sorts them and drops repeats: how many are left.
usize unique_u64(Array<u64>& v) {
    if (!v.size())
        return 0;
    qsort(v.data(), v.size(), sizeof(u64), cmp_u64);
    usize k = 1;
    for (usize i = 1; i < v.size(); ++i)
        if (v[i] != v[k - 1])
            v[k++] = v[i];
    v.resize(k);
    return k;
}

// Each place's links from `from` along the map's data links (~0: no way).
void data_hops(const Map& m, u32 from, u32 n, Array<u32>* hops) {
    hops->resize(0);
    hops->resize(n);
    hops->fill(~0u);
    Array<u32> queue;
    (*hops)[from] = 0;
    queue.push(from);
    for (usize q = 0; q < queue.size(); ++q) {
        const u32 v = queue[q];
        for (u32 j = 0; j < m.degree(v); ++j) {
            const u32 u = m.other(m.link_at(v, j), v);
            if ((*hops)[u] == ~0u) {
                (*hops)[u] = (*hops)[v] + 1;
                queue.push(u);
            }
        }
    }
}

// The ground of a cascade's first place's window (root_grounds), as the
// cascades' CSV names it.
const char* const kRootName[kRootGrounds] = {"fair", "hot", "dim", "hot+dim", "none"};

// 0, 1, 2, 3, 4 to 7, 8 or more, and no way.
u32 far_bin(u32 h) { return h == ~0u ? 6 : h < 4 ? h : h < 8 ? 4 : 5; }

// The calls as they stood at tick b: the last row at or before it (none:
// nullptr, nothing yet).
const CallsRow* row_at(const Array<CallsRow>& rows, u64 b) {
    const CallsRow* r = nullptr;
    for (usize i = 0; i < rows.size() && rows[i].tick <= b; ++i)
        r = &rows[i];
    return r;
}

double pct(u64 a, u64 b) { return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0; }
double ratio(double a, double b) { return b > 0 ? a / b : 0.0; }

// A group leading a place on one side of a strait as it joined.
struct SideLead {
    u64 ref;
    u32 join, side;
};

int cmp_side(const void* a, const void* b) {
    const SideLead& x = *static_cast<const SideLead*>(a);
    const SideLead& y = *static_cast<const SideLead*>(b);
    if (x.join != y.join)
        return x.join < y.join ? -1 : 1;
    if (x.side != y.side)
        return x.side < y.side ? -1 : 1;
    return x.ref < y.ref ? -1 : x.ref > y.ref ? 1 : 0;
}

} // namespace

void SeedWatch::add(const SeedWatch& b) {
    place_ticks += b.place_ticks;
    for (u32 k = 0; k < kHeatKinds; ++k) {
        failures[k] += b.failures[k];
        failed_ticks[k] += b.failed_ticks[k];
        devices[k] += b.devices[k];
        replaced[k] += b.replaced[k];
    }
    visits += b.visits;
    respawns += b.respawns;
    respawns_moved += b.respawns_moved;
    torn += b.torn;
    left_out += b.left_out;
    link_cuts += b.link_cuts;
    link_mends += b.link_mends;
    patch_cuts += b.patch_cuts;
    partings += b.partings;
    joinings += b.joinings;
    for (u32 k = 0; k < kPatchTypes; ++k)
        patches[k] += b.patches[k];
    strikes += b.strikes;
    hammered += b.hammered;
    hammer += b.hammer;
    flaky_parts += b.flaky_parts;
    parts += b.parts;
    boot_failures += b.boot_failures;
    drift.drawn += b.drift.drawn;
    drift.becalmed += b.drift.becalmed;
    drift.unlit += b.drift.unlit;
    drift.unread += b.drift.unread;
    drift.down += b.drift.down;
    drift.dark += b.drift.dark;
    drift.full += b.drift.full;
    drift.sent += b.drift.sent;
    drift.sent_bytes += b.drift.sent_bytes;
    drift.held += b.drift.held;
    drift.landed += b.drift.landed;
    drift.landed_bytes += b.drift.landed_bytes;
    drift.unlanded += b.drift.unlanded;
    for (u32 k = 0; k < kDeathCauses; ++k)
        deaths[k] += b.deaths[k];
    scarred += b.scarred;
    if (b.scar_most > scar_most) {
        scar_most = b.scar_most;
        scar_place = b.scar_place;
    }
    cascades += b.cascades;
    for (u32 k = 0; k < kCascadeCauses; ++k)
        by_cause[k] += b.by_cause[k];
    for (u32 k = 0; k < 4; ++k)
        sizes[k] += b.sizes[k];
    largest = b.largest > largest ? b.largest : largest;
    root_sag += b.root_sag;
    source_sag += b.source_sag;
    sources += b.sources;
    for (u32 g = 0; g < kRootGrounds; ++g) {
        by_root[g] += b.by_root[g];
        for (u32 k = 0; k < kCascadeCauses; ++k)
            root_cause[g][k] += b.root_cause[g][k];
        for (u32 k = 0; k < 4; ++k)
            root_sizes[g][k] += b.root_sizes[g][k];
        root_windows[g] += b.root_windows[g];
    }
    for (u32 g = 0; g < 2; ++g) {
        windows[g] += b.windows[g];
        for (u32 a = 0; a < kAgeBins; ++a) {
            held[g][a] += b.held[g][a];
            ended[g][a] += b.ended[g][a];
        }
        holder_programs[g] += b.holder_programs[g];
        holder_births[g] += b.holder_births[g];
        flaky_births[g] += b.flaky_births[g];
        flaky_mutations[g] += b.flaky_mutations[g];
        flaky_ticks[g] += b.flaky_ticks[g];
        flaky_windows[g] += b.flaky_windows[g];
        flaky_held[g] += b.flaky_held[g];
        for (u32 s = 0; s < kSizeBins; ++s)
            ended_by[g][s] += b.ended_by[g][s];
        ended_leader[g] += b.ended_leader[g];
        leader_share[g] += b.leader_share[g];
        join_groups[g] += b.join_groups[g];
        across[g] += b.across[g];
        for (u32 c = 0; c < kCallCount; ++c)
            era_calls[g][c] += b.era_calls[g][c];
        era_days[g] += b.era_days[g];
        era_n[g] += b.era_n[g];
    }
    for (u32 s = 0; s < kSizeBins; ++s)
        births_by_size[s] += b.births_by_size[s];
    carried += b.carried;
    carried_lineages += b.carried_lineages;
    for (u32 k = 0; k < 7; ++k)
        carried_far[k] += b.carried_far[k];
    current_colonies += b.current_colonies;
    joins += b.joins;
    join_common += b.join_common;
    join_both += b.join_both;
    join_firsts += b.join_firsts;
}

void tally_watch(World& w, const Observer& o, const WatchParams& q, u64 stage4, const Array<FirstLead>& firsts,
                 SeedWatch& x) {
    const u32 n = w.place_count();
    const Map& m = w.map();
    x.place_ticks = w.tick_count() * n;
    // What the mechanisms did, from the world's counters.
    for (u32 i = 0; i < n; ++i) {
        Place& p = w.place(i);
        for (u32 k = 0; k < kHeatKinds; ++k) {
            x.failures[k] += p.failures[k];
            x.failed_ticks[k] += p.failed_ticks[k];
            x.replaced[k] += p.replaced[k];
        }
        for (u32 c = 0; c < p.core_count(); ++c) {
            ++x.devices[p.cores[c].kind == DeviceKind::e_core ? kHeatE : kHeatP];
            x.flaky_parts += p.noise.core_flaky(c) != kFlakyOne ? 1 : 0;
        }
        for (u32 d = 0; d < p.mem.device_count(); ++d)
            x.flaky_parts += p.noise.memory_flaky(d) != kFlakyOne ? 1 : 0;
        x.parts += p.core_count() + p.mem.device_count();
        x.devices[kHeatMemory] += p.mem.device_count();
        x.devices[kHeatDisk] += p.os.disk_size() ? 1 : 0;
        x.devices[kHeatNetwork] += 1;
        x.devices[kHeatCooler] += p.has_cooler ? 1 : 0;
        x.boot_failures += p.boot_failures;
        x.visits += p.visits;
        x.hammered += p.noise.hammered;
        x.hammer += p.noise.hammer;
    }
    x.respawns = w.respawns;
    x.respawns_moved = w.respawns_moved;
    x.torn = w.torn_programs;
    x.link_cuts = w.links().cut;
    x.link_mends = w.links().mended;
    x.patch_cuts = w.links().patch_cut;
    x.partings = w.links().partings;
    x.joinings = w.links().joinings;
    for (u32 k = 0; k < kPatchTypes; ++k)
        x.patches[k] = o.tally5.patches[k];
    x.strikes = w.strikes;
    x.drift = w.drift;
    for (u32 k = 0; k < kDeathCauses; ++k)
        x.deaths[k] = o.deaths_by_cause[k];
    x.scarred = o.tally5.scarred_ticks;
    // Cascades, and by the ground of the window at their first places.
    const Array<CascadeRec>& cs = o.cascade_watch.cascades;
    x.cascades = cs.size();
    Array<u8> roots;
    root_grounds(o.ground_windows, cs, q.window, &roots);
    for (usize k = 0; k < cs.size(); ++k) {
        const CascadeRec& c = cs[k];
        const u32 cause = static_cast<u32>(c.cause) % kCascadeCauses;
        const u32 size = c.size <= 2 ? 0 : c.size <= 4 ? 1 : c.size <= 9 ? 2 : 3;
        const u32 g = roots[k] < kRootGrounds ? roots[k] : kRootNone;
        ++x.by_cause[cause];
        ++x.sizes[size];
        ++x.by_root[g];
        ++x.root_cause[g][cause];
        ++x.root_sizes[g][size];
        x.largest = c.size > x.largest ? c.size : x.largest;
        x.root_sag += c.sag;
    }
    if (w.powered())
        for (u32 k = 0; k < w.sources().count(); ++k) {
            x.source_sag += w.sources().source(k).sag;
            ++x.sources;
        }
    // The ground record: worn and fresh, flaky or not, and each place's
    // scars over its recorded windows.
    Array<u64> scar, rows;
    scar.resize(n);
    rows.resize(n);
    for (usize k = 0; k < o.ground_windows.size(); ++k) {
        const GroundWindow r = o.ground_windows[k];
        const u32 g = r.worn ? 1 : 0;
        ++x.windows[g];
        ++x.root_windows[r.both ? kRootBoth : static_cast<u32>(r.ground) % kGrounds];
        u64 held = 0;
        for (u32 a = 0; a < kAgeBins; ++a) {
            x.held[g][a] += r.held[a];
            x.ended[g][a] += r.ended[a];
            held += r.held[a];
        }
        x.holder_programs[g] += r.holder_programs;
        x.holder_births[g] += r.holder_births;
        ++x.flaky_windows[r.flaky ? 1 : 0];
        x.flaky_held[r.flaky ? 1 : 0] += held;
        if (r.place < n) {
            scar[r.place] += r.failed;
            ++rows[r.place];
        }
    }
    for (u32 i = 0; i < n; ++i) {
        const double s = rows[i] && q.window ? static_cast<double>(scar[i]) / (rows[i] * q.window) : 0;
        if (s > x.scar_most) {
            x.scar_most = s;
            x.scar_place = i;
        }
    }
    // Flaky parts, the killer and the watchdog: the observer's tallies.
    const WatchTotals& t = o.tally5;
    for (u32 g = 0; g < 2; ++g) {
        x.flaky_births[g] = t.flaky_births[g];
        x.flaky_mutations[g] = t.flaky_mutations[g];
        x.flaky_ticks[g] = t.flaky_ticks[g];
        for (u32 s = 0; s < kSizeBins; ++s)
            x.ended_by[g][s] = t.ended[g][s];
        x.ended_leader[g] = t.ended_leader[g];
        x.leader_share[g] = t.leader_share[g];
    }
    for (u32 s = 0; s < kSizeBins; ++s)
        x.births_by_size[s] = t.births_by_size[s];
    // The current's births: their lineages, and how far each came alive
    // from its lineage's home, worked home by home.
    const Array<CarriedRec>& cb = o.carried_births;
    x.carried = cb.size();
    {
        Array<u64> origins, keys;
        for (usize k = 0; k < cb.size(); ++k) {
            origins.push(cb[k].origin);
            keys.push(static_cast<u64>(cb[k].home) << 32 | cb[k].place);
        }
        x.carried_lineages = unique_u64(origins);
        qsort(keys.data(), keys.size(), sizeof(u64), cmp_u64);
        Array<u32> hops;
        u32 home = ~0u;
        for (usize k = 0; k < keys.size(); ++k) {
            const u32 h = static_cast<u32>(keys[k] >> 32), at = static_cast<u32>(keys[k]);
            if (h >= n || at >= n) {
                ++x.carried_far[6];
                continue;
            }
            if (h != home) {
                data_hops(m, h, n, &hops);
                home = h;
            }
            ++x.carried_far[far_bin(hops[at])];
        }
    }
    x.current_colonies = o.current_colonies();
    // Straits joining again: who led on each side as each joined (the
    // window under way then, as it began), colonies founded across it and
    // first leads on either side over the era after. Each by the landmass
    // its place stood on then: a window's as it began, a colony's as it was
    // founded, a first lead's at its window (place_lands).
    const Array<JoinRec>& js = o.joinings;
    x.joins = js.size();
    if (js.size() && q.window) {
        PlaceLands lands;
        place_lands(o, n, &lands);
        Array<SideLead> leads;
        for (usize k = 0; k < o.ground_windows.size(); ++k) {
            const GroundWindow r = o.ground_windows[k];
            if (r.place >= n || !group_leads(r, q.stages))
                continue;
            const u32 land = land_then(lands, m, r.place, r.start);
            for (usize j = 0; j < js.size(); ++j) {
                const JoinRec& J = js[j];
                if (J.tick < r.start || J.tick >= r.start + q.window)
                    continue;
                if (land == J.ma || land == J.mb)
                    leads.push(SideLead{r.lead.group_ref, static_cast<u32>(j), land == J.ma ? 0u : 1u});
            }
        }
        qsort(leads.data(), leads.size(), sizeof(SideLead), cmp_side);
        usize k = 0;
        while (k < leads.size()) {
            const u32 j = leads[k].join;
            usize a = k, b = k;
            while (b < leads.size() && leads[b].join == j && leads[b].side == 0)
                ++b;
            usize e = b;
            while (e < leads.size() && leads[e].join == j)
                ++e;
            // [a, b): the first side, [b, e): the second, each sorted by group.
            u64 common = 0;
            for (usize i = a; i < b; ++i) {
                if (i > a && leads[i].ref == leads[i - 1].ref)
                    continue;
                ++x.join_groups[0];
                for (usize u = b; u < e; ++u)
                    if (leads[u].ref == leads[i].ref) {
                        ++common;
                        break;
                    }
            }
            for (usize u = b; u < e; ++u)
                x.join_groups[1] += u == b || leads[u].ref != leads[u - 1].ref ? 1 : 0;
            x.join_common += common;
            x.join_both += common ? 1 : 0;
            k = e;
        }
        for (usize j = 0; j < js.size(); ++j) {
            const JoinRec& J = js[j];
            const u64 to = J.tick + q.era;
            for (u32 i = 0; i < o.colony_count(); ++i) {
                const ColonyRec& C = o.colony_at(i);
                if (C.by_current() || C.first <= J.tick || C.first > to || C.from >= n || C.place >= n)
                    continue;
                const u32 lf = land_then(lands, m, C.from, C.first), lp = land_then(lands, m, C.place, C.first);
                x.across[0] += lf == J.ma && lp == J.mb ? 1 : 0;
                x.across[1] += lf == J.mb && lp == J.ma ? 1 : 0;
            }
            for (usize f = 0; f < firsts.size(); ++f) {
                const FirstLead& F = firsts[f];
                if (F.tick <= J.tick || F.tick > to || F.place >= n)
                    continue;
                const u32 land = land_then(lands, m, F.place, F.tick);
                x.join_firsts += land == J.ma || land == J.mb ? 1 : 0;
            }
        }
    }
    // Calls by era: the rows at each era's ends, from stage 4 on; the late
    // eras are the later half (of an odd number, the middle one is early).
    if (stage4 != ~0ull && q.era && q.day && w.tick_count() > stage4) {
        const u32 eras = static_cast<u32>((w.tick_count() - stage4) / q.era), late = eras / 2;
        for (u32 e = 0; e < eras; ++e) {
            const CallsRow* r0 = row_at(o.calls_rows, stage4 + e * q.era);
            const CallsRow* r1 = row_at(o.calls_rows, stage4 + (e + 1) * q.era);
            if (!r1)
                continue;
            const u32 h = e >= eras - late ? 1 : 0;
            ++x.era_n[h];
            for (u32 c = 0; c < kCallCount; ++c)
                x.era_calls[h][c] += r1->calls[c] - (r0 ? r0->calls[c] : 0);
            const u64 ps = r1->program_samples - (r0 ? r0->program_samples : 0);
            x.era_days[h] += static_cast<double>(ps) * q.sample_every / q.day;
        }
    }
}

void write_watch_files(const Observer& o, const char* dir, u64 seed, u64 window, const char* const* calls) {
    char path[512];
    snprintf(path, sizeof path, "%s/seed-%llu-respawns.csv", dir, static_cast<ull>(seed));
    FILE* f = fopen(path, "w");
    if (f) {
        // Each respawn as a fact (13.1): a record's incarnation is the count
        // of its place's respawns up to its tick.
        fprintf(f, "tick,place,incarnation,x,y,moved\n");
        for (usize k = 0; k < o.respawn_log.size(); ++k) {
            const RespawnRec& r = o.respawn_log[k];
            fprintf(f, "%llu,%u,%u,%.6f,%.6f,%u\n", static_cast<ull>(r.tick), r.place, r.incarnation, r.x, r.y,
                    r.moved ? 1u : 0u);
        }
        fclose(f);
    } else {
        fprintf(stderr, "nova2: can't write %s\n", path);
    }
    snprintf(path, sizeof path, "%s/seed-%llu-cascades.csv", dir, static_cast<ull>(seed));
    f = fopen(path, "w");
    if (f) {
        // Each cascade: the tick its first place went off and the last one
        // joined it, that place, the places it reached, what began it, the
        // sag of the source nearest that place by power links, in mV, and
        // the ground of that place's window over its first tick.
        static const char* const kCause[kCascadeCauses] = {"load", "strike", "swell"};
        Array<u8> roots;
        root_grounds(o.ground_windows, o.cascade_watch.cascades, window, &roots);
        fprintf(f, "start,last,root,size,cause,sag,root_ground\n");
        for (usize k = 0; k < o.cascade_watch.cascades.size(); ++k) {
            const CascadeRec& c = o.cascade_watch.cascades[k];
            fprintf(f, "%llu,%llu,%u,%u,%s,%d,%s\n", static_cast<ull>(c.start), static_cast<ull>(c.last), c.root,
                    c.size, kCause[static_cast<u32>(c.cause) % kCascadeCauses], c.sag,
                    kRootName[roots[k] < kRootGrounds ? roots[k] : kRootNone]);
        }
        fclose(f);
    } else {
        fprintf(stderr, "nova2: can't write %s\n", path);
    }
    snprintf(path, sizeof path, "%s/seed-%llu-calls.csv", dir, static_cast<ull>(seed));
    f = fopen(path, "w");
    if (f) {
        // The calls so far as each turnover window ended, and the programs
        // counted at the samples so far, for calls a program era by era.
        fprintf(f, "tick,program_samples");
        for (u32 c = 0; c < kCallCount; ++c)
            fprintf(f, ",calls_%s", calls[c]);
        fprintf(f, "\n");
        for (usize k = 0; k < o.calls_rows.size(); ++k) {
            const CallsRow& r = o.calls_rows[k];
            fprintf(f, "%llu,%llu", static_cast<ull>(r.tick), static_cast<ull>(r.program_samples));
            for (u32 c = 0; c < kCallCount; ++c)
                fprintf(f, ",%llu", static_cast<ull>(r.calls[c]));
            fprintf(f, "\n");
        }
        fclose(f);
    } else {
        fprintf(stderr, "nova2: can't write %s\n", path);
    }
}

void print_watch(const SeedWatch* const* x, const u64* seeds, u32 n, const WatchParams& q,
                 const char* const* calls) {
    SeedWatch all;
    for (u32 k = 0; k < n; ++k)
        all.add(*x[k]);
    const double day = q.day ? q.day : 2000;

    // What gate 5's mechanisms did.
    printf("\ngate 5's mechanisms: failures of every kind, devices replaced and visits (7.4 to 7.8); respawns, "
           "those that found a\nnew point, the programs they tore down, and the place-windows left out of the "
           "classed mark for them; links cut\nby wear or chance and mended, and cut by outages (11.6); straits "
           "parted and joined (gap 13); strikes (12.2); bits\nrowhammer flipped (9.8)\n");
    printf("  %-10s %9s %9s %8s %8s %6s %6s %8s %7s %7s %7s %6s %6s %8s %8s\n", "", "failures", "replaced",
           "visits", "respawns", "moved", "torn", "left out", "cut", "mended", "outage", "parted", "joined",
           "strikes", "flips");
    for (u32 k = 0; k <= n; ++k) {
        const SeedWatch& s = k < n ? *x[k] : all;
        char label[32];
        if (k < n)
            snprintf(label, sizeof label, "seed %llu", static_cast<ull>(seeds[k]));
        else
            snprintf(label, sizeof label, "all seeds");
        u64 failures = 0, replaced = 0;
        for (u32 d = 0; d < kHeatKinds; ++d) {
            failures += s.failures[d];
            replaced += s.replaced[d];
        }
        printf("  %-10s %9llu %9llu %8llu %8llu %6llu %6llu %8llu %7llu %7llu %7llu %6llu %6llu %8llu %8llu\n",
               label, static_cast<ull>(failures), static_cast<ull>(replaced), static_cast<ull>(s.visits),
               static_cast<ull>(s.respawns), static_cast<ull>(s.respawns_moved), static_cast<ull>(s.torn),
               static_cast<ull>(s.left_out), static_cast<ull>(s.link_cuts), static_cast<ull>(s.link_mends),
               static_cast<ull>(s.patch_cuts), static_cast<ull>(s.partings), static_cast<ull>(s.joinings),
               static_cast<ull>(s.strikes), static_cast<ull>(s.hammer));
    }
    printf("  by kind over all seeds: failures, of the devices there were at the ends; device-ticks failed and "
           "waiting; replaced\n");
    for (u32 d = 0; d < kHeatKinds; ++d)
        printf("    %-8s %9llu of %9llu %14llu %9llu\n", kHeatKindNames[d], static_cast<ull>(all.failures[d]),
               static_cast<ull>(all.devices[d]), static_cast<ull>(all.failed_ticks[d]),
               static_cast<ull>(all.replaced[d]));
    printf("    of the disks' failures, by a worn boot block (its first block past its write endurance, 7.4, 10.5): "
           "%llu; by\n    the disk's own wear (time, heat, overvoltage, strikes): %llu. by a worn boot block, by seed:",
           static_cast<ull>(all.boot_failures), static_cast<ull>(all.failures[kHeatDisk] - all.boot_failures));
    for (u32 k = 0; k < n; ++k)
        printf("%s %llu", k ? "," : "", static_cast<ull>(x[k]->boot_failures));
    printf("\n");
    printf("  patches formed (12.3):");
    for (u32 k = 0; k < kPatchTypes; ++k)
        printf("%s %s %llu", k ? "," : "", kPatchTypeNames[k], static_cast<ull>(all.patches[k]));
    printf("; rowhammer's activations past its threshold %llu; flaky parts at the ends: %llu of %llu cores "
           "and memory devices\n",
           static_cast<ull>(all.hammered), static_cast<ull>(all.flaky_parts), static_cast<ull>(all.parts));
    printf("  scars (7.8): place-ticks with a device failed and waiting for its visit, %.3f%% of all; the most "
           "scarred place's share\n  of its recorded windows' ticks %.1f%% (place %u of its seed)\n",
           all.place_ticks ? 100.0 * static_cast<double>(all.scarred) / static_cast<double>(all.place_ticks) : 0.0,
           100 * all.scar_most, all.scar_place);

    // Cascades (12.2).
    printf("\ncascades (12.2): a place goes off in a tick when devices there cut out or trip. it passes it on to "
           "a power-link\nneighbour that goes off within %u ticks of its voltage starting toward that edge (rising "
           "past its least maximum\nbefore a trip, falling before a cut-out), if that move started within %u ticks "
           "of the first going off; places\nthat go off in the same tick pass nothing. a cascade reaches 2 places "
           "or more, and is named by what began it: a\nstrike or a swell at its first place within %u ticks before "
           "it went off, or load. stiffness: the sag at rated\nload of the source nearest its first place by power "
           "links, against the sources' at the end\n",
           kCascadeTicks, kCascadeTicks, kCascadeTicks);
    printf("  %-10s %9s %10s %7s %7s %7s %7s %7s %7s %7s %8s %10s %10s %10s\n", "", "cascades", "per 1,000",
           "strike", "swell", "load", "size 2", "3-4", "5-9", "10+", "largest", "sag first", "sources", "dim first");
    for (u32 k = 0; k <= n; ++k) {
        const SeedWatch& s = k < n ? *x[k] : all;
        char label[32];
        if (k < n)
            snprintf(label, sizeof label, "seed %llu", static_cast<ull>(seeds[k]));
        else
            snprintf(label, sizeof label, "all seeds");
        const double days = static_cast<double>(s.place_ticks) / day;
        printf("  %-10s %9llu %10.3f %7llu %7llu %7llu %7llu %7llu %7llu %7llu %8llu %7.0f mV %7.0f mV %10llu\n",
               label,
               static_cast<ull>(s.cascades), ratio(1000.0 * static_cast<double>(s.cascades), days),
               static_cast<ull>(s.by_cause[static_cast<u32>(CascadeCause::strike)]),
               static_cast<ull>(s.by_cause[static_cast<u32>(CascadeCause::swell)]),
               static_cast<ull>(s.by_cause[static_cast<u32>(CascadeCause::load)]), static_cast<ull>(s.sizes[0]),
               static_cast<ull>(s.sizes[1]), static_cast<ull>(s.sizes[2]), static_cast<ull>(s.sizes[3]),
               static_cast<ull>(s.largest), ratio(s.root_sag, static_cast<double>(s.cascades)),
               ratio(s.source_sag, static_cast<double>(s.sources)),
               static_cast<ull>(s.by_root[static_cast<u32>(Ground::dim)] + s.by_root[kRootBoth]));
    }
    printf("  (per 1,000 place-days; sags are means; dim first: those whose first place's window was dim, as "
           "below)\n");
    // By the ground of the first place's window (step 11's second
    // question): the soft places' flicker apart from the rest.
    printf("  by the ground of the first place's window over the tick it began, as gate 4 names a window: dim if "
           "over a quarter\n  of its core time was lost to cut-outs or its reset region's memory was off over a "
           "quarter of it, hot if over a\n  quarter was lost to heat trips (hot and dim: both at once); over all "
           "seeds, per 1,000 place-days of windows so\n  (not recorded: of the place-days outside the recorded "
           "windows)\n");
    printf("  %-12s %9s %10s %7s %7s %7s %7s %7s %7s %7s %14s\n", "", "cascades", "per 1,000", "strike", "swell",
           "load", "size 2", "3-4", "5-9", "10+", "place-windows");
    {
        static const char* const kRow[kRootGrounds] = {"fair", "hot", "dim", "hot and dim", "not recorded"};
        u64 recorded = 0;
        for (u32 g = 0; g < kRootNone; ++g)
            recorded += all.root_windows[g];
        const u64 outside = all.place_ticks > recorded * q.window ? all.place_ticks - recorded * q.window : 0;
        for (u32 g = 0; g < kRootGrounds; ++g) {
            const u64 ticks = g < kRootNone ? all.root_windows[g] * q.window : outside;
            printf("  %-12s %9llu %10.3f %7llu %7llu %7llu %7llu %7llu %7llu %7llu", kRow[g],
                   static_cast<ull>(all.by_root[g]),
                   ratio(1000.0 * static_cast<double>(all.by_root[g]), static_cast<double>(ticks) / day),
                   static_cast<ull>(all.root_cause[g][static_cast<u32>(CascadeCause::strike)]),
                   static_cast<ull>(all.root_cause[g][static_cast<u32>(CascadeCause::swell)]),
                   static_cast<ull>(all.root_cause[g][static_cast<u32>(CascadeCause::load)]),
                   static_cast<ull>(all.root_sizes[g][0]), static_cast<ull>(all.root_sizes[g][1]),
                   static_cast<ull>(all.root_sizes[g][2]), static_cast<ull>(all.root_sizes[g][3]));
            if (g < kRootNone)
                printf(" %14llu\n", static_cast<ull>(all.root_windows[g]));
            else
                printf("\n");
        }
        const u64 dim = all.by_root[static_cast<u32>(Ground::dim)] + all.by_root[kRootBoth];
        const u64 dim_windows = all.root_windows[static_cast<u32>(Ground::dim)] + all.root_windows[kRootBoth];
        printf("  so from dim places (dim, or hot and dim): %llu of %llu cascades (%.1f%%), from %.1f%% of the "
               "recorded place-windows\n",
               static_cast<ull>(dim), static_cast<ull>(all.cascades), pct(dim, all.cascades),
               pct(dim_windows, recorded));
    }

    // Worn ground and holds (gap 1).
    static const char* const kAge[kAgeBins] = {"<1M", "1-4M", "4-16M", "16M+"};
    printf("\nworn ground (gap 1): a place-window is worn if wear slowed its cores by a tenth or more at the "
           "place's voltage, on\naverage over it, or a device there failed in it; fresh otherwise. a hold: a "
           "lineage's of its home, or a colony's\nof its place, as stage 4 and settling have it; its age in "
           "ticks. holds standing as the window began, and those\nthat ended in it, by age, per 100 "
           "place-windows; the holders' births there in it per program they had as it began\n");
    printf("  %-6s %10s  standing:", "", "windows");
    for (u32 a = 0; a < kAgeBins; ++a)
        printf(" %7s", kAge[a]);
    printf("   ended:");
    for (u32 a = 0; a < kAgeBins; ++a)
        printf(" %7s", kAge[a]);
    printf(" %8s\n", "births");
    for (u32 g = 0; g < 2; ++g) {
        const double per100 = all.windows[g] ? 100.0 / static_cast<double>(all.windows[g]) : 0.0;
        printf("  %-6s %10llu           ", g ? "worn" : "fresh", static_cast<ull>(all.windows[g]));
        for (u32 a = 0; a < kAgeBins; ++a)
            printf(" %7.2f", static_cast<double>(all.held[g][a]) * per100);
        printf("         ");
        for (u32 a = 0; a < kAgeBins; ++a)
            printf(" %7.2f", static_cast<double>(all.ended[g][a]) * per100);
        printf(" %8.3f\n",
               ratio(static_cast<double>(all.holder_births[g]), static_cast<double>(all.holder_programs[g])));
    }

    // Flaky parts (gap 12).
    printf("\nflaky parts (gap 12): births a parent made at a place with a flaky core or memory device then, "
           "against the rest, and\nthe mutations among them (a genome other than the parent's); the share of "
           "place-ticks so, the place-windows\nbegun so, and the holds standing as those windows began, per "
           "window\n");
    printf("  %-10s %12s %8s %12s %14s %8s\n", "", "births", "mutated", "place-ticks", "place-windows", "holds");
    for (u32 g = 0; g < 2; ++g) {
        const u64 ticks = all.flaky_ticks[0] + all.flaky_ticks[1];
        printf("  %-10s %12llu %7.2f%% %11.2f%% %14llu %8.3f\n", g ? "with one" : "with none",
               static_cast<ull>(all.flaky_births[g]), pct(all.flaky_mutations[g], all.flaky_births[g]),
               pct(all.flaky_ticks[g], ticks), static_cast<ull>(all.flaky_windows[g]),
               ratio(static_cast<double>(all.flaky_held[g]), static_cast<double>(all.flaky_windows[g])));
    }

    // The killer and the watchdog (gap 14).
    static const char* const kSize[kSizeBins] = {"<256", "256-1K", "1-4K", "4-16K", "16K+"};
    printf("\nthe out-of-memory killer and the watchdog (gap 14): the programs they ended by size at birth "
           "(bytes), against all\nbirths; those of their place's leading lineage, and that lineage's share of "
           "the place's programs as its window\nbegan, on average\n");
    printf("  %-10s", "");
    for (u32 s = 0; s < kSizeBins; ++s)
        printf(" %10s", kSize[s]);
    printf(" %10s %8s\n", "leading", "its share");
    {
        u64 births = 0;
        for (u32 s = 0; s < kSizeBins; ++s)
            births += all.births_by_size[s];
        printf("  %-10s", "births");
        for (u32 s = 0; s < kSizeBins; ++s)
            printf(" %9.2f%%", pct(all.births_by_size[s], births));
        printf("\n");
    }
    for (u32 g = 0; g < 2; ++g) {
        u64 ended = 0;
        for (u32 s = 0; s < kSizeBins; ++s)
            ended += all.ended_by[g][s];
        printf("  %-10s", g ? "watchdog" : "killer");
        for (u32 s = 0; s < kSizeBins; ++s)
            printf(" %10llu", static_cast<ull>(all.ended_by[g][s]));
        printf(" %10llu %7.1f%%\n", static_cast<ull>(all.ended_leader[g]),
               100 * ratio(all.leader_share[g], static_cast<double>(ended)));
    }

    // The current (12.6; gap 15).
    const CurrentCounts& d = all.drift;
    printf("\nthe current (12.6; gap 15): %llu blocks drawn; dropped: no link downstream %llu, the place's network "
           "device off %llu,\nthe memory off or failed %llu, the link failed or parted %llu, the next place's "
           "network device off %llu, no room\n%llu; sent %llu (%llu held mostly by a living program), landed "
           "%llu, found the region's memory off %llu\n",
           static_cast<ull>(d.drawn), static_cast<ull>(d.becalmed), static_cast<ull>(d.unlit),
           static_cast<ull>(d.unread), static_cast<ull>(d.down), static_cast<ull>(d.dark),
           static_cast<ull>(d.full), static_cast<ull>(d.sent), static_cast<ull>(d.held),
           static_cast<ull>(d.landed), static_cast<ull>(d.unlanded));
    printf("  what came alive from them: %llu programs, of %llu lineages; by data links from their lineage's "
           "home: 0 %llu, 1 %llu,\n  2 %llu, 3 %llu, 4 to 7 %llu, 8 or more %llu, no way %llu; colonies they "
           "founded (counted apart from spread) %llu\n",
           static_cast<ull>(all.carried), static_cast<ull>(all.carried_lineages),
           static_cast<ull>(all.carried_far[0]), static_cast<ull>(all.carried_far[1]),
           static_cast<ull>(all.carried_far[2]), static_cast<ull>(all.carried_far[3]),
           static_cast<ull>(all.carried_far[4]), static_cast<ull>(all.carried_far[5]),
           static_cast<ull>(all.carried_far[6]), static_cast<ull>(all.current_colonies));

    // Straits joining again (gap 13).
    printf("\nstraits joining again (gap 13): %llu joinings. at each, the genome groups leading a place on each "
           "side as the window\nunder way began: %.2f and %.2f on average, %llu in common, and %llu joinings "
           "with a group leading on both;\nover the era after each (%llu ticks): colonies founded across it, "
           "%llu from its first landmass and %llu from its\nsecond, and %llu first leads on either side\n",
           static_cast<ull>(all.joins), ratio(static_cast<double>(all.join_groups[0]), static_cast<double>(all.joins)),
           ratio(static_cast<double>(all.join_groups[1]), static_cast<double>(all.joins)),
           static_cast<ull>(all.join_common), static_cast<ull>(all.join_both), static_cast<ull>(q.era),
           static_cast<ull>(all.across[0]), static_cast<ull>(all.across[1]), static_cast<ull>(all.join_firsts));

    // Calls by era (1).
    printf("\ncalls by era (1): each kind's calls per 1,000 program-days in the early eras (%u, %.0f "
           "program-days) and the late\n(%u, %.0f program-days), over all seeds\n",
           all.era_n[0], all.era_days[0], all.era_n[1], all.era_days[1]);
    printf("  %-11s %14s %14s %9s\n", "", "early", "late", "late/early");
    for (u32 c = 0; c < kCallCount; ++c) {
        const double e = ratio(1000.0 * static_cast<double>(all.era_calls[0][c]), all.era_days[0]);
        const double l = ratio(1000.0 * static_cast<double>(all.era_calls[1][c]), all.era_days[1]);
        printf("  %-11s %14.2f %14.2f %9.2f\n", calls[c], e, l, ratio(l, e));
    }
}

} // namespace nova2
