# Design review after gate 2's runs (2026-11-15)

`build-plan-gate2.md` section 10: "A fail: the reserve lever, a visit that
reboots a place (7.8), or another rule, with Damian. Then a design review
if needed." Gate 2 failed. This is that review: what the runs showed, and
a proposal for each question, for Damian to decide. The evidence is in
`build-plan-gate2.md` section 11, step 9. Numbers in brackets are sections
of `design.md`.

## Where gate 2 stands

- **The pass mark** (section 7, agreed 2026-11-14), checked across all
  seeds:
  - 9 in 10 counted windows turn over, and 9 in 10 crowded ones;
  - on at least 100 crowded windows from 5 seeds;
  - windows with a cold start in them count;
  - the origin regression reaches stage 4 in half the seeds.
- **The result** (`runs/gate2-1`, seeds 1 to 20 under gate 2's world, at
  gate 1's budget):
  - 283 of 848 counted windows turned over (33%), and 130 of 279 crowded
    ones (47%). Gate 1's world had 6% and 9%.
  - The floor was met: 279 crowded windows from 9 seeds.
  - Stage 4 came in 8 of 20 seeds, against the baseline's 11.
- **The windows fail on their residents.** In 562 of the 565 that didn't
  turn over, fewer than half the starters died. On average only 15% of
  them did.
- **Cold starts made a third of the turnover.** 103 of the 106 counted
  windows with a cold start in them turned over. Without one, 24% did.
- **Frozen places are back.** Seed 5's place 1 had 263 programs and no
  births or deaths for four windows in a row. Its memory was 99.8% full,
  and every one of its 845 threads was asleep or waiting, none ready to
  run. A cold start ended them all. 7 of the 8 first stage-4 lineages died
  out, each in a window with a cold start in it.
- **The saved-state rule doesn't do its job.**
  - 9.4 brought the rule in for exactly this case: "Without it, a thread
    asleep until an interrupt that never comes would hold its memory
    until a cold start."
  - But memory fades only below its threshold, and only blackouts and the
    deepest drop-outs take it there. Those cold-start the place anyway.
  - The 9.4 test world shows it. Each of its 48 takers held until its
    place's first blackout or near-blackout, 1.7 million ticks on average.
    No night, swing, cut-out or shallower drop-out ended one.
- **The residents aren't all asleep, and their places aren't all full.**
  - 416 of the 565 windows that didn't turn over had a tenth or more of
    memory free.
  - Snapshots at such windows show mostly waiting threads. For example,
    in seed 6's place 33 at 18 million ticks, all 235 threads were asleep
    or waiting, with 22% of memory free.
  - But some places were busy. In seed 15's place 46 at 11 million ticks,
    339 of 681 threads were ready, and none of the 63 starters died in the
    window.

## 1. What can end a frozen place

**a. The reserve lever (7.8): a visit that reboots a place** on a
schedule, using the existing cold start.

- **It's a cold start on a timetable,** so the runs say what it would
  take. Windows with a cold start turned over 97% of the time, and others
  24%. To reach 9 in 10, about 9 counted windows in 10 would need a cold
  start, against 1 in 8 now. That means a reboot every 200,000 ticks or
  so in every place.
- **At that rate places wouldn't fill.**
  - After a cold start, life starts again from static. Boot blocks
    carried it across only 57 times in 24,211 cold starts.
  - Stage 1 took between 28,000 and 13.5 million ticks to come in the 20
    seeds.
  - In the taker world, 39 of the 48 wiped places had no programs at the
    end, after 13 million ticks or more to refill.
  - So the floor and the origin regression would fail even if turnover
    passed.
- **It breaks "programs are never touched"** (7.8). Its rate would also
  be chosen to give the answer, which section 8 of the plan warns about.

**b. More outages:** section 7's drop-out rates raised. It's the same
arithmetic as a, with more cut-outs and fading for early life on top. It
also changes numbers agreed before the run.

**c. A low-memory killer:** when an ALLOC fails, the OS ends the program
whose threads have gone longest without running.

- **Real anchors:** Android's low memory killer ends cached background
  processes, least recently used first, when memory runs short. Linux's
  OOM killer ends the biggest.
- **It aims at crowding,** the gate's question, and leaves sleepers in
  roomy places alone.
- **But it would miss most of the windows that failed.** 416 of the 565
  had a tenth or more of memory free. It would also miss busy residents,
  like seed 15's.
- **It picks losers by rule,** which is 9.5's argument against priority.
  And it lets one program's ALLOC end another, close to naming it
  (principle 12).

**d. Widen the saved-state rule from "fades" to "fails."** A thread ends
when any bit of its saved state goes wrong by the physics: fading, a
cosmic-ray flip or an edge error. An overwrite by a program still only
corrupts it.

- **Real anchor:** Linux's memory-failure handling, which gap 2 already
  cited. With `vm.memory_failure_early_kill` set, the kernel kills a
  process as soon as an uncorrectable error is found in its memory,
  including one found by patrol scrubbing in memory no one is reading.
  Nova 2 has no ECC, so the rule acts as if the OS's saved contexts had
  parity: detection only, as early PCs' parity memory had.
- **What it does at today's numbers:**
  - A saved state is 24 bytes (192 bits), more in a handler. The cosmic
    floor is 10^-6 a bit a tick (lever 3). So one bit goes wrong about
    every 5,200 ticks a thread spends off a core.
  - A program of 3 threads that never runs lasts about 10,000 ticks,
    against a 200,000-tick window. Seed 5's frozen place would empty in a
    few tens of thousands of ticks.
- **It reaches busy residents too.** A ready thread waiting its turn keeps
  its state in memory. In a crowded place most threads are off a core
  most of the time, so all of them face it. A thread alone on its core
  keeps its registers there and is exempt.
- **It puts a price on sleep.** A thread asleep for 1,000 ticks (half a
  day) survives 83% of the time. That's principle 6's trade-off: sleep
  through a cut-out and risk a flip, or run and risk the cut-out. 7.6's
  night-sleepers still win, but only with threads to spare.
- **It's small:**
  - no new law and no new number;
  - one lookup per flip, about 1.3 flips a place a tick, which are
    already drawn;
  - the engine already does the same lookup for faded cells.
- **The risks:**
  - It ties two things to one number. The cosmic floor was raised 1,000
    times at gate 1 for the origin test, and now it would also set how
    long waiting threads live.
  - It may cost the origin regression more, by ending young lineages'
    queued threads.
  - It reverses part of 9.4's agreed rule ("A flip or an overwrite only
    corrupts the state"), for flips only.

**Considered and not proposed:**

- **Waking waiting threads after a limit:** it ends nothing, and the taker
  would go back to sleep.
- **Making held memory cost power:** power is drawn, not held (9.4), and no
  program has an energy budget.
- **Raising memory's thresholds so that nights fade it:** that would fade
  busy places, which sag the most, and spare sleepers, which sag the
  least.

**Proposal:** d, built behind a switch for a pilot, as `os.cutout_pause`
was. It becomes the rule only if the pilot bears it out. The reserve lever
stays in reserve.

## 2. The origin regression

- **Stage by stage, against the baseline:**
  - stage 1 in 20 of 20 seeds (20);
  - stage 2 in 17 (20);
  - stage 3 in 16 (19);
  - stage 4 in 8 (11);
  - stage 5 in 7 (6).

  So gate 2 loses lineages early, as well as at the hold.
- **It may be chance.** The supply changes every place's history from the
  first tick, so it's the counts that compare, not the seeds. At the
  baseline's rate of 11 in 20, 8 or fewer would come about 13% of the
  time. The mark stands either way.
- **The crash is the suspect,** untested. 1.6 million threads ended at
  cut-outs, and 134,835 programs died of them. 7.6 names the risk: a
  fresh replicator alone on a P core dies at its first cut-out.
- **The plan's fallbacks** (section 8):
  - the pause switch (2.5), already built;
  - a milder supply. That changes agreed numbers, and it weakens the
    pressure that 1 needs.

**Proposal:** no change yet. The rerun after 1 judges the regression
afresh. If it fails again, rerun the same seeds with the pause switch on,
as 7.6 says, before considering a milder supply.

## 3. Cold starts come in clusters

- There were 24,211 cold starts, about 25 a place a seed. Section 7
  expected about 7. Yet 12.5% of counted windows had one, close to
  section 7's 1 in 10.
- **The likely cause, untraced:** a deep partial drop-out. Every device
  cuts out, the place draws nothing, its voltage climbs back, the devices
  restart, and the place sags below the threshold again. Real hardware
  does this too: a device on a weak supply can get stuck in a brown-out
  reset loop.
- It doesn't change the arithmetic in 1, which counts windows with a cold
  start in them, not cold starts.

**Proposal:** trace one from a snapshot while d is built, and record it.
No change unless it's a bug.

## 4. The pass mark stays

- 14.1 sets each gate's pass mark before the first run, and section 7
  says changing one means rerunning. Lowering it now, with the result in,
  would choose the answer.

**Proposal:** the rerun is judged against the same mark, the floor and the
regression included.

## The next steps, if all the proposals are accepted

1. **Record gate 2's first answer in design.md (9.4, 14.1) and the log:**
   no, not by the physics as written. Frozen places end only at cold
   starts.
2. **Build d behind a switch.** The cause of death is memory. Tests:
   - a sleeping thread whose saved state takes a flip ends;
   - one whose state a program overwrites doesn't;
   - a thread alone on its core is exempt;
   - with the switch off, the steady-supply equivalence and the hashes are
     unchanged;
   - determinism across thread counts and snapshots.
3. **Trace one cold-start cluster** (3).
4. **A pilot:** seeds 1 to 10 to 5 million ticks under gate 2's world with
   d on, against `runs/gate2-1`'s same seeds to the same ticks. It
   compares turnover, counted and crowded windows, births and the stages
   so far. About 2 to 3 hours.
5. **If the pilot bears it out,** d becomes the rule (9.4, appendix B), and
   gate 2's runs are rerun in full (`runs/gate2-2`) against the same mark:
   about 11 hours.
6. **If the regression fails there,** the same seeds with the pause switch
   on.

## Decisions

Damian accepted all four proposals on 2026-11-15:

1. **d, behind a switch for a pilot:** `os.saved_state_fails`, off by
   default. It becomes the rule only if the pilot bears it out. The reserve
   lever stays in reserve.
2. **No change for the origin regression yet.** The rerun judges it. If it
   fails again, the same seeds are rerun with the pause switch on before a
   milder supply is considered.
3. **One cold-start cluster is traced and recorded,** with no change unless
   it's a bug.
4. **The pass mark stays.**

The next steps above follow in that order.
