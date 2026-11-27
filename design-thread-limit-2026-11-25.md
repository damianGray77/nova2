# A thread limit that varies by place, and a lower flip rate: a proposal (2026-11-25)

Damian, 2026-11-25: "there has to be a mechanic we can use that should
balance things to ensure that there is no one king". On the limit below:
"I like your suggestion and I think it's better than mine." On the flip
rate: "The point was supposed to be allowing mutation, not actively
culling". Then: "Yes, write it up".

This is Claude's proposal for Damian to decide. Nothing in design.md or
the code changes until he does, and `runs/open-5` is not touched. Each
choice has a lean; the decisions Damian needs to make are listed together
at the end (section 8).

## 1. Where things stand

- **Threads flood.** `runs/open-5` at tick 22.5 million: about 3.3 million
  threads, about 140,000 programs and about 3,600 cores. That is about 900
  threads a core and 23 a program. A thread's turn comes about every 230
  ticks.
- **A few programs hold most of them.** In the save at tick 19,492,800,
  programs with 20 or more threads were 5.6% of place 0's programs and
  held 75% of its threads; in place 50, 14% held 79%; in place 24, 1.6%
  held 36%; in place 80, 2.5% held 25%. Over 99% of all threads are ready
  and waiting; none is lost or leaked.
- **Why it pays.** 9.5 shares a core by thread, and a thread costs 64
  bytes and one SPAWN. The proposal of 2026-11-10 said the cost "has to be
  real, or programs would spawn endless threads"; at 64 bytes it isn't.
  The out-of-memory killer (2026-11-20) ends one program a crowded place a
  day, which is too slow against hundreds of flooders a place.
- **Bit flips cull as well as mutate.** open-5 runs `noise.cosmic` at 1e-6
  a bit a tick, 1,000 times the engine's default. From its own report,
  ticks 3.67 to 4.62 million: 55% of all threads started were ended by a
  flip in their saved state, and 46% of program deaths were memory failing
  under the program (faults: 47%).
- **Genome length may be capped by the flips** (arithmetic that fits, not
  tested). A 27-byte genome takes a flip about every 4,600 ticks, about
  one a lifetime. A 100-byte genome would take about five. The leading
  genomes are 26 to 29 bytes. Most of a lifetime is waiting for a turn, so
  the flood makes this worse.

## 2. The proposal: a thread limit as a third OS value

**Each place's OS has a limit on the threads one program may have.** It is
set when the place boots and never changes, like the page size and strict
or lax (9.6). Some places allow one thread a program, some a few, some a
few dozen, some any number.

- **The rule.** A SPAWN that would start a thread (into an allocation the
  caller's program is running on, or a live thread's stack) fails when the
  program already has as many live threads as the place's limit. It fails
  as any SPAWN fails today: nothing is started and nobody is ended.
- **Births are untouched.** A SPAWN that gives an allocation to a new
  program works as now. A new program starts with one thread, and the
  limit is never under one.
- **Everything else in 9.5 stays.** Each thread still gets a fair share of
  its core in plain round robin. "The main lever is the number of threads"
  stays true, up to the place's limit.
- **What each kind of place favours:**
  - **One thread:** every program gets one turn in the rotation, so making
    working programs is what pays. A program has no spare thread, so its
    first saved-state flip ends it.
  - **A few (4):** threads buy time and insurance, within a small bound.
  - **A few dozen (32):** room for a program built of many threads, without
    floods of thousands.
  - **No limit:** today's rule.
- **The setting.** `os.thread_limits`, a list like `os.page_sizes`, with 0
  for no limit. The values are dealt to the places by a field of their
  own, as the page size is (7.3), so neighbours are alike with barriers
  between. The default is `0`: every place unlimited, and the world is
  today's, byte for byte.
- **For the pilot:** `os.thread_limits = 1 4 32 0`, a quarter of the places
  each.
- **A saved world** with programs over a limit keeps those threads until
  they end, as `os.stack_held` does for its old state.

**Real anchor.** Every real machine has such a limit, set by whoever runs
it and different from machine to machine: `ulimit -u` for a user, and
`pids.max` for a group of processes. Linux's own default scales at 1,024
thread ids a CPU and never under 8 (kernel source, `threads.h` and
`pid.c`, read 2026-11-25).

**It passes 9.1 as first written.** It is a fixed rule, the same for every
program in the place, and it mentions only the asker. No amendment is
needed, and the choice of 2026-11-20 is not reversed.

## 3. Why this and not the others

- **One limit everywhere** crowns one king. With a low limit everywhere,
  the only way to more core time is more programs, so copying fast is all
  that pays (Damian: "that doesn't sound hopeful to me"). With none, it is
  starting threads, as now.
- **A share of cores by program** gives each program the core time a
  limit of one everywhere would, so it crowns the same king, and it
  rewrites the waiting lines that optimization passes 3, 4 and 13 tuned.
- **32 threads a core** (a limit on the place) makes thread slots the
  scarce thing. A full place refuses every SPAWN, births included, and the
  killer doesn't see it, since the place isn't short of memory.
- **Variety** is how real ecosystems avoid one winner: different ground
  favours different ways of living. Nova 2 already does this for hardware
  and for the OS's other two values.

## 4. The flip rate

- **The rate is one number with two effects.** A flip in code is a
  mutation. A flip in a thread's saved state ends the thread, by the rule
  gate 2 adopted so that full places turn over (`os.saved_state_fails`).
  That rule stays.
- **Lowering the rate alone feeds the flood.** At a tenth of the rate,
  threads die a tenth as often. So it is proposed only together with the
  limit.
- **For the pilot:** the rate as it is, 1e-6, against 1e-7, each starting
  at ten times its floor as open-5 does. At 1e-7 a 270-byte genome has the
  exposure a 27-byte one has today.
- **No number is proposed as the new default yet.** The pilot is what
  tells whether longer genomes hold at the lower rate and whether places
  still turn over.

## 5. What it costs in the engine

- One comparison when a thread is started (about 1,600 SPAWN calls a tick
  in open-5, against about 180,000 instructions), and one value a place.
- The scheduler, the waiting lines and the thread records are untouched.
- The build session builds it. With the default the world must come out
  byte for byte the same (the optimization passes' checks), and the speed
  is measured before and after.
- Fewer threads should mean less bookkeeping and less memory. That is
  expected, not measured.

## 6. The pilot

- **Two runs,** seed 20261122 on open-5's settings, with
  `os.thread_limits = 1 4 32 0`:
  - **A:** `noise.cosmic` 1e-6 (open-5's).
  - **B:** `noise.cosmic` 1e-7, starting at 1e-6.
- **Each world is its own comparison:** the four kinds of place sit side
  by side on the same seed, the unlimited ones as the control. open-5
  itself is the control for the whole world.
- **Length:** 5 million ticks each, longer if the picture is still moving.
  In open-5 the floods showed by tick 2 million.
- **Run one at a time beside open-5,** each in its own folder under
  `runs/`, started only when the machine has room. If memory runs short,
  the pilot is stopped, not open-5.
- **Read, by kind of place:** programs; threads a program; births and
  faithful copies; genome length of the leaders and of all the living;
  lifespans; deaths by cause; births of empty programs; which lineages
  lead and whether they differ by kind of place; arrivals from one kind
  into another. For the whole world: ticks a second and memory.
- **What would bear it out:**
  1. threads a program fall where there is a limit, and the world's thread
     count falls;
  2. the kinds of place end up led by different lineages or different
     ways of living;
  3. in run B, longer genomes hold, and places still turn over;
  4. limit-one places don't fill with empty programs;
  5. speed and memory are no worse.
- **Then** a report to Damian. The next open run's settings are decided
  from it, not before.

## 7. Risks

- **Each place still has a local king.** Variety stops one king across
  the world, not within a place. Within a place the checks are the killer
  and whatever enemies evolve.
- **The flood may re-form as programs** where the limit is low: an empty
  program costs only a tenth more than a thread (ALLOC 2,000 cycles and
  SPAWN 20,000). One lineage in open-5 already launches empty programs.
- **Limit-one places may be thin.** With no spare thread, every program
  there ends at its first saved-state flip.
- **At the lower rate, unlimited places may flood worse** and turn over
  less. The pilot's unlimited places show this.
- **Life may not differ by kind of place.** Then the limit only caps
  floods, which is still worth having, but it is not a balance.
- **Results won't compare with the gates' marks.** Those were got with no
  limit; with the default they stay reproducible.

## 8. Decisions for Damian

Each has Claude's lean. "Yes, as leaned" to the lot is enough to start
the build.

1. **The rule:** a limit on threads a program, as a third per-place OS
   value, off by default. Lean: yes.
2. **The limits for the pilot:** 1, 4, 32 and none, a quarter of the
   places each. Lean: yes.
3. **How places get them:** dealt by a field of their own, as the page
   size is. Lean: yes.
4. **The lower flip rate to try:** 1e-7 against open-5's 1e-6, tried only
   with the limit. Lean: yes.
5. **Can programs read the limit with SENSE,** as they read the page size?
   Lean: not in the pilot. A program learns it when its SPAWN fails, as a
   real one does, and a new reading would have to be kept out of old
   worlds.
6. **The pilot:** two runs of 5 million ticks, one at a time beside
   open-5. Lean: yes.

## 9. What was decided (2026-11-25)

- **The six decisions:** Damian, "Yes, as leaned". The rule is in
  `design.md` 9.5 and 9.6, with an entry in `design-history.md`.
- **The pilot changed the same evening.** Damian: "let's keep OpenFive
  running in the background while you make the changes for the threads. I
  guess that'll be for Open6, and once that is done, we can switch over to
  Open6 and stop OpenFive." Pilot A's settings are what open-6 would be, so
  open-6 itself is the first pilot ("Your recommendation sounds good to
  me"):
  - `runs/open-6` starts with `os.thread_limits = 1 4 32 0` on open-5's
    settings and seed, open-ended, on one build from tick 0. open-5 stops
    then.
  - Pilot B (`runs/limit-b`, `noise.cosmic` 1e-7 starting at 1e-6) runs
    beside open-6 for 5 million ticks.
  - A report at 5 million ticks reads both by kind of place. If it says the
    limits or the rate should change, open-6 starts again.
- **The deal on this seed:** 28 places get a limit of 1, 32 a limit of 4,
  20 a limit of 32 and 20 none, the rest of each place as in open-5.
- **Started (2026-11-26):** the build with the limit passed its checks
  (with no limit set its world is the earlier build's byte for byte; with
  the list above, every place's cores, memory, strictness and page size
  are unchanged). open-6 began from tick 0 at 01:53 and open-5 was saved
  at tick 37,658,600 and stopped at 01:54. Pilot B began at 01:55.
  - The limit is dealt by a field of its own, `threads`, as the page size
    is. That adds two settings at the field family's defaults:
    `map.threads.edge` (1) and `map.threads.sharpness` (8).
  - Refused SPAWNs are counted in each place's counters and shown in the
    report, by limit. They are not a column in the samples.
- **After the report (2026-11-26):** `thread-limit-pilot-report-2026-11-26.md`
  reads both runs at 5 million ticks against section 6's five tests.
  - Test 1 holds: threads a program fell wherever there is a limit.
  - Test 2 does not: one lineage of one-thread copiers leads places of
    every kind.
  - Test 3 holds for turnover and not for length: the lower rate did not
    give longer genomes. Almost none passes 127 bytes in any run, the
    most one instruction's constant can hold.
  - Test 4 was not measured directly.
  - Test 5 holds, nearly: speed and memory are close to open-5's.
  - Damian, asked whether open-6 should keep running as it is: "Yes, keep
    it". open-6 runs on with these limits at open-5's flip rate. The
    lower rate is not taken up, and the limit stays off by default.
