// Saving and loading the counters kept for reports (archive.h;
// build-plan-viewer.md step 9). They are not part of the world: a snapshot
// leaves them out and the hash never sees them (serialize.cpp). But a test
// carried on from a save reports on the whole of a seed's life, so its
// counts must go on from where they stood. They are saved beside the world,
// with the observers' memory (design.md 13.1), and read back into a world
// that has loaded its snapshot and has its observer watching.
//
// With them go the observers' measures that live in the engine: the reset
// region's table of paths tried, and what is remembered of each idle run
// under way for the counts (its path so far, its arrival's bytes as they
// landed, whether it made a program or sent).
//
// The cycles spent in the network step and on the routes go too, so a report's
// time a tick is of the whole run. Left out: the cycles spent in each of a
// tick's parts (Place::phase_cycles), which only the benchmark asks for.
#include "nova2/archive.h"
#include "nova2/memory.h"
#include "nova2/net.h"
#include "nova2/noise.h"
#include "nova2/os.h"
#include "nova2/place.h"
#include "nova2/region.h"
#include "nova2/sched.h"
#include "nova2/world.h"

namespace nova2 {

void Os::counters(Archive& ar) {
    ar.tag(0x4F534331); // "OSC1"
    ar.u64_(threads_started);
    ar.u64_(idle_sends);
    for (u32 i = 0; i < kCallCount; ++i)
        ar.u64_(run_calls[i]);
    ar.u64_(run_alloc_ok);
    ar.u64_(run_spawn_ok);
    ar.u64_(torn_writes);
    ar.u64_(failed_reads);
    ar.u64_(recv_direct);
    ar.u64_(inbox_joined);
    ar.u64_(inbox_dropped);
    ar.u64_(inbox_taken);
}

// Gate 5's (build-plan-gate5.md step 1): the calls programs' threads make,
// and who sent each message waiting in an inbox, which the facts of RECVs
// name (RecvFact). The inboxes themselves are the world's, loaded before.
void Os::counters5(Archive& ar) {
    ar.tag(0x4F534335); // "OSC5"
    for (u32 i = 0; i < kCallCount; ++i) {
        ar.u64_(calls[i]);
        ar.u64_(calls_ok[i]);
    }
    ar.u64_(sends_other);
    ar.u64_(sends_other_ok);
    if (ar.carry)
        return; // a place built again starts with empty inboxes
    u32 n = static_cast<u32>(inbox_.size());
    ar.u32_(n);
    if (n != inbox_.size()) {
        ar.fail();
        return;
    }
    for (u32 i = 0; i < n; ++i)
        ar.u64_(inbox_[i].sender);
}

void Scheduler::counters(Archive& ar) {
    ar.tag(0x53434331); // "SCC1"
    ar.u64_(instructions);
    ar.u64_(switches);
}

void Region::counters(Archive& ar) {
    ar.tag(0x52474331); // "RGC1"
    u64* const all[] = {&started,
                        &faulted,
                        &capped,
                        &preempted,
                        &instructions,
                        &cut_runs,
                        &prompt_runs,
                        &boot_runs,
                        &dropped,
                        &arrival_runs,
                        &arrival_made,
                        &travellers,
                        &arrival_neither,
                        &arrival_joined,
                        &arrival_dropped,
                        &arrival_cleared,
                        &static_cycles16,
                        &arrival_cycles16,
                        &static_sent,
                        &arrival_preempted,
                        &arrival_overwritten,
                        &arrival_alloc_elsewhere,
                        &arrival_alloc_nowhere,
                        &arrival_alloc_refused,
                        &arrival_free_failed,
                        &arrival_protect_failed,
                        &arrival_spawn_not_held,
                        &arrival_spawn_data,
                        &arrival_spawn_no_record,
                        &arrival_no_failure,
                        &arrival_faulted,
                        &arrival_capped,
                        &arrival_cut,
                        &landed_runs,
                        &landed_made,
                        &paths_new,
                        &paths_ended,
                        &run_cpy,
                        &run_cpy_own};
    for (usize i = 0; i < sizeof all / sizeof all[0]; ++i)
        ar.u64_(*all[i]);
    ar.u32_(landed);
    // The paths tried: the table, as full as it is; each run's path so far;
    // and each arrival's bytes as they landed. Paths that have ended are
    // looked up by a tick's end, so between ticks none waits.
    bool track = track_;
    ar.bool_(track);
    if (track != track_ || path_waiting_ != 0) {
        ar.fail();
        return;
    }
    ar.quads(path_set_);
    ar.u32_(path_used_);
    if (path_used_ > path_set_.size())
        ar.fail();
    if (ar.carry)
        return; // the table of paths tried goes on; the runs under way ended with the place
    ar.quads(path_acc_);
    ar.quads(landed_hash_);
    ar.words(landed_len_);
    // And of each core's idle run, what the counts ask at its end.
    for (u32 c = 0; c < core_count_; ++c) {
        Core& k = cores_[c];
        ar.bool_(k.run_made);
        ar.bool_(k.run_sent);
        ar.u8_(k.run_failed);
        ar.u8_(k.run_miss);
    }
}

void Noise::counters(Archive& ar) {
    ar.tag(0x4E4F4331); // "NOC1" (the weak cells' lost bits go with the place's, "PLC6")
    ar.u64_(result_errors);
    ar.u64_(band_errors);
    ar.u64_(cosmic);
    ar.u64_(memory_edge);
    ar.u64_(cells_faded);
}

void Network::counters(Archive& ar) {
    // "NTC2" adds gate 5's (build-plan-gate5.md step 5), last. A save of a
    // build before them is still read: they go on from 0.
    // "NTC3" adds the messages lost with links that went at a respawn
    // (step 6).
    // "NTC4" adds the messages lost crossing a strait as it parted, and the
    // current's blocks (step 9).
    const u32 kNtc1 = 0x4E544331, kNtc2 = 0x4E544332, kNtc3 = 0x4E544333, kNtc4 = 0x4E544334;
    u32 version = kNtc4;
    ar.u32_(version);
    if (version < kNtc1 || version > kNtc4) {
        ar.fail();
        return;
    }
    NetCounts& k = counts_;
    u64* const all[] = {&k.sent,  &k.sent_bytes, &k.delivered, &k.delivered_bytes, &k.hops,
                        &k.full,  &k.stranded,   &k.lost,      &k.cut,             &k.flips,
                        &k.corrupted, &k.looped, &k.pings,     &k.echoes};
    for (usize i = 0; i < sizeof all / sizeof all[0]; ++i)
        ar.u64_(*all[i]);
    if (version >= kNtc2) {
        ar.u64_(k.waited);
        ar.u64_(k.returned);
        ar.u64_(k.rerouted);
        ar.u64_(k.expired);
    }
    if (version >= kNtc3)
        ar.u64_(k.unlinked);
    if (version >= kNtc4) {
        ar.u64_(k.parted);
        ar.u64_(k.drifted);
        ar.u64_(k.drifted_bytes);
        ar.u64_(k.adrift);
    } else if (ar.loading()) {
        k.parted = k.drifted = k.drifted_bytes = k.adrift = 0;
    }
    ar.quads(dropped_);
    // The routes' counts: a world carried on works its routes out again as
    // they're asked for, so from there these run ahead of an unbroken run's.
    ar.u64_(routes_.changes);
    ar.u64_(routes_.columns);
    ar.u64_(routes_.cycles);
}

void Place::counters(Archive& ar) {
    // "PLC2" adds the ground measure's counters (build-plan-gate4.md step 1),
    // and "PLC3" gate 4's: heat's and the low-voltage interrupt's (steps 5
    // to 7). A save of a build before them is still read: they go on from 0.
    // "PLC4" adds gate 5's (build-plan-gate5.md step 1), at the end, and
    // "PLC5" the core time worked and SPAWN counted two ways
    // (build-plan-gate4.md 11, the reviewer's measures), after those; and
    // "PLC6" wear's (build-plan-gate5.md step 2), last.
    const u32 kPlc1 = 0x504C4331, kPlc2 = 0x504C4332, kPlc3 = 0x504C4333, kPlc4 = 0x504C4334, kPlc5 = 0x504C4335;
    const u32 kPlc6 = 0x504C4336, kPlc7 = 0x504C4337; // "PLC7": the disk's blocks (step 3)
    const u32 kPlc8 = 0x504C4338;                     // "PLC8": visits (step 4)
    const u32 kPlc9 = 0x504C4339;                     // "PLC9": rowhammer (step 8)
    const u32 kPlc10 = 0x504C433A;                    // "PLC:": the current's landings (step 9)
    const u32 kPlc11 = 0x504C433B;                    // "PLC;": the killer's and the watchdog's (step 10)
    u32 version = kPlc11;
    ar.u32_(version);
    if (version < kPlc1 || version > kPlc11) {
        ar.fail();
        return;
    }
    ar.u64_(births);
    ar.u64_(deaths);
    ar.u64_(activations_total);
    ar.u64_(energy.cores);
    ar.u64_(energy.memory);
    ar.u64_(energy.disk);
    ar.u64_(energy.network);
    ar.u64_(peak_draw);
    ar.u64_(mv_total);
    ar.i32_(mv_lowest);
    ar.u64_(below_p);
    ar.u64_(below_e);
    ar.u64_(below_region);
    ar.u64_(network_cutouts);
    for (u32 i = 0; i < 2; ++i) {
        ar.u64_(cutouts[i]);
        ar.u64_(off_ticks[i]);
    }
    ar.u64_(cut_threads);
    ar.u64_(paused_threads);
    ar.u64_(disk_cutouts);
    ar.u64_(memory_cutouts);
    ar.u64_(memory_off_ticks);
    ar.u64_(faded_threads);
    ar.u64_(failed_threads);
    ar.u64_(cold_starts);
    ar.u64_(cold_written);
    if (version >= kPlc2) {
        ar.u64_(heat_ticks[0]);
        ar.u64_(heat_ticks[1]);
        ar.u64_(region_off_ticks);
    }
    if (version >= kPlc3) {
        ar.u64_(energy.cooling);
        for (u32 k = 0; k < kHeatKinds; ++k) {
            ar.u64_(trips[k]);
            ar.u64_(tripped_ticks[k]);
        }
        ar.u64_(heat_threads);
        ar.u64_(cooler_cutouts);
        ar.u64_(region_heat_ticks);
        ar.i64_(temp_total);
        ar.i64_(temp_highest);
        ar.u64_(lv_slept);
        ar.u64_(lv_outlived);
        ar.u64_(os.lv_handlers);
        ar.u64_(sched.lv_raised);
        ar.u64_(sched.lv_taken);
        // The threads watched for lv_slept and lv_outlived, so a run carried
        // on from a save counts what one never stopped does.
        u32 n = ar.carry ? 0 : static_cast<u32>(lv_watch_.size()); // the threads watched ended with the place
        ar.u32_(n);
        if (ar.loading()) {
            if (n > os.thread_capacity()) {
                ar.fail();
                return;
            }
            lv_watch_.resize(0);
            lv_watch_.resize(n);
        }
        for (u32 i = 0; i < n && ar.ok(); ++i) {
            LvWatch& w = lv_watch_[i];
            ar.u32_(w.thread);
            ar.u32_(w.id);
            ar.u64_(w.program);
            ar.u64_(w.tick);
            ar.u64_(w.cutouts);
            ar.bool_(w.asleep);
        }
    }
    os.counters(ar);
    sched.counters(ar);
    region.counters(ar);
    noise.counters(ar);
    if (version >= kPlc4)
        os.counters5(ar);
    if (version >= kPlc5) {
        ar.u64_(worked20);
        ar.u64_(os.spawns_thread);
        ar.u64_(os.spawns_thread_ok);
        ar.u64_(os.spawns_program);
        ar.u64_(os.spawns_program_ok);
    }
    if (version >= kPlc6) {
        for (u32 k = 0; k < kHeatKinds; ++k) {
            ar.u64_(failures[k]);
            ar.u64_(failed_ticks[k]);
        }
        ar.u64_(failure_threads);
        ar.u64_(noise.weak);
    }
    if (version >= kPlc7) {
        ar.u64_(boot_failures);
        ar.u64_(os.block_writes_all);
        ar.u64_(os.worn_writes);
        ar.u64_(os.worn_flips);
        ar.u64_(os.level_swaps);
    }
    if (version >= kPlc8) {
        for (u32 k = 0; k < kHeatKinds; ++k) {
            ar.u64_(replaced[k]);
            ar.u64_(renewed[k]);
        }
        ar.u64_(region_swaps);
    }
    if (version >= kPlc9) {
        ar.u64_(noise.hammered);
        ar.u64_(noise.hammer);
    } else if (ar.loading()) {
        noise.hammered = noise.hammer = 0;
    }
    if (version >= kPlc10) {
        ar.u64_(region.carried_runs);
        ar.u64_(region.carried_made);
        ar.u64_(region.drift_blocks);
        ar.u64_(region.drift_bytes);
    } else if (ar.loading()) {
        region.carried_runs = region.carried_made = region.drift_blocks = region.drift_bytes = 0;
    }
    if (version >= kPlc11) {
        ar.u64_(os.kills);
        ar.u64_(os.killed_bytes);
        ar.u64_(os.killed_threads);
        ar.u64_(os.watchdog_faults);
        ar.u64_(os.watchdog_handled);
    } else if (ar.loading()) {
        os.kills = os.killed_bytes = os.killed_threads = os.watchdog_faults = os.watchdog_handled = 0;
    }
}

void World::save_counters(ByteWriter& out) {
    Archive ar(&out);
    u64 t = tick_;
    ar.u64_(t);
    ar.u64_(net_cycles);
    for (usize i = 0; i < places_.size(); ++i)
        places_[i]->counters(ar);
    net_.counters(ar);
    grid_counters(ar);
    if (links_.on())
        links_.counters(ar);
    if (respawns_on_)
        respawn_counters(ar);
    if (events_.on())
        event_counters(ar);
    if (drifting())
        current_counters(ar);
}

// The current's (build-plan-gate5.md step 9): what became of its blocks.
void World::current_counters(Archive& ar) {
    ar.tag(0x43554331); // "CUC1"
    CurrentCounts& k = drift;
    u64* const all[] = {&k.drawn, &k.becalmed, &k.unlit,      &k.unread, &k.down,   &k.dark,        &k.full,
                        &k.sent,  &k.sent_bytes, &k.held,     &k.landed, &k.landed_bytes, &k.unlanded};
    for (usize i = 0; i < sizeof all / sizeof all[0]; ++i)
        ar.u64_(*all[i]);
}

// The events' (build-plan-gate5.md step 7): the strikes that have landed.
void World::event_counters(Archive& ar) {
    ar.tag(0x45564331); // "EVC1"
    ar.u64_(strikes);
}

// Respawning's (build-plan-gate5.md step 6): the counts, and the tick of
// each place's first.
void World::respawn_counters(Archive& ar) {
    ar.tag(0x52534331); // "RSC1"
    ar.u64_(respawns);
    ar.u64_(respawns_moved);
    ar.u64_(torn_programs);
    ar.quads(first_respawn_);
}

// Gate 4's (build-plan-gate4.md step 8): the solve's counts, and the cycles
// the solve and heat between places took.
void World::grid_counters(Archive& ar) {
    ar.u64_(grid_.solves);
    ar.u64_(grid_.sweeps);
    ar.u64_(grid_.capped);
    ar.u64_(solve_cycles);
    ar.u64_(heat_cycles);
}

bool World::load_counters(ByteReader& in) {
    Archive ar(&in);
    u64 t = 0;
    ar.u64_(t);
    if (t != tick_) // they are another tick's
        return false;
    ar.u64_(net_cycles);
    for (usize i = 0; i < places_.size() && ar.ok(); ++i)
        places_[i]->counters(ar);
    if (ar.ok())
        net_.counters(ar);
    if (ar.ok())
        grid_counters(ar);
    if (ar.ok() && links_.on())
        links_.counters(ar);
    if (ar.ok() && respawns_on_)
        respawn_counters(ar);
    if (ar.ok() && events_.on())
        event_counters(ar);
    if (ar.ok() && drifting())
        current_counters(ar);
    return ar.ok() && in.remaining() == 0;
}

} // namespace nova2
