#include "nova2/region.h"

#include <intrin.h> // _mm_prefetch

#include <cstring> // memcpy, memmove

#include "nova2/hash.h"

namespace nova2 {

namespace {

u32 gcd(u32 a, u32 b) {
    while (b) {
        const u32 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

i64 hook_run(void* ctx, u32 core, i64 left16) { return static_cast<Region*>(ctx)->run(core, left16); }
void hook_end(void* ctx, u32 core) { static_cast<Region*>(ctx)->end(core); }

} // namespace

bool Region::init(Os* os, Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const RegionSettings& s,
                  const CostTable* p_costs, const CostTable* e_costs, TextBuf* why) {
    if (s.cap_cycles < 1 || s.retry_ticks < 1) {
        if (why)
            why->append("region: the cap and the retry must be at least 1");
        return false;
    }
    if (s.step == 0 || gcd(s.step, mem->region_size()) != 1) {
        if (why)
            why->append("region.step: must share no factor with the region's size, so every byte becomes a start spot");
        return false;
    }
    os_ = os;
    mem_ = mem;
    cores_ = cores;
    core_count_ = core_count;
    arrival_cores_ = static_cast<u32>(s.arrival_share * core_count); // rounded down
    if (arrival_cores_ < 1)
        arrival_cores_ = 1;
    env_ = env;
    s_ = s;
    costs_[0] = p_costs;
    costs_[1] = e_costs;
    runs_.resize(0);
    runs_.resize(core_count);
    // Each core's sequence of spots is offset by an equal share of the region,
    // so the cores don't start together.
    for (u32 c = 0; c < core_count; ++c) {
        runs_[c] = IdleRun{};
        runs_[c].spot = static_cast<u32>(static_cast<u64>(c) * mem->region_size() / core_count);
    }
    queue_.clear();
    queue_bytes_.clear();
    landings_.clear();
    landed_hash_.resize(0);
    landed_hash_.resize(core_count);
    landed_len_.resize(0);
    landed_len_.resize(core_count);
    started = faulted = capped = preempted = instructions = cut_runs = 0;
    prompt_runs = boot_runs = dropped = arrival_preempted = arrival_overwritten = 0;
    arrival_alloc_elsewhere = arrival_alloc_nowhere = arrival_alloc_refused = 0;
    arrival_free_failed = arrival_protect_failed = 0;
    arrival_spawn_not_held = arrival_spawn_data = arrival_spawn_no_record = 0;
    arrival_no_failure = arrival_faulted = arrival_capped = arrival_cut = 0;
    landed_runs = landed_made = 0;
    carried_runs = carried_made = drift_blocks = drift_bytes = 0;
    arrival_joined = arrival_dropped = arrival_cleared = 0;
    static_cycles16 = arrival_cycles16 = static_sent = 0;
    landed = 0;
    return true;
}

IdleHooks Region::hooks() {
    IdleHooks h;
    h.run = hook_run;
    h.end = hook_end;
    h.ctx = this;
    return h;
}

void Region::start(u32 core) {
    IdleRun& r = runs_[core];
    Core& c = cores_[core];
    r.phase = RunPhase::running;
    r.used16 = 0;
    r.debt16 = 0;
    r.started_at = r.spot;
    c.idle_run = true;
    c.thread = 0;
    c.run_cause = BirthCause::static_bytes;
    c.run_parent = 0;
    c.disk_wait = false;
    c.disk_debt16 = 0;
    c.net_until = 0;
    c.run_made = c.run_sent = false;
    c.run_failed = c.run_miss = 0;
    c.x = ExecContext{};
    c.x.holder = core + 1;
    c.x.clock = &c.clock;
    c.x.acts = mem_->activations();
    c.x.act_total = mem_->activation_total();
    set_level(c.clock, 10); // an idle run runs at stock
    // The registers point at the start spot, as a real core starts with set values.
    const u32 at = mem_->region_base() + r.spot;
    if (takes_head()) {
        // A prompt run (10.4): the arrival at the queue's head lands at the
        // spot, cut short at the region's end, and runs from its first byte.
        const Arrival a = queue_[0];
        const u32 room = mem_->region_size() - r.spot;
        const u32 n = a.len < room ? a.len : room;
        memcpy(mem_->bytes() + at, queue_bytes_.data() + a.at, n);
        mem_->note_region_change(); // it wakes the halted cores, as any write to the region does (10.2)
        note_landing(r.spot, n, a.cause == BirthCause::arrival ? a.sender : 0);
        c.run_cause = a.cause;
        c.run_parent = a.sender;
        if (a.cause == BirthCause::arrival) {
            landed_hash_[core] = hash_bytes(mem_->bytes() + at, n);
            landed_len_[core] = n;
        }
        landed = n;
        ++prompt_runs;
        boot_runs += a.boot ? 1 : 0;
        arrival_runs += a.cause == BirthCause::arrival ? 1 : 0;
        pop_arrival();
    } else if (landings_.size()) {
        // A static run in an arrival's landed bytes, all as they landed,
        // carries on its sender's lineage, as the arrival's own run did (13.2).
        const u64 by = landed_sender(r.spot);
        if (by) {
            c.run_parent = by;
            ++(carried_by_current(by) ? carried_runs : landed_runs); // the current's are counted apart (12.6)
        }
    }
    c.x.r.a = c.x.r.b = c.x.r.c = c.x.r.d = c.x.r.ip = at;
    r.spot = (r.spot + s_.step) % mem_->region_size();
    if (track_)
        path_acc_[core] = kGolden;
    ++started;
}

void Region::stop(u32 core, RunPhase then, StopWhy why) {
    IdleRun& r = runs_[core];
    Core& c = cores_[core];
    if (track_ && r.phase == RunPhase::running)
        end_path(core);
    if (c.disk_wait)
        os_->cancel_disk(core);
    os_->cancel_net(core);
    if (r.phase == RunPhase::running && c.run_cause == BirthCause::static_bytes) {
        static_cycles16 += r.used16;
        static_sent += c.run_sent ? 1 : 0;
    } else if (r.phase == RunPhase::running && c.run_cause == BirthCause::arrival) {
        arrival_cycles16 += r.used16;
    }
    if (r.phase == RunPhase::running && c.run_cause == BirthCause::arrival) {
        if (c.run_made)
            ++arrival_made;
        else if (c.run_sent)
            ++travellers; // it only sent itself on (10.4)
        else {
            ++arrival_neither;
            const u8* landed_at = mem_->bytes() + mem_->region_base() + r.started_at;
            if (why != StopWhy::preempt) {
                if (hash_bytes(landed_at, landed_len_[core]) != landed_hash_[core])
                    ++arrival_overwritten;
                count_rest(c, why);
            }
        }
    } else if (r.phase == RunPhase::running && c.run_cause == BirthCause::static_bytes && c.run_parent) {
        (carried_by_current(c.run_parent) ? carried_made : landed_made) += c.run_made ? 1 : 0;
    }
    mem_->free_all(core + 1); // whatever it didn't give at SPAWN (9.4)
    c.idle_run = false;
    r.phase = then;
    if (then == RunPhase::halted) {
        r.halted_at = env_->tick;
        r.seen_writes = mem_->region_writes();
    }
}

// An arrival that kept its core and did neither: its first failed call,
// and how it ended.
void Region::count_rest(const Core& c, StopWhy why) {
    arrival_faulted += why == StopWhy::fault ? 1 : 0;
    arrival_capped += why == StopWhy::cap ? 1 : 0;
    arrival_cut += why == StopWhy::cut ? 1 : 0;
    const u32 call = c.run_failed ? c.run_failed - 1u : ~0u;
    if (call == kAlloc) {
        const AllocMiss m = static_cast<AllocMiss>(c.run_miss);
        arrival_alloc_elsewhere += m == AllocMiss::elsewhere ? 1 : 0;
        arrival_alloc_nowhere += m == AllocMiss::nowhere ? 1 : 0;
        arrival_alloc_refused += m == AllocMiss::refused ? 1 : 0;
    } else if (call == kFree) {
        ++arrival_free_failed;
    } else if (call == kProtect) {
        ++arrival_protect_failed;
    } else if (call == kSpawn) {
        const SpawnMiss m = static_cast<SpawnMiss>(c.run_miss);
        arrival_spawn_not_held += m == SpawnMiss::not_held ? 1 : 0;
        arrival_spawn_data += m == SpawnMiss::data_page ? 1 : 0;
        arrival_spawn_no_record += m == SpawnMiss::no_record ? 1 : 0;
    } else {
        ++arrival_no_failure;
    }
}

void Region::end(u32 core) {
    if (runs_[core].phase != RunPhase::running)
        return;
    ++preempted;
    const Core& c = cores_[core];
    if (c.run_cause == BirthCause::arrival && !c.run_made && !c.run_sent)
        ++arrival_preempted;
    stop(core, RunPhase::none, StopWhy::preempt);
}

void Region::reboot() {
    for (u32 c = 0; c < core_count_; ++c) {
        if (runs_[c].phase == RunPhase::running)
            stop(c, RunPhase::none, StopWhy::other);
        runs_[c].phase = RunPhase::none;
    }
    arrival_cleared += arrivals_waiting();
    queue_.clear();
    queue_bytes_.clear();
}

void Region::lose() {
    for (u32 c = 0; c < core_count_; ++c) {
        if (runs_[c].phase == RunPhase::running)
            stop(c, RunPhase::none, StopWhy::other);
        runs_[c].phase = RunPhase::none;
    }
    landings_.clear(); // what landed there is gone with the device
}

// Arrivals share the cores with static (10.4; 2026-11-17, Damian, after
// build-plan-gate3.md step 8's flood test): while arrival_share's cores hold
// prompt runs of arrivals, a core that comes free runs static instead, and a
// halted one stays halted. A boot block is never held back.
bool Region::takes_head() const {
    if (queue_.empty())
        return false;
    if (queue_[0].cause != BirthCause::arrival)
        return true;
    u32 held = 0;
    for (u32 k = 0; k < core_count_; ++k)
        held += runs_[k].phase == RunPhase::running && cores_[k].run_cause == BirthCause::arrival ? 1 : 0;
    return held < arrival_cores_;
}

u32 Region::arrivals_waiting() const {
    u32 n = 0;
    for (usize i = 0; i < queue_.size(); ++i)
        n += queue_[i].cause == BirthCause::arrival ? 1 : 0;
    return n;
}

bool Region::arrive(const u8* bytes, u32 len, BirthCause cause, u64 sender, bool boot, bool at_head) {
    if (len == 0 || queue_bytes_.size() + len > s_.queue_bytes) {
        ++dropped; // full: the newcomer is dropped, as at every other queue (R3)
        arrival_dropped += cause == BirthCause::arrival ? 1 : 0;
        return false;
    }
    arrival_joined += cause == BirthCause::arrival ? 1 : 0;
    Arrival a{0, len, cause, sender, boot};
    const usize old = queue_bytes_.size();
    queue_bytes_.resize_unfilled(old + len);
    u8* q = queue_bytes_.data();
    if (at_head) {
        memmove(q + len, q, old);
        memcpy(q, bytes, len);
        for (usize i = 0; i < queue_.size(); ++i)
            queue_[i].at += len;
        queue_.insert(0, a);
    } else {
        a.at = static_cast<u32>(old);
        memcpy(q + old, bytes, len);
        queue_.push(a);
    }
    return true;
}

u32 Region::drift(const u8* bytes, u32 len, u32 fraction, u64 holder) {
    const u32 size = mem_->region_size();
    if (!size || !len)
        return 0;
    const u32 spot = static_cast<u32>((static_cast<u64>(fraction) * size) >> 32);
    const u32 n = len < size - spot ? len : size - spot;
    memcpy(mem_->bytes() + mem_->region_base() + spot, bytes, n);
    mem_->note_region_change(); // it wakes the halted cores, as any write to the region does (10.2)
    note_landing(spot, n, holder | kCarried);
    ++drift_blocks;
    drift_bytes += n;
    return n;
}

void Region::note_landing(u32 spot, u32 n, u64 sender) {
    usize k = 0;
    for (usize i = 0; i < landings_.size(); ++i) {
        const Landing& l = landings_[i];
        if (l.at < spot + n && spot < l.at + l.len)
            continue; // landed over: no longer as it landed
        landings_[k++] = l;
    }
    landings_.resize(k);
    if (!sender || n == 0)
        return;
    if (landings_.size() == kLandings) {
        for (usize i = 1; i < landings_.size(); ++i)
            landings_[i - 1] = landings_[i];
        landings_.pop();
    }
    landings_.push(Landing{spot, n, sender, hash_bytes(mem_->bytes() + mem_->region_base() + spot, n)});
}

u64 Region::landed_sender(u32 spot) {
    for (usize i = 0; i < landings_.size(); ++i) {
        const Landing& l = landings_[i];
        if (spot < l.at || spot >= l.at + l.len)
            continue;
        if (hash_bytes(mem_->bytes() + mem_->region_base() + l.at, l.len) == l.hash)
            return l.sender;
        for (usize j = i + 1; j < landings_.size(); ++j) // changed since: it goes
            landings_[j - 1] = landings_[j];
        landings_.pop();
        return 0;
    }
    return 0;
}

// The arrival at the head is done with: its bytes and record go.
void Region::pop_arrival() {
    const u32 len = queue_[0].len;
    u8* q = queue_bytes_.data();
    memmove(q, q + len, queue_bytes_.size() - len);
    queue_bytes_.resize_unfilled(queue_bytes_.size() - len);
    for (usize i = 1; i < queue_.size(); ++i) {
        queue_[i - 1] = queue_[i];
        queue_[i - 1].at -= len;
    }
    queue_.pop();
}

void Region::cut(u32 core) {
    if (runs_[core].phase == RunPhase::running) {
        ++cut_runs;
        stop(core, RunPhase::none, StopWhy::cut);
    }
    runs_[core].phase = RunPhase::none;
}

i64 Region::run(u32 core, i64 left16) {
    IdleRun& r = runs_[core];
    Core& c = cores_[core];
    if (!mem_->region_ok())
        return left16; // the region's device has failed (10.2): the core halts until it's replaced
    if (r.phase == RunPhase::halted) {
        // Halted: only a change to the region, the slow retry, or an arrival
        // waiting for it (one it may take) starts it again (R1, 10.4).
        if (!takes_head() && mem_->region_writes() == r.seen_writes && env_->tick < r.halted_at + s_.retry_ticks)
            return left16; // halted: the whole time is unused
        start(core);
    } else if (r.phase == RunPhase::none) {
        start(core);
    }
    if (c.disk_wait)
        return left16; // its DREAD completes at the barrier: it waits, halted
    if (c.net_until) {
        if (env_->tick < c.net_until)
            return left16; // its SEND or neighbour SENSE: it waits on its core
        c.net_until = 0;
    }
    if (c.disk_debt16) {
        r.debt16 += c.disk_debt16;
        r.used16 += c.disk_debt16;
        c.disk_debt16 = 0;
    }
    if (r.debt16) {
        left16 -= r.debt16;
        r.debt16 = 0;
        if (left16 <= 0) {
            r.debt16 = -left16;
            return 0;
        }
    }
    const CostTable& costs = *costs_[c.kind == DeviceKind::e_core ? 1 : 0];
    const i64 cap16 = static_cast<i64>(s_.cap_cycles) * 16;
    i64 budget = left16; // an idle run runs at stock speed
    const bool track = track_;
    while (budget > 0) {
        if (r.used16 >= cap16) {
            ++capped;
            stop(core, RunPhase::halted, StopWhy::cap);
            return budget;
        }
        // One stretch of instructions, as far as the nearer of the budget and
        // the cap. With paths tracked it hashes the bytes executed (the path,
        // an observer's, not state) and comes back after each CPY.
        const i64 room = cap16 - r.used16;
        const i64 limit = budget < room ? budget : room;
        Stretch run;
        run_steps(*mem_, c.x, costs, limit, track ? &path_acc_[core] : nullptr, run);
        const Step& st = run.last;
        const i64 spent = limit - run.left16;
        instructions += run.instructions;
        budget -= spent;
        r.used16 += spent;
        if (st.kind == StepKind::done) {
            if (run.cpy && st.results) { // the copying ladder: an observer's count, not state
                // A CPY changes neither D nor who holds what, so this reads as it did before it.
                const u32 id = mem_->alloc_at(c.x.r.d);
                ++run_cpy;
                run_cpy_own += id && mem_->record(id).holder == core + 1 ? 1 : 0;
            }
            continue;
        }
        if (st.kind == StepKind::sys) {
            const CallOutcome out = os_->call(core, st.sys_value, run.ip);
            budget -= out.cost16;
            r.used16 += out.cost16;
            if (out.disk || out.wait) { // a DREAD (the barrier), a SEND or a neighbour SENSE: it waits
                if (budget < 0)
                    r.debt16 = -budget;
                return budget > 0 ? budget : 0;
            }
            continue;
        }
        // A fault: an idle run has no handler, so it ends and the core halts
        // (a real triple fault resets a PC).
        ++faulted;
        stop(core, RunPhase::halted, StopWhy::fault);
        return budget > 0 ? budget : 0;
    }
    if (budget < 0)
        r.debt16 = -budget;
    return 0;
}

} // namespace nova2

namespace nova2 {

void Region::track_paths(bool on, u32 table) {
    track_ = on;
    path_acc_.resize(0);
    path_acc_.resize(core_count_);
    u32 cap = 1024;
    while (cap < table)
        cap *= 2;
    path_set_.resize(0);
    path_set_.resize(on ? cap : 0);
    path_used_ = 0;
    path_waiting_ = 0;
}

// A run's path is done: it waits to be looked up (region.h), and the line of
// the table it will look at first is asked for now.
void Region::end_path(u32 core) {
    ++paths_ended;
    if (path_waiting_ == kPathsWaiting)
        flush_paths();
    const u64 h = path_acc_[core] | 1; // 0 marks an empty slot
    path_wait_[path_waiting_++] = h;
    const usize mask = path_set_.size() - 1;
    _mm_prefetch(reinterpret_cast<const char*>(&path_set_[static_cast<usize>(mix64(h)) & mask]), _MM_HINT_T0);
}

// Each waiting path is counted if the table hasn't seen it, oldest first.
void Region::flush_paths() {
    const usize mask = path_set_.size() - 1;
    for (u32 k = 0; k < path_waiting_; ++k) {
        const u64 h = path_wait_[k];
        usize i = static_cast<usize>(mix64(h)) & mask;
        bool seen = false;
        while (path_set_[i] && !seen) {
            seen = path_set_[i] == h;
            i = (i + 1) & mask;
        }
        if (seen)
            continue;
        path_set_[i] = h;
        ++paths_new;
        if (++path_used_ * 4 > path_set_.size() * 3) { // three quarters full: start afresh
            path_set_.fill(0);
            path_used_ = 0;
        }
    }
    path_waiting_ = 0;
}

} // namespace nova2

namespace nova2 {

namespace {

bool region_broken(TextBuf* why, const char* what, u64 n) {
    if (why) {
        why->append(what);
        why->append_char(' ');
        why->append_u64(n);
    }
    return false;
}

} // namespace

bool Region::check(TextBuf* why) const {
    // Port 0's queue: its arrivals end to end in its bytes, within its bound.
    u32 at = 0;
    for (u32 i = 0; i < queue_.size(); ++i) {
        if (queue_[i].at != at || queue_[i].len == 0)
            return region_broken(why, "region check: port 0's queue is out of step at arrival", i);
        at += queue_[i].len;
    }
    if (at != queue_bytes_.size() || at > s_.queue_bytes)
        return region_broken(why, "region check: port 0's queue holds the wrong bytes:", queue_bytes_.size());
    // Each core's run: running exactly when the core says it runs one.
    for (u32 c = 0; c < core_count_; ++c) {
        const IdleRun& r = runs_[c];
        const Core& k = cores_[c];
        if ((r.phase == RunPhase::running) != k.idle_run)
            return region_broken(why, "region check: a core and its idle run disagree: core", c);
        if (r.spot >= mem_->region_size() || r.started_at >= mem_->region_size())
            return region_broken(why, "region check: a start spot is outside the region: core", c);
        if (k.idle_run && k.x.holder != c + 1)
            return region_broken(why, "region check: an idle run holds as another: core", c);
    }
    // The arrivals' landed bytes: in the region, from a sender, within the
    // bound, and none overlapping another.
    if (landings_.size() > kLandings)
        return region_broken(why, "region check: too many landings listed:", landings_.size());
    for (u32 i = 0; i < landings_.size(); ++i) {
        const Landing& l = landings_[i];
        if (!l.sender || l.len == 0 || l.at >= mem_->region_size() || l.len > mem_->region_size() - l.at)
            return region_broken(why, "region check: a landing is empty or outside the region:", i);
        for (u32 j = i + 1; j < landings_.size(); ++j)
            if (landings_[j].at < l.at + l.len && l.at < landings_[j].at + landings_[j].len)
                return region_broken(why, "region check: two landings overlap:", j);
    }
    return true;
}

} // namespace nova2
