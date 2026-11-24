# Nova 2 — Gate 1 build plan

> **Status (2026-11-11):** a draft for Damian. Gate 1 asks one question:
> does a replicator arise from static? (design 10.6). This plan says what is
> built, in what order, how each step is checked, and what must be fixed
> before the first origin run. Numbers in brackets, like (9.3), are sections
> of `design.md`, which stays the authority. When the two disagree, the
> design wins and the plan is fixed.
>
> **Approved 2026-11-11:** the three design fixes (2), the toolchain and the
> parts taken from Nova 1 (4). The numbers in 7 are agreed later, before the
> first origin run.
>
> **Progress:** steps 1–12 built and tested (2026-11-11), so gate 1's build is
> done. What's left is agreeing the numbers in 7, then the real origin run.
> Results so far are in 11.

## 1. What gate 1 delivers

- **The origin test's answer** (10.6): stages 1–5 for each seed, within the
  budget, against the pass mark, with:
  - distinct paths tried per second, so replays can't pass for search;
  - the region measures (compressibility, R4);
  - deaths by cause;
  - all of it split by OS type (strict or lax, page size), since a strict
    place's shortest replicator is 24 bytes and a lax one's 18.
- **The side measurements** (14.1): whether faults are too harsh on the
  first, sloppy replicators; whether the reset region keeps changing; the
  cost of the per-access counters.
- **The first benchmark** (2): the interpreter on one place, its scaling
  across host cores, and the per-access counters' cost.
- **An engine later gates build on,** deterministic across thread counts
  and through snapshot and resume (principle 20), with the facts log and
  observers kept apart from the world (13.1).

## 2. Three design fixes found while planning

These need Damian's yes before the code follows them. Each is applied to
`design.md` and logged in `design-history.md` once agreed.

1. **The scheduler moves into gate 1, with every call that works inside one
   place on a steady supply.**
   - The problem: the origin test's first stage needs a SPAWNed program to
     run and copy itself, but the gate table (2) builds the scheduler at
     gate 2.
   - The wider point: if gate 1 stubs a call that is live later, the origin
     test answers a different question from the one the real world asks.
     For example, SETCLOCK costs 10,000–100,000 cycles, so random code that
     hits it loses much of a run's cap; a cheap stub would make origins
     look easier than they are.
   - So gate 1 builds:
     - the scheduler: round robin in rotating quanta, turns, SLEEP and
       yielding, SETCORE (9.5);
     - SETCLOCK's effect on speed and errors (its draw and heat come with
       gate 4);
     - handlers for faults and timers, and HRET (handlers for arriving data
       come with ports at gate 3; low voltage at gate 4);
     - the raw disk: DREAD and DWRITE at the tick's barrier (9.3). Idle runs
       may DREAD, so static on the disk is a second source of new bytes for
       origins.
   - Gate 2 keeps memory fade, cut-outs, the crash rule and the varying
     supply, and gains the boot block's cold start, which needs a voltage
     that can fall.
   - SEND, RECV and PING still wait for the network (gate 3). Until then
     they behave as at a place whose network device is off: SEND and PING
     fail, and RECV waits on inboxes that never fill. They charge their
     real costs.
2. **SYS can't give ALLOC and SPAWN extra slots as written.**
   - 9.2 says the numbering's defaults give ALLOC and SPAWN extra slots,
     and 10.6 names those slots as the first lever if origins fail. But
     appendix A takes the call number modulo 16, with 16 calls, which
     leaves no room.
   - The fix: SYS s picks slot (s modulo the slot count), and a numbering
     table maps slots to calls. Both are settings, fixed for the run.
   - Default: 18 slots. Slots 0–15 are the calls in 9.2's order, slot 16 is
     a second ALLOC and slot 17 a second SPAWN. The lever adds more.
   - The traced replicators (A.5) are unchanged, since they use slots 0, 2
     and 3.
3. **SENSE with an index into an empty list.**
   - A.4 takes the index modulo the count it refers to. A place with no
     neighbours (every place until gate 3) makes that a division by zero.
   - The fix: an index into an empty list reads 0.

## 3. The gate 1 world

- **Places with no links.** A seed's world is a number of places (a
  setting, 48 proposed) that share nothing. Each is its own soup, and the
  places run in parallel on host threads. That gives the benchmark its
  scaling measure, and each seed many independent origins for the same
  budget.
- **A steady supply.** One voltage, constant, set so every device sits in
  mid-range. Every device is on at its stock speed, edge errors are
  negligible, and only the floors act.
- **Hardware drawn per place** from the catalogue's ranges (7.2, 7.3), with
  no map fields yet: 2–8 cores, P and E; 2–4 memory devices, about 160 KB
  in all, each with its own latency (+4 to +12 cycles); about 1 MB of disk.
  The catalogue is built with all its fields, though draw, heat and wear
  aren't used until later gates.
- **OS values drawn per place:** a page size from the list, and strict or
  lax.
- **Not built at gate 1:**
  - the map, links and the network (gate 3);
  - the voltage solve, heat, the day and sources (gate 4);
  - memory fade, cut-outs and the boot block's cold start (gate 2);
  - wear, visits, regional events and outages (gate 5);
  - rowhammer's flips (gate 5). Its per-access counters are built behind a
    switch, for the benchmark only;
  - the GPU and NPU (parked).

## 4. The code: layout, toolchain and what comes from Nova 1

### 4.1 Layout and toolchain

- **The same layout as Nova 1:**
  - `core/`: the engine and observers, a static library;
  - `app/`: the executable (the headless driver now, the viewer later);
  - `tests/`;
  - `tools/`: build scripts.

  The namespace is `nova2`. `E:\Projects\nova2` becomes a git repository at
  step 1, with the design files as its first commit.
- **The toolchain is Nova 1's:** MSVC, with CMake and Ninja from the Visual
  Studio install, and the Windows SDK. C++20, `/W4 /permissive-`.
  - These are installed tools, so under the dependency rule (2) they need
    Damian's yes for Nova 2, even though Nova 1 uses them.
  - Nova 1's flags leave exceptions and RTTI on (CMake's defaults). Nova 2
    turns both off (`/EHs-c- /GR-`), so the compiler enforces the code
    rules.
  - Nova 1's no-STL guard (`tools/check_includes.cmake`, a CTest entry)
    comes too, with the same allowed headers.

### 4.2 What comes from Nova 1

From a survey of Nova 1's code. Each item is still confirmed one by one, as
2 says; this table is Claude's recommendation for each.

| Part | Nova 1 files | Lines | Recommendation |
|---|---|---:|---|
| Build and test harness | `CMakePresets.json`, `.claude/verify.ps1`, `tools/build.ps1`, `tools/check_includes.cmake`, `tests/check.h`, the suite table in `test_main.cpp` | about 500 | **As is,** without the GPU build steps (the shader compiles and Direct3D libraries) |
| Containers | `types.h`, `array.h`, `hashmap.h`, `str.*`, `io.*`, `fatal.cpp` | about 900 | **As is,** after removing one lambda (`array.h`). They hold plain data only, which suits the engine style |
| Hash and RNG | `hash.h`, `rng.h` | 145 | **As is,** with a purpose key added to the RNG's stream (seed, place, purpose, counter) |
| Thread pool | `pool.*` | 235 | **As is.** Its `run()` is the tick's barrier, and its dispatch-order hook serves the determinism tests |
| Settings registry | `keys.*`, `config.*` | about 1,300 | **The mechanism, emptied.** Nova 1's 316 entries and groups go. New constraint checks are written, since Nova 1's are tied to its world |
| Snapshot container | `snapshot.*` | 155 | **Small edits:** it takes a seed, a tick and a payload instead of Nova 1's world. The world section is new |
| Replay log | `replay.*` | 395 | **As is** |
| Determinism tests | the structure of `test_gate.cpp` | about 100 | **The structure,** with new worlds |
| Compressibility | `compress.cpp` | 104 | **As is.** It's the region measure (R4) |
| Run driver | the generic commands in `app/main.cpp` (run, resume, replay, outputs, stop file) | about 600 | **Extracted.** Nova 1's `Run` is tied to its detectors and budgets, so it's rewritten |
| CSV stats | `stats.*` | 883 | **The pattern only.** Its columns are Nova 1's |
| Analysis maths | `core/src/learn/` (PCA, k-means, n-grams, alignment, tree building) | about 1,200 | **Later, as needed.** Generic, so it lifts as is |
| Settings search | the skeleton of `analysis/search.cpp` | about 500 | **Later,** if tuning by hand fails (appendix D) |
| UI frame and widgets | `widgets.*`, `theme.h`, `png.*`, the window code in `view_cmd.cpp` | about 1,600 | **Step 12.** Small to moderate edits |
| Setup page | `setup.*` | about 2,400 | **Step 12.** Its controls are generated from the registry and carry over; its check panel is Nova 1's and is rewritten |
| Canvas | `canvas.*` | 348 | **Replaced** by Direct2D (2). Its small interface is kept |
| Viewer and view layer | `viewer.*`, `view.*` | about 6,000 | **Rewritten,** as 2 says |

About 4,000 lines lift for the engine and tests, and about 4,000 more for the
UI at step 12.

## 5. The order of work

Each step ends with tests that pass before the next starts. "Done when"
names the check.

**Step 1. The skeleton.** From Nova 1 (4.2): the CMake presets, the build
and verify scripts, the test harness and the no-STL guard, with exceptions
and RTTI turned off.

- Done when: an empty engine library, a headless driver and a test runner
  build with one command, and one trivial test passes.

**Step 2. Foundations.** From Nova 1 (4.2): the containers, the hash, the RNG
and the settings registry's mechanism.

- New: the constraint checks between settings (2), and Nova 2's settings
  themselves, added step by step as each mechanism is built.
- RNG streams are keyed by (seed, place, purpose, counter), so a place's
  randomness never depends on which host thread runs it.
- Done when: the registry refuses a world that breaks a constraint (for
  example, a reset region bigger than its device), and RNG streams give the
  same numbers in any order of use.

**Step 3. Memory (6.3, 9.3).**

- Memory devices laid end to end in one flat address space, with page 0
  reserved. Device ranges never move.
- Pages by size class and permission; first fit within a class; allocations
  that span pages for large sizes.
- Access by page, ownership by allocation. A program (or idle run) may touch
  any byte of a page where it holds an allocation, within the page's
  permission. Losing the last allocation in a page loses access.
- PROTECT's whole-page rule; strict and lax places.
- Nothing is zeroed.
- An address-to-allocation lookup in constant time: a map from every 8-byte
  granule (the smallest class) to its allocation, so no division. The genome
  rule (13.2) needs this on every write.
- The reset region's pages, and setup refusing a region that doesn't fit its
  device (step 2's constraint, which needed these settings to exist).
- Done when: allocator tests pass, and an invariant checker (every
  allocation has exactly one holder or is free; free lists and page
  permissions agree) passes after a million random ALLOC, FREE and PROTECT
  calls.

**Step 4. The interpreter (8, appendix A).**

- Decoding by table: 256 byte values, each an instruction and an operand.
- The 22 instructions exactly as in A.2: wrapping arithmetic, DIV by 0
  giving 0, sign-extended byte reads, relative jumps from the instruction's
  own address, CPY checked whole before it copies.
- Faults for memory and stack (8.3, A.2).
- Costs in sixteenths of a cycle: the instruction's work, the fetch latency
  of the device it lies in, the operand's device latency, and the P and E
  tables (8.4, 7.2).
- A hook for every result (A.2's results, and every byte CPY moves), for the
  error floor in step 8.
- Done when: every row of A.2 has a test, including the edge values (0, −1,
  the smallest and largest numbers), and the cycle cost of each instruction
  matches 8.4 on both core kinds.

**Step 5. The OS (9.1–9.4, A.3, A.4).**

- All 16 calls, with A.4's argument rules: every 32-bit value means
  something.
- Programs, threads and stacks: the stack allocation at SPAWN, saved
  registers at its base, SPAWN's gift rule (own code is the allocation
  holding the caller's IP), a program ending with its last thread and its
  allocations freed with their bytes kept.
- Calls whose mechanism comes at a later gate behave as they will at a
  place without it: SEND and PING fail as when the network device is off;
  RECV waits on inboxes that never fill. They still charge their real costs.
- Done when: test worlds run the traced replicators (A.5) by hand placement
  (allowed in tests, R7) and match the trace register by register, child
  included; the out-of-memory death happens as A.5 says; and every A.4 row
  has tests for its edge values.

**Step 6. The scheduler and interrupts (9.5).**

- Round robin in rotating quanta; turns of several quanta; threads moved to
  idle cores; overruns from long CPYs repaid next turn.
- SLEEP (ticks, until an interrupt, yield), SETCORE's strict pin, SETCLOCK's
  per-turn clock (speed and errors; draw and heat wait for gate 4).
- Handlers for faults and timers, and HRET (A.3).
- Done when: a test world with threads of known costs gets exactly the
  cycles each should, on mixed P and E cores, and handler entry and HRET
  round-trip the registers.

**Step 7. The reset region and idle runs (10.2, 10.3).**

- The region on the device with the lowest threshold; idle cores running it
  from stepping start spots, offset per core; registers set at each start;
  no stack; the restricted calls; the cap.
- Halting at idle after a fault or the cap, and waking on a write to the
  region or a slow retry.
- Holdings freed when a run ends; a SPAWN from a run creating a program with
  the cause "static".
- The raw disk: DREAD and DWRITE completing at the tick's barrier, in core
  then request order (9.3). Idle runs may DREAD, not DWRITE.
- Done when: a test world shows a hand-placed replicator in the region
  being found by an idle run, giving a program whose cause is "static"; and
  an empty place's cores halt and wake as 10.2 says.

**Step 8. The noise floors (7.5, appendix C).**

- The error floor: each core counts down "results until the next wrong one"
  drawn from a geometric distribution, then flips a random bit of that
  result. SETCLOCK above stock raises the rate.
- The cosmic-ray floor: a count of flips per memory device per tick, then
  their positions.
- Done when: measured error rates over many results match the settings
  within statistical bounds, and a rate of 0 gives none.

**Step 9. The tick, places in parallel, determinism and snapshots.**

- The tick loop (appendix C, gate 1's part): execution in rotating quanta,
  the barrier (disk I/O), the floor flips, the facts and samples.
- Places on a thread pool. At gate 1 places share nothing at all.
- The world hash, and snapshots that save and resume a world exactly.
  Nova 1's thread pool, snapshot container and thread-count test carry
  over (4.2).
- Done when: the same seed gives the same hash on 1, 2 and 24 host threads,
  after a snapshot and resume, and with observers on or off (13.1).

**Step 10. Facts and observers (13.1–13.3, gate 1's part).**

- The facts log: births (parent, cause, the written extent and its hash, the
  birth image's hash) and deaths (cause), plus counts per sample.
- The genebank; lineage by descent; generation depth.
- The region measures (R4): each reset region's compressibility per sample,
  by Nova 1's compressibility estimate (4.2), and distinct paths tried:
  each idle run's executed bytes hashed as it goes, counted once per new
  hash in a bounded table.
- Genome groups by shared 3-byte runs (13.2), for stage 5.
- Detectors: Replicator, Scavenger, Hijack and cross-owner execution (13.3).
- Done when: on a test world with a hand-placed replicator, the log
  reconstructs the family tree exactly, and generation depth and genome
  groups come out as expected.

**Step 11. The origin test and the first benchmark.**

- A runner for many seeds, each with its budget, reporting stages 1–5 per
  seed and per OS type (strict or lax, page size), the region measures,
  distinct paths per second, and deaths by cause (10.6).
- The benchmark (2): instructions per second on one place, scaling from 1
  to 24 host threads, and the cost of the per-access counters, built behind
  a switch for this purpose.
- Done when: a dry run with a high error floor (where static changes fast)
  runs to its budget without errors, and the report reads correctly.

**Step 12. The gate 1 views (13.4).** Nova 1's frame, widgets and setup page,
with the canvas replaced (4.2).

- Direct2D and DirectWrite on screen, WIC off screen (2).
- The place inspector and the memory strips: pages by owner, lineage,
  permission and size class, with the reset region marked.
- Charts: programs and threads, births and deaths by cause, distinct paths
  tried.
- Done when: a headless render through WIC matches a stored image pixel for
  pixel, and the views open on a saved snapshot.

## 6. Tests that run throughout

- **Unit tests** for each step, as named in its "done when".
- **Test worlds** with hand-placed programs (R7). They check mechanics,
  never origins:
  - the traced replicators, register by register;
  - a crowded place, for the allocator and the scheduler under load;
  - snapshot and resume in the middle of a CPY, a sleep and a handler.
- **A fuzz world:** places full of static, run for many ticks with the
  invariant checker after every tick (in the checked build). It passes if
  the engine never crashes and no invariant breaks. This is the closest
  thing to the origin test's own conditions.
- **Determinism checks** on every change to the engine: the same hash on 1,
  2 and 24 threads, through a snapshot, and with observers on or off.
- **The checked build and the fast build** run the same tests. The checked
  build adds the invariant checker and bounds checks; the fast build is
  what the benchmark and the origin test use.

## 7. Numbers to fix before the first origin run

All are placeholders, proposed here and agreed with Damian before the first
run, as 10.6 requires. Changing them afterwards means rerunning.

**For the first run (2026-11-11).** Damian chose to run with these and tune
from the results, rather than settle every number in advance. Four
recommendations were taken:

- **The budget is in ticks:** 10 million ticks a seed. An instruction
  budget would make a seed where life appears run far longer, because
  programs spend most of their time paying for SPAWN.
- **The retry is 1 tick.**
- **The error floor stays at 10⁻⁵.**
- **The seeds run side by side,** 10 at once on 2 host threads each.

The rest are as below. The run's settings are in `runs/origin-1/origin.cfg`.

| Number | Proposed | Why |
|---|---|---|
| Budget per seed | 10¹² instructions — **replaced by 10 million ticks** | 10.6; about Nova 1's 12-hour trial |
| Seeds | 10 | 10.6 |
| Pass mark | Stage 4 in at least half the seeds | 10.6 |
| Places per seed | 48 | Two per host thread on a 24-thread machine. About 2 × 10¹⁰ instructions, or roughly 1.7 million ticks, per place |
| Stage 3: generations | 10 | 10.6's example |
| Stage 4: establishing | The lineage lives at its place for 100,000 ticks without a gap, and holds at least 10% of the place's programs at the end. **Tightened after run 3 (section 11):** it holds its place for 100,000 ticks without a break, with 20 living members or more, at least half of them born as copies, and at least 10% of the place's programs | Long against a birth (about a tick), short against the budget |
| Stage 5: adapting | At least 3 new genome groups arise within the lineage while its place is full (free memory below 10%), counted from when it reaches stage 4 [2026-11-12, after run 4, agreed with Damian] | 13.2's groups |
| Reset region | 1 KB | Appendix E |
| Idle-run cap | 100,000 cycles | Appendix E; above the dearest birth (about 28,000 cycles with the costs below) |
| Start-spot step | 13 bytes, each core offset by an equal share of the region | Any odd step visits every byte of a 1 KB region |
| Slow retry | Every 100 ticks — **measured at step 9: too slow; propose 1 tick** | Static faults within a few instructions, so the retry sets the search rate. In 48 places over 200 ticks: 820 idle runs and 278,000 instructions at 100 ticks, against 47,000 runs and 15 million at 1 tick. At gate 1 nothing draws power, so a halted core saves nothing yet |
| Call slots | 18 (fix 2) | ALLOC and SPAWN get one extra slot each |
| Stack | 64 bytes | Appendix E |
| Strict places | Half | Both kinds tested equally |
| Page sizes | 64, 256 and 1,024 bytes, equally often | Appendix E |
| Error floor, per result | 10⁻⁵ | About 2 × 10⁻⁴ errors per copy of an 18-byte replicator: well under Eigen's threshold (7.5), and enough to mutate |
| Cosmic-ray floor, per bit per tick | 10⁻⁹ | About one flip per 800 ticks per place, and one per 120,000 ticks in a 1 KB region |
| SYS crossing | 150 cycles | 8.4 |
| Service costs | ALLOC 2,000; PROTECT 5,000; SPAWN 20,000; SETCLOCK 30,000; "a few hundred" as 300 | 9.7's ranges |

## 8. Risks for this gate

- **The origin test may fail.** That's why it's first. If it does, the
  levers come in 10.6's order: call slots, the region's size and step, the
  floors, the argument rules, the instruction set. Then the design is
  revisited with Damian.
- **A pass on unlinked places isn't a pass for the full world.** Gate 3
  adds arrivals, which change what reaches a reset region. The origin test
  is rerun at each later gate as a regression check, not only once.
- **Observer costs inside the interpreter.** The path hash runs on every
  idle-run instruction, and the genome rule needs an allocation lookup on
  every write. Both are measured in the benchmark, with observers on and
  off.
- **The viewer is the largest single piece** (about 5,000 lines to rework,
  2). It comes last in this gate, so it can't delay the origin answer. The
  Direct2D drawing layer can be built alongside the engine if wanted.
- **Placeholders that look like results.** The numbers in 7 are guesses.
  The report states which values a run used.

## 9. Size

Claude's estimate, not a measure: about 7,000 lines of engine, observers and
runners, and 3,000 lines of tests, before the viewer. About 4,000 of those
10,000 lift from Nova 1 (4.2), so about 6,000 are new.

## 10. After gate 1

- **A pass:** gate 2 (does a full place keep turning over?), on the same
  engine, adding fade, cut-outs, the crash rule, a varying supply and the
  boot block.
- **A fail:** the levers in 8, each tried with the same seeds and budget,
  then a design review with Damian.
- Either way, the result, the numbers used and the benchmark go into
  `design-history.md`'s log.
- **Wanted later (Damian, 2026-11-11): periodic snapshots during a run.**
  Long runs, the origin test's included, save each world every so often,
  so the viewer can open any seed at its latest point rather than compute
  it again from tick 0.

## 11. Results so far

**The first benchmark** (`nova2 bench`, a release build, 24 host threads,
48 places, 2026-11-11):

| Workload | One place, one thread | 48 places, 24 threads | Scaling |
|---|---:|---:|---:|
| Loops: a counting thread on every core | 85 M instructions/s | 1,150 M/s | 14× |
| Static, idle runs retrying every tick | 71 M/s | 400 M/s | 6× |
| Replicators in every lax region | 150,000 ticks/s, 600,000 births/s | best at 8 threads: 16,000 ticks/s, 1.8 M births/s | 6× at 8 threads, less beyond |

- **The interpreter runs at about 12 ns an instruction** on one thread. That
  puts 24 threads at 2 to 4 times below Claude's estimate of 2–5 billion a
  second (2). There's room to make it faster; nothing has been tuned yet.
- **Worlds with little work per place per tick** (static, replicators)
  scale poorly. The per-tick synchronisation dominates, and adding threads
  past 8 slows the replicator world down. Bigger worlds should scale
  better.
- **The per-access counters cost under 1%,** within the noise. Rowhammer
  and memory draw can count every access.

**The origin test's dry run** (`nova2 origin`, 2 seeds, 5 × 10⁸ instructions
each, error floors of 10⁻³, a 1-tick retry; nothing seeded):

- **Each seed ran about 30,000 ticks** in about 2.5 seconds.
- **Random code made programs:** 59 in all, every one from static, and
  every one dead by a fault. None made a faithful copy, so no seed reached
  stage 1.
- **The idle runs:** 15.5 million, of which 1.9 million took distinct
  paths, and they made 330,000 SENDs.
- **The reset regions got more repetitive:** their compressibility fell
  from about 0.97 for static to 0.85–0.90.
- **What the real run will cost:** at about 200 M instructions a second
  for one seed, the planned budget of 10¹² instructions is about 1.4 hours
  a seed, or 14 hours for 10 seeds one after another. Static worlds use
  host threads poorly, so running several seeds side by side would be
  faster than giving one seed all 24 threads.

**The first origin run** (`runs/origin-1`, 2026-11-11): 10 seeds, 10 million
ticks each, 48 places a seed, side by side. 2.9 hours of wall time; about
6 × 10¹¹ instructions a seed. **FAIL: no seed reached stage 1.**

| Measure | All ten seeds |
|---|---:|
| Births (all from static) | 230,233 (seed 2: 1,174; seed 10: 88,738) |
| Faithful births | 0 |
| Deaths | 230,228 by faults, 4 by EXIT |
| Genomes banked | 1–4 a seed |
| Idle runs | 21.5 billion: 97% faulted, 3% capped |
| Distinct paths | 123 million (0.6% of idle runs) |
| SENDs from idle runs | 630 million |
| Reset regions' compressibility | 0.83 at the end (mean), 0.74 at the lowest |

- **Random code finds ALLOC and SPAWN easily.** 230,000 births is about one
  a place every 2,000 ticks.
- **It almost never copies anything in between.** Almost every birth's
  genome is empty: an idle run allocates, spawns into the allocation
  without writing to it, and the "program" runs whatever bytes were left
  there, then faults.
- **A copy needs two more things:** a CPY aimed at the new allocation (D
  set to ALLOC's answer), and code that finds its own address again once
  it's the child. An idle run starts with every register pointing at its
  spot; a child starts with them at 0.
- **The regions stagnate.** Under 1% of runs try a new path, and the
  regions grow repetitive. The cosmic floor flips a bit in a 1 KB region
  about once every 120,000 ticks, so it adds almost nothing.
- **One program lived:** in seed 3, a program that spawned into its own
  code over and over, ending with about 1,200 threads. Gate 2's "one
  program fills a place" risk, arising on its own.

**The lever trials** (`runs/pilot-1`, 2026-11-11 to 12). Six variants, each
8 seeds of 500,000 ticks side by side, compared on the copying ladder: the
counts that show progress before stage 1. Each variant changes the first
run's settings as shown.

| Variant | Births | Births with bytes written | Idle runs' CPYs into memory they held | Distinct paths | Region copies (8 bytes or more) | Compressibility at the end |
|---|---:|---:|---:|---:|---:|---:|
| a. as the first run | 6,944 | 4 | 9,465 | 10.4 M | 0 | 0.86 |
| b. 24 call slots (ALLOC and SPAWN four extra each) | 42,116 | 291 | 10,262 | 8.4 M | 0 | 0.88 |
| c. a 4 KB reset region | 8,520 | 14 | 4,585 | 123.6 M | 0 | 0.69 |
| d. floors 10⁻⁴, cosmic 10⁻⁶ | 7,423 | 3 | 8,659 | 67.8 M | 1 | 0.91 |
| e. new threads start with B, C, D at their start | 6,785 | 1 | 10,489 | 10.4 M | 0 | 0.86 |
| f. all of b to e together | 57,002 | 6,227 | 36,324 | 274.2 M | 15 | 0.87 |

- **Each lever does what it should, alone:** the extra slots give about 6
  times the births, and the bigger region and the noise give 12 and 6.5
  times the distinct paths. The new threads' registers do nothing alone.
- **Together they do more than their sum:** f made 8,337 births by
  programs (none of the others made any), 274 million distinct paths, and
  15 copies of region code.
- **No program in any variant copied 8 bytes or more into a child.** The
  rung between "an idle run makes a program" and "a program copies itself"
  is still missing.
- **f's stages 1 to 3 were an artifact,** found by reading the genomes:
  - First, an empty genome matched an empty parent's, so spawning without
    writing counted as faithful.
  - Then, in seed 7, from tick 445,600 in a lax place with 1,024-byte
    pages: 6,082 births in about 4,000 ticks, each a 4-byte allocation with
    one byte written (`MOV B`). At most 17 programs were alive at once, and
    every one died by a fault.
  - On a lax place a program may use every byte of a page it holds an
    allocation in. A 4-byte gift inside the parent's page gives the child
    the whole page, so the child runs its parent's code in place. Nothing
    is copied, but the one-byte genome matched the parent's.
- **The fixes:**
  - A faithful birth now needs a genome of at least 8 bytes
    (`observe.min_genome`).
  - The ladder counts births whose whole allocation is the parent's.
  - `origin.genomes` writes each seed's most common genomes, disassembled,
    with their allocation sizes.

**The second origin run** (`runs/origin-2`, started 2026-11-12): variant
f's settings at full length, 10 seeds of 10 million ticks, with the
8-byte rule.

**Lever 4, the calls' argument rules** (`runs/pilot-2`, 2026-11-12). An
experimental switch, `os.alloc_d`: a successful ALLOC leaves the new
address in D as well as A, so a CPY straight after it lands there. With new
threads starting with B at their start, the shortest copier is then 8
bytes: `MOV # n; SYS # ALLOC; CPY # n; SYS # SPAWN` (CPY leaves A alone).
Add 2 bytes for a loop. Under the switch, A.5's traced 18 bytes no longer
copy themselves. Same trial as before: 8 seeds of 500,000 ticks.

| Variant | Births | Births by programs | Idle runs' CPYs into memory they held | Region copies (8 bytes or more) | Programs copying 8 bytes or more |
|---|---:|---:|---:|---:|---:|
| a. as the first run | 6,944 | 0 | 9,465 | 0 | 0 |
| h. a, plus both register rules | 6,206 | 14 | 149,479 | 0 | 0 |
| f. all earlier levers | 57,002 | 8,337 | 36,324 | 15 | 0 |
| g. f, plus lever 4 | 84,187 | 36,799 | 695,697 | 408 | 0 |

- **Lever 4 does what it should:** it aims about 16 to 19 times as many
  CPYs at new memory, and g made 27 times f's copies of region code.
- **Still no copier.** g's copies of region code are random bytes. None
  holds ALLOC, CPY and SPAWN in order, so none copies itself again. Almost
  all of g's births by programs are the page-sharing kind: a gift with
  nothing written.
- **The narrow step is now SPAWN after the copy.** In g, 696,000 aimed
  CPYs gave 732 births with anything written.
- **ALLOC reads its permission from B mod 3, and B is also CPY's source.**
  So only a third of addresses give "both", a copy that can be written and
  run.

**The second origin run's result** (`runs/origin-2`, finished 2026-11-12,
2.9 hours): **FAIL**, no seed reached stage 4. Seed 5 reached stage 1 at
tick 4,089,048, in two places, with 3 faithful births in all; neither went
further. No other seed got past stage 0.

| Rung (all 10 seeds) | Count |
|---|---:|
| Births | 1,884,647 |
| Idle runs' ALLOCs that worked | 269,852,003 |
| Idle runs' SPAWNs that worked | 1,036,301 |
| Idle runs' CPYs into memory they held | 4,543,363 |
| Births with something written | 33,397 |
| Copies of region code (8 bytes or more) | 2,453 |
| Births by programs that wrote 8 bytes or more | 20 (4 at least half like the parent) |
| Births whose whole allocation was the parent's | 846,258 |
| Faithful births (8 bytes or more) | 3 |

- **Copying of region code grew over the run.** Per seed and tick, run 2
  made about 6 times pilot f's copies of region code.
- **In seed 5, place 27, the region grew copier code.** From tick 6.0
  million to the end, its idle runs made dozens of births with 19, 47 and
  70-byte genomes. Their code repeats ALLOC, `CPY # n` (n = the genome's
  length) and SPAWN, in short repeated units. Most of these births were by
  idle runs: all ten seeds had only 20 births by programs that wrote 8
  bytes or more.
- **One of those genomes is a working replicator under lever 4, and not
  without it.** Each seed's dumped genomes of 8 to 64 bytes (70 in all)
  were run as programs, with run 2's settings and then with `os.alloc_d`
  added, from a start at each address mod 3. This was a temporary test,
  not kept.
  - Under run 2's settings, 1 copies itself (seed 4, 18 bytes), and its
    child does not.
  - With `os.alloc_d`, 3 copy themselves. One, seed 5's 47-byte group 303,
    has a child that copies itself twice:
    `CPY # 47; SWP D; SWP D; SYS ALLOC; CPY # 47; ADD # 19; SWP D; SYS SPAWN; ...`
    Traced step by step, its sizes come by accident. Its first ALLOC asks
    for 0 bytes and fails. Its first SPAWN, aimed at its own start, starts
    a second thread, and that thread's id (2) becomes the next ALLOC's
    size. The address that ALLOC returns then becomes the size of the one
    after it. After each ALLOC, the switch puts the new address in D, so
    the `CPY # 47` that follows lands there. `ADD # 19; SWP D` moves the
    address back into A for SPAWN. Without the switch, D holds the start
    or a small sum, so the copies land on the code itself or fault.
  - It copies only from a start that is 2 mod 3 (ALLOC's "both"), so a
    lineage would last only by luck.
  - Pilot-2's variant g genomes (500,000 ticks) had none: in run 2 this
    took 6.8 million ticks to appear.

**The third origin run** (`runs/origin-3`, 2026-11-12, 2.7 hours): run
2's settings plus `os.alloc_d`, 10 seeds of 10 million ticks. **FAIL, but
close: stage 4 in 3 of 10 seeds** (the pass mark is 5), and two of those
went on to stage 5. A seed stops when it reaches stage 5.

| Seed | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 |
|---|---:|---:|---:|---:|---:|
| 1 | 3,093,100 | 3,093,100 | 3,093,200 | – | – |
| 3 | 6,822,698 | 6,822,698 | 6,822,700 | – | – |
| 4 | 1,444,047 | – | – | – | – |
| 5 | 1,559,367 | – | – | – | – |
| 6 | 4,251,880 | 4,251,883 | 4,251,900 | 4,331,400 | – |
| 7 | 2,610,839 | 2,610,839 | 2,610,900 | 8,596,400 | 9,093,700 |
| 8 | 3,009,458 | – | – | – | – |
| 9 | 1,292,656 | 1,292,669 | 1,292,800 | 2,221,400 | 8,203,200 |

Seeds 2 and 10 stayed at stage 0.

| Rung (all 10 seeds) | Run 2 | Run 3 |
|---|---:|---:|
| Births | 1,884,647 | 3,491,751 |
| Idle runs' CPYs into memory they held | 4,543,363 | 51,157,967 |
| Births with something written | 33,397 | 630,063 |
| Copies of region code (8 bytes or more) | 2,453 | 28,697 |
| Births by programs that wrote 8 bytes or more | 20 | 312,921 |
| Faithful births (8 bytes or more) | 3 | 305,890 |

- **Lever 4 turned copying on.** Faithful births went from 3 to 305,890,
  and nearly all births by programs that wrote 8 bytes or more were at
  least half like the parent (311,893 of 312,921).
- **Once a lineage starts, it goes deep fast.** Stages 1 to 3 (10
  generations) came within about 150 ticks of each other in every seed
  that reached stage 3.
- **Only lax places got past stage 1.** Lax places with 256 and 1,024-byte
  pages reached stage 5 and 64-byte pages stage 3. Every strict place
  stayed at stage 0. A strict place never gives "both" (memory that can be
  written and run), so a copy must be written and then PROTECTed before
  SPAWN, which is several steps more than the copier above.
- **Run 3 wrote no samples or genome dumps.** Their folders didn't exist,
  and `nova2 origin` skipped the files without a word. It now makes the
  folders (and stops at the start if it can't) and says when a file can't
  be written. Seeds 6 to 9 were run again (`runs/origin-3b`, then
  `runs/origin-3c` with the lineage measures below).

**What run 3's stage 4s were.** Both reruns of seeds 6 to 9 matched run 3
exactly, with 6 host threads a seed instead of 2: runs don't depend on the
thread count. The observer now also counts, for each lineage, how many
living members were born faithful (`alive_faithful`) and its longest line
of faithful births in a row (`max_fdepth`). The genome dump lists the
lineages 10 generations deep, and the first stage 4 lineage as it stood
then.

| Seed | Stage 4 at | Its place's programs then | Lineage members alive | Born faithful |
|---|---:|---:|---:|---:|
| 6 | 4,331,400 | 79 | 29 | 0 |
| 7 | 8,596,400 | 6 | 1 | 0 |
| 9 | 2,221,400 | 4 | 1 | 0 |

- **None was a population of copiers.** In seeds 7 and 9 the lineage was
  one living program in a place of 4 or 6, which is 10% or more. Stage 4's
  100,000 ticks count from the lineage's first program's birth, so a
  long-lived survivor qualifies.
- **Most copiers are relays.** They make one child and then fault, so one
  member lives at a time. Seed 9's place 24 had lineages of 7,000 to
  19,000 faithful births in a row, each dying out within about 4,000 ticks.
  Seed 7's places 17 and 36 were the same. A relay can't grow: each
  program replaces itself once.
- **Seed 9 grew one real population, late.** A lineage born at tick
  8,103,181 in place 13 (lax, 1,024-byte pages) reached 100 of the place's
  132 programs within 100,000 ticks. It made 10,838 births and 209 genome
  groups, and is the lineage that reached stage 5 at 8,203,200. Its
  72-byte genomes copy themselves (`ADD # 72; SYS C; ...; CPY [B]`) with
  many variants: only 3,664 of its births were exact copies, and 21 of the
  100 living members. The seed stopped at stage 5, so it isn't known
  whether it lasted.
- **Seed 6's lineage** (5,978 births, 568 faithful, 2,374 generations deep)
  held 29 of 79 programs, none born faithful. Most of its births were 1 to
  45-byte genomes, and few were copies of the whole parent.
- **So stage 4 is too easy to meet.** One survivor in a near-empty place
  counts, and nothing asks that the members it holds be copies. Changing
  the stages is Damian's call: proposed in the history.

**Stage 4 tightened** (2026-11-12, agreed with Damian). A lineage holds its
place at a check when:

- it has 20 living members or more (`origin.members`);
- at least half of them were born as copies (`origin.copies`). A copy is a
  faithful birth, or a near one: a program wrote 8 bytes or more into its
  child, at least half like its own genome;
- they are at least 10% of the place's programs (`origin.share`).

Stage 4 is a hold of 100,000 ticks (`origin.span`) without a break, checked
at every observer sample (every 100 ticks). One failed check ends a hold,
and the next one starts afresh. The report's new "best hold" column gives
each seed's longest hold, so a near miss shows. The observer counts each
lineage's living copies (`alive_copies`). Run 3's settings are being run
again to score them under it (runs/origin-3d).

**Run 3 scored under the tightened stage 4** (runs/origin-3d, 2.6 hours):
**FAIL, stage 4 in 1 of 10 seeds** (the pass mark is 5). The worlds were
run 3's exactly: every seed that ran the same ticks has the same
instruction count.

| Seed | Stage 3 | Stage 4 (run 3) | Stage 4 (now) | Best hold |
|---|---|---|---|---|
| 1 | 3,093,200 | - | - | 0 |
| 3 | 6,822,700 | - | - | 0 |
| 6 | 4,251,900 | 4,331,400 | - | 0 |
| 7 | 2,610,900 | 8,596,400 | - | 48,900 (place 36) |
| 9 | 1,292,800 | 2,221,400 | 8,203,200 | 100,000 (place 13) |

Seeds 2 and 10 stayed at stage 0, and seeds 4, 5 and 8 at stage 1.

- **Seed 9's stage 4 is real.** The lineage born at 8,103,181 in place 13
  (lax, 1,024-byte pages) held its place from the first check after its
  first copy, 8,103,200, for 100,000 ticks without a break. At the end it
  had 100 of the place's 132 programs, 77 of them born as copies (21
  faithful).
- **Seeds 6 and 7 no longer pass.** Neither ever held a place: seed 6's
  members weren't copies. Seed 7 now runs to the end, since the old stage 5
  no longer stops it at 9.09 million ticks. Its best hold was 48,900 ticks,
  in place 36. Its births went from 414,000 to 1,032,000, almost all
  relays in places 17 and 36. Relays have one member alive at a time, so
  they never hold a place.
- **Stage 5 comes at the same check as stage 4,** because it counts the new
  groups a lineage made while its place was crowded over its whole life,
  before it established too. Seed 9 stopped there again, at 8,203,200, so
  whether its population lasted is still unknown.
- **The commonest relay** (seed 7, place 36: 14,367 births) is 15 bytes:
  `MOV D; MOV D; SYS [D]` and then twelve `MOV D`. It has no jump, so
  after its one SYS it runs on past its own code.

**What the relays are: hand-overs, not copies** (2026-11-12). Traced in
place from snapshots of seed 9, with a temporary hook in the scheduler (since
removed):

- **The relay in seed 9's place 24** (lax, 256-byte pages; the lineage born
  at 1,428,378) is a program in a 94-byte block that starts at byte 87. It
  runs past the block's end, which a lax page allows, and at byte 96 calls
  SPAWN on its own block. SPAWN's "own code" test (R15) asks only whether
  the caller's IP is inside the allocation, and it isn't, so the block is
  given to a new program. The child starts at byte 87 and does the same. The
  parent faults on its next instruction, since it holds nothing in that
  page any more. In 50 ticks, 248 births all went into the same block
  (7424, 94 bytes), and every parent died by a fault.
- **The observer counts each hand-over as a faithful copy.** A block's
  written extent stays with it, so the child's "genome" is the 88 bytes
  written there long before, and it matches the parent's.
- **Over 100,000 ticks of seed 9** (1,428,300 to 1,528,300, all 48 places),
  11,636 of the 11,639 births whose parent was born in that window went
  into the parent's own birth block. None were copies into a new block.
  Most of run 3's 305,890 faithful births (and run 3d's 401,118) are
  probably hand-overs, and with them most of the stages 1 to 3. Only seed
  9's are confirmed.
- **Seed 9's population in place 13 does copy itself.** From 8,103,100 to
  8,203,200, the place had 3,619 exact copies into new blocks and 2,043
  hand-overs, only 144 of which kept the parent's genome. Its first program
  keeps its own block. It starts extra threads in itself (a SPAWN aimed at
  its own start), and each thread allocates 72 bytes, copies the code in
  with CPY and spawns the child.
- **Proposed, for Damian to decide:**
  - SPAWN counts a block as in use when one of the program's threads is
    running in the page access the block gives it: its IP is in the block's
    page and in no other block the program holds. A hand-over would then
    start a thread instead. Only lax places change.
  - The observer stops counting a birth into the parent's own birth block
    as a copy, whatever the OS does.
- **The genome dump now shows up to 256 bytes** of each genome. It used to
  cut them at 64, which hid most of the 88-byte relay.

**The hand-over gap closed** (2026-11-12, on Damian's go-ahead):

- **SPAWN** (`Os::runs_on`): a thread whose IP is in no allocation of its
  own runs on every allocation its program holds in that page. A SPAWN
  there starts a thread. A thread inside another allocation of its own
  doesn't count, so a program can still give away a block it isn't running
  on.
- **The observer:** each program remembers the block it was born in. A
  birth into its parent's birth block is a hand-over. It is never faithful,
  near or a copy, and is off the ladder. It is counted on its own: the
  `handovers` CSV column and a line in the report.
- **Tests:** `test_spawn_past_its_end` (OS) covers a gap in the page,
  someone else's block in the page, and another block of its own.
  `test_handover` (observer) covers a crafted hand-over against a copy into
  a new block.
- **Check:** seed 9 from the 1,428,300 snapshot, 100,000 ticks: 12,984
  births under the old rule, 1,439 under the new one.
- Run 3's settings run again with both fixes as run 4 (runs/origin-4).

**Run 4** (`runs/origin-4`, 2026-11-12; run 3's settings with the hand-over
gap closed, scored under the tightened stage 4): **FAIL, stage 4 in 1 of
10 seeds.** 9,657 s.

| Seed | Stage 1 | Stage 3 | Stage 4 | Births | Best hold |
|---|---|---|---|---|---|
| 1 | 5,898,144 | 6,086,300 | - | 103,443 | 0 |
| 2 | 5,595,091 | 5,595,100 | 5,695,200 | 142,407 | 100,000 (place 6) |
| 3 | 6,711,955 | 6,712,000 | - | 199,278 | 0 |
| 4 | 1,444,047 | - | - | 79,056 | 0 |
| 5 | 1,559,367 | - | - | 118,981 | 0 |
| 6 | - | - | - | 246,874 | 0 |
| 7 | - | - | - | 162,934 | 0 |
| 8 | 3,009,458 | - | - | 134,499 | 0 |
| 9 | 1,480,206 | 4,026,800 | - | 163,417 | 1,300 (place 24) |
| 10 | - | - | - | 97,648 | 0 |

- **The gap is closed.** Hand-overs fell to 898 across all seeds (5 in
  seed 2). Against run 3d: births 4.11 million to 1.45 million, faithful
  births 401,118 to 42,315, births whose whole starting memory was the
  parent's 2.84 million to 41,094. Copies of the reset region's code rose,
  41,956 to 77,527.
- **Run 3d's stage 3s in seeds 1, 6 and 7 went with the hand-overs.** In
  run 4 none had reached even stage 1 by the tick it reached stage 3 in run
  3d (3.09, 4.25 and 2.61 million). Seeds 6 and 7 never did. Seed 1 reached
  stage 3 on its own later, at 6,086,300.
- **Seeds 4, 5 and 8 reach stage 1 at the same tick as in run 3d,** so
  their worlds hadn't met a hand-over by then. Seed 9's 72-byte copier in
  place 13 (run 3d's pass) didn't appear: the world's history changed.
- **Seed 2 is real copying.** Its world had no faithful births and no
  births by a program that wrote 8 bytes or more in its first 5,595,000
  ticks. Its lineage began from static at 5,595,091 in place 6 (lax,
  1,024-byte pages). By 5,595,200 the place went from 57 to 146 programs,
  with 248 faithful births, 251 births by programs that wrote 8 bytes or
  more, and no hand-overs. It held the place from that first check for the
  whole 100,000 ticks: 151 of 280 programs at the end, 83 of them born as
  copies (35 faithful). 55,979 births, 177 generations, 1,356 groups.
- **Seed 2's copier is 17 bytes** (`75 c6 e2 0a 5f 44 a2 77 42 af 2e 13 a5
  ac 18 eb 67`; 12,224 births). Read against appendix A, not traced:
  - `SHR [B]` with B at its own start shifts A to 0; `STO C`; `XOR # 68`;
    `SHR # 66` leaves A = 17.
  - `SYS # 46` is ALLOC (slot 22) of 17 bytes. B, its own start, gives the
    permission (address mod 3), and `os.alloc_d` puts the new block in D.
  - `CPY [B]` copies 117 bytes (its own first byte, 0x75) from its start
    into the new block. On a lax 1,024-byte page the 100 bytes past the
    block are writable too, so it carries what follows it along.
  - `SYS IP` makes the call in slot (its address + 13) mod 24, so it is a
    SPAWN only for copies at the right addresses. The child gets the
    parent's start as A, which shifts to 0 again.
  - It then runs on past its end into whatever follows in the page, often
    another copy: a 68-byte variant, four copies in a row, also spread.
  - It depends on two of run 3's experimental settings: `os.thread_regs =
    start` (B at its own start) and `os.alloc_d` (D at the new block).
- **The near misses:**
  - Seed 9's best lineage is an 88-byte copier in place 24 (lax, 256-byte
    pages) that first appeared at 8,243,273: 3,152 births, 3,075 faithful,
    152 generations. Its best hold was 1,300 ticks.
  - Seed 3's best (place 13, lax 1,024) ended with 17 of the place's 51
    programs, only 5 born as copies.
  - Seed 1's best (place 3, lax 64-byte pages) is a chain: 224 births over
    223 generations, one member alive at a time.
- **By OS type:** every stage 3 or better was on a lax place; stage 4 only
  on lax 1,024-byte pages. Strict places never reached stage 1, in this run
  or any before it.
- **Budget:** the seeds that ran to 10 million ticks used 6.1 to 8.2 ×
  10^11 instructions. 10.6's budget is about 10^12 a seed, so run 4 used
  about 70% of it.
- **Stage 5 still stops a seed at stage 4:** seed 2 stopped at 5,695,200,
  so whether its population lasted is unknown.

**Stage 5 counts from stage 4, and the budget is 10.6's** (2026-11-12, on
Damian's go-ahead after run 4):

- **Stage 5** counts only the new groups a lineage makes while its place is
  crowded after it reaches stage 4 (`LineageClock` in `observe.h`: the check
  it established at, and its `groups_full` then). Reaching stage 4 is now
  for good, like the stages below it; a hold that breaks later stops only
  the hold's clock.
- **Seeds no longer stop at stage 5.** They run to `origin.ticks` or
  `origin.budget`. The report adds, for each seed's first lineage to reach
  stage 4, where it stood at the seed's end: its members and copies, and
  whether it was still holding its place, or when it died out. The genome
  dump has the same line.
- **Progress lines** come at each tenth of the ticks or of the instruction
  budget, whichever is further on.
- **Run 5** (`runs/origin-5`): run 4's settings with `origin.budget =
  1,000,000,000,000` (10.6's budget a seed) and `origin.ticks` raised to
  100 million so the budget is what stops each seed, at about 14 to 16
  million ticks.

**Run 5** (`runs/origin-5`, 2026-11-12; run 4's settings to 10.6's budget,
stage 5 counted from stage 4): **FAIL, stage 4 in 1 of 10 seeds.** 14,007
s. Every seed stopped at the budget, at 12.1 to 16.3 million ticks.

| Seed | Ticks | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Faithful births | Best hold |
|---|---:|---:|---:|---:|---:|---:|---|
| 1 | 14,375,900 | 5,898,144 | 5,907,710 | 6,086,300 | - | 7,866 | 0 |
| 2 | 14,880,800 | 5,595,091 | 5,595,091 | 5,595,100 | 5,695,200 | 166,291 | 9,140,400 (place 6) |
| 3 | 14,477,700 | 6,711,955 | 6,711,964 | 6,712,000 | - | 2,885 | 0 |
| 4 | 16,337,300 | 1,444,047 | 12,087,375 | - | - | 66 | 0 |
| 5 | 13,889,600 | 1,559,367 | - | - | - | 64 | 0 |
| 6 | 14,432,700 | - | - | - | - | 0 | 0 |
| 7 | 13,889,700 | 11,168,419 | - | - | - | 1 | 0 |
| 8 | 16,342,000 | 3,009,458 | - | - | - | 11 | 0 |
| 9 | 13,724,800 | 1,480,206 | 1,480,206 | 4,026,800 | - | 10,184 | 1,300 (place 24) |
| 10 | 12,126,700 | 11,740,259 | - | - | - | 2 | 0 |

- **Run 4's worlds, extended.** The samples match run 4's where both ran
  (checked at 2,500,000 in seeds 2, 8 and 9), and every stage run 4
  reached came at the same tick.
- **Seed 2's copier lasted.** Its family held place 6 to the seed's end,
  9.2 million ticks after taking it. No seed reached stage 5.
  - The best hold, 9,140,400 ticks, is either the first lineage's (from
    5,595,200 to about 14,735,600) or the second's (from 5,740,400 to the
    end). The report doesn't say which lineage held it.
  - At the end the first lineage had 19 of the place's 216 programs, 12
    born as copies: one member short of holding.
  - The second lineage, from program 7696581473670, began at 5,691,505
    with no parent the observer knew, so it counts as its own lineage. Its
    commonest genomes are the same copier with `SHR # 66` become
    `SHR # -94; OR # 64` (17 bytes, 5,166 births), and an 81-byte run of
    copies (5,355). At the end it had 45 of the 216, 39 born as copies, so
    it was holding. 110,884 births, 52,083 faithful, 316 generations.
  - Its lineages made 1,356 and 780 groups, but never 3 while place 6 was
    crowded (under 10% free) after stage 4.
- **Nothing new held a place after 10 million ticks.** Seed 4 reached
  stage 2 at 12,087,375, and seeds 7 and 10 stage 1 (1 and 2 faithful
  births). Seeds 1 and 9 copied more after 10 million ticks: their
  faithful births rose from 1,149 to 7,866 and from 5,250 to 10,184.
- **The near misses are chains.** A member leaves one child, so a single
  failed copy ends the lineage:
  - Seed 1's best (place 14, lax 1,024-byte pages, from 13,978,709) are
    74-byte copiers with 1,149, 946 and 518 births over 1,148, 945 and 517
    generations. Each died out within 700 ticks.
  - Seed 9's 88-byte copier in place 24 (8,243,273: 3,152 births, 152
    generations) died out within 2,700 ticks. Its later ones at 12.1
    million had 184 to 192 births over 183 to 191 generations.
  - Seed 3's best (place 13) lived from 6,702,536 to the end, but as 17 of
    64 programs with 5 born as copies (1,099 faithful births of 6,009).
  - Seed 2's copier is the exception. As read in run 4 (not traced), after
    its SPAWN it runs on into what follows it in the page, often another
    copy, so one member can leave several children.
- **Hand-overs:** 972 in all, 855 of them in seed 4.
- **By OS type:** as before. Stage 3 or better only on lax places, stage 4
  only on lax 1,024-byte pages, and strict places never reached stage 1.
- **What's left.** Levers 1 to 4 are all in these settings: the extra
  slots, the 4 KB region, the floors, and `os.alloc_d` with
  `os.thread_regs = start`. 10.6's lever 5, the instruction set, is the
  last before the design review with Damian.

**Lever 5, the instruction set, drafted** (2026-11-12, on Damian's
go-ahead after run 5):

- **`nova2 trace`** (app/trace.cpp) runs one genome alone in a sandbox
  place, instruction by instruction, with every call's result, every birth
  and how each thread ends. The place has one page size, lax or strict, on
  its static or on zeros. `--at` picks the genome's address and `--copies`
  puts copies after it. It's placed by hand, so it checks mechanics, not
  origins (R7).
- **Seed 2's 17-byte copier, traced** at 2,048 with three copies after it:
  - ALLOC 17; CPY 117 bytes from its own start, itself and the 100 bytes
    after it; SPAWN, a faithful child. Then it runs on past its end into
    the next copy.
  - On zeros the next copy computes a wrong length and its CPY faults, so
    the parent leaves one child. In run 5 the bytes between copies set A
    right again.
  - At 1,024 its ALLOC gets an executable block (permission = its address
    mod 3), and the CPY faults. Its blocks are 24 bytes apart, a multiple
    of 3, so every block in a page has the same address mod 3: a page is
    good or bad for it as a whole, and a third of 1,024-byte pages are
    good. 24 is also the slot count, so `SYS IP` lands on SPAWN in every
    copy.
- **Seed 1's 74-byte copier (the chains) can't be traced from the dump.**
  It does ALLOC 74 and CPY # 109, but has no SPAWN inside its block. It
  copies 109 bytes, so what spawns its child lies past its block in the
  page, and a genome is only what was written inside the block. The
  copiers found so far are page-sharers: on lax pages they copy, and run,
  past their own blocks.
- **The variants,** all switches, off by default:
  - **`cpu.top`:** spare instruction codes made TOP. It jumps to the start
    of the allocation holding it, with any operand, and does nothing
    outside one. One spare code is 8 of the 256 byte values. Traced: a
    10-byte one-shot copier (`MOV # 10; SYS # ALLOC; CPY # 10; MOV D;
    SYS # SPAWN; TOP A`) made 20 faithful children in 120 instructions,
    against 1 without TOP.
  - **`os.spawn_top`:** a SPAWN that makes a new program sends its caller to
    the start of the allocation its SYS lies in. It's a call rule, not the
    instruction set, but aimed at the same failure. Traced: seed 2's copier
    on zeros made 27 faithful children in 300 instructions with no deaths,
    against 1 without it.
  - **Not tried: IP wrapping at its allocation's end,** like Avida's
    circular genomes. Any code that doesn't fault would loop forever, and
    nothing at gate 1 ends a thread that loops, so places would fill with
    programs that never die. TOP has the same risk, more weakly: a random
    program that reaches a TOP before faulting may loop forever. The pilot
    counts programs alive to see it.
- **The report names the lineage behind each seed's best hold,** with the
  ticks the hold ran from and to.
- **The lever 5 pilot** (`runs/pilot-3`): run 5's settings for 5 million
  ticks, 10 seeds, in three variants side by side:
  - a. `cpu.top = 22`;
  - b. `cpu.top = 22 23 24 25`;
  - c. `os.spawn_top = true`.

  Run 5 is the baseline at the same ticks. At 5 million ticks it had
  stage 1 in seeds 4, 5, 8 and 9, stage 3 in seed 9, and no stage 4.

**The lever 5 pilot** (`runs/pilot-3`, 2026-11-12 to 13, 16,064 s; 5
million ticks a seed): **`os.spawn_top` works and TOP doesn't.** spawn_top
reached stage 4 in seed 1. Both TOP variants did no better than run 5.

| At 5 million ticks | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Births | Faithful | Programs / threads alive | Best hold |
|---|---|---|---|---|---:|---:|---|---|
| run 5 (baseline) | 4, 5, 8, 9 | 9 | 9 | - | 713,306 | 75 | 635 / 4,222 | - |
| a. `cpu.top = 22` | 1, 5 | 1, 5 | - | - | 581,699 | 345 | 574 / 5,595 | 400 (seed 5) |
| b. `cpu.top = 22 23 24 25` | 5, 9 | - | - | - | 471,689 | 3 | 627 / 8,816 | 0 |
| c. `os.spawn_top = true` | 1, 3, 5, 8 | 1, 3 | 1, 3 | 1 | 1,912,290 | 582,583 | 737 / 3,116 | 1,964,700 (seed 1) |

(The stage columns list the seeds that reached each stage. Births, faithful
births, programs and threads are summed over the 10 seeds.)

- **spawn_top, seed 1: stage 4 at 3,135,300,** the first on lax 64-byte
  pages. The lineage of program 4398046533241 in place 3 began at
  3,034,600. Its first faithful births were at 2,461,794. Then births in the
  seed jumped from 45,344 at 3.0 million to 500,638 at 3.5 million.
  - The lineage: 245,132 births, 192,604 faithful, 730 generations, 252
    groups. That's about 336 births a generation, so its members leave
    many children, not one as in run 5's chains.
  - It held place 3 from 3,035,300 to the end (1,964,700 ticks), with 95 of
    the place's 188 programs at the end, 94 born as copies.
  - Its commonest genomes are 87 bytes in 250-byte allocations, with
    `CPY # 87`. Alone in the sandbox the genome faults at its CPY, so its
    working isn't traced. It calls SENSE before each ALLOC, so it depends
    on its surroundings.
  - **After 3.5 million it all but stopped copying.** The seed's faithful
    births rose by only 1,248 in its last 1.5 million ticks (395,460 to
    396,708). Its births and fault deaths stayed level at about 5,000 per
    500,000 ticks. The hold went on because its members stopped dying, not
    because they kept copying. It made no 3 new groups while crowded, so
    no stage 5. Why copying stopped isn't known yet; a full place, where
    ALLOC fails, is the likeliest reason.
- **spawn_top, seed 3: boom and bust.** Place 22 (lax, 1,024-byte pages)
  reached stages 1 to 3 at 1,159,172 to 1,159,200. Then seven lineages
  boomed there one after another, each with 31,358 to 130,810 births over
  only 23 to 76 generations. Five died out within 41,000 to 235,000
  ticks; the other two had one member each at the end. The seed never had
  more than 81 programs alive, and its births and fault deaths moved
  together (719,799 births). The best hold was 28,700 ticks.
  - Its commonest genome (40 bytes, traced) is a 9-instruction loop:
    `SYS A` = ALLOC 40, `CPY # 119`, `SYS [B]` = SPAWN. With spawn_top it
    makes a faithful child every 9 instructions. Without it, it makes one
    and dies on a `POP`.
  - Like seed 2's run 5 copier, it works only where its address mod 3 is
    2. At 1,024 its ALLOC gets an executable block and its CPY faults. Its
    48-byte slots share one value mod 3 across a page, so a third of pages
    are fertile.
  - Why each boom ended isn't traced.
- **Seeds 5 and 8 reached stage 1 at the same ticks as in run 5**
  (1,559,367 and 3,009,458). spawn_top changes nothing until a program's
  SPAWN makes a new program, so until then the worlds are run 5's. The
  TOP variants differ from the start: any byte that decodes to TOP changes
  what that code does. Variant a's seed 5 reached stage 1 at 687,573.
- **TOP: more loops, fewer copies.** Variant b had the most threads alive
  (8,816, twice run 5's) and the fewest births, with 3 faithful. Variant
  a's best was stage 2 in seeds 1 and 5, and a 400-tick hold. TOP helps a
  copier only if it sits just after its SPAWN, which takes a mutation, but
  it gives junk code a loop at once. Nothing ran away: the places didn't
  fill with immortal loopers.
- **By OS type:** under spawn_top, stage 4 on lax 64-byte pages and stage 3
  on lax 1,024-byte pages. Strict places still never reached stage 1 in
  any variant.
- **Where this leaves lever 5.** spawn_top is the first change that makes
  copiers leave many children, and the first to reach stage 4 within 5
  million ticks. One seed in 10 is still far from the pass mark of 5. Its
  holds raise three questions for the design review:
  - a lineage that stops copying (likely because its place is full) keeps
    its members, since nothing at gate 1 ends them, so the place freezes
    and stage 5 can't follow;
  - the copiers found so far depend on ALLOC's permission being B mod 3,
    so most pages are barren for them (lever 4 territory);
  - the copiers use the experimental `os.thread_regs = start`,
    `os.alloc_d` and now `os.spawn_top`.

**Run 6** (`runs/origin-6`, 2026-11-13; run 5's settings plus
`os.spawn_top = true`, to 10.6's budget): **FAIL, stage 4 in 1 of 10
seeds.** 13,530 s. Every seed stopped at the budget, at 13.1 to 16.3
million ticks.

| Seed | Ticks | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Births | Faithful births | Best hold |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 14,174,100 | 2,461,794 | 2,981,236 | 2,995,500 | 3,135,300 | 585,956 | 396,762 | 11,138,800 (place 3) |
| 2 | 14,507,800 | 9,932,295 | 9,934,580 | 9,948,700 | - | 257,511 | 14,818 | 59,900 (place 24) |
| 3 | 14,989,700 | 1,159,172 | 1,159,172 | 1,159,200 | - | 968,889 | 185,907 | 28,700 (place 22) |
| 4 | 16,321,900 | 9,603,521 | - | - | - | 494,468 | 4 | 0 |
| 5 | 14,565,300 | 1,559,367 | - | - | - | 361,666 | 6 | 0 |
| 6 | 14,330,300 | 11,207,442 | - | - | - | 306,457 | 87 | 0 |
| 7 | 14,122,000 | - | - | - | - | 292,453 | 0 | 0 |
| 8 | 15,916,600 | 3,009,458 | 6,979,216 | 6,983,700 | - | 412,640 | 1,601 | 0 |
| 9 | 14,072,800 | 11,783,762 | 11,869,039 | 11,869,100 | - | 581,231 | 53,975 | 18,700 (place 11) |
| 10 | 13,071,500 | - | - | - | - | 206,690 | 0 | 0 |

- **Against run 5:** the same count at stage 4 (1 seed) and none at
  stage 5. Stage 3 in 5 seeds against 4, stage 1 in 8 against 9. 4,467,961
  births against 2,347,216, and 653,160 faithful against 187,370, but
  582,669 of those are seeds 1 and 3's. The longest hold, 11,138,800 ticks,
  against 9,140,400. Hand-overs rose from 972 to 18,887.
- **Run 5's stage 4 didn't come back.** Seed 2's world left run 5's
  before 5.6 million ticks (its first stage 1 came at 9,932,295, not
  5,595,091), and its 17-byte copier never appeared. Seed 1 took its
  place. Seeds 5 and 8 reached stage 1 at run 5's ticks. Run 6's first 5
  million ticks are the pilot's spawn_top variant exactly (checked at
  5,000,000 in seeds 1, 3 and 7).
- **Seed 1's hold is frozen.** Its lineage held place 3 from 3,035,300 to
  the end, with 95 members at 5 million ticks and 95 at the end (of 188,
  then 204, programs). It made 54 faithful births in its last 9.2 million
  ticks (396,708 to 396,762). It lasts because its members don't die, not
  because it copies. No stage 5.
  - Why it stopped copying isn't traced. Its genome feeds the address its
    first ALLOC returns to SENSE, and takes the next ALLOC's size from what
    SENSE reads (SENSE 0 reads the temperature, 250, the size of every
    allocation in its dump). So it works only in the layout it grew up in.
    In the sandbox its CPY faults: at 64 it gets its 250 bytes but an
    executable block (64 mod 3 is 1), and at 192 SENSE reads something
    else and it asks for 3 bytes. The run kept no snapshot of place 3 to
    trace it in. A full place, where ALLOC fails without killing it, is the
    likeliest reason.
- **The other copiers boom and bust.** Under spawn_top a copier leaves
  many children, but no other lineage held a place:
  - Seed 9's in place 11 (lax 1,024-byte pages, from 12,186,205): 16,579
    and 16,467 births over 33 and 137 generations; each died out within
    20,000 to 80,000 ticks. A 977-generation chain in place 33 lasted
    1,300 ticks.
  - Seed 2's in place 24: up to 3,901 births; a 59,900-tick hold, then
    a few members left at the end.
  - Seed 3's seven lineages in place 22, all by 2.9 million (see the
    pilot). It added 249,090 births after 5 million but only 37 faithful.
  - Seed 8's in place 36 (lax 256-byte pages): lineages of 578 to 3,055
    births, each gone within 24,000 ticks.
  - Why the booms end isn't traced either. A place filling until ALLOC
    fails, and copiers that fault when it does, would explain it.
- **By OS type:** stage 4 on lax 64-byte pages, stage 3 on lax 256- and
  1,024-byte pages. Strict places still never reached stage 1.
- **Lever 5 is spent, and so are 10.6's levers.** spawn_top makes copiers
  that leave many children, but they either die out or freeze, and the
  count at stage 4 is the same as run 5's. Next is the design review with
  Damian. Its questions, from runs 5 and 6:
  - nothing at gate 1 ends a program that stops copying without
    faulting, so a full place freezes and stage 5 can't follow;
  - a place with no room left kills copiers whose ALLOC fails, so booms
    end in busts (not yet traced);
  - ALLOC's permission comes from B mod 3, so a copier works only in a
    third of pages, and strict places never reach stage 1;
  - every copier so far leans on three experimental switches
    (`os.thread_regs = start`, `os.alloc_d`, `os.spawn_top`);
  - there's no snapshot of a place to trace a frozen or failed lineage
    in (the periodic snapshots wanted later).

**The design review after 10.6's levers** (2026-11-13,
`design-review-origin-2026-11-13.md`; all seven proposals accepted by
Damian). What changed in the code:

- **Lever 5's experiments are gone:** `os.spawn_top` and `cpu.top` (TOP),
  their settings, the CPU's TOP and their tests. spawn_top broke 9.1 and
  principles 9 and 12, and changed which seed reached stage 4, not how
  many.
- **Two conventions are now the rules, with no setting:** a new thread
  starts with B, C and D at its start (A.3; was `os.thread_regs = start`),
  and ALLOC's result lands in D as well as A (A.4; was `os.alloc_d`). D is
  set on a failure too (0), where `os.alloc_d` had left it alone, so a
  copier whose ALLOC fails copies to address 0 and faults, as A.5 means
  it to.
- **A lax place has no no-execute bit** (9.3): ALLOC's and PROTECT's
  "data" give "both" there.
- **A.5's replicators,** rewritten for the new conventions (the old 18
  bytes' `SWP D` no longer brings the parent's address back): 14 bytes on
  a lax place (`MOV # 14; STO B; SYS # ALLOC; MOV C; STO B; CPY # 14; MOV
  D; SYS # SPAWN; JMP # -12`, hex `070e09af000209a70e03af037ff4`), and 20
  on a strict one with 64-byte pages (`MOV # 63`, data, then PROTECT). The
  11-byte copier with no register moves works where its address isn't 1
  mod 3. Every test that used the 18 bytes uses the 14 now, and the
  benchmark too. The viewer's stored image changed: replicators now die
  when memory runs out (146 deaths in its 60 ticks, against 22).
- **A side effect to know:** with C at its own address, a copier that
  doesn't set C gets ALLOC's device clamped to the slowest memory device.
- **Place snapshots** (`origin.snapshots`, `origin.snapshot_every`,
  default 1,000,000 ticks). A whole world is 125 MB (each place holds a
  1 MB raw disk), so the origin test saves single places instead, about
  2.6 MB each, as `seed-S-tick-T-place-P.snap`: at every interval and at
  the seed's end, each place where a lineage `origin.generations` deep has
  half `origin.members` alive or more; and a place as its lineage reaches
  stage 4. The snapshot container gained a flag for a one-place file;
  `resume` and `view` refuse those.
- **`nova2 trace --snapshot <file>`** reads a place snapshot (or a world
  snapshot with `--place P`): `--list` prints the place (its free memory
  among it) and every living program with its parent, birth,
  allocations and first bytes, and each thread's state, IP and
  registers; `--program U` runs that program and the children it makes
  in the place as it was, the other threads standing still; `--wake`
  ends every sleep and wait first. Checked end to end on a short run (8
  places, 2 million ticks): two place snapshots of 2.6 MB, read back. Its
  three programs were junk asleep until ticks in the hundreds of millions,
  with saved registers overwritten (a stack lies in memory others can
  write, A.3): sleepers are another kind of program nothing ends at gate
  1. All 17 test suites pass (test_world gained the place
  snapshot's round trip).

**Pilot 4** (`runs/pilot-4`, 2026-11-13, 4,689 s; run 5's settings under
the review's rules, 10 seeds of 5 million ticks, against run 5 at 5
million): **stage 4 in 4 of 10 seeds within a third of the budget.** Run 5
had none at that tick, and run 6 had 1 at its end. Each seed used 3.0 to
3.6 × 10¹¹ instructions.

| Seed | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Births | Faithful births | Best hold |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 1,536,230 | 1,536,230 | 1,536,300 | 1,705,400 | 4,133,000 | 2,447,177 | 2,260,617 | 1,427,900 (place 20) |
| 2 | - | - | - | - | - | 70,792 | 0 | 0 |
| 3 | 638,166 | 2,761,327 | 2,761,400 | 2,903,500 | 3,444,600 | 311,750 | 158,298 | 1,656,100 (place 20) |
| 4 | 2,956,256 | - | - | - | - | 53,221 | 1 | 0 |
| 5 | 759,309 | 1,168,409 | 1,391,100 | 1,489,800 | 1,490,800 | 743,063 | 562,732 | 3,001,700 (place 27) |
| 6 | 1,865,578 | 1,865,580 | 1,865,600 | 1,981,800 | - | 233,709 | 138,191 | 128,300 (place 13) |
| 7 | 2,380,387 | 2,380,387 | 2,380,400 | - | - | 99,650 | 3,642 | 0 |
| 8 | - | - | - | - | - | 44,444 | 0 | 0 |
| 9 | - | - | - | - | - | 61,890 | 0 | 0 |
| 10 | 499,245 | - | - | - | - | 53,048 | 3 | 0 |

- **Against run 5 at 5 million ticks:** stage 4 in 4 seeds against none,
  and stage 5 in 3 (1, 3 and 5) against none. 4,118,744 births against
  713,306, and 3,123,484 faithful against 75. 1,497 programs alive against
  635, and 4,680 threads against 4,222. Run 6 at the same tick (the lever 5
  pilot's spawn_top variant) had stage 4 in one seed and 582,583 faithful
  births.
- **What changed from run 5 is two rules,** since run 5 already ran with
  `os.thread_regs = start` and `os.alloc_d`: a lax place's data pages run
  (no no-execute bit), and a failed ALLOC sets D to 0. The pilot can't
  separate the two, but both copiers traced lean on the first:
  - Seed 5's commonest genome (51 bytes, place 27, lax 256-byte pages) is
    a loop with no jump. It sets its fault handler to its own start, so
    every fault starts the next pass. It asks ALLOC for its own length,
    which it reads from its first byte (51, which is also its first
    instruction, `SUB D`). It copies that many bytes from B, its start,
    to D, the new block, and calls the number in the child's first byte,
    51, which the slots make SPAWN. A thread's first pass also SPAWNs a
    new thread at its own start. In the sandbox at
    384 it made 501 faithful births in 20,000 instructions. At 256 (1 mod
    3) its new block is execute-only and its CPY faults. The block's
    permission is B mod 3, and B is its own address. Under run 5's rules
    it worked at 1 address in 3 (2 mod 3, "both"); now it works at 2 in
    3, because "data" runs as well.
  - Seed 1's commonest genome at the end (17 bytes, place 20, lax
    1,024-byte pages, 26,476 births) takes two of its call numbers from
    its own address (`SYS D`, `SYS IP`), so what it does depends on where
    it sits. Alone in the sandbox it didn't copy at 1,024, 2,048 or 3,072:
    it exited, faulted after an ALLOC, and faulted after a RECV. At 2,048
    it jumped 102 bytes past its block and ran the static there, which
    lax memory now allows. In place 20, 26,419 of its births got a whole
    block of the parent's bytes, so it needs its place's layout. Without
    a snapshot of place 20, it can't be traced further.
- **Holds don't freeze, and lineages take turns.** Births kept coming in
  the stage 4 seeds to the end: seed 1 made 71,000 to 533,000 faithful
  births in every 500,000 ticks after 2 million, and seed 5 made 17,000 to
  234,000. Most children die by faulting (3,963,685 fault deaths against
  4,118,744 births; run 5 had 703,258), so members come and go. In seed
  1's place 20 the lineage that reached stage 4 was down to 4 members at
  the end, and one born at 4,831,932 had 159 of the place's 282 programs.
  In seed 5's place 27 the best hold (3,001,700 ticks, to the end) was by
  a lineage born at 1,998,208, after the one that reached stage 4. That
  is the turnover run 6's frozen place lacked, from the copiers' own
  faults, without the gate 2 rules.
- **Booms and busts still happen.** Seed 3's stage 4 lineage (place 25,
  lax 256-byte pages) died out 24,223 ticks after reaching stage 4; the
  seed's best hold came later in place 20 (lax 64-byte pages). Seed 6's lineage
  in place 13 held for 128,300 ticks and had 18 members at the end; a
  later one there made 16,562 births and died out in 55,000 ticks. Seed
  7 reached stage 3 at 2,380,400; its biggest lineage, a chain of 1,753
  births with one child each, ended in 506 ticks, and the seed has had no faithful birth since 2.5 million.
- **Stage 5 came 1,000 ticks after stage 4 in seed 5.** A place crowded
  with one lineage under these noise floors makes new groups quickly. It
  doesn't bear on the pass mark. Whether 10.6's stage 5 asks enough is a
  question for later.
- **By OS type:** lax places with 64-, 256- and 1,024-byte pages all
  reached stage 5. Strict places still never reached stage 1.
- **Exits rose** from 9,413 to 153,562, nearly all in seed 1 (143,217), and
  112,474 of those after 4.5 million ticks. Its newest genome exits at some
  addresses (in the sandbox at 1,024 its `SYS IP` was EXIT).
- **Next: run 7,** pilot 4's settings to 10.6's budget with
  `origin.snapshots` on. Its first 5 million ticks should repeat the
  pilot's, so seeds 1, 3, 5 and 6 carry their stage 4 in. It needs one
  more from seeds 2, 4, 7, 8, 9 and 10 in about 10 million more ticks. In
  run 6, four seeds first reached stage 1 between 9.6 and 11.8 million ticks.
- **Snapshots don't change a run** (checked after the pilot): seed 5 to
  1.6 million ticks, with and without `origin.snapshots`, gave the same
  samples file, row for row, and the same samples as the pilot's. The
  binary was the current one, built after the pilot started. It saved
  place 27 at stage 4 (1,489,800) and at the end, 2.6 MB each. Read
  back with `nova2 trace --snapshot`, place 27 at 1.6 million is 99.5%
  full (792 bytes free of 163,840), with 71 programs and 1,081 threads.
  The place ran 3,000 steps with no birth. Some threads run code outside
  the blocks their program holds (an IP 9,216 bytes before its first
  block). So a full place is now something run 7 can trace.
- **A small fix on the way:** the config parser's errors named the line
  twice ("line 42: line 42: duplicate key"). The parser's message no
  longer carries the line, since its callers print it.

**Run 7** (`runs/origin-7`, 2026-11-13, 1:46 PM to 6:14 PM, 16,071 s, on
Damian's go-ahead; pilot 4's settings to 10.6's budget, with
`origin.snapshots` every million ticks): **PASS, stage 4 in 5 of 10
seeds, exactly the pass mark.** Each seed ran 10¹² instructions, which
took 13.95 to 15.64 million ticks.

| Seed | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Births | Faithful births | Best hold |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 1,536,230 | 1,536,230 | 1,536,300 | 1,705,400 | 4,133,000 | 4,418,314 | 4,009,849 | 8,541,600 (place 20) |
| 2 | 11,721,806 | 11,754,151 | 11,983,500 | - | - | 289,552 | 13,138 | 1,700 (place 18) |
| 3 | 638,166 | 2,761,327 | 2,761,400 | 2,903,500 | 3,444,600 | 10,026,094 | 7,153,522 | 11,798,000 (place 20) |
| 4 | 2,956,256 | 9,509,879 | - | - | - | 150,129 | 71 | 0 |
| 5 | 759,309 | 1,168,409 | 1,391,100 | 1,489,800 | 1,490,800 | 895,430 | 577,429 | 12,596,300 (place 27) |
| 6 | 1,865,578 | 1,865,580 | 1,865,600 | 1,981,800 | - | 442,408 | 213,889 | 128,300 (place 13) |
| 7 | 2,380,387 | 2,380,387 | 2,380,400 | - | - | 259,107 | 5,350 | 67,100 (place 18) |
| 8 | 9,393,877 | 15,315,113 | 15,315,300 | 15,415,200 | - | 128,295 | 3,799 | 320,900 (place 23) |
| 9 | 5,556,848 | 5,556,951 | 5,697,300 | - | - | 286,953 | 53,831 | 9,200 (place 13) |
| 10 | 499,245 | 5,047,157 | 5,047,200 | - | - | 137,953 | 83 | 0 |

- **It repeated the pilot.** Every seed's samples row at 5 million ticks
  is the same as pilot 4's, so seeds 1, 3, 5 and 6 carried their stage 4
  in, at the same ticks.
- **The fifth seed came at the last moment.** Seed 8 reached stage 4 at
  15,415,200, 221,000 ticks before its budget ran out (98.6% of its
  ticks). A lineage born at 15,315,113 in place 23 (lax, 256-byte pages)
  made 5,054 births, 3,644 of them faithful, nearly all in 25,000 ticks,
  and took 132 of the place's 147 programs. Its genome is 91 bytes: a
  23-byte unit (`XOR D; MOV C; XOR C; OR [B]; SYS B; CPY [B]; ...;
  SYS [B]; SUB # -23; SWP D`) repeated four times, less a byte. Then
  it stopped. In the last 296,000
  ticks the seed made 5 faithful births, and at the end 362 of the place's
  365 threads were waiting for a message that never came. The place was
  67.5% free. So the hold that passed the test was asleep for most of its
  span.
- **Most holds freeze in the end.** Pilot 4 said holds don't freeze; over
  the full budget they do, in all but seed 3. Faithful births per million
  ticks, in thousands:
  - seed 1: 0, 518, 868, 326, 547, 1,006, 41, 198, 294, 162, 32, 12, then
    0 from 12 million;
  - seed 5: 0, 48, 341, 108, 64, 5, then about 0 from 6 million;
  - seed 6: 0, 2, 8, 10, 117, 39, 30, 5, then 0 from 8 million;
  - seed 3: still 1,603 in its last million.

  The holding lineages stay, asleep. In seed 5's place 27 the best hold
  (by a lineage born at 1,998,208) runs to the end with 43 members, all
  copies, of 161 programs, though the place has made no faithful birth
  since about 6 million. At the end, the threads in the holding places
  were mostly sleeping until an interrupt (SLEEP 0): 308 of 377 in seed
  1's place 20, 387 of 481 in seed 5's place 27, 425 of 476 in seed 3's
  place 20, and 1,042 of 1,127 in seed 6's place 13. None of them
  were ready to run. A sleeping thread doesn't fault, so nothing ends it.
  That is run 6's frozen place again, reached by another route: the
  copiers' faults gave turnover for a few million ticks, then the
  sleepers won. Places weren't full: 14% to 23% free in seeds 1, 5 and
  6. 10.6 counts a frozen hold, and the design review left turnover to
  gate 2.
- **Seed 3 is the one that keeps going.** It has copiers in four places
  (17, 20, 23 and 25), arising separately, since places have no links at
  gate 1. In places 17 and 23 lineages took turns to the end: one born at
  13,867,306 made 1,530,165 faithful births and was down to 5 members at
  the end, and one born at 14,871,813 made 409,872 in its 270,000 ticks.
  Its boom made it the slowest seed (62.2 M/s against about 75), and it
  finished at 6:14 PM, 50 minutes after the rest.
- **Seed 2 came late and short.** Its first copier appeared at 11.7
  million ticks. A lineage born at 14,095,723 in place 18 (lax 1,024-byte
  pages) made 77,845 births in 28,000 ticks, and at the end had 161 of
  the place's 170 programs, with 183 threads ready. But only 75 of the
  161 were born as copies (4 faithful), short of half, so it held for
  only 1,700 ticks. Its commonest genome is 10 bytes (`ADD B; MOV C; XOR
  IP; SYS # -94; nop A; CPY [B]; SYS # 41; MOV C`), with at most 4
  faithful births in a row: it copies, but badly.
- **The others:** seed 7 made no faithful birth after 10 million ticks,
  and its place 18 was down to 23 programs and 48 threads at the end.
  Seed 9's copier made 53,831 faithful births between 5.5 and
  11 million ticks and 2 after. Seed 10 reached stage 3 with 83
  faithful births, and seed 4 stage 2 with 71.
- **All seeds:** 17,034,235 births (12,030,961 faithful, 137,060
  scavenged). Deaths: 16,792,562 by fault and 237,892 by exit. Strict
  places still never reached stage 1; lax places of every page size
  reached stage 5.
- **Snapshots:** 95 place snapshots (237 MB) in `runs/origin-7/snapshots`,
  of seeds 1, 2, 3, 5, 6, 7 and 8. Seeds 9 and 10 have none: a place is
  saved at a million-tick mark or a seed's end only while a lineage 10
  generations deep has 10 members alive there, and theirs never did then.
- **What this means for gate 1.** The test passes 10.6's mark as
  written, but narrowly. The fifth seed came with 1.4% of its budget left,
  so a different set of seeds could give 4. And three of the five holds
  (seeds 1, 5 and 8) end as places of sleeping copies that no longer copy.
  The questions for Damian: whether that counts as a pass for gate 1;
  whether stage 4 should ask for copying during the hold; and whether to
  confirm the pass with more seeds before moving on.

**Run 8** (`runs/origin-8`, 2026-11-13 7:31 PM to 2026-11-14 0:03 AM,
16,304 s, on Damian's word to confirm run 7's pass with more seeds; run
7's settings and binary for seeds 11 to 20): **PASS, stage 4 in 6 of 10
seeds. With run 7, stage 4 in 11 of 20, against a mark of 10.** Each seed
ran 10¹² instructions, which took 12.85 to 16.25 million ticks.

| Seed | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Births | Faithful births | Best hold |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 11 | 3,871,497 | 3,871,497 | 3,871,500 | - | - | 260,405 | 18 | 0 |
| 12 | 150,635 | 1,544,506 | 7,645,200 | - | - | 185,135 | 556 | 0 |
| 13 | 5,389,748 | 7,328,788 | 7,328,800 | 8,555,700 | - | 349,822 | 98,930 | 188,800 (place 15) |
| 14 | 3,867,128 | 4,086,792 | 4,086,900 | 4,224,300 | - | 1,163,823 | 306,079 | 10,074,400 (place 21) |
| 15 | 1,620,848 | 4,218,956 | 4,358,300 | 7,821,500 | 7,822,500 | 1,077,835 | 547,438 | 5,988,700 (place 32) |
| 16 | 3,238,244 | 4,359,181 | 5,837,600 | - | - | 1,079,465 | 801,315 | 28,400 (place 22) |
| 17 | 7,771,368 | 7,771,368 | 11,123,800 | - | - | 165,882 | 31 | 0 |
| 18 | 4,264,773 | 4,264,794 | 4,837,900 | 4,938,400 | 4,938,600 | 5,105,991 | 3,897,233 | 9,522,200 (place 42) |
| 19 | 3,666,868 | 3,666,887 | 3,668,500 | 3,766,900 | - | 398,584 | 133,897 | 158,800 (place 43) |
| 20 | 3,581,940 | 7,963,184 | 8,149,100 | 9,900,500 | 9,901,000 | 472,312 | 195,844 | 282,100 (place 10) |

- **No last-moment pass this time.** Stage 4 came between 3.77 and 9.9
  million ticks, with at least a quarter of each seed's ticks left.
  Across both runs, every seed reached stage 1, 19 of 20 stage 3, 11
  stage 4 (1, 3, 5, 6, 8, 13, 14, 15, 18, 19 and 20) and 6 stage 5 (1,
  3, 5, 15, 18 and 20).
- **More holds kept copying.** Faithful births in each stage 4 seed's
  last million ticks:
  - still copying: seeds 15 (123,523), 18 (142,039) and 19 (122,085), and
    20 more slowly (10,109);
  - stopped: seed 13 (none after 11 million) and seed 14 (4 in its last
    million, none to speak of after 10 million).

  In run 7 only seed 3 was still copying, so across the 20 seeds it is 5
  of 11 (3, 15, 18, 19 and 20), and 6 stopped (1, 5, 6, 8, 13 and 14).
- **Where copying goes on, lineages take turns.** In seed 18's place 42
  (lax 256-byte pages), the lineage that reached stage 4 (born at
  4,838,139) still had 245 of the place's 588 programs at the end, beside
  lineages born at 7.65, 10.1 and 10.6 million that made 1,141,418,
  542,029 and 311,708 births. Seed 15's place 32 had new lineages at 11.1,
  11.9 and 13.5 million, and seed 19's place 43 four between 13.8 and 14.3
  million, the last of which had 113 of 193 programs at the end. Seed
  15's place had 98 threads ready at the end; seed 18's, though 99.3%
  full, had 486 of 605 waiting for an interrupt at that moment, though
  the seed made 142,039 faithful births in its last million ticks.
- **Where it stopped, the place sleeps.** Seed 14's place 21 (lax 1,024-byte
  pages) is 94.9% full, with 519 programs: 746 of its 755 threads were
  waiting for an interrupt at the end, and its best hold ran 10,074,400
  ticks to the end without copying for the last 6 million. Seed 13's
  place 15 had 290 of 308 threads waiting for an interrupt, and its
  lineage lost the place.
- **A boom that didn't hold:** seed 16 made 801,315 faithful births, 621,000
  of them between 8 and 9 million ticks, but its best hold was 28,400
  ticks and it never reached stage 4.
- **All seeds:** 10,259,254 births (5,981,341 faithful, 118,058
  scavenged). Deaths: 10,216,094 by fault and 40,001 by exit. Strict
  places still never reached stage 1, and lax 64-byte places only stage
  3 this time; lax 256- and 1,024-byte places reached stage 5. Its seeds
  ran at 61 to 68 M/s, against run 7's 62 to 77.
- **Snapshots:** 48 (119 MB) in `runs/origin-8/snapshots`, of seeds 13, 14,
  15, 18, 19 and 20.
- **What this means for gate 1.** The pass holds: 11 of 20 seeds, and the
  second set of 10 passes alone with a seed to spare. It is still close to
  half, so the true share could be a little either side. The holds split
  about evenly between places where lineages take turns to the end (5)
  and places that fall asleep, full or not, with no way at gate 1 to end
  a sleeping program (6). Whether a full place keeps turning over is gate
  2's question (10), and these runs give it a first answer: sometimes.

**Gate 1's origin test passes** (2026-11-14, accepted by Damian): stage 4
in 11 of 20 seeds within the budget, against a mark of 10 (design.md
10.6, "The result"). With the build's 12 steps done, the benchmark above
and the side measurements below, that completes what gate 1 delivers (1).

- **Faults weren't too harsh** for the first, sloppy copiers (7.6): they
  established in 11 seeds although nearly every child died by a fault
  (16,792,562 fault deaths against 17,034,235 births in run 7, and
  10,216,094 against 10,259,254 in run 8).
- **The reset regions kept changing** (10.2): their compressibility drifted
  from about 0.84 at 2 million ticks to about 0.77 at the end (means of
  0.775 and 0.769 over the regions; lowest 0.642 and 0.618), rising at
  times, against 0.97 for static.
- **The per-access counters cost under 1%** (the first benchmark).
- **Carried to gate 2** (10): turnover in a full place. In 6 of the 11
  stage 4 seeds the holding lineage stopped copying and slept, and nothing
  at gate 1 ends a sleeping program. Strict places stayed barren in all 20
  seeds, as the design review recorded.
