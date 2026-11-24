#include "nova2/sched.h"

#include <cstring> // memcpy

namespace nova2 {

bool Scheduler::init(Os* os, Memory* mem, Core* cores, u32 core_count, PlaceEnv* env, const SchedSettings& s,
                     const CostTable* p_costs, const CostTable* e_costs, TextBuf* why) {
    if (s.quanta_per_tick < 1 || s.quanta_per_turn < 1 || s.switch_cycles < 1) {
        if (why)
            why->append("sched: a tick needs at least one quantum, a turn at least one, and a switch must cost time");
        return false;
    }
    os_ = os;
    mem_ = mem;
    cores_ = cores;
    core_count_ = core_count;
    env_ = env;
    s_ = s;
    costs_[0] = p_costs;
    costs_[1] = e_costs;
    turn_left_.resize(0);
    turn_left_.resize(core_count);
    last_clock_.resize(0);
    last_clock_.resize(core_count);
    last_clock_.fill(10);
    busy_.resize(0);
    busy_.resize(core_count);
    work_.resize(0);
    work_.resize(core_count);
    quantum_speed_.resize(0);
    quantum_speed_.resize(core_count);
    quantum_speed_.fill(~0u); // no speed yet
    quantum_.resize(0);
    quantum_.resize(core_count);
    rounds_ = 0;
    instructions = switches = 0;
    return true;
}

i64 Scheduler::quantum16(u32 core) const {
    return static_cast<i64>(cores_[core].speed) * 16 / s_.quanta_per_tick;
}

i64 Scheduler::quantum(u32 core) {
    const u32 speed = cores_[core].speed;
    if (quantum_speed_[core] != speed) {
        quantum_speed_[core] = speed;
        quantum_[core] = quantum16(core);
    }
    return quantum_[core];
}

void Scheduler::tick() {
    lv_taken_now.resize(0);
    begin_tick();
    // The place's memory has likely left the host's cache since its last
    // tick: what the threads about to run will read is asked for now.
    for (u32 c = 0; c < core_count_; ++c) {
        if (cores_[c].thread) {
            mem_->ask_for(cores_[c].x.r.ip);
            mem_->ask_for(cores_[c].x.r.sp);
        }
    }
    os_->ask_for_ready();
    for (u32 c = 0; c < core_count_; ++c) {
        if (cores_[c].thread) {
            mem_->ask_for_record(cores_[c].x.r.ip);
            mem_->ask_for_record(cores_[c].x.r.sp);
        }
    }
    for (u32 r = 0; r < s_.quanta_per_tick; ++r)
        run_round();
}

// Timers fire, and threads whose wait is over become ready, in record order.
void Scheduler::begin_tick() {
    os_->note_working();
    for (u32 c = 0; c < core_count_; ++c) {
        busy_[c] = work_[c] = 0;
        cores_[c].x.last_row = ~0u; // each tick's activations count afresh (cpu.h)
    }
    if (!os_->wake_scan())
        return; // nobody sleeps, waits or has a timer
    const u64 now = env_->tick;
    // Only a thread that waits or has a timer has anything below to do, and
    // the OS keeps a byte a thread that says so: the walk reads those, eight
    // at a time. Most of a late world's threads do wait, so it keeps the
    // first tick each has anything due as well (Os::due), and a thread's
    // record is read only at that tick or after: what's below would find
    // nothing to do for it before.
    const u8* const attention = os_->attention();
    const u64* const due = os_->due();
    for (u32 t = 0, top = os_->thread_top(); t < top; ++t) {
        if ((t & 7) == 0 && t + 8 <= top) {
            u64 eight;
            memcpy(&eight, attention + t, 8);
            if (!eight) {
                t += 7;
                continue;
            }
        }
        if (!attention[t] || due[t] > now)
            continue;
        Thread& th = os_->thread(t);
        if (th.state == ThreadState::free)
            continue;
        if (th.handler[kOnTimer] && th.timer_next <= now) {
            th.pending |= static_cast<u8>(1u << kOnTimer);
            th.timer_next = now + th.timer_period;
            os_->thread_changed(t); // its next firing is when it is due again
        }
        if ((th.state == ThreadState::sleeping || th.state == ThreadState::waiting_recv ||
             th.state == ThreadState::waiting_ping) &&
            th.wake_tick <= now)
            os_->wake(t);
        else if (th.state == ThreadState::waiting_interrupt && (th.pending & kPendingKinds))
            os_->wake(t);
    }
}

void Scheduler::run_round() {
    const u32 first = static_cast<u32>(rounds_ % core_count_);
    ++rounds_;
    u32 core = first;
    for (u32 k = 0; k < core_count_; ++k) {
        run_quantum(core);
        if (++core == core_count_)
            core = 0;
    }
}

bool Scheduler::switch_in(u32 core, i64* left16) {
    Core& c = cores_[core];
    if (os_->none_queued())
        return false; // no thread is ready (os.h)
    const u32 t = os_->pick(c.kind);
    if (t == ~0u)
        return false;
    if (c.idle_run) { // a ready thread takes the core from an idle run (10.2)
        if (idle.end)
            idle.end(idle.ctx, core);
        c.idle_run = false;
    }
    const Thread& th = os_->thread(t);
    i64 cost = static_cast<i64>(s_.switch_cycles) * 16;
    if (last_clock_[core] != th.clock)
        cost += static_cast<i64>(s_.clock_switch_cycles) * 16;
    *left16 -= cost;
    os_->put_on(t, core);
    os_->ask_for_next();
    turn_left_[core] = s_.quanta_per_turn;
    last_clock_[core] = th.clock;
    ++switches;
    return true;
}

// Delivers one pending interrupt, if the thread isn't already in a handler
// (A.3). False if the thread ended (its stack couldn't take the frame).
bool Scheduler::deliver(u32 core, i64* budget16) {
    Core& c = cores_[core];
    Thread& th = os_->thread(c.thread - 1);
    if (!(th.pending & kPendingKinds) || th.in_handler)
        return true;
    static const u8 kOrder[3] = {kOnLowVoltage, kOnData, kOnTimer};
    for (u32 i = 0; i < 3; ++i) {
        const u8 kind = kOrder[i];
        const u8 bit = static_cast<u8>(1u << kind);
        if (!(th.pending & bit))
            continue;
        th.pending = static_cast<u8>(th.pending & ~bit);
        const u32 h = th.handler[kind];
        if (!h)
            continue; // cleared since it was raised
        const u32 a = kind == kOnData ? th.data_port
                                      : (kind == kOnLowVoltage ? static_cast<u32>(env_->voltage_mv) : 0);
        const OsSettings& os = os_->settings();
        *budget16 -= static_cast<i64>(os.trap_cycles) * os.call_factor16[c.kind == DeviceKind::e_core ? 1 : 0];
        if (!os_->enter_handler(core, h, a)) {
            os_->end_thread(c.thread - 1, DeathCause::fault);
            return false;
        }
        if (kind == kOnLowVoltage) {
            ++lv_taken;
            lv_taken_now.push(c.thread - 1);
        }
        return true;
    }
    return true;
}

// The low-voltage interrupt (9.5; build-plan-gate4.md 2, gap 6), looked at
// as a thread starts a quantum: with the voltage within the margin of its
// core's cut-off, a thread with a handler has it raised, once; the latch
// holds until it starts a quantum with the voltage back above the margin.
// Nothing asleep or waiting is woken: only a thread on a core is asked.
void Scheduler::low_voltage(u32 core, Thread& th) {
    if (s_.low_voltage_margin <= 0 || core >= env_->devices.size())
        return; // off, or a core with no threshold listed (a scheduler built by hand)
    // The core's cut-off is its worn threshold (7.4), not the nominal one SENSE reads.
    if (env_->voltage_mv >= env_->devices[core].threshold_mv + cores_[core].worn_mv + s_.low_voltage_margin) {
        th.pending = static_cast<u8>(th.pending & ~kLowVoltageTaken);
        return;
    }
    if (!th.handler[kOnLowVoltage] || (th.pending & kLowVoltageTaken))
        return;
    th.pending = static_cast<u8>(th.pending | (1u << kOnLowVoltage) | kLowVoltageTaken);
    ++lv_raised;
}

i64 Scheduler::run_thread(u32 core, i64 budget16) {
    Core& c = cores_[core];
    const u32 t = c.thread - 1;
    const CostTable& costs = *costs_[c.kind == DeviceKind::e_core ? 1 : 0];
    const i64 k = os_->thread(t).clock;
    const i64 dog16 = os_->watchdog16(); // 0: no watchdog
    while (budget16 > 0) {
        Thread& th = os_->thread(t);
        CallOutcome out;
        if (dog16 && th.quiet16 >= dog16) {
            // The watchdog (9.1): its cycles since its last call are up, and
            // it faults before its next instruction.
            out = os_->watchdog(core);
            budget16 -= out.cost16;
            if (!out.stays || c.thread != t + 1)
                break;
            continue;
        }
        // With the watchdog on, a stretch ends where the count reaches it.
        const i64 cap16 = dog16 && dog16 - th.quiet16 < budget16 ? dog16 - th.quiet16 : budget16;
        Stretch run;
        run_steps(*mem_, c.x, costs, cap16, nullptr, run);
        const Step& st = run.last;
        instructions += run.instructions;
        budget16 -= cap16 - run.left16;
        if (dog16)
            th.quiet16 += cap16 - run.left16;
        if (st.kind == StepKind::done)
            continue; // the budget is spent, or the watchdog's count
        if (st.kind == StepKind::sys) {
            th.quiet16 = 0; // any call starts the count again
            // SENSE can read what's left of the turn, in cycles at the thread's clock.
            const i64 turn = budget16 + static_cast<i64>(turn_left_[core] - 1) * quantum(core) * k / 10;
            c.turn_left16 = turn > 0 ? static_cast<u32>(turn < 0xFFFFFFFFll ? turn : 0xFFFFFFFFll) : 0;
            out = os_->call(core, st.sys_value, run.ip);
        } else {
            out = os_->fault(core, st.kind, st.length);
        }
        budget16 -= out.cost16;
        if (!out.stays || c.thread != t + 1)
            break;
    }
    return budget16;
}

void Scheduler::run_quantum(u32 core) {
    Core& c = cores_[core];
    if (!c.working)
        return;
    const i64 full = quantum(core);
    i64 left = full;
    i64 work = 0; // the time it ran something, weighted by the clock (tenths of stock)
    while (left > 0) {
        if (!c.thread) {
            const i64 before = left;
            if (!switch_in(core, &left)) {
                if (idle.run)
                    left = idle.run(idle.ctx, core, left);
                if (left < 0)
                    left = 0;
                work += (before - left) * 10; // an idle run runs at stock
                break; // idle, or halted, for the rest of the quantum
            }
            work += (before - (left > 0 ? left : 0)) * 10; // the switch
            continue;
        }
        const u32 t = c.thread - 1;
        Thread& th = os_->thread(t);
        const i64 k = th.clock;
        const i64 start = left;
        if (th.debt16) { // an earlier overrun comes out of this quantum first
            left -= th.debt16;
            th.debt16 = 0;
            if (left <= 0) {
                th.debt16 = -left;
                left = 0;
                work += start * k;
                break;
            }
        }
        set_level(c.clock, th.clock); // its errors at its own clock (7.5)
        i64 budget = left * k / 10;
        low_voltage(core, th);
        if (!deliver(core, &budget)) {
            left = budget > 0 ? budget * 10 / k : 0;
            work += (start - left) * k;
            continue;
        }
        budget = run_thread(core, budget);
        left = budget * 10 / k;
        if (left < 0) {
            Thread& ran = os_->thread(t);
            if (ran.state != ThreadState::free)
                ran.debt16 += -left; // whoever overran repays it
            left = 0;
        }
        work += (start - left) * k;
    }
    busy_[core] += full - (left > 0 ? left : 0);
    work_[core] += work;
    // The quantum is over: one more of the current thread's turn. At the
    // turn's end it goes to the back of the queue, if anyone is waiting.
    if (c.thread && --turn_left_[core] == 0) {
        if (os_->any_ready(c.kind))
            os_->take_off(core, ThreadState::ready);
        else
            turn_left_[core] = s_.quanta_per_turn;
    }
}

} // namespace nova2
