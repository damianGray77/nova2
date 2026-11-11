# Nova 2 — Gate 1 build plan

> **Status (2026-11-11):** a draft for Damian. Gate 1 asks one question:
> does a replicator arise from static? (design 10.6). This plan says what is
> built, in what order, how each step is checked, and what must be fixed
> before the first origin run. Numbers in brackets, like (9.3), are sections
> of `design.md`, which stays the authority. When the two disagree, the
> design wins and the plan is fixed.
>
> **Needs Damian's yes before coding starts:** the three design fixes (2),
> and the toolchain and the parts taken from Nova 1 (4). The numbers in 7
> are agreed later, before the first origin run.

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
- An address-to-allocation lookup in constant time: a size-class page has a
  fixed slot size, so the slot is the offset divided by it. The genome rule
  (13.2) needs this on every write.
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

| Number | Proposed | Why |
|---|---|---|
| Budget per seed | 10¹² instructions | 10.6; about Nova 1's 12-hour trial |
| Seeds | 10 | 10.6 |
| Pass mark | Stage 4 in at least half the seeds | 10.6 |
| Places per seed | 48 | Two per host thread on a 24-thread machine. About 2 × 10¹⁰ instructions, or roughly 1.7 million ticks, per place |
| Stage 3: generations | 10 | 10.6's example |
| Stage 4: establishing | The lineage lives at its place for 100,000 ticks without a gap, and holds at least 10% of the place's programs at the end | Long against a birth (about a tick), short against the budget |
| Stage 5: adapting | At least 3 new genome groups arise within the lineage after its place is full (free memory below 10%) | 13.2's groups |
| Reset region | 1 KB | Appendix E |
| Idle-run cap | 100,000 cycles | Appendix E; above the dearest birth (about 28,000 cycles with the costs below) |
| Start-spot step | 13 bytes, each core offset by an equal share of the region | Any odd step visits every byte of a 1 KB region |
| Slow retry | Every 100 ticks | Rare enough that halted places draw little |
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
