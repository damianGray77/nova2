# Nova 2 — Gate 2 build plan

> **Status (2026-11-14):** a draft for Damian. Gate 2 asks one question:
> does a full place keep turning over? (R30, design 14.1). This plan says
> what is built, in what order, how each step is checked, and what must be
> fixed before the first gate 2 run. Numbers in brackets, like (7.6), are
> sections of `design.md`, which stays the authority. When the two
> disagree, the design wins and the plan is fixed.
>
> **Approved 2026-11-14:** the five design gaps (2), as proposed, with the
> day at each place (2.1a). They're applied to `design.md` (2, 7.6, 9.4,
> 10.5, 13.1) and logged in `design-history.md`. The numbers in 7, the
> pass mark among them, were agreed on 2026-11-14, after the baseline.
>
> **Gate 2 passed on 2026-11-16,** at its second run, with the saved-state
> rule widened from "fades" to "fails" (11, step 10). Damian agreed the
> readings, and the widened rule is now the design's (9.4).
>
> **Where gate 1 left it:** the origin test passed, with stage 4 in 11 of
> 20 seeds (`build-plan-gate1.md` 11, runs 7 and 8). In 6 of those 11 the
> holding lineage stopped copying and slept, mostly in SLEEP 0 waiting for
> an interrupt that never came. Nothing at gate 1 ends a program that
> doesn't fault, so those places froze. That is the problem gate 2 is for.

## 1. What gate 2 delivers

- **The turnover answer** (14.1): whether crowded places keep turning
  over, measured as deaths per program per day, by cause and by place,
  against a pass mark set before the first run (7).
- **Its two checks** (14.1):
  - whether the crash at a cut-out still lets early life arise and
    establish (7.6): the origin test rerun under gate 2's world, as a
    regression check (`build-plan-gate1.md` 8 promised one at every gate);
  - whether one program taking all of a place's memory freezes it (9.4): a
    test world with that program placed by hand (R7), run under gate 2's
    supply.
- **A baseline from gate 1:** the same measure run on gate 1's world, so the
  answer says what gate 2's mechanisms changed, not only where it ended.
- **The mechanisms the gate table names** (2): a varying supply, memory
  fade, cut-outs and the crash rule (7.6), and the boot block's cold start
  (10.5), with the edge errors that come with a varying supply (7.5).
- **The engine still deterministic** across thread counts and through
  snapshot and resume, with the new state in the world hash.

## 2. Design gaps found while planning

These need Damian's yes before the code follows them. Each is applied to
`design.md` and logged in `design-history.md` once agreed.

1. **What the varying supply is before gate 4.**
   - The problem: gate 2 builds "a varying supply", but sources, the
     voltage solve and the day are gate 4's (2), and places have no links
     until gate 3. The design doesn't say what a place's supply is in
     between.
   - Proposal: **each place gets a source of its own,** 4.2's model
     reduced to one place: a voltage behind an internal resistance. The
     place's own draw sags it, through appendix C's one-tick lag (the
     draw from last tick's voltage and activity, as a conductance), which
     for one source and one place is a single division, not a solve.
     - It varies by 4.3's **swings** (a slow drift of its capacity) and
       **drop-outs** (its capacity falling low or to nothing for a while),
       drawn from the seed before the run. It never reacts to the world
       (principle 3).
     - **Draw comes with it,** from the catalogue (7.1, appendix C): cores'
       idle and working draw, memory's holding draw and draw per access
       (from the per-access counters gate 1 built behind a switch, which
       cost under 1%), the disk's and the network device's. Heat stays at
       gate 4, so temperature is constant and leakage a fixed factor.
     - Why sag, not a voltage simply scheduled: R1 says life raises draw,
       and 4.3 says night and swings "punish crowding, not presence". A
       full, busy place sags more than an empty one, and that is the
       pressure on full places that gate 2's question is about. Without
       sag, crowding wouldn't matter until gate 4.
   - **And the day?** The design's main source of cut-outs is night (7.6:
     "night is a crash for whatever is running on a core"), and night
     lowers capacity, not voltage (4.3). Two options:
     - a. **a day at each place now,** its capacity rising and falling
       over 4.3's day length, each place at its own phase drawn from the
       seed (as if at a random longitude). The sweep across the map comes
       with the map at gate 4. **Claude's lean:** gate 2 would otherwise
       test turnover without the world's main rhythm, and it costs one
       factor on the internal resistance;
     - b. leave the day at gate 4, and rely on swings and drop-outs.
   - Not at gate 2: swells and the heat trips they cause (gate 4, with
     heat), the ramps before drop-outs (12.4, gate 5), and sources coming
     and going (4.2, gate 4).
2. **What "fading past use" means for a waiting thread.**
   - The problem: appendix B ends a thread when "its stack's memory fail[s]
     or fad[es] past use", and 7.6 says waiting threads "survive as long as
     memory holds". Neither says when that is. Gate 1's frozen places were
     full of threads in SLEEP 0 that never wake, so whether fading ends
     them decides much of gate 2's answer.
   - Proposal: **a thread ends when a bit of its saved state fades:** its
     saved registers at its stack's base (9.4), or the 20-byte frame of a
     handler it is in (A.4). The death's cause is "memory". The engine
     knows which cells fade (appendix C visits only those), so this costs
     a lookup per faded cell, not a scan. Real anchor, loosely: Linux ends
     a process whose memory the hardware reports lost (its memory-failure
     handling), though there the report comes from ECC, which Nova 2
     doesn't have.
   - The alternative is to end nothing: the corrupted state is just bytes,
     read when the thread next runs. A thread in SLEEP 0 with no handler
     never runs again, so it would hold its memory until a cold start.
3. **What a cold start ends.**
   - 10.5 says "a deep outage wipes memory and ends every thread", but R16
     says the OS's records are protected and never fade.
   - Proposal: **a cold start reboots the place's OS.** Every program and
     thread ends (cause "cold start", a new cause, so the measure can tell
     these deaths from the rest), every allocation is freed with its bytes
     as fading left them, idle runs start afresh, and then the boot block
     runs (10.5). R16 still holds while the power is on.
   - So even a short dip below the reset region's threshold reboots the
     place, though most of memory still holds. Real anchor: a PC that
     loses power for a moment reboots, though its DRAM keeps most of its
     contents for seconds (the cold-boot attack, Halderman and others,
     2008). What survives the dip is what the booted code and new idle
     runs find.
4. **Port 0's queue and prompt runs come forward from gate 3.**
   - 10.5 runs the boot block "like an arrival (10.4)", at the head of port
     0's queue. Arrivals are gate 3's.
   - Proposal: build port 0's queue and the prompt run (an arrival waiting
     for a core, written at the core's start spot, run from its first byte,
     10.4) at gate 2, with the boot block as its only source. SEND to port 0
     keeps failing until gate 3, as every SEND does.
5. **The crash rule's fallback is built as a switch,** off by default.
   7.6 names "pause instead of crashing" as the fallback if the crash is
   too harsh on early life. A switch (`os.cutout_pause`) lets the origin
   regression try it without a rebuild, as an experiment (2: switches are
   welcome for experiments).

## 3. The gate 2 world

- **Gate 1's world,** with every lever and rule that passed: 48 places a
  seed with no links, the 24 call slots, a 4 KB reset region, the raised
  noise floors, and the design review's conventions.
- **A source per place** (2.1): its voltage, internal resistance, swings and
  drop-outs, and the day (2.1a). Hardware draws power, and the
  place's voltage sags with its draw.
- **Devices have their full voltage range** (4.4, 7.1): a threshold and a
  maximum, speed in proportion to (voltage − threshold), wrong results near
  either edge, a cut-out below the threshold, and a restart above it only
  after the restart gap and the minimum off-time (4.1, 4.4).
- **With the supply steady and stiff,** gate 2's world is gate 1's: every
  device in mid-range at its gate 1 stock speed. That is the regression
  check of step 2.
- **Not built at gate 2:**
  - the map, links, the network and arrivals from other places (gate 3);
  - the voltage solve across places, heat, sources coming and going,
    swells, the low-voltage interrupt and cooling devices (gate 4);
  - wear, failures, visits and respawning, ramps, regional events and
    rowhammer's flips (gate 5). So at gate 2 devices cut out but never
    fail;
  - the lever in reserve, a visit that reboots a place (7.8). It breaks
    "programs are never touched", so it waits for Damian's decision, and
    only if gate 2 shows full places don't turn over;
  - the GPU and NPU (parked).

## 4. The code: what changes

Gate 2 builds on gate 1's engine (`core/`, `app/`, `tests/`). Little comes
from Nova 1 this time: its crash rule was a rule, and Nova 2 rebuilds it
from physics (7.6).

| Area | Files | What changes |
|---|---|---|
| Settings | `keys.inc`, `place.h` | The catalogue's power fields per kind (threshold, maximum, speed slope, draw), the restart gap and off-time, the source and its schedule, fade, edge errors, the boot block's size |
| Supply | new `supply.*` | A place's source, its seeded schedule, the sag from last tick's draw |
| Devices | `place.cpp`, new `power.*` | Each device's state (on, cut out, off-time left), its speed this tick, its draw |
| Scheduler | `sched.cpp` | Speeds that change by tick; a cut-out core's running thread ends and its queue moves to powered cores; the pause switch |
| Errors and fade | `noise.cpp`, `memory.cpp` | Edge errors on the result hook; memory fading by leak rank; the reset region fading |
| OS | `os.cpp` | Death causes (cut-out, memory, cold start); the saved-state rule (2.2); the cold start (2.3); SENSE's voltage, speeds and idle cores live |
| Reset region | `region.cpp` | Port 0's queue and prompt runs (2.4); a cut-out ends an idle run |
| Disk | `os.cpp`, `place.cpp` | I/O waits while the disk is out; a torn write; the boot block |
| Observers | `observe.cpp`, `origin.cpp` | The turnover measure; supply traces; boots |
| Views | `app/ui`, `view_cmd.cpp` | Voltage, device states and deaths by cause |

## 5. The order of work

Each step ends with tests that pass before the next starts. "Done when"
names the check.

**Step 1. The turnover measure, on gate 1's world.** Built first, so the
baseline comes from gate 1's engine before anything changes it.

- For each place and window (a setting, 200,000 ticks proposed): the
  programs alive at the window's start, how many have died by its end and
  by what cause, births in it, and the place's free memory. Deaths per
  program per day, with a day of 2,000 ticks (appendix E) until the day
  exists.
- **Which windows count:** those that start with at least 20 programs
  alive (stage 4's member count). Gate 1's frozen places weren't all full:
  seeds 1, 5, 6 and 8's had 14% to 68% of their memory free. So the
  measure covers every populated place, and reports **crowded** windows
  (13.3's: free memory below a tenth), the question's full places,
  separately.
- **A window turns over** if at least half the programs alive at its start
  have died by its end, and births made up at least half of the dead (so
  it isn't just emptying).
- The origin runner reports, per seed and per OS type: counted and crowded
  windows, how many turned over, deaths per program per day by cause, and
  the longest run of windows without turnover in any place.
- Then the **baseline**: seeds 1 to 20 at gate 1's settings and budget,
  rerunning runs 7 and 8 with the measure on. The engine is
  deterministic, so their stages repeat exactly; about 9 hours, run while
  step 2 is built.
- Done when: a test world with hand-placed programs of known lifetimes
  gives the expected mean lives and turnover counts, and the baseline
  reproduces runs 7 and 8's stages. (Mean lives rather than half-lives:
  programs alive over deaths, Little's law, is exact in a steady state
  whatever shape the lifetimes have, where a half-life assumes one.)

**Step 2. The power fields and draw (4.4, 7.1, appendix C).**

- Each kind's threshold, maximum and speed slope, with the slopes set so
  that stock speeds at gate 1's 1,000 mV are gate 1's (25,000 and 18,000
  cycles a tick).
- Draw: a core's working draw (∝ V² × speed) while it runs, its idle draw
  when halted or when all its threads sleep; memory's holding draw plus
  its draw per access, from the per-access counters (now always on); the
  disk's and the network device's.
- Setup checks: memory's thresholds below every core's (4.4); the restart
  gap and off-time positive; maxima above thresholds.
- Done when: with the supply steady and stiff, the origin runner's
  samples for seed 5 to 1.6 million ticks are identical to run 7's (as the
  snapshot check was), and each device's draw matches appendix C's
  equations.

**Step 3. The supply (2.1, 4.1–4.3).**

- A source per place: voltage, internal resistance, and its schedule of
  swings and drop-outs, and the day (2.1a), drawn from a stream keyed
  by (seed, place, purpose), never from anything the place does.
- Each tick: the place's conductance from last tick's draw and voltage
  (G = P ÷ V²), then its voltage, V = Vs × Gs ÷ (Gs + G), in fixed point.
- Done when: two worlds with the same seed and different programs have
  identical supply schedules, a place's voltage follows its draw as the
  equation says, and a test world's voltage trace through a drop-out
  matches one worked by hand.

**Step 4. Cut-outs and the crash rule (4.4, 7.6, appendix B).**

- Device states each tick (appendix C step 4): cut out below the
  threshold, back on only at threshold + gap after the minimum off-time.
- A core that cuts out ends the thread running on it (cause "cut-out", as
  a fault with no handler would) and any idle run on it. Its queued
  threads move to powered cores (9.5), or wait if none is on. Pinned
  threads wait in their kind's queue, or fall back to any if every core of
  their kind is out (9.5's rule for failed cores, applied to cut-outs).
- Speeds change by tick: a core runs speed × slice cycles in each quantum,
  so the scheduler's time-keeping takes each core's speed this tick.
- The disk out: DREAD and DWRITE wait for it; a write in flight when it
  cuts out is torn, part new and part old; a read in flight fails (7.6).
- SENSE's own voltage, device speeds and idle cores read the live values.
- The pause switch (2.5): with it on, a cut-out freezes the running
  thread in place instead of ending it.
- Done when: test worlds show a running replicator ending when its core
  cuts out while a sleeping one survives; threads moving to a powered E
  core when the P cores go; no device flickering within the off-time; and
  a torn write landing as 7.6 says.

**Step 5. Edge errors and memory fade (4.4, 7.5, appendix C).**

- Edge errors: each device's rate of wrong results rises towards both
  edges of its range, added to the floor (ε = floor + edge(V) + clock),
  through gate 1's countdowns and result hook. Memory's wrong result is a
  bit it fails to hold.
- Fade: below its threshold a memory device loses cells by leak rank.
  Each device has a seeded bijection from rank to cell, a small Feistel
  network over its bits, computed rather than stored, so memory stays
  small and snapshots don't grow. Each cell's retention time comes from
  its rank, spread over orders of magnitude (the catalogue's spread), and
  shortens the further the voltage is below the threshold. Only the cells
  failing this tick are visited, and they read back random. Power coming
  back resets the clock: the cells still held are refreshed.
- The saved-state rule (2.2).
- The reset region fades with its device (10.2).
- Done when: measured edge-error rates match the settings within
  statistical bounds; a dip of known depth and length loses the expected
  share of a device; fade visits only the failing cells; and a sleeping
  thread whose saved state fades ends with the cause "memory".

**Step 6. The cold start and the boot block (2.3, 2.4, 10.4, 10.5).**

- A cold start: power returns after the voltage fell below the threshold of
  the device holding the reset region. Every program and thread ends
  (cause "cold start"), allocations are freed, and the boot block (the
  first block of the disk space, its size a setting) goes to the head of
  port 0's queue.
- Port 0's queue (bounded in bytes, R3) and prompt runs: the arrival waits
  for a halted core or one whose run ends, is written at its start spot
  (cut short at the region's end), and runs from its first byte with the
  registers at the landing spot. A program it makes has the cause "boot",
  and its birth record names the boot block's last writer (13.1), so the
  disk keeps who last wrote its first block.
- The turnover measure marks each window that had a cold start in it. The
  report says how many of the windows that turned over had one, counted
  and crowded, so the answer shows how much of the turnover the cold
  starts made (7's pass mark counts them).
- Done when: in a test world, a program that DWRITEs itself to offset 0
  survives a deep drop-out by booting, giving a birth with the cause
  "boot"; a block nobody wrote boots static; and a second stage DREADs the
  rest of a program bigger than the block (10.5).

**Step 7. Determinism, snapshots and cost.**

- The new state in snapshots and the world hash: each device's state and
  off-time, the supply's schedule position, fade clocks, port 0's queue.
- The fuzz world under the varying supply, with the invariant checker
  after every tick in the checked build.
- The benchmark's gate 2 part: the cost of the supply, draw and fade per
  tick, against gate 1's.
- Done when: the same seed gives the same hash on 1, 2 and 24 host threads,
  through a snapshot taken mid-drop-out, mid-off-time and mid-fade, and
  with observers on or off.

**Step 8. The gate 2 views (13.4).**

- The place inspector shows its voltage, its devices' states (on, cut out,
  off-time left) and speeds; the place tiles shade by voltage.
- Charts: a place's voltage over time, deaths by cause (cut-out, memory,
  cold start), boots, and turnover per window.
- Done when: a headless render matches a stored image pixel for pixel, and
  the views open on a snapshot taken mid-drop-out.

**Step 9. The gate 2 runs.**

- **The 9.4 test world:** one hand-placed program takes all of a place's
  free memory and fills it with sleeping threads. It runs under gate 2's
  supply for the budget's length. Reported: how long it holds, and what
  ends it. A test world, so it counts as evidence about the mechanism, not
  about life (R7).
- **The origin and turnover runs:** seeds 1 to 20 under gate 2's world, at
  gate 1's budget, reporting the origin test's stages (the Allee check)
  and the turnover measure, against the baseline from step 1. The report
  checks 7's pass mark itself, its floor of crowded windows included, as
  gate 1's report checked the origin test's.
- Done when: both run to their budgets without errors, and the report
  reads correctly against a hand check of one place.

## 6. Tests that run throughout

- **Unit tests** for each step, as named in its "done when".
- **The steady-supply equivalence test:** with the supply steady and stiff,
  gate 2's engine gives gate 1's samples. It runs on every engine change,
  so gate 1's result can't drift unnoticed.
- **The supply independence test:** the supply schedule is the same
  whatever the place's programs do (principle 3).
- **Test worlds** with hand-placed programs (R7): a replicator through a
  cut-out, a sleeper through a fade, a booter through a drop-out, and
  9.4's memory-taker.
- **The fuzz world,** gate 1's, now under the varying supply.
- **Determinism checks** on every engine change: the same hash on 1, 2 and
  24 threads, through a snapshot, and with observers on or off.
- **The checked build and the fast build** run the same tests.

## 7. Numbers to fix before the first gate 2 run

All are placeholders, proposed here and agreed with Damian before the first
run, as 14.1 requires a pass mark set in advance. Changing them afterwards
means rerunning.

**Agreed 2026-11-14,** with the baseline in (11). Changed from the first
draft: the pass mark's reason, its floor of crowded windows and its rule
for cold starts; fade's retention just below the threshold; the day's
voltages, worked again from step 2's measured draws.

| Number | Proposed | Why |
|---|---|---|
| Seeds and budget | Seeds 1 to 20, 10¹² instructions each | Gate 1's, so the baseline compares like with like |
| Turnover window | 200,000 ticks | Two of stage 4's spans; about 100 days at 2,000 ticks a day |
| A window counts | At least 20 programs alive at its start | Stage 4's member count |
| A window turns over | Half its starting programs dead by its end, at least half of the dead replaced by births | Gate 1's frozen holds kept the same members for millions of ticks |
| **Pass mark** | At least 9 in 10 counted windows turn over, and 9 in 10 crowded ones, across all seeds, with at least 100 crowded windows from at least 5 seeds. Windows that turn over with a cold start in them count. And the origin regression still reaches stage 4 in at least half the seeds | Gate 1 turned over 6% of counted windows and 9% of crowded ones (11). A window starts with about 150 programs on average, so it turns over almost exactly when its residents' mean life is under about 200,000 to 250,000 ticks: the share counts the full places that turn over, and 9 in 10 is nearly every one. Gate 1's residents live about 1.6 million ticks. The floor stops a world that empties its places passing on a few windows; short of it, the answer is that places stopped filling (the baseline had 342, from 11 seeds). At the proposed drop-outs a cold start lands in about 1 window in 10, so cold starts alone can't reach the mark |
| Source voltage | 1,000 mV | Gate 1's steady supply, every device in mid-range |
| Internal resistance | Set for each place so that at its own full draw, at full capacity, it sags by 50 mV | A busy place sags noticeably by day, without cutting out. Step 2: an average place's full draw is about 35 W (5 cores, half of them E) |
| Day (2.1a) | 2,000 ticks; capacity at midnight a fifth of noon's | Appendix E's day. At a fifth, a place at full draw sags to about 790 mV, below the P cores' threshold but above the E cores'. From step 2's draws: a typical place, at about 40% of its full draw, sits near 905 mV (875 mV on a low swing, inside the P cores' edge-error band); one drawing over two-thirds of its full draw (over half on a low swing) loses its P cores; one with every core halted, about a fifth of its full draw, stays near 950 mV (935 mV) |
| Swings | Capacity drifting by up to a quarter, over about 10 days | 4.3: moderate, and sometimes taking a place out of a device's range |
| Drop-outs | One per place per million ticks on average; 10 to 1,000 ticks long, spread evenly on a log scale (half shorter than 100); half to nothing (a blackout, so a cold start), the rest leaving a share of capacity drawn evenly between nothing and half (step 3, agreed 2026-11-14) | About 15 per place in a seed's budget, 7 or so of them cold starts: rare enough for holds of 100,000 ticks, often enough to matter |
| Thresholds | P 850, E 750, fast memory 700, slow memory 600 mV | Gate 1's values |
| Maxima | P 1,300, E 1,200, memory 1,300 mV | Above the supply with room; only the top edge's errors use them before swells |
| Restart gap, minimum off-time | 50 mV, 20 ticks | 4.1 and 4.4: stops a place's own sag making devices flicker |
| Draw | P core 10 W working, 1 W idle; E core 2.5 W, 0.3 W; memory 0.5 W a device plus 500 µW for each row activation in a tick (set at step 2, 11); disk 1 W; network device 1 W | Real parts' orders of magnitude (7.2); only the ratios matter until gate 4 |
| Edge errors | Doubling every 10 mV closer than 50 mV to either edge | Placeholder; kept under Eigen's threshold in mid-range (7.5). Only the threshold edge is reached at gate 2: a place's voltage never rises above its source's 1,000 mV, and that's mostly at night |
| Fade | Retention times spread from 1 to 1,000 ticks at 0 V, rising evenly on a log scale to 1,000 times longer at the threshold (1,000 to a million ticks) | Real DRAM cells' retention spreads over orders of magnitude; the weak few set the refresh interval. A shallow dip loses almost nothing, a deep or long one its weak cells first. A dip below memory's threshold is the supply's only way to end a sleeper without a cold start |
| Boot block | 512 bytes | One sector; a real master boot record holds 446 bytes of code (10.5) |
| Port 0's queue | 4 KB | Room for the boot block and a second stage |

## 8. Risks for this gate

- **Gate 2 may lose gate 1's pass.** A fresh replicator alone on a core
  dies at its first cut-out (7.6's Allee effect), and drop-outs wipe young
  places. The origin regression shows it. The fallbacks: the pause switch
  (2.5), or a milder supply, agreed before rerunning.
- **Turnover by catastrophe only.** Cold starts wipe whole places, so a
  place might "turn over" only by being emptied every few hundred
  thousand ticks. Decided in 7, before the runs: a window that turns over
  with a cold start in it counts, and the report says how many did. At
  the proposed drop-outs a cold start lands in about 1 window in 10, so
  cold starts alone can't reach the mark.
- **Sleepers may outlast everything but a cold start (Claude's
  prediction).** A place whose threads all sleep draws only idle power,
  so it barely sags: night and partial drop-outs leave it above every
  threshold, its memory doesn't fade, and the saved-state rule (2.2)
  rarely fires. Only blackouts reach it, and at the proposed rate that's
  a cold start about every 2 million ticks per place. So a sleeping hold
  would get through most 200,000-tick windows unbroken, and the pass mark
  would fail on gate 1's kind of frozen place. Step 2's draws bear the
  first part out: with every core halted, a place stays near 950 mV at
  night, and 935 mV on a low swing. If the runs show that, gate
  2's answer is "no, not by the physics alone at these rates", and the
  reserve lever (7.8), another rule, or the outage rates are Damian's
  decision.
- **One source per place isn't gate 4's world.** At gate 4, places share
  sources and sag along links, and the day sweeps the map. The turnover
  test is rerun there.
- **Tuning the supply could choose the answer.** The supply's numbers set
  how often places are wiped. They, and the pass mark, are fixed before
  the first run, and the report states which values a run used.
- **Fixed point and the one-tick lag.** A place's draw and voltage feed
  each other; the lag keeps it explicit, but oscillation is possible. The
  off-time and restart gap are the design's answer (4.1); step 3's tests
  watch for it.

## 9. Size

Claude's estimate, not a measure: about 2,500 lines of engine, observers
and runners, 1,500 lines of tests, and 500 for the views. Nearly all new.

## 10. After gate 2

- **A pass:** gate 3 (does life spread?): the map, links, SEND, RECV and
  PING, arrivals at port 0 from other places (its queue and prompt runs
  are already built), and the network device.
- **A fail:** the reserve lever, a visit that reboots a place (7.8), or
  another rule, with Damian. Then a design review if needed.
- **Possible once turnover exists:** IP wrapping at an allocation's end,
  which the origin review ruled out because nothing would end the junk
  loops it makes (`design-review-origin-2026-11-13.md` 4). Not proposed
  for gate 2 unless Damian wants it.
- Either way, the result, the numbers used and the benchmark go into
  `design-history.md`'s log.

## 11. Results so far

### Step 1: the turnover measure (2026-11-14)

Built on gate 1's engine, before anything else changes it.

- **The measure** (`observe.h`, `PlaceTurnover`): each place keeps the
  window under way and its totals. A death counts against the window's
  starters if the program was born before the window started. Every
  death counts by cause, window or not. Programs alive are summed every
  tick, for the rates. The window that ends with a seed's budget, part
  way through, isn't counted.
- **Settings:** `observe.turnover_window` (200,000; 0 turns it off),
  `observe.turnover_members` (20), `observe.day` (2,000 ticks, until the
  supply's day exists at step 3, which made it `supply.day`) and
  `origin.windows`, a folder for each seed's windows as `seed-N.csv`.
- **The report**, after the stages by OS type, has a line for each seed,
  each OS type and all seeds. Each line gives counted windows, how many
  turned over, crowded windows and how many of those turned over, deaths
  per program per day, the mean life in days, and the longest run without
  turnover (its place, and the tick it ended). Then the shares that turned
  over, and deaths per program per day by cause.
- **Tests** (`test_observe.cpp`; 17 suites green):
  - Cohorts placed by hand, born together and dying together: the six
    windows come out as worked by hand. One turns over, one is frozen
    while crowded, one is emptying (all the starters die but births are
    too few), and one has too few programs to count. The longest frozen
    run is 2 windows. The programs alive sum to 13,800 ticks, so the mean
    life is the lifetimes' mean, 172.5 ticks.
  - A steady place, one program born and one dying each tick, each living
    100 ticks: all 9 counted windows turn over.
  - A death the observer never saw born counts as a death but not as a
    starter's.
  - In a test world, every window agrees with the facts log: its
    starters, their deaths, its births and its deaths.
- **Smoke run:** seed 5 to 400,000 ticks, with the measure on. Its 4,000
  samples match run 7's byte for byte. Static's programs live about 260
  ticks (0.13 days); 98% of them die of faults.
- **The baseline** (`runs/baseline-gate2`): seeds 1 to 20 at run 7's
  settings, 10 at a time, 8.1 hours. No snapshots, since they'd be runs 7
  and 8's. Its results are under "The baseline" below.

### Step 2: the power fields and draw (2026-11-14)

Built on gate 1's engine with the supply still steady and stiff
(`hw.voltage`, now `supply.voltage`, 1,000 mV), so nothing the place does
feels its draw yet.

- **The fields** (`power.h`): for each core kind a threshold, a maximum,
  a speed slope, appendix C's working-draw coefficient k and an idle
  draw; memory's maximum, holding draw and draw per access; the disk's
  range and draw; the network device's draw; the restart gap and the
  minimum off-time. New keys: `hw.<kind>.slope`, `.maximum`, `.draw`,
  `.idle_draw`, `hw.network.draw`, `memory.maximum`, `.hold_draw`,
  `.access_draw`, `disk.threshold`, `.maximum`, `.draw`,
  `power.restart_gap` and `power.off_time`. `hw.<kind>.speed` is gone.
- **Speed:** s = slope × min(max(V − Vth, 0), Vmax − Vth). The slopes
  are in cycles a tick per volt: P 166,667 and E 72,000, which give
  exactly gate 1's 25,000 and 18,000 at 1,000 mV. A slope rather than a
  speed quoted at some voltage, as 7.1 and 7.5 have it, so no reference
  voltage hides in the code.
- **Core draw:** k × V² × s for the share of the tick the core ran
  something, scaled by the clock it ran at, and its idle draw for the
  share it was halted. k is in nanowatts per volt squared per cycle a
  tick: P 400,000 (10 W at 1,000 mV and 25,000 cycles), E 138,889
  (2.5 W). Idle: P 1 W, E 0.3 W.
  - "Ran something" is a thread or an idle run, switches and debts
    included. A core waiting on a DREAD counts as halted. So a core whose
    threads all sleep draws its idle run's time and then idles (4.5).
  - The idle run now hands back the time it leaves unused when it halts.
    The scheduler never used what it returned, so nothing else changes.
  - The shares are worked in 2⁻²⁰ths of the tick, so every product fits
    in 64 bits whatever the settings.
- **Memory draw:** each device's holding draw plus a draw per row
  activation in the tick. The per-access counters are now always on, with
  a place-wide count beside the per-page ones. The scheduler forgets each
  core's last row at the start of a tick, so no tick's count depends on
  an earlier one, and a snapshot between ticks needs nothing more. The
  disk and the network device draw a constant 1 W each.
- **Not state yet:** nothing reads the draw back at step 2. Step 3's sag
  does, so last tick's draw joins the saved state there.
- **Setup checks:** for P, E, memory (its fastest device) and the disk,
  the threshold plus the restart gap must fit under the maximum, or a
  device that cut out could never restart. The supply must be above both
  cores' thresholds. Memory's thresholds must sit below the cores' (as
  before). The restart gap and the off-time must be at least 1.
- **The origin report** gains a power section: each seed's mean draw a
  place, by device, the most any place drew in one tick, and row
  activations a place a tick.
- **Tests** (a new `power` suite, and the scheduler's; 18 suites green):
  - speeds against voltage: gate 1's at 1,000 mV, nothing at or below the
    threshold, linear steps, capped at the maximum;
  - draws against appendix C: 10 W and 2.5 W at 1,000 mV, the same
    equation at 1,100 mV, and 4.4's example, where 10% more speed costs
    about 17% more power (16.7%);
  - a core's tick: halted (its idle draw), all tick at stock, half and
    half, at 1.5 times stock, and a quarter at half stock;
  - in a place, the devices' draws add up; a core spinning all tick
    draws its working draw exactly; memory's draw follows its row
    activations, none for JMP and some for the replicator;
  - the scheduler's busy time: none with nothing to run, all of it for a
    spinning thread, weighted by the clock, and short for a thread that
    sleeps;
  - setup refuses each broken range, and a supply below the P cores'
    threshold.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  with the power fields. Its 16,000 samples match run 7's byte for byte,
  and stage 4 comes at tick 1,489,800 in place 27, as before. Done when
  is met: gate 1's world, and each draw as appendix C has it.
- **What seed 5's places drew** over those ticks: 13.9 W a place on
  average (cores 10.3 W, memory 1.6 W, disk and network device 1 W each),
  and at most 69 W in one tick (place 39). Places averaged 14 row
  activations a tick, and a place filling with A.5's replicator about 5.
- **Memory's draw per access, set from that:** 500 µW for each row
  activation in a tick, in place of the "small amount" in section 7.
  - A core touching a new row with every instruction makes one to three
    thousand a tick, so it adds about 1 W, twice a device's holding
    draw: within "several times" for real DRAM (4.5).
  - Copying programs make only a few a tick, so memory draws little more
    for them than its holding draw. Life raises a place's draw through
    its cores, not its memory.
- **For step 3:** a place with every core halted draws about 4 to 12 W:
  0.3 or 1 W a core, plus memory, the disk and the network device. With
  every core working it draws up to about 84 W (8 P cores). Section 7
  sets the internal resistance from a place's full draw, so step 3 needs
  that number for each place.

### The baseline: gate 1's turnover (2026-11-14)

Seeds 1 to 20 at runs 7 and 8's settings, with the turnover measure on
(`runs/baseline-gate2`). 29,239 s (8.1 hours), 10 seeds at a time, about
1,070 ticks a second a seed.

- **The stages repeat exactly.** Every per-seed figure but the timings
  is runs 7 and 8's: the ticks, the instructions, the five stages' ticks,
  the births and the best hold. So are every best hold and first stage-4
  lineage, and the totals, which are runs 7 and 8's added together:
  births, deaths by cause, idle runs, paths, SENDs, the copying ladder and
  the best stage by OS type. Stage 4 in 11 of 20 seeds. With the test
  world's windows, step 1's done when is met.
- **Counted windows:** 1,164, from 43 places (7 of them strict). 28 of
  those places give 10 windows or more. Most populated windows don't
  count: 24,693 started with programs alive but fewer than 20.
- **Turnover: 73 of the 1,164 counted windows (6%), and 31 of the 342
  crowded ones (9%).** They're in 21 places. Strict places turned over
  once in 220 windows; lax places 72 times in 944. Section 7's mark is 9
  in 10, so gate 1's world is far below it, as the mark intended.
- **Every window that didn't turn over failed on its starters.** No
  window lost half its starters without births replacing them. In 896 of
  the counted windows (77%), fewer than a tenth of the starters died.
- **Residents and churn.** The counted windows started with 177,202
  programs between them, and 20,717 of those died in their window
  (11.7%). The same windows had 22.5 million births and as many deaths,
  so 99.9% of the dead were born in the window they died in. A frozen
  place is a fixed set of residents with a stream of offspring dying young
  around them. The busiest window, seed 3's place 23 from tick 14
  million, crowded, started with 363 programs: 863,034 were born in it
  and 863,056 died, and 311 of the 363 lived through it.
- **Mean life: 0.9 days** (about 1,900 ticks), from 1.06 deaths per
  program per day: faults 1.05, exits 0.01, and none of gate 2's causes
  yet. That's the churn's mean, not the residents'. Strict places'
  programs live 2.6 to 2.7 days on average, lax places' 0.6 to 1.5. So the mean
  life can't show whether residents are being replaced; the starters'
  deaths in each window can.
- **The longest run without turnover:** 64 windows (12.8 million ticks),
  place 13 of seed 6, to tick 14.8 million, nearly its whole run. Places
  in seeds 7, 10 and 15 ran 61 to 63.
- **A wording fix:** the report's "in 43 places" counts the places with
  counted windows, not the ones that turned over. Later runs' reports say
  both ("21 of the 43 places with counted windows turned over at least
  once").

### Step 3: the supply (2026-11-14)

Each place now has its own source, and its voltage follows what it drew.
Nothing cuts out until step 4, so a core below its threshold keeps
running, at its stock speed. The defaults are gate 1's steady, stiff
supply.

- **The source** (`supply.h`): a voltage, an internal resistance, and a
  schedule of capacity, from 0 to 1.
  - The internal resistance is set from the place's full draw: every
    core working at stock clock, every memory device holding, the disk and
    the network device. It's worked at the sagged voltage, so at full draw
    and full capacity a place sits exactly `supply.sag` below its source
    (950 mV at section 7's numbers).
  - Seed 5's places average 24.5 W of full draw there. At 1,000 mV, as
    section 7 worked it, they'd average 37.0 W (the steady regression's
    figure, below); a core at 950 mV is slower and draws less for it.
- **The voltage,** each tick before anything runs (appendix C's steps 1
  to 3):
  - the place's conductance from the last tick's draw and voltage,
    G = P ÷ V²;
  - then V = Vs × c·Gs ÷ (c·Gs + G), with capacity c scaling the
    source's conductance Gs.
  - It's in integers (2⁻²⁰ths, with 128-bit products), rounded to the
    millivolt. A blackout (c = 0) gives 0 V, and a place that drew nothing
    gets its source's full voltage.
- **The schedule** is a function of (seed, place, tick) and nothing else:
  no state, and nothing a place does reaches it. Capacity is the product
  of three parts.
  - The day: sin² of the phase, between night's share (`supply.night`)
    and 1. Each place's phase is drawn once, at setup. In integers by
    Bhaskara's approximation, within 0.0017 of sin².
  - Swings: a knot every `supply.swing_period` ticks, drawn evenly
    between 1 − `supply.swing` and 1, eased from one to the next by
    smoothstep.
  - Drop-outs: time is cut into stretches as long as the longest
    drop-out, rounded up to a power of two. Each stretch has at most one,
    with a chance of the rate times the stretch's length, starting
    anywhere in it. Its length comes from a table spaced evenly on a log
    scale between `supply.dropout.min` and `.max`.
    `supply.blackout_share` of them are blackouts. The rest leave a share
    of capacity drawn evenly between `supply.dropout.keep_min` and
    `.keep_max`. Where two overlap, the deeper wins.
- **Two readings of section 7, agreed by Damian** (and written into its table):
  - Drop-out lengths are spread evenly on a log scale, so half are
    shorter than 100 ticks, the middle of 10 to 1,000. Spread evenly in
    ticks, most would be long (the middle would be 505).
  - "The rest to a depth drawn evenly up to half of capacity" is read as
    leaving between nothing and half of capacity, so a partial drop-out
    takes at least half. The other reading, losing up to half, is
    `supply.dropout.keep_min` 0.5 and `.keep_max` 1.
- **A device below its threshold draws nothing:** it's off (4.4).
  Constant draws would otherwise pull a weak source to nothing: memory,
  the disk and the network device keep drawing their watts as the voltage
  falls, so G = P ÷ V² grows without limit. With them off, a blacked-out
  place draws nothing and comes back at its source's voltage. The network
  device gets a range for this, 600 to 1,300 mV (`hw.network.threshold`,
  `.maximum`), placeholders like the disk's.
- **Until step 4, cores run at their stock speed** whatever the voltage.
  Their draw is worked at the live voltage, so it falls as the voltage
  does: a P core at 945 mV draws 57% of its stock draw. Step 4 makes
  speeds change by tick.
- **State:** the voltage and the last tick's draw are saved with each
  place (tag `PLC2`, snapshot format version 2), since the next tick's
  voltage comes from them. The schedule needs nothing saved. Gate 1's
  snapshots won't load in this build.
- **Keys:** a new Supply group:
  - `supply.voltage` (was `hw.voltage`), `supply.sag`, `supply.day` (was
    `observe.day`), `supply.night`, `supply.swing`, `supply.swing_period`;
  - `supply.dropouts` (a place's per million ticks), `supply.dropout.min`,
    `.max`, `supply.blackout_share`, `supply.dropout.keep_min` and
    `.keep_max`.
  - The defaults are steady and stiff (sag 0, night 1, swing 0, no
    drop-outs), which is gate 1's world. Gate 2's runs set section 7's
    numbers in their configs: sag 50, night 0.2, swing 0.25, drop-outs 1.
    The rest of section 7's are the defaults: a 2,000-tick day, a
    20,000-tick swing period, drop-outs of 10 to 1,000 ticks, half of them
    blackouts, the rest keeping up to half.
- **Setup checks:** the sag below the source, the longest drop-out at
  least the shortest, keep_max at least keep_min, no more than one
  drop-out a stretch, the network device's threshold plus the restart gap
  under its maximum, and the source above both cores' thresholds.
- **The origin report** gains a supply section, after power: each seed's
  mean voltage a place, the lowest any place saw (and which), the shares
  of place-ticks below the P cores', the E cores' and the reset region's
  memory's thresholds, and a place's mean full draw. Its header names the
  run's supply settings.
- **Tests** (a new `supply` suite, 102 checks; 19 suites green):
  - the day within 0.0017 of sin² between a fifth and full; swings
    averaging 0.869 of full at a swing of 0.25 (0.875 expected);
  - drop-outs at a chance of a half: 2,035 in 4,000 stretches, 1,047 of
    them blackouts, 49% shorter than 100 ticks. Capacity, tick by tick,
    agrees with the drop-outs listed.
  - two worlds with the same seed and different programs (one left empty,
    one seeded with replicators and a place spinning on every core) have
    the same capacity in every place at every tick, through nights and
    drop-outs, while their voltages part, since their draws differ;
  - the equation: in a busy test world with a sagging, swinging supply,
    2,400 place-ticks each within half a millivolt of it, worked in
    doubles from the place's own draw, 23 of them blacked out. The full
    draw and the conductance are checked by hand.
  - three drop-outs, each traced through and worked by hand: 1,711 ticks
    each, all to the millivolt. Before each, the busy place sits at 950 mV.
    - A blackout: 0 mV to its end, then 1,000 mV the first tick back,
      since the place drew nothing.
    - Down to 0.6: it settles at 930 mV, then 957 mV the tick after.
    - Down to 0.2: it flips between 821 and 926 mV, then 958 mV the
      tick after.
  - setup refuses each broken setting;
  - the same world hash on 1 and 3 threads, and through a snapshot and
    resume.
- **The one-tick lag oscillates** when a busy place's capacity falls low,
  as section 8 warned. In the drop-out to 0.2, the voltage falls below the
  P cores' threshold, so they draw nothing. The next tick it rises above
  it, so they draw again. Step 4's cut-outs turn that into an off-time
  cycle: off for 20 ticks, back on at 900 mV (4.1).
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the default steady, stiff supply. Its 16,000 samples match run 7's
  byte for byte, and stage 4 comes at tick 1,489,800 in place 27, as
  before. Every place sat at 1,000 mV throughout. (The run ends on a
  sample tick, so its last sample is written twice; run 7's went on.)
  With the schedule and equation tests above, step 3's done when is met.
- **Section 7's supply on seed 5,** to 400,000 ticks (sag 50, night 0.2,
  swing 0.25, drop-outs 1):
  - A place averaged 945.1 mV. 0.54% of place-ticks were below the P
    cores' threshold, none below the E cores', and 0.003% below the reset
    region's memory's. The lowest was 0 mV, a blackout in place 16.
  - Places drew 10.7 W on average: cores 7.1 W, memory 1.6 W, the disk
    and network device 1 W each. That's against a full draw of 24.5 W.
    The most any place drew in one tick was 55 W.
  - No stage yet, with 5,835 births. Run 7's seed 5 had none by then
    either: its stage 1 came at tick 759,309.

### Step 4: cut-outs and the crash rule (2026-11-14)

Every device is now on or cut out, and a dip in the supply does what 7.6
says. The defaults are still gate 1's steady, stiff supply, under which
nothing cuts out.

- **Device states,** each tick after the voltage and before anything runs
  (appendix C's step 4; `step_device`, `power.h`):
  - a device that's on cuts out below its threshold;
  - one that's out comes back once it has been out `power.off_time` (20
    ticks) and the voltage is at its threshold plus `power.restart_gap`
    (50 mV). The off-time counts from the cut-out.
  - Every core, memory device, the disk and the network device has one.
    A device that's out draws nothing: step 3 judged that by the voltage,
    and it now follows the state, so a core sitting out its off-time
    draws nothing though the voltage is back.
  - Memory's states do nothing more yet (fading is step 5), and the
    network device carries nothing until gate 3.
- **A core that cuts out** (`Place::cut_core`):
  - The thread running on it ends, cause "cut-out", as a fault with no
    handler would. If it was its program's last, the program dies and
    frees what it held (9.4).
  - An idle run on it ends, and its disk request with it. When the core
    comes back it starts a fresh one.
  - Threads queued, asleep or waiting keep their saved state and survive.
    The scheduler already uses only working cores (9.5), so queued
    threads go to powered ones. A pinned thread waits for its kind, or
    falls back to any core once every core of its kind is out (9.5's
    rule for failed cores, which was already built).
- **The pause switch** (`os.cutout_pause`, off): the thread running on a
  core that cuts out goes back to the ready queue as it was, instead of
  ending. It may resume on another core.
- **Speeds change by tick** (7.5): each working core's speed is worked
  from this tick's voltage, and is 0 while it's out.
  - The scheduler's quanta are cycles at that speed, so a core at a lower
    voltage runs fewer instructions in a quantum. A debt (the disk's
    latency) is paid at whatever speed the core has when it's paid.
  - SENSE reads the live values: the voltage, each core's speed this
    tick (0 while out), and the idle cores (a core that's out isn't
    idle).
- **The disk out** (7.6):
  - While it's out, the barrier does nothing: DREAD and DWRITE wait,
    across ticks, until it's back.
  - I/O is in flight from the barrier that did it until the next tick's
    device states. If the disk cuts out there, a point is drawn evenly
    along all the bytes in flight, taken in the barrier's order (core,
    then request), from (seed, place, tick) under purpose 7 (`kPurposeTear`).
  - Everything before the point landed. A write from the point on is
    torn: its bytes from there go back to what the disk held. A read from
    the point on fails: its count, in the caller's saved A (or an idle
    run's A), becomes 0. What it brought stays in the caller's buffer.
- **State:** each core's cut-out tick, and the states of memory, the disk
  and the network device (tag `PLC3`). The disk's waiting requests and
  its I/O in flight, with the old bytes under writes in flight (`OS02`).
  Gate 1 asserted that no request waits at a snapshot; now one can, and
  it's saved. The format version stays 2, but step 3's snapshots won't
  load in this build (the tags changed).
- **Keys:** `os.cutout_pause` (OS and calls). The off-time and gap were
  step 2's.
- **The origin report** gains a cut-out section, after supply:
  - for each kind of core, its cut-outs, the mean ticks it was out each
    time, and the share of its ticks it was out;
  - the threads ended or paused, and the idle runs ended;
  - the disk's cut-outs, writes torn and reads failed.
  - Its header names the off-time, the gap, and whether threads end or
    pause. The turnover section's deaths by cause already had cut-outs.
- **Tests** (a new `cutout` suite, 247 checks; 20 suites, 9,379 checks,
  green). Each is a small test world on a supply with a drop-out placed
  where the test wants it:
  - device steps by hand: on at the threshold, out a millivolt below;
    not back at 19 ticks, nor at 20 under the gap; back at 20 at the gap;
    on inside the gap once on.
  - The crash rule: one P core, a replicator and a sleeper, a 30-tick
    blackout. The replicator running on the core ends with cause
    cut-out. The sleeper lives through it, and is asleep again by tick
    1,706, 15 ticks after power came back. With the pause switch, the
    replicator goes back to the queue and runs on.
  - Moving to E: a P core and an E core, and a drop-out to 0.2 for 30
    ticks that takes the P core and leaves the E core. Four threads: two
    counters, one of them pinned to P, and a senser that yields. The one
    on the P core ends. The 3 left all run on the E core, the pinned one
    among them. The senser reads the P core's speed as 0, and no idle
    core. With the pause switch, all 4.
  - Speed by tick: at 950 mV a P core runs 16,666 cycles a tick, and at
    928 mV 13,000. A counter on it ran 1,041 and 812 instructions a tick,
    0.780 of it, as the speeds are.
  - No flicker: step 3's three traced drop-outs now work out each
    device's state by hand too, and match it every tick. No device comes
    back within the off-time. The drop-out to 0.2, lengthened to 60
    ticks, cuts the P cores out 3 times, and the place sits at 943 mV
    while they're out. The traces run with the pause switch on, so their
    spinners live through the cut-outs.
  - A torn write: a 200-byte write in flight when the disk cut out at
    tick 1,230 was torn 146 bytes in. The disk holds the first 146 bytes
    new and the rest old. The writer's next write waited 39 ticks for the
    disk, then landed whole.
  - A failed read: a read in flight when the disk cut out answered 0,
    which its reader found and kept.
  - State: resumed from snapshots taken with a write in flight, and with
    one waiting for the disk, each run matches one never stopped. A busy
    4-place world with 47 core cut-outs has the same hash on 1 and 3
    threads.
  - Setup still refuses a disk that couldn't come back (its threshold plus the
    gap over its maximum), and reads the pause switch.
- **Turns line up with ticks** (a finding). Cut-outs fall on tick
  boundaries, and a thread that never yields has turns of 4 quanta, 16 a
  tick, which end exactly on them. So on a core shared by threads that
  never yield, none is part way through a turn when the tick ends, and a
  cut-out ends none of them.
  - A thread dies only if its turn straddles the boundary (someone on
    its core yielded or waited part way through a turn, shifting the
    turns after), or if it's alone on its core, so its turns run on.
  - That's 7.6's "a crowded place loses few", sharper: often none. The
    move-to-E test needed a senser that yields to get a thread caught.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the default steady, stiff supply. Its 16,000 samples match run 7's
  byte for byte, and stage 4 comes at tick 1,489,800 in place 27, as
  before. Nothing cut out, and the cores ran at their stock speed
  throughout, since every place sat at 1,000 mV. With the tests above,
  step 4's done when is met.
- **Section 7's supply on seed 5,** to 400,000 ticks, at step 3's
  settings:
  - **The P cores cycle at night.** They cut out 101,364 times, out 20.7
    ticks a time on average, which is 4.10% of their ticks. Only 0.64% of
    place-ticks were below their threshold.
    - At the deepest of the night a busy place's P cores pull it under
      850 mV. Out, it rises past 900 mV. Back after 20 ticks, they pull
      it under again within a tick or so.
    - The off-time bounds the cycle, as section 8 expected, but the gap
      doesn't stop it: at a fifth of capacity, the P cores' own draw
      moves the voltage by more than 50 mV. Step 3 predicted the cycle
      from its 0.2 drop-out.
  - Those cut-outs ended only 344 threads, 27 of them their programs'
    last (deaths by cause: 27 cut-outs, 5,715 faults, 122 exits), since
    turns line up with ticks. They ended 79,248 idle runs.
  - The E cores cut out 22 times, 50.1 ticks each: blackouts and the
    deepest drop-outs. The disk cut out 7 times, with nothing in flight.
  - A place averaged 942.5 mV (945.1 at step 3), 0.64% of place-ticks
    below the P cores' threshold (0.54%), none below the E cores'. Cores
    drew 7.7 W on average (7.1 at step 3).
  - 22.4 billion instructions against step 3's 25.9: the cores now slow
    with the voltage, and the P cores are out at night.
  - Stage 1 at tick 94,967, with 5,866 births by the end. Step 3's run
    had no stage by then, nor had run 7's seed 5 (its stage 1 came at
    tick 759,309): a different history, since step 4's rules change
    which threads live.
- **Readings, agreed by Damian (2026-11-14), all as built:**
  - (a) In flight means the last barrier's I/O, until the next tick's
    device states, cut at one point drawn along all of it in order.
  - (b) A failed read answers 0, but leaves what it brought in the
    caller's buffer.
  - (c) 7.6 has DREAD and DWRITE wait "up to the call's own limit", and
    gate 1's calls have none, so they wait for as long as the disk is
    out.
  - (d) The pause switch puts the thread back in the ready queue, where
    it may resume on another core, rather than holding it on its own
    core until that core comes back.
  - (e) A core that comes back starts a fresh idle run. A core that had
    halted forgets it.
  - (f) The crash rule ends the thread part way through its turn at the
    boundary, which lets turns that line up with ticks get through (above).
    The other reading is to end the thread that ran last on the core, as
    if the voltage fell on its last instruction, so every cut-out of a
    busy core ends one. With the night cycle, that would end about one
    thread per P core every 21 ticks of deep night.

### Step 5: edge errors and memory fade (2026-11-14)

Devices now make more errors near the edges of their range, and memory
that's cut out fades. On gate 1's steady 1,000 mV supply neither happens:
every device is more than 50 mV from both its edges, and nothing cuts out.

- **Edge errors** (7.5; `noise.h`): ε = floor + edge(V) + clock, as
  appendix C has it.
  - Within `noise.edge_band` (50 mV) of either edge of a range, the
    total at stock clock doubles every `noise.edge_doubling` (10 mV)
    closer. So at the edge it's 32 times the floor, and edge(V) is the
    floor × (2^((50 − d) ÷ 10) − 1), d mV from the nearer edge. Past the
    edge it stays at the edge's rate. The clock scales the floor, not
    the edge part.
  - A core's extra rate is set each tick at the device states, from the
    voltage, for its kind's range (P 850 to 1,300 mV, E 750 to 1,200).
    Where it changes, the core's countdown is redrawn: a countdown at a
    fixed rate has no memory, so that keeps it exact. A core that's cut
    out gets none.
  - Memory's floor is the cosmic-ray floor. Its edge errors are bits a
    device that's on fails to hold, which read back random, through a
    countdown of bit-ticks of its own (purpose `0x300` + device), like
    the floor's.
- **Fading** (4.4, appendix C):
  - A cell is a bit. Each device has a bijection from leak rank to cell:
    four Feistel rounds over the bit index's halves, keyed from the seed
    (purpose `0x400` + device), walking the cycle past the last cell. It's
    computed, not stored.
  - A cell's retention at 0 V rises evenly on a log scale with its rank,
    from `memory.fade.fastest` (1 tick) to `memory.fade.slowest` (1,000).
    Just below the threshold it holds `memory.fade.at_threshold` (1,000)
    times longer, on a log scale with the voltage between: section 7's
    numbers.
  - So each tick a device is cut out below its threshold adds
    1000^(−V ÷ Vth) ticks of 0 V to its dose. The cells whose retention
    at 0 V the dose has passed have failed: log10(dose) ÷ 3 of them. Only
    the newly failed cells are visited, in rank order, and each reads
    back random. The dose and count are integers (2⁻³² ticks, the Q56
    log2), with the per-millivolt doses worked once at setup.
  - A device that's cut out but back at its threshold (sitting out its
    off-time or gap) holds without fading. When it restarts, its dose
    goes back to 0: the cells still held are refreshed. The leakiest fail
    first again next time, at the same spots.
  - The reset region fades with its device, and a change there counts as
    a write to it, so a halted core can start again (10.2).
- **The saved-state rule** (9.4): a thread ends, cause "memory", when a
  bit of its saved state fades.
  - That's its saved registers (24 bytes at its stack's base) while it's
    off its core, or the frame of a handler it's in (20 bytes), on or
    off.
  - Each thread now records where its handler's frame went
    (`Thread::frame`), since SP moves in the handler.
  - When anything fades in a tick, the place lists the threads' saved
    states sorted by address. Each faded cell looks itself up there, a
    binary search, so the cost is per faded cell. A flip or a bit that
    fails near an edge only corrupts, as 9.4 says.
- **Order:** the edges are set at the device states (appendix C step 4).
  Fading, then memory's edge errors, then the cosmic-ray floor, come
  after the barrier (step 8).
- **State:** each core's edge rate, memory's edge rates and countdowns,
  and each device's dose and cells failed (tag `NOI2`). Each thread's
  frame (`OS03`). The format version stays 2, but step 4's snapshots
  won't load in this build.
- **Keys:** `noise.edge_band` and `noise.edge_doubling` (Noise);
  `memory.fade.fastest`, `.slowest` and `.at_threshold` (Memory). Their
  defaults are section 7's numbers, which change nothing on the steady
  supply. Setup refuses more than 62 doublings in the band, and slowest
  below fastest.
- **The origin report** gains an errors-and-fading section after
  cut-outs:
  - the cores' wrong results, and how many were made near an edge;
  - memory's cut-outs and the share of its device-ticks it was out;
  - the cells faded and the threads it ended;
  - bits that failed near an edge, and the cosmic flips.
- **Tests** (a new `fade` suite, 301 checks; 21 suites, 9,680 checks,
  green):
  - Edge rates through a core's countdown, against the settings, at a
    floor of 10⁻³, each to about 10,000 errors:
    - a P core measured 4.06, 16.22 and 31.99 times its floor at 880, 860
      and 850 mV (4, 16 and 32 set), 1.01 at 900 mV, and 15.92 10 mV from
      its maximum (16);
    - an E core 16.07 at 760 mV (16);
    - 63 floors at the edge at 1.5 times stock clock (32 and 31). None
      for a core that's cut out, or with no band.
  - Memory at its threshold, at a cosmic floor of 10⁻⁶: 10,179 bits
    failed to hold in 2,500 ticks (10,158 expected), 4,889 of them left
    changed. None on a device 100 mV up, or on one that's cut out.
  - A dip of known depth and length (done when 2):
    - at 0 V a device lost 0.3333 of its cells in 10 ticks, 0.6667 in 100
      and all of them in 2,000;
    - at half its threshold, 0.5000 in 1,000 ticks;
    - a millivolt below it, 0.0014 in 1,000 and nothing in 990.
    - Each is within 2 × 10⁻⁵ of log10(dose) ÷ 3.
  - Only the failing cells (done when 3):
    - the ranks map onto all 65,536 cells of a device, each once;
    - 10 ticks at 0 V failed 43,691 of 131,072 cells, 21,715 of them read
      back 1, and no other bit changed;
    - refreshed and faded again, the same cells failed;
    - the reset region faded with its device, counted as written.
  - The saved-state rule (done when 4): a place with 24 sleepers and 24
    that sleep inside a timer handler, and a 2-tick blackout, with cells
    holding 1.9 ticks to a billion at 0 V so 0.26% of memory fades.
    - The test works out which saved states lost a bit from the faded
      ranks themselves. The 27 that did all ended, cause memory, 4 of them
      through their handler's frame alone. The 21 that didn't all lived.
  - A busy world (6 places, a fast day, drop-outs of 20 to 200 ticks)
    made 5 errors near an edge, failed 4 bits near one, and faded 306,760
    cells, ending 137 threads. It's the same on 1 and 3 threads, and
    resumed from a snapshot taken mid-fade at tick 1,739 it comes out the
    same.
  - Setup refuses the broken settings above.
- **Step 4's tests, adjusted:** a blackout now fades memory, so the crash
  test and step 3's three traced drop-outs hold their memory (fastest and
  slowest a billion ticks). They test the cut-out and the voltage, and
  their sleepers' and spinners' code would otherwise fade.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the default steady, stiff supply. Its 16,000 samples match run 7's
  byte for byte, and stage 4 comes at tick 1,489,800 in place 27, as
  before. No result went wrong near an edge, no bit failed near one,
  and nothing cut out or faded. The floor's 13,097,027 wrong results and
  100,665,947 cosmic flips draw from the same streams as before. With
  the tests above, step 5's done when is met.
- **Section 7's supply on seed 5,** to 400,000 ticks:
  - 2,848,300 wrong results, 222,758 of them (7.8%) made near an edge.
    All are at the bottom edges, since the supply never passes 1,000 mV.
    The run doesn't split them by kind.
  - Memory cut out 24 times, 0.004% of its device-ticks (the reset
    region's memory was below its threshold 0.003% of place-ticks).
    3,920,908 cells faded, ending 1 thread. That's about 163,000 cells a
    cut-out, a third or more of a device. A sleeper's 192 bits of saved
    registers on such a device would almost surely lose one, so the
    devices that faded can have held almost no threads off their cores.
    I haven't checked which places they were. Only 3 bits failed near
    memory's edge.
  - The P cores cut out 77,240 times (20.9 ticks out a time, 3.15% of
    their ticks), ending 219 threads. Deaths by cause: 4,839 faults, 107
    exits, 13 cut-outs, 1 memory. 4,964 births, and no stage by 400,000
    ticks (step 4's run had stage 1 at tick 94,967, and run 7's seed 5
    at 759,309): a different history again.
- **Readings, agreed by Damian (2026-11-14), all as built:**
  - (a) Edge errors are relative to the floor: the total doubles every
    10 mV closer than 50 mV, so 32 times the floor at the edge, and a
    kind with no floor has none. The other reading gives the edges a
    base rate of their own.
  - (b) The clock scales the floor but not the edge part (ε = floor ×
    clock + edge).
  - (c) Memory's edge errors are at the cosmic-ray floor × the same
    factor, as bits that fail to hold. Like flips, they only corrupt a
    saved state: only fading ends a thread.
  - (d) A cell is a bit.
  - (e) A device fades only while it's cut out and below its threshold,
    and its restart refreshes it. Between the threshold and its restart
    it holds.
  - (f) The dose adds up a varying voltage: each tick at V counts as
    1000^(−V ÷ Vth) ticks at 0 V.
  - (g) A thread's saved registers count only while it's off its core;
    a handler's frame counts on or off.

### Step 6: the cold start and the boot block (2026-11-14)

A place now cold-starts when power comes back to the memory holding its
reset region, and its boot block runs from port 0's queue. On gate 1's
steady supply nothing cuts out, so neither happens.

- **The cold start** (10.5, 2.3; `Place::cold_start`):
  - It comes at the device states (appendix C step 4), after the
    network's: if the reset region's memory device restarted this tick,
    the place cold-starts. A device restarts only after a cut-out below its
    threshold, so power has come back after the voltage fell below it.
  - Every idle run ends, its holdings freed, and every core starts afresh.
    Port 0's queue, the old OS's, is emptied.
  - Every thread ends, in record order, cause "cold start", and every
    program with its last thread. Their memory is freed with its bytes as
    fading left them. The disk requests waiting are dropped and the ready
    queues emptied. The disk keeps its bytes.
  - The place records a cold-start fact: the tick, the place, and the
    boot block's last writer (0: nobody).
  - Then the boot block, the disk space's first `disk.boot_block` bytes
    (512, or the whole disk if that's smaller), joins port 0's queue at
    its head.
- **The boot block's last writer** (13.1): a DWRITE that lands at the
  barrier with its first byte in the block makes its program the last
  writer. Only threads DWRITE, so it's always a program. If the disk cuts
  out and tears a write so that none of it landed, the write is put back
  whole, and so is the writer before it.
- **Port 0's queue and prompt runs** (10.4, R3; `region.h`):
  - The queue is bounded in bytes (`region.queue`, 4 KB). An arrival that
    doesn't fit is dropped, as at every other queue. A boot block joins at
    the head, anything else at the tail.
  - An arrival never pre-empts anything. It waits for a core starting a
    run: a halted core starts at once when one is waiting, and a running
    one takes it when its run ends.
  - The core writes it at its start spot, cut short at the region's end,
    and counts that as a write to the region, so halted cores wake. It
    runs it from its first byte as a fresh idle run, the registers at the
    landing spot. The spot steps on as usual.
  - The run carries the arrival's cause and sender. A program its SPAWN
    makes has the cause "boot" and the block's last writer for parent; for
    a block nobody wrote, the cause "static" and no parent.
- **The observer** (13.2):
  - A boot's parent is often dead by then. Each place keeps the record of
    a writer that died while it was the block's last, so the boot carries
    its lineage on: the same origin, a generation down, faithful if its
    genome is the writer's.
  - A birth into a lineage with nobody alive brings it back: its
    extinction tick is cleared.
  - A hand-over now needs a thread's birth, since a boot's run copies.
  - Deaths have six causes, births are counted by cause, and cold starts
    are counted.
- **The turnover measure** marks each window with a cold start in it.
  Each place counts its counted windows with one, and of those that
  turned over, all and crowded. The windows CSV gains a `cold` column.
- **The barrier** now checks a thread's request before any bytes move: a
  request whose thread has ended moves none. Before, it moved them and
  only skipped the wake. On the steady supply a thread can't end between
  its call and the barrier, so nothing changes there.
- **State:** port 0's queue (tag `REG2`); the boot block's last writer,
  and each write in flight's flag and writer before it (`OS04`); each
  core's run parent (`PLC4`). The format version stays 2, but step 5's
  snapshots won't load in this build.
- **Keys:** `disk.boot_block` (Memory, 512) and `region.queue` (Reset
  region, 4,096), section 7's numbers. Setup refuses a queue too small for
  the boot block.
- **The origin report** gains a cold-start section after errors and
  fading: the cold starts, those booting a block a program wrote, the boot
  blocks run, and the births from them. The turnover report says how many
  counted windows had a cold start in them, and how many of those that
  turned over did, all and crowded. Deaths by cause gain "cold start".
- **Tests** (a new `boot` suite, 683 checks; 22 suites, 10,351 checks,
  green):
  - Done when (1): a 34-byte booter DWRITEs itself to offset 0 and waits.
    A 20-tick blackout from tick 1,661 cuts out every device, and the place
    cold-starts at tick 1,681, when power comes back.
    - The booter's saved state faded in the blackout, so it ended, cause
      memory, before the cold start. The cold-start fact names it.
    - Its block landed whole (512 bytes) and ran. The booted code copied
      its 34 bytes into an allocation and SPAWNed them: a birth at tick
      1,681, cause boot, the booter its parent and its 34 bytes the genome.
      The copy wrote itself back, the block's last writer next.
    - The observer has it in the booter's lineage, a generation down, the
      lineage alive again. The window the cold start fell in is marked,
      and turned over; no other is marked.
    - The region is filled with PUSHes for this test, which end an idle
      run at once. Without that, idle runs on static made programs from
      tick 661, and one wrote a few bytes into the block before the
      blackout: the last writer then, as the rule says.
  - Done when (2): on a one-core place, a cold start with a block nobody
    wrote queues it with the cause "static" and no parent. The core's next
    run landed all 512 bytes at its spot, the disk's static byte for byte,
    and ran them.
  - Done when (3): a 96-byte program and a 64-byte block. Booted, its
    first stage DREADs the other 32 bytes from offset 64 into the region
    just past where it landed, and runs on into them. The second stage
    copies all 96 and SPAWNs the copy: a birth, cause boot, all 96 bytes,
    which wrote itself back.
  - A cold start on its own: all 40 programs of a busy place ended, cause
    cold start, with every thread record free. Memory was freed back to
    what setup left, every byte as it was. The disk requests, the ready
    queues and every idle run were gone, and the boot block waited at the
    queue's head with its writer.
    - The queue, full, dropped an arrival at its tail and a boot block at
      its head; a second cold start emptied it. Two arrivals waiting went
      to the next two runs to start.
  - The turnover marks on their own: marked, counted, crowded, and not
    carried into the next window.
  - A booter ended by hand before the cold start was its boot's parent
    all the same, and its extinct lineage came back.
  - A busy world (6 places, drop-outs, 70% of them blackouts) made 25
    cold starts, all with a written block, and 25 boots. It's the same on
    1 and 3 threads, and resumed from a snapshot taken with two arrivals
    waiting, it comes out the same.
  - Setup refuses a queue smaller than the block.
- **Older tests, adjusted:** a blackout now cold-starts the place,
  ending every thread. So step 4's crash test uses a drop-out to a fifth
  (792 mV), which cuts out the P cores but not memory, and step 3's traced
  blackout ends at the first tick back, the one it cold-starts on.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the default steady, stiff supply. Its 16,000 samples match run 7's
  (and step 5's) byte for byte, and stage 4 comes at tick 1,489,800 in
  place 27, as before. Nothing cut out, so there was no cold start, and
  port 0's queue stayed empty. With the tests above, step 6's done when
  is met.
- **Section 7's supply on seed 5,** to 400,000 ticks:
  - 6 cold starts (memory cut out 24 times in all). None booted a block a
    program wrote, so no birth has the cause "boot", and nothing died at
    one: those places had no program alive by then. Programs there live
    about a tenth of a day, and most die by faults.
  - Memory's cut-outs and fading came out as in step 5's run (24
    cut-outs, 3,920,908 cells faded, 1 thread ended), since they follow
    the voltage. The rest differs from the first cold start on: 5,198
    births (4,964 before), the P cores cut out 78,215 times, and 2,922,901
    wrong results, 225,684 of them near an edge. Deaths by cause: 5,035
    faults, 142 exits, 16 cut-outs, 1 memory, none at a cold start.
  - No window counted (none started with 20 programs alive), so the
    turnover measure, and its cold marks, have nothing to say on this run.
- **Readings, agreed by Damian (2026-11-14), all as built:**
  - (a) A cold start empties port 0's queue (the old OS's) before the
    boot block joins it.
  - (b) A block nobody wrote boots as static: the cause "static" and no
    parent, so origins after an outage are unchanged (10.5).
  - (c) The last writer is the last DWRITE to land with its first byte in
    the block, whatever its length. So a rival that writes one byte there
    becomes the boot's parent, though it leaves the code. A torn write put
    back whole restores the writer before it.
  - (d) The start spots keep stepping through a reboot, so the block lands
    wherever the first core to start is, cut short if that's near the
    region's end.
  - (e) The observer keeps a dead writer's record so its boot carries its
    lineage on, and brings back a lineage that had died out.
  - (f) The two older tests, adjusted as above.
  - (g) The barrier checks a thread's request before any bytes move.
  - (h) After a boot, its code stays in the region where it landed. Idle
    runs that later start inside it can copy it again, and those are
    births of static, with no parent: only the prompt run carries the
    cause "boot". In the second-stage test, one did 4 ticks after the boot,
    and wrote itself back as the block's last writer.

### Step 7: determinism, snapshots and cost (2026-11-14)

Gate 2's state was already in the snapshots and the world hash: each step
added its part as it went. This step checks that it's all there, adds the
invariant checker and the fuzz world that gate 1's plan called for but
never got, and costs gate 2's parts of a tick.

- **The new state** (the plan's first point): a place's hash is the hash
  of its whole saved state, so it covers everything a snapshot holds. Gate
  2 added each device's state and the tick it cut out (`PLC4`), the
  voltage and the last tick's draw, the fade doses and cells failed and
  memory's edge countdowns (`NOI2`), I/O in flight, the boot block's
  writer (`OS04`), and port 0's queue (`REG2`). The supply's schedule is a
  function of the tick, so it has no position to save. Nothing new was
  needed; the determinism test below is what shows it.
- **The invariant checker** (`Place::check`, with `Os::check` and
  `Region::check`): memory's own check (gate 1's), then:
  - the OS: each live program's list of threads is whole and its count
    right; the live counts are right; a thread is running exactly when
    it's on a core, and that core has it; every ready thread has a live
    entry in a ready queue; every thread or idle run waiting on the disk
    has its request; no request outlives a barrier with the disk on; no
    request or I/O in flight runs off the disk; a core that's cut out runs
    nothing;
  - the idle runs: port 0's queue holds its arrivals end to end, within
    its bound; a core runs an idle run exactly when its run says so, as
    its own holder; every start spot is inside the region;
  - across them: every allocation is held by a live program or by a
    running idle run; a core that's cut out has no speed; memory that's on
    holds no fade, and no device has more cells faded than it has; nothing
    cut out in the future; the voltage isn't negative.
  - It's a function the tests call, not part of the tick, so it costs a
    run nothing.
- **The fuzz world** (a new `fuzz` suite), in the test build, which is the
  checked build (assertions on): 8 places of static, as an origin run
  starts, under the hardest supply the settings allow. A 400-tick day with
  nights at a tenth, swings of 0.3 every 150 ticks, and drop-outs as often
  as allowed (1,000 a million ticks), 2 to 300 ticks long, half of them
  blackouts. Error floors of 10⁻³ and cosmic rays at 10⁻⁵. Every place is
  checked after every tick. Three runs, about 7 seconds in all, so it runs
  with every test run:
  - Static, 4,000 ticks (32,000 checks): 424 core, 53 memory and 17 disk
    cut-outs; 17 cold starts, each booting the disk's static; 1,779,815
    cells faded. Static made only 1 birth in that time.
  - Crowded, 3,000 ticks (24,000 checks): a replicator in every lax
    region, and a booter, a disk writer and a disk reader placed by hand
    in every place. The disk's threshold is raised to 900 mV, above the
    cores', so it cuts out while they're at work. 452 core, 70 memory and
    155 disk cut-outs; 13 cold starts; 1,406,083 cells faded; 2,122 births
    and 1,894 deaths (56 at cut-outs, 878 by fading); 10 writes torn and 6
    reads failed.
  - The same, pausing threads at cut-outs (`os.cutout_pause`): 63 threads
    paused and none ended at a cut-out; 10 writes torn, 8 reads failed.
  - No invariant broke and no assertion fired in any of them.
- **Determinism** (a new `determinism` suite; the plan's done when): 8
  places under a supply nearly as hard: a 600-tick day, nights at a
  fifth, swings of 0.25 every 200 ticks, drop-outs as often as allowed,
  5 to 120 ticks long, half of them blackouts. The disk cuts out at 900
  mV, and every place has a replicator in its region (if lax), a booter
  and a disk writer.
  - In 2,400 ticks it went through 423 cut-outs, 1,621,099 cells faded,
    9 torn writes, 15 cold starts and 15 boots, and 3,100 births.
  - Its hash, compared every 400 ticks, is the same on 1, 2 and 24 host
    threads, and with an observer on 1 thread and on 24 (whose births and
    cold starts match the world's).
  - Snapshots are taken at the first tick after 100 at which some place
    is mid-drop-out (tick 414, place 2), mid-off-time (a device cut out,
    its off-time not over: tick 101, place 5) and mid-fade (memory cut
    out, some of its cells failed and more to go: tick 588, place 6).
    Each, resumed on 2 threads unwatched and on 24 watched, ends on the
    same hash as the run that never stopped.
- **The benchmark's gate 2 part** (`nova2 bench`): a place can time its
  tick's parts with the time-stamp counter, behind a switch (`timing`,
  off, not state) that only the benchmark turns on; the normal tick never
  reads the counter. The memory part is split into fading (with memory's
  edge errors) and the cosmic-ray floor, in the same order as before. The
  benchmark times the static world's ticks, 48 places on one host thread,
  on three supplies. Gate 2's parts are the supply, the devices' states,
  fading and the draw.
  - Gate 1's steady supply, 400 ticks: a place-tick is 23.8 µs, and gate
    2's parts 279 ns of it (1.2%).
  - Section 7's supply (sag 50, nights at a fifth, swings of a quarter,
    one drop-out a million ticks), 400 ticks: 18.9 µs, and gate 2's parts
    463 ns (2.5%). The devices' part doubles with 169 cut-outs, and
    execution is cheaper, since a core that's cut out runs nothing.
    Nothing faded.
  - The fuzz world's supply, 2,000 ticks: 39.0 µs, and gate 2's parts
    20.3 µs (52%), nearly all of it fading (19.8 µs), with 2,302 cut-outs,
    75 cold starts and 34.6 million cells faded. Fading visits each cell
    that fails once, through the permutation that orders the cells: about
    55 ns a cell, and at most a device's cells a cut-out.
  - Against gate 1's binary (run 7's), on the same machine, the rates are
    within the run-to-run noise of about 5%, either way. Gate 1 first,
    then gate 2, in million instructions a second: static on one host
    thread 74.0 and 69.9, on 24 threads 543.7 and 556.7; loops in one
    place 80.5 and 82.4; replicators on 24 threads 363.6 and 371.1.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the default steady supply, with this step's binary (the tick's
  memory part is now two functions). Its 16,000 samples match step 6's
  (and so run 7's) byte for byte, and stage 4 comes at tick 1,489,800 in
  place 27, as before. The report differs only in its times: 586 seconds
  against step 6's 617.
- **Tests:** 24 of 24 pass: the 23 suites in the checked build (10,407
  checks, about 17 seconds) and the rules check (83 sources clean).
- **Readings, agreed by Damian (2026-11-14), all as built:**
  - (a) The checker is a function the tests call, not part of the tick.
    The fuzz world calls it after every tick; a run in the release build
    neither checks nor pays for it. The other way would be to build it
    into the tick behind a switch, so any run could check itself.
  - (b) The checked build is the Debug test build, with assertions on.
    The release build, the fast one, has no tests.
  - (c) The fuzz world runs with every test run, three runs of 8 places
    in about 7 seconds, rather than as a long soak on its own.
  - (d) The crowded fuzz runs and the determinism world set the disk's
    threshold to 900 mV, above the cores', so the disk cuts out while
    programs use it and writes tear. It's a test setting; the default
    stays as it was.
  - (e) The ready-queue check goes one way: every ready thread must have
    a live entry, but an entry left by a thread that has since run or
    ended is allowed, since the queue drops those when they reach its
    front.
  - (f) The timing uses the time-stamp counter, behind a switch only the
    benchmark turns on. The memory part is split in two (fading, then the
    cosmic-ray floor, in the order they already ran).
  - (g) Fading's cost is left as it is: about 55 ns a failed cell, once
    per cell a cut-out. That's half a place-tick on the fuzz world's
    supply, but on section 7's nothing faded in the benchmark's 400
    ticks, and step 6's 400,000-tick run had 24 memory cut-outs in all.
  - (h) Static made only one birth in the fuzz world's 4,000 ticks, so
    the crowded runs, with programs placed by hand, are what put threads
    through cut-outs, fading and cold starts.

### Step 8: the gate 2 views (2026-11-14)

The viewer (`app/ui/viewer.cpp`) now shows power. Its frame is gate 1's,
rearranged to make room.

- **The place tiles** dim as a place's voltage falls: a quarter as bright
  at 0 V, as bright as at gate 1 at the source's voltage. A red corner
  marks a place with any device cut out.
- **The place inspector**, now running the full height of the window on
  the right:
  - Power: the last tick's voltage (against the source's) and capacity;
    the drop-out the last tick was in, or the next one is, with its depth
    and its ticks (a blackout says so); and the place's cut-outs by
    device and its cold starts.
  - Each core's speed this tick, at the voltage (0 while cut out). A core
    that's cut out says how much of its off-time is left, or, once that's
    over, the voltage it waits for (its threshold plus the restart gap).
  - Each memory device the same, with how much of it has faded (9.4);
    the disk and the network device the same.
- **The charts**, two rows under the places and the memory:
  - gate 1's: programs and threads; births and deaths, the deaths now by
    cause (exit, fault, cut-out, memory, cold start; no device fails
    before wear, at gate 4); distinct paths tried;
  - the inspected place's voltage, the mean and the lowest over each
    sample, so a drop-out shorter than a sample still shows, against
    faint lines at the P cores', the E cores' and the reset region's
    memory's thresholds;
  - cold starts, and births from a boot block a program wrote, per sample;
  - turnover per window: for each of the last 24 windows, the places it
    counted in, those that turned over, and of those the ones with a cold
    start in them.
- **The observer** keeps what the voltage chart needs: each place's mean
  and lowest voltage per sample, 4 bytes a place a sample (3 MB for 48
  places over 1.6 million ticks at 100-tick samples). Samples gain cold
  starts and births from boots as running totals. The origin runs' sample
  files are unchanged (they write named fields).
- **A loaded place restores its speeds and capacity.** A snapshot doesn't
  save the cores' speeds or the tick's capacity: each tick sets them
  afresh. A world opened from a snapshot showed the speeds from setup until
  its first tick. Loading now works them out from the saved voltage, the
  cores' states and the tick. They aren't in the hash, and nothing in a
  tick reads them before setting them. (The invariant checker reads them
  too: before this, it would have found a core that was cut out at a
  snapshot running at its setup speed once loaded.)
- `nova2 view` now passes the turnover settings to its observer, as
  origin runs do.
- **Done when (1):** a headless render matches a stored image pixel for
  pixel. The image is `tests/golden/view_gate2.png`, 1,280 by 800 (the
  window's default size). It shows a busy world of 6 places under a varying
  supply, with a replicator in every lax region and a booter in every
  place. It's drawn at the first moment after tick 300 that a lax place
  with programs alive is 15 ticks or more into a drop-out with a device
  cut out: place 0 at tick 1,281, in a drop-out to 0.15 over ticks 1,266 to
  1,309, with every device cut out and 5 ticks of off-time left. The same
  frame renders the same twice.
- **Done when (2):** a snapshot taken mid-drop-out (tick 414, place 2,
  with a device cut out) opens and draws every place in every memory
  colouring, 24 renders, exactly as the world it came from. The cores'
  speeds and the capacity come back as they were; the test checks that
  some core was running slow at the snapshot, so a speed loaded as its
  setup value would have shown.
- **The observer's voltages** are checked against voltages read tick by
  tick under a varying supply: every mean and lowest, in every sample and
  place, matches.
- **The regression:** seed 5 to 1.6 million ticks at run 7's settings,
  on the steady supply, with this step's binary (its observer keeping the
  voltages). Its 16,000 samples match step 6's and step 7's byte for byte,
  and stage 4 comes at tick 1,489,800 in place 27, as before. It took 611
  seconds against step 7's 586, with tests and renders running beside it.
- **Tests:** 24 of 24 pass: the 23 suites in the checked build (10,477
  checks) and the rules check.
- **Readings, agreed by Damian (2026-11-15), all as built:**
  - (a) The stored image: `tests/golden/view_gate2.png` replaces gate 1's
    as the one the test checks. Gate 1's (`view_gate1.png`) is left in
    `tests/golden`, unused. The new one is 1,280 by 800, the window's
    default, not gate 1's 960 by 600, because the inspector no longer fits
    at that size.
  - (b) The layout: the inspector runs the window's full height, and the
    charts are two rows under the places and the memory.
  - (c) The tiles also get a red corner when a device is cut out, beyond
    the plan. A place whose devices are all off draws nothing, so its
    voltage climbs back to the source's and dimming alone shows it
    bright: place 0 in the image, at 1,000 mV with everything off.
  - (d) The voltage chart plots the mean and the lowest over each sample,
    from 0 to the source's voltage, with the thresholds as faint lines
    named in its legend. The observer keeps 4 bytes a place a sample for
    it.
  - (e) Loading restores the cores' speeds and the tick's capacity (a
    small change in `Place::serialize`), so an opened snapshot shows them
    as they were.
  - (f) "Births from boots" counts births with the cause boot, from a
    block a program wrote. A block nobody wrote boots static, and shows
    only as a cold start.
  - (g) The turnover chart is the world's, not the inspected place's, over
    the last 24 windows; the observer records only windows that began
    with a program alive.
  - (h) The inspector's drop-out line shows the drop-out the last tick was
    in, or else the one the next tick is in. A device's off-time left
    counts the next tick.

### Step 9: the gate 2 runs (2026-11-15)

**Gate 2 fails its pass mark.** Turnover rose from the baseline's 6% of
counted windows to 33%, against the 90% it needed. The windows that
didn't turn over are gate 1's frozen places again: their residents asleep
or waiting for interrupts, ended only when a cold start wiped the place.
The origin regression reached stage 4 in 8 of 20 seeds, against the 10 it
needed. Both runs went to their budgets without errors, and the report
matches a hand check.

What was built:

- **The pass mark in the origin report.** After gate 1's stage 4 line,
  the report checks section 7's mark itself. It prints each part as met
  or not met, then PASS or FAIL:
  - the counted windows that turned over, against 9 in 10;
  - the crowded ones, against 9 in 10;
  - the floor, 100 crowded windows from 5 seeds;
  - the origin regression, stage 4 in half the seeds.

  The numbers are three new settings at section 7's values:
  `origin.turnover_share` 0.9, `origin.crowded_min` 100 and
  `origin.crowded_seeds` 5. The windows needed are rounded up, so 900 of
  1,000 is enough even though 0.9 in binary is a hair over.
- **`nova2 taker`, the 9.4 test world** (`app/taker.cpp`). The taker itself
  is in `app/taker_world.h`/`.cpp`, a small library the tests share.
  - The taker is 36 bytes of code. Its first thread SPAWNs threads of its
    own until its device has no room for another 64-byte stack. Then it
    ALLOCs whatever is left on any device, from 4,096 bytes down. Then
    every thread runs SLEEP 0 and waits for an interrupt that never comes.
  - The command places a taker in every place, runs the world to the
    origin test's budget, and reports on each place:
    - how full the taker made it;
    - when the taker ended, and what ended it;
    - the drop-out it ended in or after;
    - the threads it lost to fading before then;
    - births in the place afterwards.
- **The fill is built by hand at tick 0,** through the OS's own spawn and
  ALLOC, with each thread put to sleep holding the registers its code
  leaves.
  - The first version let the code do the fill. Its first thread, alone
    on its P core while it filled, ended at the first night-time cut-out
    in most places. Place 4 made only 8 threads, and several places made
    none.
  - Pinning that thread to E cores wouldn't cover every place: about 1
    place in 10 has no E core.
  - The question 9.4 asks is what ends a full place, not whether a fill
    survives the night.
- **The taker test** (`tests/test_taker.cpp`, a new suite, 1,380 checks)
  shows the state built by hand is the one the code reaches on a steady
  supply. The threads, their ids, stacks and saved registers, the held and
  free bytes and every byte of memory all match, on lax and strict
  places. The code takes 939 ticks to fill a 32 KB place, with 152
  threads.
  - A filled place stays frozen on a steady supply: no births in 4,000
    ticks, though 5,754 idle runs start on its cores.
  - A 20-tick blackout ends the taker within 2 ticks, when its last
    thread's saved registers fade. That's before the cold start comes, so
    the cause is memory.
- **Tests:** 25 of 25 pass: the 24 suites in the checked build and the
  rules check. The release build is clean.

**The 9.4 test world** (`runs/taker-1`): seed 1, a taker in each of the 48
places, run under gate 2's supply to the origin test's budget. It took
17,770,700 ticks and 8,843 s on 4 host threads.

- **Every place started full.** Each taker held 157,744 to 159,664 of the
  163,840 bytes, leaving 0 to 960 free, with 608 to 1,279 threads.
- **No taker held to the end.**
  - 44 ended by fading, in or just after a blackout.
  - 4 ended at a cold start after a drop-out that left 0.017 to 0.050 of
    capacity, nearly a blackout.
  - None was ended by a shallow drop-out, night, a swing or a cut-out.
- **How long they held:** 1.69 million ticks on average (median 1.34
  million, shortest 30,620, longest 4,450,053). That's about what one
  blackout every 2 million ticks a place predicts. In effect, a taker
  lasts until its place's first blackout.
- **No taker lost a thread before its end.** Each place went from full to
  empty at once.
- **Afterwards, life mostly didn't take.** Every place had births once its
  taker was gone, from 30 to 36,189. But only 9 places had programs alive
  at the end, 37 programs in all, 21 of them in place 31.
- **Section 8's prediction holds: sleepers outlast everything but a cold
  start.** A blackout ends them by fading, two ticks before the cold
  start would. One part of the prediction was wrong. A full place isn't
  quiet: its cores run idle runs on static all the time, so it draws far
  more than idle power. That still wasn't enough for night or a shallow
  drop-out to reach its memory's threshold. In a short stress check at
  500 times section 7's drop-out rate, drop-outs to 0.15 and 0.125 did
  cold-start full places.

**The origin and turnover runs** (`runs/gate2-1`): seeds 1 to 20 under gate
2's world at gate 1's budget, 10 seeds at a time. It took 40,130 s (11.1
hours) against the baseline's 29,239 s. For 2.5 hours the taker run
shared the machine.

- **The seeds ran 16.0 to 20.4 million ticks,** against the baseline's
  12.9 to 16.2 million. The budget is in instructions, and cores spend
  time cut out or slowed. P cores cut out 91.7 million times, for 21
  ticks at a time on average, and were out 4.5% of their ticks.
- **The stages: stage 4 in 8 of 20 seeds** (1, 2, 3, 5, 6, 8, 14 and 15),
  against the baseline's 11 (1, 3, 5, 6, 8, 13, 14, 15, 18, 19 and 20).
  - Stage 5 came in 7 seeds, against the baseline's 6.
  - The seeds don't correspond across the two worlds: the supply changes
    every place's history from the first tick. So it's the counts that
    compare. At the baseline's rate of 11 in 20, 8 or fewer would come
    about 13% of the time.
  - Strict places again reached no stage, as at the baseline.
- **The holds no longer last.**
  - At the baseline, three seeds' best holds ran to the end, for 8.5 to
    12.6 million ticks. Here none does, and the longest is 3.52 million
    ticks (seed 5, place 1).
  - Of the 8 first lineages to reach stage 4, 7 died out, each in a
    window with a cold start in it.
  - The eighth, seed 8's, reached stage 4 late, at 16.7 million ticks. At
    the end it still had 14 of its place's 78 programs.
- **Turnover: 283 of 848 counted windows (33.4%), and 130 of 279 crowded
  ones (46.6%).** The baseline had 6% and 9%.
  - The counted windows come from 37 places, and 34 of those turned over
    at least once (21 of 43 at the baseline).
  - By OS type, lax places turned over 278 times in 774 windows. Strict
    places turned over 5 times in 74.
  - The longest run without turnover was 26 windows (5.2 million ticks),
    in place 42 of seed 15. At the baseline it was 64 windows.
- **Cold starts made a third of the turnover.**
  - 106 counted windows had a cold start in them, and 103 of those turned
    over. All 42 crowded ones did.
  - Without a cold start, 180 of 742 counted windows turned over (24%),
    and 88 of 237 crowded ones (37%).
  - 12.5% of counted windows had a cold start, close to section 7's "about
    1 window in 10". Yet there were 24,211 cold starts in all, 1.4 a
    place per million ticks, nearly three times the blackouts' 0.5. The
    taker world had the same rate (1,188). So cold starts come in
    clusters. The likely reason is a deep partial drop-out: once every
    device cuts out, the place draws nothing, its voltage climbs back,
    the devices restart, and the place sags below the threshold again. I
    haven't traced one.
- **The windows that didn't turn over failed on their residents.**
  - In 562 of the 565, fewer than half the starters died. The other 3
    lost half their starters without births replacing them.
  - Only 14.7% of their starters died, against 86% in the windows that
    turned over.
- **A frozen place, seed 5's place 1.** This is the longest hold.
  - Its lineage reached stage 4 at 5.2 million ticks.
  - From then on, fewer and fewer of each window's starters died: 239 of
    246 at 5.0 million, 35 of 247 at 6.4 million, 2 of 233 at 7.0
    million.
  - From 7.8 to 8.6 million ticks, four windows in a row had 263
    programs, no births and no deaths.
  - Its snapshots at 7 and 8 million show the memory 99.7% and 99.8%
    full, and no thread ready. Of the 793 and 845 threads, 755 and 809
    were waiting for an interrupt, and the rest were sleeping or waiting
    for a message.
  - The residents were born between 4.9 and 6.9 million ticks.
  - The window from 8.6 million had a cold start, and all 263 died.
  - This is gate 1's frozen place again. Gate 2's physics ended it only
    with a cold start.
- **Deaths:** 1.27 per program per day (baseline 1.06), so a mean life of
  0.8 days. By cause:
  - faults 1.25;
  - cut-outs 0.010;
  - exits 0.008;
  - memory 0.001;
  - cold starts 0.0003.

  So 134,835 programs died at cut-outs, 14,156 of memory and 4,125 at
  cold starts. The churn is still faulting offspring.
- **Births:** 17.1 million, 7.8 million of them faithful, against the
  baseline's 27.3 million and 18.0 million.
  - Of the 24,211 cold starts, 10,265 booted a block a program wrote.
    They gave 57 births.
- **Supply:** places averaged 940 mV.
  - They were below the P cores' threshold 1.1% of place-ticks, the E
    cores' 0.01% and the reset region's memory's 0.011%.
  - Memory cut out 76,276 times, and 7.4 billion cells faded, ending
    93,725 threads.
- **The pass mark, as the report checks it:**
  - counted windows: 283 of 848 turned over, with 764 needed: not met;
  - crowded windows: 130 of 279, with 252 needed: not met;
  - the floor: 279 crowded windows from 9 seeds: met;
  - the origin regression: stage 4 in 8 of 20 seeds: not met;
  - **FAIL.**
- **The hand check** (seed 1, place 3, which held through stage 4 from
  6.2 million ticks):
  - Each of its seven snapshots at a window's start (7 to 13 million) was
    listed with `nova2 trace --snapshot --list`. The programs alive and
    the free memory match the window's starters and its crowded flag in
    every case: for example 137 programs and 0.8% free at 10 million
    (crowded), and 146 programs and 31.6% free at 13 million (not).
  - Recomputing every window in every seed's file from the rule gives the
    same counted and turned flags in every row. Their sums give the
    report's figures exactly:
    - each seed's line (seed 1: 29, 28, 24, 24);
    - the totals (848, 283, 279, 130);
    - the cold-start lines (106, 103, 42);
    - the 34 of 37 places;
    - the 9 seeds with crowded windows.
  - The needs are 0.9 times 848 and 279, rounded up: 764 and 252.
- **Done when:** both runs went to their budgets without errors (exit 0),
  and the report reads correctly against the hand check. Met.
- **What the fail means** (sections 8 and 10).
  - Gate 2's answer is the one section 8 feared: "no, not by the physics
    alone at these rates".
  - Raising the outage rates would buy turnover by catastrophe and cost
    the origin regression more.
  - The choices section 10 names are the reserve lever (a visit that
    reboots a place, 7.8) or another rule, then a design review if needed.
  - For the origin regression, section 8 names the pause switch (built,
    `os.cutout_pause`) or a milder supply.
  - All of these are Damian's decision.
- **Readings, agreed by Damian (2026-11-15), all as built:**
  - (a) The taker's fill is built by hand at tick 0, and proved equal to
    the code's own fill on a steady supply. That's instead of letting the
    code fill under gate 2's supply, where its first thread died at
    night-time P-core cut-outs.
  - (b) The test world is one seed (1), with 48 places and one taker in
    each. "The budget's length" is read as the origin test's budget,
    10^12 instructions or 10^8 ticks. The instructions ran out first, at
    17.8 million ticks.
  - (c) A taker's end is its last thread's death, and its cause is that
    thread's. A blackout ends it by fading before the cold start, so the
    report also names the drop-out it ended in or after: the last to
    start at or before its end, in its schedule stretch or the two before
    it, 1,024 ticks each.
  - (d) Threads a taker lost before its end are put down to fading, by
    elimination: its threads never run, so nothing else can end them.
    None was lost.
  - (e) The pass mark's three settings. The windows needed are rounded
    up, after a millionth is taken off so that 0.9 of 1,000 gives 900.
  - (f) The 20-seed run kept snapshots every million ticks of the places
    worth tracing, as runs 7 and 8 did (the baseline had none). The hand
    check used them.
  - (g) The 20-seed run's binary was built before `nova2 taker` was
    added. It has the same engine and origin runner. The taker run's
    binary is the later build.

### Step 10: the widened saved-state rule, on trial (2026-11-15)

Damian accepted the design review's four proposals
(`design-review-gate2-2026-11-15.md`). This step builds its first, the
saved-state rule widened from "fades" to "fails", behind a switch, and
tries it. It also traces a cluster of cold starts, the review's third.

**The rule does its job: it ends frozen places.** In the 9.4 test world,
every taker now ends within 26,000 to 50,000 ticks, against 1.69 million
without it. Early life isn't hurt: by 5 million ticks the pilot reached
each stage in as many seeds as `runs/gate2-1`, or more. But places hold
far fewer programs, and the pilot is too short to say whether they still
crowd: in `runs/gate2-1`, crowded windows came only after 5 million ticks.

What was built:

- **The switch:** `os.saved_state_fails`, off by default (`OsSettings`,
  key in "OS and calls").
- **Memory's two other paths list what they fail.** Memory's edge errors
  and the cosmic-ray floor (`Noise::memory_errors`, `Noise::cosmic_flips`)
  take an optional list. Each appends the address of every byte a bit of
  which it failed: a flip always changes its bit, and an edge error's bit
  reads back random.
- **The place ends the threads they hit**
  (`Place::end_failed_states`). After the memory step's fading, edge
  errors and flips, each thread whose saved state holds a failed byte
  ends, cause "memory".
  - The saved state is fading's: the saved registers (24 bytes at the
    stack's base) while the thread is off its core, and a handler's frame
    (20 bytes) on or off.
  - It's by address, as fading's is, so a stack its program freed or gave
    away still counts. So it scans every thread record when any bit fails,
    about 0.4 ns a record.
  - Fading's own rule is unchanged, and an overwrite still only corrupts.
- **The reports:**
  - The origin report's errors-and-fading lines name the rule and count
    the threads it ended, only when it's on.
  - The taker report says "memory failing" instead of "fading" when it's
    on, and counts the threads it ended.
- **Tests** (a new `fails` suite, 42,655 checks; 26 of 26 green):
  - **The lists:** the floor listed all 946 of its flips' bytes, each
    changed. Edge errors listed all 20,303 of theirs, 10,022 left
    unchanged. No changed byte went unlisted.
  - **Address by address,** through the place's own call: a sleeper's
    last register byte, a handler's frame's last byte and a handler
    sleeper's first register byte each end their thread. The bytes just
    outside them, the gap between registers and frame, and a running
    thread's registers don't.
  - **Real flips,** tick by tick for 102 ticks, 48 sleepers (half in a
    handler) on a steady supply, so only the floor touches their stacks.
    The 32 whose saved state took a flip each ended in that tick, cause
    memory, 7 through a handler's frame alone. The 11 whose didn't lived,
    and 5 were left out, ended or changed before the window began. With
    the rule off, 80 flips in saved states ended no one.
  - **A thread alone on its core** lived through 43 ticks with a flip in
    the stale copy of its registers.
  - **An overwritten sleeper** lived, and after it woke and slept again
    its saved B was still the written value: it ran with it.
  - **The state:** a busy world with the rule on (1,526 threads ended by
    it, 137 by fading) is the same on 1 and 3 threads, and resumed from a
    snapshot at tick 1,200. With the rule off it goes another way.
  - **The fuzz world** gains a fourth crowded run with the rule on: 1,050
    threads ended by it, every invariant held.
- **With the switch off, nothing changes.** `runs/gate2-1`'s own binary
  and this one ran its config for 2 seeds to 200,000 ticks. The samples,
  genomes and windows are byte for byte the same; the reports differ only
  in their timing columns.
- **The cost** (`nova2 bench` at the runs' cosmic floor of 10^-6): the
  floor's part of a place-tick goes from about 520 ns to 1,560 ns, about
  1 µs, or 4% of section 7's 24 µs place-tick. A cheaper lookup is
  possible if the rule stays.

**A cluster of cold starts, traced** (`runs/cluster-trace-1`, the
review's third proposal):

- A scratch probe, kept there and not part of Nova 2, ran single places
  of `runs/gate2-1`'s seed 1 alone from tick 0. Places don't touch each
  other at gate 2, so that's their history in the run.
- **Place 1:** a drop-out at night at tick 3,374,156, 419 ticks long,
  leaving 0.037 of capacity (about 0.008 with the night). It gave 20 cold
  starts, one every 21 ticks.
- **Every 21 ticks it's the same:**
  1. With nothing drawing, the voltage is back at the source's 1,000 mV,
     so after the 20-tick off-time every device restarts at once.
  2. The reset region's memory coming back is a cold start.
  3. The restart draws 7 to 18 W, and at under 1% of capacity the voltage
     falls to 170 to 290 mV, below every threshold.
  4. Everything cuts out, and back to 1.
- The first cold start ended the place's one program; the other 19 found
  nothing alive. Place 2 did the same, 5 times in a 97-tick drop-out.
- **It's the physics as written, not a bug:** a brown-out reset loop.
  Capacity scales the source's conductance, not its voltage (appendix C
  3), so the restart gap and off-time can't hold devices off. It changes
  nothing in the turnover arithmetic, which counts windows with a cold
  start in them.
- The P cores' cut-outs average the same 21 ticks (gate 2's runs: 21.2
  ticks out a time), so the night-time P-core cut-outs may be the same
  loop at the P cores' threshold. I haven't traced one.

**The 9.4 test world again** (`runs/taker-2`): `runs/taker-1`'s world
with the rule on, to 2 million ticks.

- All 48 takers ended, every one by memory failing, 47 with no drop-out
  near. They held 37,442 ticks on average (26,036 to 50,307), against
  1.69 million without the rule.
- That's the rule's arithmetic. A sleeper's 192 bits take a wrong bit in
  about 5,200 ticks, and the last of N goes after about 5,200 × (ln N +
  0.58): about 36,500 for 624 threads, 40,100 for 1,248.
- Afterwards every place had births of static (17,937 in all), but none
  had a program alive at 2 million ticks.

**The pilot** (`runs/gate2-pilot-1`): `runs/gate2-1`'s config for seeds 1
to 10 to 5 million ticks, with the rule on, read against `runs/gate2-1`'s
same seeds at 5 million ticks (its numbers in brackets).

- **Stages:** stage 1 in 9 seeds (9), stage 2 in 7 (4), stage 3 in 3 (3).
  Ahead in seeds 2, 4, 6 and 7, behind in 5 and 9, level in the rest.
- **Births:** 650,097 (607,894). Faithful births: 14,162 (3,189), 12,998
  of them in seed 4.
- **Alive at 5 million ticks:** 13 programs (550), with 3,420 threads
  (1,520). Two programs in seed 10 had 1,951 threads between them.
- **Windows:** 227 began with a program alive (1,606).
  - 5 were counted, and all turned over, every one of their 378 starters
    dying (22 counted, 2 turned over, 118 of 974 starters died).
  - None was crowded (none).
- **Deaths:** the rule ended 1,413,202 threads; fading 4,719, cut-outs
  124,570. Programs died of memory 18,534 times, against 621,685 faults.
- **Holds:** the longest were 33,700 ticks (seed 4) and 11,900 (seed 2).
  No other seed held.
- 3,261 cold starts, as before.

**What the pilot can't show.** In `runs/gate2-1`, the first crowded
windows came between 5 and 7.5 million ticks (35 across 20 seeds), and
most came after 10 million. So the pilot says nothing about crowded
places, the floor or stage 4. What it does show:

- the rule ends frozen places, by the physics, at the rate worked out;
- early life comes as often as before, or more often;
- populations are much thinner. Every counted window turned over, but
  places hold few programs and holds are short. Whether busy lineages
  still crowd places and hold them for stage 4 is the full run's question.

**Readings, agreed by Damian (2026-11-16), all as built:**

- (a) An edge error fails its bit whatever it reads back, as a faded cell
  does, so it ends a thread even when the bit happens to read back
  unchanged. Parity would only catch the bits that changed.
- (b) The saved state is exactly fading's: registers only while off the
  core, and a handler's frame on or off. So a thread alone on its core is
  exempt, but not while it's in a handler.
- (c) A tick's failed bits are gathered over the whole memory step, and
  the threads they hit end after all of them, in record order, rather
  than one by one as each bit fails. It changes only the order deaths are
  recorded in.
- (d) The lookup scans every thread record when any bit fails, for 1 µs a
  place-tick, rather than a faster index kept by the OS.
- (e) The overwrite test writes into the sleeper's stack from the test
  itself, not from another program. The rule's code only sees the
  addresses memory's noise hands it, so any write is the same to it.
- (f) The taker rerun (`runs/taker-2`) was added to the agreed pilot, to
  2 million ticks, as the direct check on frozen places.
- (g) The cluster was traced from tick 0 with a scratch probe, not from a
  snapshot as the review said, because the probe needs every tick of the
  drop-out and the place's own history up to it.
- (h) The pilot is read against `runs/gate2-1`'s samples and windows at 5
  million ticks: windows count if they ended by then.

### Step 10, continued: gate 2's second run (2026-11-16)

**Gate 2 passes its pass mark, with the widened rule on.**
`runs/gate2-2` is `runs/gate2-1` again, all 20 seeds to the budget, with
`os.saved_state_fails` on and nothing else changed. It ran on Damian's
word after the pilot. Every counted window turned over, every crowded one
too, and the origin regression reached stage 4 in 13 seeds. Damian
agreed the readings below on 2026-11-16, and the rule is now the
design's, on by default.

- **Against the pass mark** (`runs/gate2-1`'s figures in brackets):
  - counted windows: 586 of 586 turned over (283 of 848), 528 needed;
  - crowded windows: 448 of 448 (130 of 279), 404 needed;
  - the floor: 448 crowded windows from 13 seeds (279 from 9);
  - the origin regression: stage 4 in 13 of 20 seeds (8), 10 needed.
  - **PASS.**
- **The hand check.**
  - Each window's counted and turned flags were recomputed from the rule:
    20 starters or more; half of them dead, and births half the dead.
  - They match the file's flags in all 2,757 rows.
  - Their sums give the report's figures:
    - each seed's line;
    - the totals;
    - the 71 windows with a cold start (71 turned, 49 of them crowded);
    - the 24 of 24 places;
    - the 13 seeds with crowded windows.
  - The needs are 0.9 times 586 and 448, rounded up.
- **Every starter died.**
  - In all 586 counted windows, every one of the 140,784 starters died
    within the window.
  - In `runs/gate2-1`, 48% died, and in 735 of the 848 windows some
    starter lived.
  - That's the rule's arithmetic. A sleeping thread lasts about 5,200
    ticks, and no program kept a thread going for 200,000.
  - So with the rule on, the measure can't fall short: a crowded place
    turns over in every window. What lasts is lineages, by copying.
- **Lineages:**
  - The best holds were over a million ticks in 8 seeds (6), up to 3.6
    million.
  - Stage 4 came in eight seeds that didn't reach it in `runs/gate2-1`
    (4, 7, 9, 11, 12, 16, 19 and 20).
  - It didn't come in three that did (1, 2 and 5).
  - Of the 13 first stage-4 lineages:
    - 12 were extinct by the end;
    - seed 16's held its place at the end, all 364 of the place's
      programs, every one born as a copy.
- **Programs:**
  - Births: 39.3 million (17.1), 17.1 million of them faithful (7.8).
  - Deaths per program per day: 2.67 (1.27), so a mean life of 0.4 days
    (0.8). By cause:
    - fault 2.36 (1.25);
    - memory 0.28 (0.001);
    - exit 0.017;
    - cut-out 0.015.
  - The rule ended 45.2 million threads through flips and edge errors.
    Fading ended 110,179 (93,725).
  - Cold starts: 24,559 (24,211).
- **Where they gather:**
  - The counted windows came from 24 places (37), all lax.
  - Strict places had none (74 in `runs/gate2-1`, 5 of them turned) and
    stayed at stage 0, as at gate 1.
- **The cost:** 17.1 hours, against 11.1.
  - Seed 3 ran at 16.2 million instructions a second, against 37 to 70
    for the others (47 to 66 in `runs/gate2-1`). It finished 6 hours
    after the rest.
  - Its ticks held their usual 56,000 instructions. But a tenth of its
    budget went from about 2,000 s before 9 million ticks to 18,000 s in
    its last tenth, as its places crowded (up to 871 programs, and about
    a million births a million ticks).
  - The other crowded seeds (12, 14, 15 and 20) ran at 37 to 41.
  - The cause, traced afterwards, was the observer's genome grouping
    ("Seed 3's slowness, traced", below). The rule's lookup costs about
    1 µs a place-tick, too little to explain it.
  - Snapshots of seed 3's two crowded places (23 and 34) are kept, every
    million ticks.
- **Done when:** the run went to its budget without errors (exit 0), and
  the report reads correctly against the hand check. Met.

**What the pass means:**

- Gate 2's question (R30) has an answer: with the widened rule, a full
  place keeps turning over.
- Early life still arises and establishes: stage 4 in 13 seeds, against
  gate 1's 11 and `runs/gate2-1`'s 8.
- The 9.4 check is `runs/taker-2`: every taker ended by memory failing,
  within 26,000 to 50,000 ticks.
- Section 10's next step after a pass is gate 3.

**Readings and decisions, agreed by Damian (2026-11-16):**

- (i) **The pass is the switch's,** so the rule becomes the design's, on
  by default. The switch stays, so gate 1's world and the baseline can
  still be run as they were.
- (j) **The turnover measure stays as it is,** though with the rule on it
  can't fail: every starter dies in its window.
  - Gate 2's question was whether full places freeze, and they now can't.
  - Lineage holds and stage 4 already show what lasts.
  - A sharper measure could come at a later gate, if one needs it.
- (k) **Seed 3's slowness is noted, not traced.** It cost 6 hours here,
  and would cost more at gate 3's scale. A profile from its snapshots is
  a short job, if you want it before gate 3.

**Done after the readings were agreed (2026-11-16):**

- **The rule is the design's:**
  - `design.md` 9.4 states it, with gate 2's second answer under
    Turnover.
  - 14.1 records the pass.
  - Appendix A's saved state, appendix B's thread lifecycle, appendix C's
    memory step and appendix D's ninth law name it.
- **It's on by default:** `OsSettings::saved_state_fails` and its key.
  Configs that need fading alone say `os.saved_state_fails = false`. Each
  run in `runs/` keeps its own binary, so the runs before this reproduce
  as they were.
- **The tests follow the default:**
  - `fails` turns the rule off where it compares against it.
  - The key test checks that it's on by default and that the key turns it
    off.
  - The fuzz world's first three runs now have the rule on. The crowded
    one also checks that the rule ended threads.
  - Its fourth run now turns the rule off, so fading alone is still
    fuzzed: 0 threads ended by flips or edge errors.
  - The fade and taker suites run with no cosmic rays, so the default
    leaves what they test unchanged.
  - 26 of 26 pass.

### Seed 3's slowness, traced: the genome groups' index (2026-11-16)

Reading (k) left seed 3's slowness untraced, with a profile offered.
Damian asked for the profile ("Yes, profile it"), then for the fix it
pointed to ("Yes, build it").

**The places weren't slow.**

- A scratch harness replayed place 34, where seed 3's stage-4 lineage
  lived, from its snapshots.
- It cost 11 µs a tick at 9 million ticks and 12 µs at 17 million, about
  33 million instructions a second.
- Seed 3's whole ticks had gone from about 1.1 ms to about 10 ms, so the
  time went somewhere else.

**The observer's genome grouping was:**

- Each new distinct genome was compared with each group's reference in
  turn (13.2's greedy grouping), until one shared 80% of its 3-byte runs.
  One that started a group was compared with all of them.
- The observer runs on one host thread after each tick's barrier, so the
  seed's second thread couldn't share it.
- Seed 3 made the most genomes of any seed: 1.4 million, in 191,000
  groups.
- In the replay, with the observer's code timed, grouping was 96 to 99%
  of the observer's time:
  - each comparison took 350 to 540 ns, on genomes of 80 to 108 bytes;
  - each new genome was compared with about 70% of the groups.
- Those figures account for the run's lost time:
  - In seed 3's last tenth they give about 15,600 extra seconds; the run
    lost 16,026 against its first tenths' 2,000 s.
  - In the tenth before, they give 8,400; it lost 9,200.
  - In all, about 11 of seed 3's 17 hours.
- Seed 15 slowed the same way as its groups reached 148,000. Seed 13,
  under 9,000 groups, kept about 1,350 s a tenth throughout.

**The fix: an index of the groups' runs (`core/src/observe.cpp`).**

- Each 3-byte run lists the groups whose reference holds it, in group
  order.
- A genome of n runs can join a group only if they share at least
  need = ceil(threshold × n) runs, so the group holds at least one of any
  n − need + 1 of the genome's runs.
- So only the groups on the lists of its n − need + 1 rarest runs are
  checked: in group order, with the same comparison as before, the first
  that passes taken.
- That is the group the scan picked, so nothing it computes changes:
  groups, stage 5, and every count and run.
- Genomes under 3 bytes are grouped by equality as before.
- It's the filtering CD-HIT and UCLUST use, the tools 13.2 cites.
- Nothing in `design.md` changes: 13.2's greedy grouping is as written,
  only computed faster.

**Checks:**

- **A test** (`observe`'s `test_group_index`):
  - It feeds thousands of genomes as births: mutants, fresh ones and ones
    under 3 bytes.
  - Every genome's group is checked against a scan written apart from the
    observer's, at thresholds of 0.8, 0.5, 1 and 0.01.
  - Broken on purpose, it fails: with one run too few on the lists checked
    (6 checks fail), and with the groups checked out of order (3).
- **Seven places replayed** from gate2-2's snapshots (seeds 3, 11, 12 and
  14), with the old and the new observer:
  - 173,821 genomes, every one in the same group.
  - The observer took 25 to 38 times less time, at 1,500 to 6,600
    groups. Place 34 from 9 million ticks: 68.2 s against 1.8 s.
- **Seed 3 again, in full** (`nova2 origin`, gate2-2's config, seed 3
  alone on 24 host threads):
  - It ran to its budget at the same tick, 17,746,700.
  - Its samples (177,469 rows, the genome and group counts among them),
    its turnover windows, its genome dump and the report's seed 3 lines
    are gate2-2's, byte for byte.
  - Its 19 place snapshots hold the same places: their state hashes
    match. The files differ only in the settings text they start with.
  - It took 3,199 s. That can't be set against gate2-2's 61,691 s, since
    it ran alone on 24 host threads, not as one of ten on two each.
  - Against its own first tenth, though: its last tenth took 1.7 times as
    long (456 s against 265 s). In gate2-2 it took 9 times as long (18,026
    s against 2,002 s).
- **Memory:** the index keeps one 4-byte entry, plus its blocks' spare
  room, for each run of each group's reference. The observer already
  keeps each of those runs in 8 bytes. Seed 3's rerun held about 0.9 GB
  near its end.
- 26 of 26 suites pass.

**Readings, agreed by Damian (2026-11-16):**

- (l) **Seed 3's slowness was the observer's, not the world's:** the
  grouping's scan, its cost growing with the groups.
- (m) **The index changes no result.** Seed 3 reran row for row, groups
  included, and runs before it reproduce. `design.md` is unchanged.
- (n) **Gate 3 starts without this cost.** Life spreading across places
  will make more genomes, so it would have grown there.
