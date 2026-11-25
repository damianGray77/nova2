# Nova 2 — Optimization pass

> **Status (2026-11-17):** started on Damian's word, before gate 3's 20
> seeds (`build-plan-gate3.md` step 8). The audit is done and the baseline
> is measured (below). Pass 1 is done, on Damian's word ("Yes, start pass
> 1"), then passes 2 to 5 the same way. Against the baseline the static
> world now runs 3.8 times as fast on one thread, the busy one 3.0
> times, and 20 seeds side by side 4.7 to 6.0 times; pass 4 added 11 to
> 14% where 20 live seeds run side by side (7). The profiles are flat
> now and the pass is closed: what is left would change something a
> place or an observer computes, so it's Damian's to decide (7, pass 4).
> What moving the engine to the GPU would take, and gain, is in 6. Every
> patch must leave every world the same, byte for byte, so nothing here
> is a design change and `design.md` stays as it is.

## 1. Guidelines (Damian, 2026-11-17)

- **Plain loops,** not loop functions. (The code has none: no `std::`, no
  lambdas, by `design.md` 2.)
- **Memory allocated up front,** at the start of a run. No allocation in
  a hot loop.
- **Structure of arrays, not arrays of structures,** where it fits.
- **Data sized to keep cache misses down.**
- **As few branches as possible** in hot code.
- **A full audit** for other places worth refactoring (3).
- **At least three passes** of profiling and patching (5).

Added by Damian at the start of pass 3:

- **Intrinsics where possible.** `intrin.h` is already one of the allowed
  headers. What every x64 processor has (SSE2, the bit scans, the wide
  multiply, prefetch) is used freely. AVX2 needs a build flag and ties
  the binary to machines that have it, so it stays a question for Damian
  when a profile shows a loop that would use it (6).
- **Contiguous memory where possible,** sorted on the fly if that's what
  keeps it so.
- **As embarrassingly parallel as possible:** work that touches one
  place's state runs in that place's task, not serially at the barrier,
  and seeds share nothing.

## 2. How it's measured

- **The profiler** is a throwaway sampler outside the repo (scratchpad
  `sampler/sampler.cpp`). It starts `nova2.exe`, stops each thread every 2
  to 3 ms, walks its stack, and reports time by function, by source line
  and by address. It needs no elevation and adds no dependency.
- **The profiled binary** is `build-profile/`: the release flags plus
  debug symbols (`/Zi`, `/DEBUG /OPT:REF /OPT:ICF`), so the code is the
  release build's.
- **Two worlds, since a run's early hours differ from its late ones**
  (Damian):
  - **Static:** `runs/gate3-1/origin.cfg` as it is, from tick 0. Places
    of static, idle runs only. Every seed spends millions of ticks here,
    and a seed that never reaches stage 1 spends them all.
  - **Busy:** `nova2 busy runs/gate3-1/origin.cfg`, a new command: the
    origin test's own loop (observer, samples, stages, windows) on a test
    world with A.5's replicator at every core's first start spot in the
    lax places, and a sensing migrant in every place (R7: placed by hand,
    so it says nothing about life). Full within 2,000 ticks: about 15,000
    threads and 10 births a tick across the 48 places. That's busier than
    a real late run (gate 2's seed 14 ended with 2,427 threads and 0.3
    births a tick), on purpose.
- **Under pressure** (from pass 4): one seed profiled on one thread while
  19 others run beside it, since 20 seeds together hold far more than
  the cache does and a seed alone doesn't show what that costs
  (scratchpad `press.sh`).
- **The machine:** Core Ultra 9 285K, 8 P cores and 16 E cores, 24
  threads, 36 MB of L3.
- **The check on every patch:** the 30 test suites; the static and busy
  worlds' samples, windows, genome dumps and reports the same as the
  baseline's, byte for byte (timings aside); and gate 2's world, seed 5
  to 1.6 million ticks, byte for byte, as every step has checked.
- **The baseline** is kept in `runs/opt-baseline/`: the binary
  (`nova2.exe`, built 2026-11-17 13:48) and the source it was built from.

## 3. The baseline (2026-11-17)

- **One seed, one host thread, static, 200,000 ticks:** 214 s. 935 ticks a
  second, 46.7 million instructions a second, 1.07 ms a tick, 22 µs a
  place-tick.
- **One seed, one host thread, busy, 60,000 ticks:** 46 s. 1,300 ticks a
  second.
- **20 seeds, static, 30,000 ticks each:**
  - 10 at once with 2 host threads each (the run's layout): 27 s a batch,
    54 s for the 20. 55 million instructions a second a seed.
  - 20 at once with 1 host thread each: 67 s. 22 million a seed.
  - One seed alone on one thread does the same 30,000 ticks in 32 s. So
    20 side by side run at under half the speed of one alone: the seeds
    contend for cache and memory, not for cores.

**Where a tick goes** (share of the working thread's time):

| | Static | Busy |
|---|---|---|
| `Scheduler::begin_tick` (`sched.cpp:54`) | 18% | 34% |
| `step`, the interpreter | 41% (`cpu.cpp:380` alone: 32%) | 10% |
| `Region::run`, the idle runs' loop | 14% | under 1% |
| `Place::end_failed_states` (`place.cpp:555`) | 5% | 9% |
| `log2_q56` and the noise draws | 3% | 8% |
| `access`, `read_src`, `write_dst` | 6% | 2% |
| `Region::end_path` | 2% | under 1% |
| `load32` (`os.cpp:13`), `holds_in_page`, `Os::deliver`, `new_thread` | under 1% | 8% |
| The observer and `estimate_compressibility` | under 1% | 2% |

## 4. The audit

Read by Claude (the interpreter, scheduler, idle runs, places, memory and
noise) and by two read-only subagents (the observers and run loop; the
network, supply, thread pool, world and snapshots). Findings marked
*measured* show in the profiles above. The rest are read from the code
and need measuring before they're patched.

### 4.1 Thread records are scanned whole, several times a tick (measured)

- A place's threads are an array of 128-byte records, one for every slot
  a thread could ever take (memory over the stack size: about 2,500
  slots, 320 KB). The fields a scan reads sit on both of a record's cache
  lines.
- **Every tick, every place walks all of them** in `begin_tick`, looking
  for timers and sleepers. With no thread alive at all (static), it still
  reads 320 KB a place, 15 MB a tick a seed. That's the 18% and the 34%.
  Claude's reading, not measured: it's also much of why 20 seeds side by
  side run at half speed, since 20 seeds read 300 MB a tick between them.
- **The same walk** is in `end_failed_states` (whenever a bit failed in
  the tick), `fade_tick`, `Os::deliver` (each message), `Os::new_thread`
  (each SPAWN looks for the first free slot from 0) and the observer's
  sample.
- **Fix:** the fields the scans read (state, wake tick, timer, pending,
  stack, frame) go in arrays of their own (structure of arrays), bounded
  by the highest live slot, with counts of sleepers and timers so a scan
  with nothing to find doesn't run, and a lowest-free-slot mark for
  `new_thread`. Record order is kept, so the world is unchanged.

### 4.2 The interpreter (measured)

- `step` returns a 16-byte `Step` through memory for every instruction,
  and its caller reads it straight back. One line, the return, holds 32%
  of static's time: to be looked at by address in pass 1 before it's
  patched.
- Static's code is random bytes, so the instruction switch and the
  operand switches mispredict about once an instruction. A table of
  operand reads and one switch would halve the branches.
- `Region::run` checks the cap, the path tracking (twice) and the copying
  ladder before every instruction. The checks can move out of the loop,
  or be folded into one test.
- `result` calls through a pointer and tests three things per result;
  the countdown alone can stay in the loop.

### 4.3 Noise (measured)

- `log2_q56` works out a logarithm bit by bit, 56 multiplies and 56
  unpredictable branches a draw. A table and one interpolation, or fewer
  bits, must give the same numbers, or the worlds change: so the fix is
  an exact one (the same bits, by a faster route) or none.

### 4.4 Allocation in a run (read)

- **`estimate_compressibility`** allocates and fills 512 KB, for each of
  48 places, at every sample, to compress about 4 KB. Scratch kept by the
  observer, sized to the input.
- **`similarity` and `grams_of`** allocate two arrays at every mutant
  birth. Member scratch.
- **`Routes::column`** allocates its scratch and a column whenever a
  route is worked out, in the middle of a tick. Kept scratch.
- **`forget_senders`** allocates a list at a sample.
- **The observer keeps every sample** (400 bytes each, 80 MB a seed) and
  every voltage (38 MB a seed), and the origin test reads only the last.
  Growth by doubling, so copies of up to 80 MB late in a run.
- Elsewhere the tick's arrays grow only to a new high-water mark. They
  can be reserved at the start instead.

### 4.5 Work every tick that finds nothing (read)

- **The barrier walks all 48 places five times a tick** on the main
  thread, reading and writing lines the workers just wrote (the outbox,
  the dark flag, the bytes sent, the asks, the facts), whether or not
  there's anything there.
- **The network step walks every link end three times a tick** with no
  messages in flight. A count of live messages already exists.
- **The observer's `ingest`** visits every place every tick, and the run
  loop adds a walk of its own for the dark ticks.
- **`evaluate`** walks every lineage ever made at every sample (212,000
  by the end of a gate 2 seed), for one field of a 120-byte record.
- **Colonies are walked twice a sample,** once by the observer and once
  by the origin test.

### 4.6 The thread pool (read)

- Every worker sleeps in the kernel and is woken in the kernel each
  tick, one at a time through one lock, and the tick waits until every
  worker has checked in, not until the places are done. With ticks of
  about a millisecond and 2 workers a seed that's small. It matters more
  if ticks get shorter.
- Places aren't kept on the same thread from tick to tick, so a place's
  memory moves between cores' caches.

### 4.7 Records and maps (read)

- `HashMap` keeps its used flags, keys and values in three arrays: two or
  three misses a lookup on the observer's big tables. One array of slots.
- `LineageRec` (120 bytes), `ColonyRec` (72, 8 of them padding),
  `ProgramRec` (72) and `Message` (80) mix fields read often with fields
  read rarely.
- `Supply` holds the same 4 KB table in every place.
- About five map lookups a birth and three a death, some of them twice
  for the same key.

### 4.8 Smaller

- Per-byte loads and stores (`load32`, `store32`) where the bytes could
  go as a word.
- Division by values that aren't constants in loops: the run loop's
  progress line and the observer's window test every tick, the routes'
  search, the supply's swing and day.
- `Region::pop_arrival` and `arrive` shift the whole queue.

### 4.9 Not found

- No loop functions, no recursion in a tick, no virtual calls in the
  engine, no file written without buffering. Maps, fields and snapshots
  run at the start or the end only.

## 5. The passes

Each pass: profile both worlds, patch the top of the list, run the check
(2), measure again, and write the numbers here.

- **Pass 1: the thread records and the allocations.** 4.1 in full, then
  `estimate_compressibility`'s scratch (4.4), then `step`'s return
  (4.2). The measured top of both worlds.
- **Pass 2: the interpreter and the idle runs' loop.** The rest of 4.2,
  the noise draws (4.3) if an exact faster route exists, and what pass
  1's profile then shows.
- **Pass 3: the barrier, the network step and the observer.** 4.5, the
  rest of 4.4, then 4.6 and 4.7 as the profile ranks them. The 20 seeds'
  layout (10 by 2 or 20 by 1) is measured again here, since it follows
  from how much memory a seed touches.
- **Pass 4: 20 seeds side by side.** What a live world waits for when
  the cache is shared with 19 others (7).
- **Pass 5: the interpreter once more,** for posterity (7).
- **More passes** while a profile still shows something worth it.

## 6. For Damian

- **The build's flags.** Whole-program optimization (`/GL`, `/LTCG`) and
  AVX2 (`/arch:AVX2`) are free to try and add no dependency. AVX2 ties
  the binary to machines that have it. Claude's recommendation: try `/GL`
  in pass 2 and keep it only if it's faster and every check still passes;
  leave AVX2 off unless a profile shows a loop that would use it.
  **Tried in pass 2:** `/GL` was no faster on either world, so the flags
  are as they were (7).
- **The GPU.** Nothing in a tick uses one. The viewer draws with
  Direct2D, and a GPU backend for the engine is parked (`design.md` 2:
  "a later option, not a plan"). The guidelines above are also what keep
  that door open: flat arrays, nothing allocated in a tick, integer
  arithmetic, no pointers between records. Two things would make a later
  port hard, and this pass avoids both: per-tick data that has to move
  between the CPU and the GPU (Nova 1's GPU lost to its CPU on
  transfers), and code whose every step branches on data, which an
  interpreter of random bytes does by nature.
- **What moving the engine to the GPU would take** (asked by Damian
  after pass 5; Claude's estimate, nothing here is measured on a GPU).
  - **What would move.** All of a tick: the interpreter, the scheduler,
    the OS and its allocator, the idle runs, noise, power and fading,
    about 5,000 lines of the engine, rewritten as a compute shader with
    every place's state in a few large buffers. A part left on the CPU
    means a transfer every tick, which is what cost Nova 1.
  - **What would stay.** The observer, snapshots and the viewer, reading
    back what they need at a sample, not at every tick. The network step
    crosses places, so it needs a parallel form of its own or a transfer.
  - **Two engines from then on.** The world must hash the same on either
    (`design.md` 2), so the CPU engine stays as the reference, and every
    later change to the engine is made twice and checked equal.
  - **What bounds the speed.** The only parallel work is across places:
    48 a world, 960 in gate 3's 20 seeds. Within a place the cores share
    memory in a set order. A GPU runs its lanes in groups on one
    instruction pointer, and 32 places interpreting 32 different
    programs take each other's branches in turn. The card here (an RTX
    5080, 16 GB) holds about 20,000 places at most.
  - **The estimate.** The CPU runs about 2.5 billion instructions a
    second over 20 static seeds now. At 960 places a GPU would likely
    land between half of that and twice it: no gain worth the port. At
    10,000 places and more, where the CPU stays at its 2.5 to 3 billion,
    a GPU might reach 2 to 4 times it.
  - **The cheap way to know:** a prototype of the interpreter alone on
    idle runs, the easy tenth of the port and the GPU's best case. If
    that doesn't beat the CPU clearly, nothing behind it will.

## 7. Results

### Pass 1 (2026-11-17)

- **What changed:**
  - **The thread records' walks stop where the records do** (4.1;
    `os.h`, `os.cpp`). The OS keeps, beside the records: one past the
    highest record in use, a mark no free record lies below (threads and
    programs both), and counts of the threads that sleep or wait and of
    those with a timer. Every change of a thread's state goes through one
    function (`Os::set_state`), which keeps the count. So:
    - `Scheduler::begin_tick` does nothing when nobody sleeps, waits or
      has a timer, and otherwise stops at the highest record in use;
    - `end_failed_states`, `fade_tick`, `Os::deliver`, `cold_start` and
      the observer's sample stop there too;
    - `new_thread` and `new_program` start their search for the first
      free record at the mark, not at 0.

    None of it is saved: it follows from the records, a load works it out
    again (`Os::reindex`), and `Os::check` checks it against them, so the
    fuzz world checks it after every tick. The records are still an array
    of structures. Record order is as it was.
  - **The compressibility estimate keeps its tables** (4.4;
    `compress.h`, `compress.cpp`): the observer holds a
    `CompressScratch`, and each call empties only the chains it started.
    The tables' entries are 32 bits, not 64.
  - **The interpreter writes its result where its caller reads it** (4.2;
    `cpu.h`, `cpu.cpp`): `step_into` fills a `Step` the caller owns. The
    one address that held 39% of static's time was a 16-byte load of the
    `Step` just written a field at a time, which the processor can't
    forward from its store buffer. `step` remains, for the tests and the
    tracer.
- **Checked:**
  - 30 of 30 test suites pass.
  - The static world (200,000 ticks) and the busy world (60,000 ticks):
    samples, windows and genome dumps the same as the baseline's, byte
    for byte, and the reports the same but for their timings.
  - Gate 3's world, 50,000 ticks on 24 host threads: the world's hash is
    the baseline's (`4faca4a64acdff6a`).
  - Gate 2's world (`world.map = none`, `map.fields = false`), seed 5 to
    1.6 million ticks: all 16,000 sample rows and the 5 turnover windows
    match `runs/gate2-2`'s byte for byte.
- **Measured** (the release build; the baseline's figure first):

  | | Baseline | Pass 1 | |
  |---|---|---|---|
  | One seed, one thread, static, 200,000 ticks | 214 s | 98 s | 2.2 times |
  | The same, in million instructions a second | 46.7 | 102.4 | |
  | One seed, one thread, busy, 60,000 ticks | 46 s | 21 s | 2.2 times |
  | 20 seeds, static, 30,000 ticks: 10 at once, 2 threads each | 54 s | 21 s | 2.6 times |
  | 20 seeds, static, 30,000 ticks: 20 at once, 1 thread each | 67 s | 20 s | 3.4 times |
  | Gate 2's world, seed 5, 24 threads, million instructions a second | 355.6 | 571.9 | 1.6 times |

  The two layouts of the 20 seeds now run level: the seeds no longer
  fight over memory as they did.
- **Not done in pass 1:** the thread records are not yet split into
  arrays of their fields. With the walks bounded and skipped, the next
  profile says whether that's still worth it (pass 2 looks).

### Pass 2 (2026-11-17)

- **What the profile said first.** A first set of patches (each
  instruction a case of its own, the operand read from a table, a core's
  quantum worked out once, the logarithm without a branch) left the
  static world where it was. The disassembly said why: the compiler was
  not inlining the interpreter's small helpers (the access check, the
  result count) though they were marked `inline`, and each instruction
  the place ran cost about 170 of the host's, most of them calls, saved
  registers and a table built on the stack. So:
- **What changed:**
  - **The few helpers every instruction calls are inlined by force**
    (4.2; `types.h`'s `NOVA2_INLINE`, `cpu.cpp`). The operand's value is
    one load from where its code says, with no table built.
  - **The loop that runs instructions is the interpreter's own** (4.2;
    `run_steps` in `cpu.h`, `cpu.cpp`). The idle runs' loop and the
    threads' loop both called `step_into` once an instruction and kept
    their counts in memory. Now each calls `run_steps` once a stretch:
    it runs instructions until one faults or is a SYS, or the budget is
    spent, with the step inlined into it and the counts in registers.
    For an idle run it also keeps the path's hash and comes back after
    each CPY, so the region can count the copy as before; the run's cap
    and its budget fall by the same costs, so they are one limit.
    `step_into` and `step` remain, one instruction at a time, for the
    tracer and the tests.
  - **A tick's walks of the threads read a few bytes a thread** (4.1;
    `os.h`, `os.cpp`, `sched.cpp`, `place.cpp`). Beside the records the
    OS now keeps three arrays, an entry a thread: a byte that says
    whether it waits or has a timer, where its saved registers lie while
    it is off its core, and where its handler's frame lies while it is in
    one (`kNowhere` when it has no such state). One function
    (`Os::note`) sets a thread's entries from its record and is called
    after every change to what they follow; `Os::reindex` works them out
    after a load and `Os::check` checks them. `Scheduler::begin_tick`
    reads the bytes, eight at a time, and a record only when its byte is
    set. `Place::end_failed_states` compares the failed addresses with
    the two arrays: 8 bytes a thread, not a 128-byte record. This is
    the split into arrays pass 1 left open, for the fields those two
    walks read; the records themselves are as they were.
  - **The access check looks at the allocation it touches first**
    (`Memory::allows`): one lookup, where it walked the page's slots.
    That allocation lies in the page, so the walk would have found it.
  - **Half of each gap's draw is kept** (4.3; `noise.cpp`, `net.cpp`): a
    gap takes two logarithms, one of the rate and one of the random
    draw. The rate's is kept per core while its rate holds, and per link
    for good. The logarithm itself has no branch in its loop.
  - **A core's quantum is worked out once for each speed it has**
    (`sched.cpp`), not divided out 16 times a tick.
- **Tried and not kept:**
  - **Whole-program optimization** (`/GL`, `/LTCG`; 6): no faster on
    either world, to the second. Not adopted; the build's flags are as
    they were. AVX2 was not tried: no profile shows a loop that would
    use it.
  - **A cheaper hash for the idle runs' paths.** Tracking paths costs
    the static world about 15% (26 s against 22 s with it off, 80,000
    ticks), but one multiplication in place of `mix64` ran no faster
    (and left every output the same), so the hash's arithmetic is not
    where that goes. `mix64` stays; nothing here needs a decision.
  - **Finding a failed address's thread through its allocation.** Not
    exact: a program can free the allocation under its own stack or
    frame, and the rule still ends its thread when a bit there fails.
- **Checked:**
  - 30 of 30 test suites pass.
  - The static world (200,000 ticks) and the busy world (60,000 ticks):
    samples, windows and genome dumps the same as the baseline's, byte
    for byte, and the reports the same but for their timings.
  - Gate 3's world, 50,000 ticks on 24 host threads: the world's hash is
    the baseline's (`4faca4a64acdff6a`).
  - Gate 2's world (`world.map = none`, `map.fields = false`), seed 5 to
    1.6 million ticks: all 16,000 sample rows and the 5 turnover windows
    match `runs/gate2-2`'s byte for byte.
- **Measured** (the release build; the baseline's figure first):

  | | Baseline | Pass 1 | Pass 2 | Against the baseline |
  |---|---|---|---|---|
  | One seed, one thread, static, 200,000 ticks | 214 s | 98 s | 66 s | 3.2 times |
  | The same, in million instructions a second | 46.7 | 102.4 | 151.3 | |
  | One seed, one thread, busy, 60,000 ticks | 46 s | 21 s | 16 s | 2.9 times |
  | 20 seeds, static, 30,000 ticks: 10 at once, 2 threads each | 54 s | 21 s | 14 s | 3.9 times |
  | 20 seeds, static, 30,000 ticks: 20 at once, 1 thread each | 67 s | 20 s | 13 s | 5.0 times |
  | Gate 3's world, 50,000 ticks, 24 threads, million instructions a second | | 611.2 | 799.0 | |
  | Gate 2's world, seed 5, 24 threads, million instructions a second | 355.6 | 571.9 | 785.7 | 2.2 times |

- **What the profile shows now** (for pass 3):
  - **Static:** the step and its loop are still most of it. The stretches
    are short (a core's quantum is a sixteenth of a tick), so what
    surrounds each one counts: `Region::run`'s start, `run_quantum`, and
    the end of each run, where `end_path` looks its path up in a table
    of 512 KB a place (24 MB a seed, so with 20 seeds at once nearly
    every lookup misses the caches).
  - **Busy:** spread thin. The thread switch (saved registers read from
    and written to the place's memory), `run_quantum`, the second
    logarithm of each gap, and `estimate_compressibility` at each sample.
- **The 20 seeds' layout** (corrected in pass 3). The two layouts run
  level, as after pass 1: 14 s for 10 at once with two threads each, 13 s
  for 20 at once with one each. This paragraph first said a seed's second
  thread bought nothing (23 s either way). That was a mistake in the
  measuring, not in the engine: `run.threads` is the total the seeds
  share, so `run.threads = 2` with 10 seeds at once gave each seed one
  thread. The figures here are with `run.threads = 24`.

### Pass 3 (2026-11-17)

Damian added three guidelines at its start (1): intrinsics, contiguous
memory, and as embarrassingly parallel as possible.

- **What changed:**
  - **The thread pool's hand-off** (4.6; `pool.cpp`). A tick of a place
    of static is now a few hundred microseconds across all its places,
    and every tick each worker went to sleep in the kernel and was woken
    again. Now a thread with nothing to do spins for about 50
    microseconds first (`_mm_pause`, timed by `__rdtsc`), watching for
    its next run or for the workers to finish, and sleeps only after
    that. The workers take the pool's lock shared, not one at a time, to
    read a run's job; the count of workers still busy is interlocked, and
    it and the queue's head each have a cache line of their own. What a
    run does, and in what order tasks may run, is as it was.
  - **A sample's compressibility estimates run on the world's threads**
    (4.5; `observe.h`, `observe.cpp`): the one piece of a sample that
    was heavy and serial. The places are dealt to at most 8 lanes, each
    with its own tables; each place's ratio depends on its bytes alone,
    and they are summed in place order afterwards, so a sample is the
    same on any number of threads.
  - **An ended path's lookup waits, with its table line asked for**
    (4.5; `region.h`, `region.cpp`). The table of paths is 512 KB a
    place, so a lookup at the moment a run ends stalls on memory. Now
    the path waits in a short list with a prefetch of its line
    (`_mm_prefetch`), and the waiting ones are looked up in the order
    they ended, at the place's tick's end. The counts are what they were.
  - **The step, three more ways** (4.2; `cpu.cpp`). What a step reports
    comes back as plain values, registers in the loop, where the `Step`
    was memory. The instruction pointer is carried from one instruction
    to the next. And a fetch from the page the last fetch was allowed in
    needs no second check within a stretch: who holds what changes only
    in a call to the OS, and a call ends the stretch.
  - **A core with no thread doesn't ask for one when none is ready**
    (`Os::none_queued`, `sched.cpp`): most quanta of a place of static.
- **What the loop is bound by.** Most of the instructions a place of
  static runs are in the few idle runs that loop until their cap, so the
  host predicts them well, and no one chain of dependent work sets the
  pace: shortening the path hash's, or the instruction pointer's, or
  both, moved little. It is the count of host instructions for each one
  of the place's, about a hundred. The patches above trim it; the rest is
  the price of checking every access.
- **Tried and not kept:**
  - **A cheaper hash for the paths, again**, with the loop shorter: 4%.
    Not worth changing how an observer's measure is made.
  - **Asking ahead for the next ready thread's record and saved
    registers** at each pick: no faster on the busy world, so it is out.
  - **AVX2:** still no loop that would use it. The intrinsics used are
    ones every x64 processor has.
- **Checked:**
  - 30 of 30 test suites pass.
  - The static world (200,000 ticks) and the busy world (60,000 ticks),
    one thread: samples, windows and genome dumps the same as the
    baseline's, byte for byte, and the reports the same but for their
    timings. The busy world again on 24 host threads: the same files,
    byte for byte, as the one-thread baseline's.
  - Gate 3's world, 50,000 ticks: the world's hash is the baseline's
    (`4faca4a64acdff6a`) on 24 host threads and on 1.
  - Gate 2's world, seed 5 to 1.6 million ticks, on 24 host threads: all
    16,000 sample rows and the 5 turnover windows match `runs/gate2-2`'s
    byte for byte.
- **Measured** (the release build; the baseline's figure first):

  | | Baseline | Pass 1 | Pass 2 | Pass 3 | Against the baseline |
  |---|---|---|---|---|---|
  | One seed, one thread, static, 200,000 ticks | 214 s | 98 s | 66 s | 60 s | 3.6 times |
  | The same, in million instructions a second | 46.7 | 102.4 | 151.3 | 167.6 | |
  | One seed, one thread, busy, 60,000 ticks | 46 s | 21 s | 16 s | 15 s | 3.0 times |
  | 20 seeds, static, 30,000 ticks: 10 at once, 2 threads each | 55 s | 21 s | 14 s | 12 s | 4.6 times |
  | 20 seeds, static, 30,000 ticks: 20 at once, 1 thread each | 70 s | 20 s | 13 s | 12 s | 5.9 times |
  | One seed, 24 threads, static, 30,000 ticks | 3.9 s | | 2.2 s | 1.8 s | 2.1 times |
  | Gate 3's world, 50,000 ticks, 24 threads, million instructions a second | | 611.2 | 799.0 | 1,067.9 | |
  | Gate 2's world, seed 5, 24 threads, million instructions a second | 355.6 | 571.9 | 785.7 | 993.3 | 2.8 times |

  (The baseline's two 20-seed figures were measured again here, with
  `run.threads = 24`: 55 s and 70 s, against the 54 s and 67 s first
  written.)
- **For the 20 seeds:** the two layouts run level, so
  `runs/gate3-1/origin.cfg`'s `origin.parallel = 10` can stay. At the
  static world's speed 20 seeds run about 2.5 billion instructions a
  second between them, so the run's 20 times 10^12 instructions would
  take about two and a quarter hours if every seed stayed static; seeds
  that come alive run slower for each instruction, as the busy world
  does.
- **What's left, if there's a pass 4:**
  - **The busy world's thread switch:** each one forgets the context's
    remembered pages and then asks the allocator three times over.
    Keeping them per thread would spare that, at 36 bytes a thread.
  - **The second logarithm of each gap** (`log2_q56`, 3 to 4%): only a
    method that gives the same bits would do, and none is known.
  - **The step's count of host instructions** (above): the error
    countdown could be kept in a register through a stretch.
  - **The compressibility estimate's inner loops** (2 to 5%): compare
    eight bytes at a time.

### Pass 4 (2026-11-17)

On Damian's word ("Do a 4th pass").

- **What it found.** A seed profiled on its own doesn't show what 20
  seeds side by side pay for. A seed's places hold about 16 MB that a
  live world keeps touching, 20 seeds hold 20 times that, and the
  processor's cache holds 36 MB (2). So a live world among 19 others
  waits on memory where a lone one doesn't: the busy world is 2.2 times
  slower for each seed when 20 run together than when one runs alone, the
  static world 1.3 times.
- **How that was profiled.** The sampler loses its grip on 20 threads and
  more. So one busy seed was profiled on one thread while 19 others ran
  beside it only to press on the cache (scratchpad `press.sh`). Under
  that pressure three waits at every thread switch took about 30% of the
  busy world's time: for the thread's record, for its saved registers in
  the place's memory, and for the allocator's tables for the code it
  runs. Each is found through the one before, so they were waited for
  one after another. The static world under the same pressure shows no
  such wait: it stays the interpreter's, and its 1.3 is the E cores'
  speed.
- **What changed:**
  - **What a switch will read is asked for ahead** (`Os::ask_for_ready`,
    `Os::ask_for_next`, `Memory::ask_for`, `Memory::ask_for_record`;
    called from `Scheduler::tick` and `switch_in`). At a place's tick's
    start, for the first 8 threads of each ready queue and the threads
    on the cores, and after every switch for the threads that have come
    that near the front: first a thread's record and saved registers;
    then, read from those, its code, its stack and their page and granule
    entries; then the allocation records. They are hints to the host's
    cache (`_mm_prefetch`) and nothing else: no byte of a place, and no
    order of anything, depends on them. Several threads' waits overlap
    where one thread's came in a row.
  - **Forgetting a context's remembered pages** (`cpu.h`) is written
    field by field. Copied whole, the compiler's code read back 8 bytes
    it had just written as two smaller pieces, and the host waits when
    that happens: 1.5% of the busy world.
  - **Cosmic flips are drawn a few ahead of flipping them** (`noise.cpp`),
    each byte asked for as it is drawn. The draws and the flips are each
    in the order they were.
  - **The saved states a failed byte ends** (`Place::end_failed_states`)
    are looked through four threads at a time (SSE2). Four with a hit
    among them are gone through one by one, as before, so who ends and
    in what order is the same.
  - **The compressibility estimate's match search** (`compress.cpp`)
    skips a candidate that disagrees with the data where the best match
    so far ended (only a longer one counts), and compares eight bytes at
    a time.
- **Tried and not kept:**
  - **The remembered pages kept with each thread** across its time off a
    core (pass 3's first item): no faster on the busy world.
  - **The error countdown in a register through a stretch** (pass 3's
    third): 6% slower on the static world (154.7 against 165 million
    instructions a second). Passing it through the step cost more than
    reading and writing it where it lies.
  - **Asking ahead for the waiting threads' records** at a tick's start,
    and for the records of the threads on the cores: nothing measurable.
  - **Asking 16 threads ahead** where 8 are asked for now: slower. 4 runs
    level with 8.
- **Checked:**
  - 30 of 30 test suites pass.
  - The static world (200,000 ticks) and the busy world (60,000 ticks),
    one thread: samples, windows and genome dumps the same as the
    baseline's, byte for byte, and the reports the same but for their
    timings. The busy world again on 24 host threads: the same files,
    byte for byte.
  - Gate 3's world, 50,000 ticks: the world's hash is the baseline's
    (`4faca4a64acdff6a`) on 24 host threads and on 1.
  - Gate 2's world, seed 5 to 1.6 million ticks, on 24 host threads: all
    16,000 sample rows and the 5 turnover windows match `runs/gate2-2`'s
    byte for byte.
- **Measured** (the release build):

  | | Pass 3 | Pass 4 |
  |---|---|---|
  | 20 seeds, busy, 30,000 ticks: 10 at once, 2 threads each | 17.1 s | 15.3 s |
  | 20 seeds, busy, 30,000 ticks: 20 at once, 1 thread each | 17.3 s | 15.0 s |
  | One seed, 24 threads, busy, 30,000 ticks | 2.7 s | 2.7 s |
  | One seed, one thread, busy, 60,000 ticks | 15 s | 15 s |
  | One seed, one thread, static, 200,000 ticks | 60 s | 60 s |
  | The same, in million instructions a second | 167.6 | 167.8 |
  | 20 seeds, static, 30,000 ticks: 10 at once, 2 threads each | 12 s | 12 s |
  | 20 seeds, static, 30,000 ticks: 20 at once, 1 thread each | 12 s | 12 s |
  | One seed, 24 threads, static, 30,000 ticks | 1.8 s | 1.7 s |

  So pass 4 is worth 11 to 14% where 20 live seeds run side by side, and
  nothing either way elsewhere. The baseline has no figure for the first
  three rows: its binary has no busy world. Gate 3's world on 24 threads
  ran level with pass 3's binary, run in turns (963 to 1,087 million
  instructions a second, either binary), and gate 2's seed 5 ran at 962.9
  against pass 3's 993.3, within what one run of it varies by.
- **For the 20 seeds:** as pass 3 left it. `origin.parallel = 10` can
  stay; while the seeds are static the run's pace is pass 3's. What pass
  4 buys comes when seeds come alive.
- **What's left:** the profiles are flat. On the busy world under
  pressure no function is above 7% of the time, and on the static world
  the interpreter's loop is all there is. What remains would change
  something an observer measures or how a place computes, so it's
  Damian's to decide and not this pass's to take:
  - **The path hash** (`mix64` on every instruction of an idle run, 4%
    measured in pass 3).
  - **The second logarithm of each gap** (`log2_q56`, about 5% of the
    busy world): only a method that gives the same bits would do, and
    none is known.

### Pass 5 (2026-11-17)

On Damian's word ("do one more pass for posterity").

- **What it looked at.** The static world again, since the 20 seeds
  spend most of their time there. The interpreter's loop is about 70% of
  it. A stretch runs 48 instructions on average and most run 33 to 256,
  so what a stretch costs to enter and leave is small beside what its
  instructions cost. With the path hash switched off altogether the
  world ran 11% faster (183 against 165 million instructions a second),
  so the hash is a tenth of the time and no more, whatever the samples
  that land on it suggest.
- **What changed:**
  - **An instruction's byte is decoded by a table** (`cpu.cpp`,
    `kDecode`): its length, where its operand's register lies, and
    whether its operand is a byte of memory read first, each looked up
    by the byte where they were worked out with shifts and compares. 3%
    on the static world's profile build, 5% on the release build.
- **Tried and not kept:**
  - **Profile-guided optimization** (the compiler's own, `/GENPROFILE`
    then `/USEPROFILE`, trained on both worlds on 1 and 24 threads): no
    faster on the static world (165.9 against 165.6), so the build is as
    it was.
- **Checked:** as pass 4, all of it again. 30 of 30 suites; the static
  and busy worlds byte for byte against the baseline on one thread, and
  the busy world on 24; gate 3's world hash `4faca4a64acdff6a` on 24
  threads and on 1; gate 2's seed 5 to 1.6 million ticks against
  `runs/gate2-2`.
- **Measured** (the release build):

  | | Baseline | Pass 4 | Pass 5 | Against the baseline |
  |---|---|---|---|---|
  | One seed, one thread, static, 200,000 ticks | 214 s | 60 s | 57 s | 3.8 times |
  | The same, in million instructions a second | 46.7 | 167.8 | 176.3 | |
  | One seed, one thread, busy, 60,000 ticks | 46 s | 15 s | 15 s | 3.0 times |
  | 20 seeds, static, 30,000 ticks: 10 at once, 2 threads each | 55 s | 11.9 s | 11.7 s | 4.7 times |
  | 20 seeds, static, 30,000 ticks: 20 at once, 1 thread each | 70 s | 12.0 s | 11.7 s | 6.0 times |
  | One seed, 24 threads, static, 30,000 ticks | 3.9 s | 1.7 s | 1.7 s | 2.3 times |
  | 20 seeds, busy, 30,000 ticks: 10 at once, 2 threads each | | 15.3 s | 15.3 s | |
  | 20 seeds, busy, 30,000 ticks: 20 at once, 1 thread each | | 15.0 s | 14.9 s | |
  | Gate 2's world, seed 5, 24 threads, million instructions a second | 355.6 | 962.9 | 982.8 | 2.8 times |

- **Where it stands.** Five passes: 2.2 times, then 1.5, 1.1, nothing on
  a lone seed (11 to 14% on 20 live ones), and 1.05. What is left in the
  interpreter is the count of host instructions for each of a place's
  (about a hundred, most of them the checks every access gets), and the
  compiler was given its best chance at those in this pass. The pass is
  closed here unless a later run's profile shows something new.

### The late-run slowdown (2026-11-18)

On Damian's word ("Profile first"), before the noise idea.

- **What was seen.** Gate 3's seeds drop from about 270 million
  instructions a second while static to 3 to 10 million once life takes
  off. Seed 11 at 31 million ticks had 7,228 programs and 37,745
  threads, near seed 7's at 7 million (3,818 and 30,377), yet ran about
  20 times slower a tick. What it had more of was history: 50 million
  births, 12.7 million genomes and 2.95 million groups.
- **What the profile showed.** Seed 7 carried on from its saves of tick
  7 million, the observers the old way (`observe.keep_generations=1`,
  185,694 groups), sampled for 150 s at 3 ms on the profile build. Of the
  seed's thread, 42% was one path: `Observer::keep`, `assign_group`,
  `new_group`, `list_run`, then `realloc` copying memory. Kept as
  redesigned (3 generations, 52,583 groups) the same path was 7%.
- **The cause.** `Array::resize` reserves exactly what it is asked for,
  where a push doubles. `list_run` adds each new block of a run's list
  with `run_pool_.resize`, so every new block copied the whole pool. The
  pool grows with the groups, so the copying grows with their square:
  harmless at 7 million ticks, ruinous at seed 11's 2.95 million groups.
- **What changed.** `list_run` reserves by doubling before it resizes
  (`observe.cpp`). Only there: the other place a running world grows an
  array by `resize` (`Os`'s `outbox_bytes_`) is emptied every tick, so
  it reaches its size once.
- **Checked.** 31 of 31 suites. Seed 7 from tick 7 million to 7.5
  million the old way, before and after side by side: the samples file
  byte for byte the same, and every stage and birth count.
- **Measured.** That half million ticks took 266 s before and 130 s
  after, twice as fast, with the gate 3 run working beside it. The gain
  grows with the run: the copying it removes grows with the square of
  the groups.
- **What is left, the fixed build from the same save.** Of the seed's
  thread: the network step about 24% (the noise's `log2_q56` 8%, in
  `Network::corrupt`); the observers' sample 17% (the compressibility
  estimate and the ratio lanes about 6% each); ingesting births 10%
  (grouping 2%); places' own ticks 14%; and 22% waiting for the pool's
  threads to finish places.
- **Not yet.** The gate 3 run in progress was built before the fix and
  carries the slowdown to its end.

### The noise log (2026-11-18)

On Damian's word ("Yes, build it"), after the slowdown's fix.

- **What changed.** `geometric_gap` needs only a / b + 1, where a comes
  from `log2_q56` of the draw: 56 squarings, each waiting on the last. A
  fast log (`log2_q56_near`, `noise.cpp`) gets within a slack of 2^12
  2^-56ths: a table of 256 spans of the mantissa, built at startup with
  the C library's `log2`, then ln(1 + x) to the x^6 term in doubles. Where
  both ends of the slack give the same quotient, that quotient is the
  exact log's; only near a multiple of b, for about 2 × 2^12 ÷ b of the
  draws, does the exact log decide. The numbers are the same either way.
  `gap_log` and `cells_failed` keep the exact log, being rarely called.
- **Checked.** 31 of 31 suites, among them a new one (`test_fast_log`):
  the fast log's worst error is 33 2^-56ths against its slack of 4,096,
  on every exponent, both sides of every span's edges and 2 million
  random values; and `geometric_gap` gives the exact log's gap on 1.8
  million draws at rates from a half down to 2^-64, where most draws go
  to the exact log. Seed 7 from tick 7 million to 7.5 million (the old
  way) and the busy world (60,000 ticks, one thread): samples, windows
  and genome dumps byte for byte as before.
- **Measured** (release builds, the gate 3 run working beside them): the
  busy world on one thread, three runs each way taken in turn, 20.58 s
  before and 20.05 s after, 2.6% faster; seed 7's half million ticks
  107 s and 106 s, 1%. Less than the profile's share for the log (5 to
  8%) suggested: the old log's chain of multiplies largely overlapped
  other work, so removing it saved less than its samples.

### Pass 6: the late world, profiled only (2026-11-18)

On Damian's word ("Do another profiling pass on nova 2 and let me know if
you have any further optimization recommendations"). Nothing was changed.

- **What was profiled.** Not the busy world of passes 1 to 5 but a real
  late one: seed 7 carried on from its saves of tick 7 million to 7.4
  million, with both fixes above, on the profile build on one host
  thread and on two, the observers as redesigned and the old way; the
  gate 3 run's 9 unfinished seeds working beside it.
- **What a late world is.** Each tick, over its 48 places: 24,000 to
  30,000 threads alive, 41 started, 1.3 births, and 6,200 instructions.
  A tick takes about 410 us on one thread, so 66 ns for each instruction
  run, against 6 in a static world: the time goes on keeping the
  threads, not on running them.
- **Where the seed's thread goes** (one host thread, observers as
  redesigned; the old way differs by a point or two):

  | | Share |
  |---|---|
  | `Scheduler::run_quantum`: switching threads in and out, and running them | 45% |
  | of which the interpreter (`step_one`, `run_steps`) | 8% |
  | of which OS calls (`Os::call`; `Os::spawn` 5.4%) | 9% |
  | `Observer::sample`, every 100 ticks | 11% |
  | `Scheduler::begin_tick`: the walk for sleepers and timers | 7.4% |
  | `Memory::ask_for_record` (pass 4's asking ahead) | 5.1% |
  | `Os::new_thread` | 4.5% |
  | `Place::end_failed_states` | 3.5% |
  | The network step | 3.0% |
  | `Noise::cosmic_flips` | 2.5% |
  | The observer's `ingest` | 2.0% |

  Nearly every hot line is a wait for memory, not arithmetic.
- **By host threads** (release build, 200,000 ticks, the gate 3 run
  beside it; every run's samples byte for byte the same): 72 s on 1
  thread, 43 s on 2, 29 s on 4, 23 s on 8, 30 s on 24. About a fifth of
  a tick is not shared out, the observer's sample the largest part of
  it, and 24 threads are slower than 8.
- **Recommended, each exact (the same world and records):**
  1. **A due tick beside each thread's record.** `begin_tick` skips
     threads with nothing to wait for by a byte each, but reads the whole
     record of every sleeper, waiter and timer every tick, and in a late
     world that's most of the threads. One u64 a thread (the earliest
     tick it needs a look, kept where `Os::note` keeps the rest) lets
     the walk read 8 bytes a thread and a record only when it is due.
     Up to 7%.
  2. **The sample's walk of the threads.** For every live thread, every
     sample, it reads the thread's saved instruction pointer in the
     place's memory (4.9% on that one line), the allocation there, the
     program's record, and hashes the code. Shared out over the pool by
     place, as the compressibility estimates already are, with each
     place's counts added in place order. 5 to 8% on one thread, more on
     several, since it's the largest part not shared out.
  3. **`Os::new_thread`'s search for the first free record.** It starts
     from the lowest-free mark but then steps through 128-byte records;
     with 41 threads started a tick the low slots are packed and the
     steps are many. A bitmap of free slots finds the same slot in a few
     words. 2 to 3%.
- **Could be done, not recommended yet:**
  - `end_failed_states` walks every thread for each failed byte. Looking
    the byte's allocation up first would skip the walk when it isn't in
    a stack, but needs the way from an allocation to its thread. 3% at
    most.
  - `ask_for_record`'s 5% may only be waiting moved from where it would
    fall anyway. An experiment: keep each thread's stack and code
    allocation numbers beside its record.
  - Skipping the compressibility estimate for a place whose region
    hasn't changed since the last sample: 3% at most, and needs a count
    of changes that every writer to the region keeps.
  - `HashMap`'s three arrays as one (4.7, still open): 2% here, more on
    the 20 seeds' larger tables.
  - A lone seed's default of every hardware thread: 8 beat 24 here, but
    with the gate 3 run on 18 of them. To be measured on an idle machine
    before anything is made of it.
- **Where it stands.** Together the three would make a late world about
  15% faster. No single large win is left in the engine: the late
  slowdown was the one, and it is fixed.

### Pass 7: pass 6's three (2026-11-18)

On Damian's word ("Yes, build them").

- **What changed:**
  - **A due tick beside each thread's record** (`Os::due`, kept by
    `Os::note`; `Scheduler::begin_tick`): the first tick a tick's start
    has anything to do for the thread, which is its timer's next firing,
    the tick its sleep or wait runs out, or now if it waits for an
    interrupt and one is pending. The walk reads a thread's record only
    at that tick or after. The entry is rewritten only for a thread that
    waits or has a timer, or just stopped: a thread that only goes on and
    off its core costs no write. A data interrupt raised for a thread
    (`Os::deliver`) and a timer fired by the scheduler now tell the OS,
    since each changes when the thread is due.
  - **A bit a thread record, set while it is free** (`free_bits_`):
    `Os::new_thread` finds the first free record from the lowest-free
    mark a word at a time, where it stepped through the records.
  - **The sample's walk of the threads is done place by place in the
    lanes** that already work out the places' compressibility
    (`Observer::walk_threads`, called from `ratio_lane`), each place's
    two counts added in place order after. Within a place, what the
    threads 8, 4 and 2 further on will need is asked for ahead, as pass
    4 did for the switches.
  - `Os::check` checks the due ticks and the free bits against the
    records, as it does the rest kept beside them; `reindex` works them
    out again after a load. Neither is saved.
- **Tried on the way:** the free bits kept by `note` on every change of
  state, and the due tick written on every one: the busy world, whose
  threads mostly run, was 1 to 2% slower. Kept where a record is taken
  or freed, and written only for threads that wait, it is level. The
  walk without its asking ahead was slower on the busy world.
- **Checked.** 31 of 31 suites. Against the build before, byte for byte:
  seed 7 from tick 7 million to 7.2 million the old way on 1, 2 and 8
  host threads (samples and windows); the same as redesigned on 2
  threads (samples, windows and all six kinds of record); the busy world
  for 60,000 ticks (samples, windows and genome dumps).
- **Measured** (release builds, the gate 3 run beside them; seed 7's
  200,000 ticks from tick 7 million):

  | | Before | After | |
  |---|---|---|---|
  | The old way, 1 host thread | 70 s | 68 s | 3% (7% on an earlier pair: 73 and 68) |
  | The old way, 2 threads | 43 s | 37 s | 14% |
  | The old way, 8 threads | 23 s | 18 s | 22% |
  | As redesigned, 2 threads | 39 s | 34 s | 13% |
  | The busy world, 1 thread, 60,000 ticks | 20.0 s | 20.0 s | level |

  Less on one thread than pass 6 reckoned (15%), since the walk's share
  only overlaps its waits there and is shared out on more; at the 20
  seeds' 2 threads a seed it is 13 to 14%.

### Pass 8: gates 4 and 5 profiled, only (2026-11-23)

On Damian's word ("go ahead and profile now, may as well"), after open-3
ran at 100 to 300 ticks a second against open-2's thousands. Nothing was
changed; the patches wait for the Fable pass on 2026-11-24. The profile
build was rebuilt from the current tree (build-profile, /Zi); the sampler
of passes 1 to 6 now also breaks out the three busiest threads.

- **Timed** (one seed, open-3's settings from tick 0, 10,000 ticks, 24
  host threads, nothing else running; "ticking" leaves out the start-up):

  | World | Ticks a second, ticking | Cores busy | Cells faded | Memory cut out |
  |---|---|---|---|---|
  | 100 places of 8 MB (open-3) | 172 | 1.96 | 877 million | 51.6% of device-ticks |
  | the same, fading made negligible | about 1,700 | 6.9 | 0 | 52.9% |
  | 200 places of 4 MB | about 1,100 | 6.9 | 59 million | 62.7% |
  | 100 places of 4 MB | about 290 | 2.45 | 1,218 million | 52.6% |

  The late world: open-3 from its save at tick 7,523,723 (865 programs,
  52,222 threads), 3,000 ticks with no observers: 111.6 s, 27 ticks a
  second, 5.0 million instructions a second. Pass 7's late seed 7 (gate
  3, 48 places) ran 11,000 ticks a second on 8 threads.
- **Where the time goes: memory fading, in one place at a time.** In
  every profile, early and late, the host threads wait 75 to 98% of the
  time. Of the work that is done, `Noise::fade` with its `cell_of` and
  `mix64` is 54% early and 86% late; the interpreter and scheduler are
  under 1% late. About 1.2 of the 24 threads are busy on average: one
  place's fade runs on one thread while the other 23 wait at the tick's
  barrier.
- **Why fading is so heavy.** A cut-out device fades from rank 0 each
  time: at 0 V a tenth of its cells per doubling of the time cut out
  (memory.fade.fastest 1 tick, slowest 1,000), so 10% after 2 ticks, a
  third after 10, two thirds after 100. An 8 MB device has 67 million
  cells (bits). Back on, the dose goes to 0 and the next cut-out starts
  again. Each cell costs four `mix64` rounds of the permutation, a write
  of one bit at a random place in the device (a cache miss: the world's
  800 MB of memory is far past the cache), and a binary search into the
  threads' saved states: roughly 40 to 70 ns a cell. The 100-place maps
  fade 15 to 20 times the cells of the 200-place map with fewer cut-outs,
  so their cut-outs must go deeper or last longer (not measured which);
  that, not the 8 MB, is most of why open-3 is slower than open-2.
- **With fading out of the way** (the second row, profiled for 30,000
  ticks): the cosmic flips are the largest work, about 38% (95,000 flips
  a tick: `cosmic_flips`, its `mix64`, `geometric_gap` and the logs); the
  interpreter and scheduler about 13%; the pool's spinning about 20% of
  the processor time; the workers still wait 58 to 74%, a tick being
  0.6 ms over 100 uneven places. Start-up (`Place::init`,
  `Memory::init`'s memmove) is about 5 s a run.
- **Recommended for the Fable pass, each exact (the same world byte for
  byte):**
  1. **A big fade shared out over the pool.** The fade of ranks
     [done, upto) is a pure function: rank k's cell is `cell_of(d, k)`,
     its bit is bit (k - done) mod 64 of draw (k - done) / 64 of the
     tick's fade stream (`Rng::at` gives any draw directly), and the
     cells are all different, so chunks can run in any order. Two
     catches: ranks in different chunks can land in the same byte, so
     the bit writes need an interlocked or/and; and the hits must stay in
     rank order, so each chunk keeps its own and they are joined in chunk
     order. The pool runs one level, so the likely shape is two pool runs
     a tick: each place's tick up to its fade, then the fades as chunks
     across all places, then the rest of each place's tick in the same
     order as now. Spreads 6 ms of fading a tick (early) over every
     thread instead of one: perhaps 5 to 6 times the early world's speed,
     and 10 times or more late, where fading is a larger share.
  2. **Each cell cheaper:** the cells of a batch (say 64) worked out
     first, their bytes asked for ahead, then written in the same order
     with the same hits, so the cache misses overlap. Perhaps 2 to 4
     times on fading itself.
  3. **Then the cosmic flips**, the same way (95,000 random writes a
     tick), and the pool's spin length (no result depends on it).
  4. **Start-up's 5 s**, last: only short runs and the tests feel it.

  Together 1 and 2 might take the early 100-place world from about 170
  to 1,000 to 1,300 ticks a second, and 3 toward 2,000: the tenfold
  Damian asked for looks reachable without changing the world, and more
  in a late world.
- **Damian's decision, since they change the world:** fade bigger cells
  (a byte or a word a cell, 8 or 64 times fewer); or fade slower at 0 V
  (memory.fade.fastest and slowest are settings, so a pilot needs no
  code); or ask why the 100-place maps cut out so deep.
- **Not measured:** the observers' cost late (a world save resumes
  without them, and the current build can't read open-3's test save,
  the observers' save tag having moved on); to be measured in the pass.
  For the GPU and NPU design: fading is the clearest candidate, being
  many independent cells.

### Pass 8's arithmetic audit (2026-11-23)

On Damian's question ("is there anywhere we are using divides where we
could coalesce into a multiply or a shift? or using multiplies instead of
shifts? or floats where an int would suffice?"). Read only; nothing was
changed. A divide or multiply by a constant (`% 8`, `/ kGranule`,
`(a + b) / 2`, `* 16`) the compiler already makes a shift, a mask or a
multiply, so only divisors known at run time cost a divide.

- **For the Fable pass, each exact:**
  1. **The memory edge errors work out an exact log for every failed
     bit** (`Noise::memory_errors`, noise.cpp 384 and 400): they call the
     two-argument `geometric_gap`, which works out `gap_log(rate)` (56
     chained 128-bit squarings) each time, where the cores and the cosmic
     flips keep it beside the rate. About 2,100 a tick in open-3's early
     world (21 million in 10,000 ticks); `log2_q56` was about 3% of the
     work with fading out of the way. Keep the log beside `mem_rate_[d]`.
  2. **`geometric_gap`'s 64-bit divide** (noise.cpp 99 and 102): one a
     cosmic flip, edge error and error-clock firing, 95,000 and more a
     tick. Its divisor is fixed for a rate, so a reciprocal worked out
     with the rate (a multiply-high and a shift, with the one correction
     step that makes it exact for every input) gives the same quotient.
     Perhaps 5 to 10% of a flip, the rest being its write.
  3. **The ready queues' `% cap`** (os.cpp 426, 443, 483, 500 to 504,
     526, 1722): up to a dozen 32-bit divides a thread switch. The index
     is under twice the capacity, so a compare and subtract gives the
     same slot. With them, the scheduler's `* k / 10` and `* 10 / k`
     (sched.cpp 292 to 300) and `quantum16`'s divide: a few a switch.
     Under 1% while fading rules, but switching was 45% of gate 3's late
     world, and 52,000 threads will make it count again once the fades
     are shared out.
- **Already as cheap as it gets:** the interpreter (cpu.cpp) divides
  only to run the simulated DIV, which must stay, and is otherwise masks,
  shifts and tables; the fade loop has no divides (its cost is the four
  `mix64` multiplies a cell and the cache miss).
- **Floats:** in the tables made at setup, the fast log (doubles on
  purpose, and fast), and the main thread's weather, currents and events
  a place and a link at a time (current.cpp, world.cpp, links.cpp).
  Integers there would round differently, so it would change the world,
  and it's microseconds a tick: not worth it.
- **What it adds up to:** 1 and 2 perhaps 3 to 5% of the work in a world
  without fading, 3 more in a late one. Small beside the fade's sharing
  out, but exact and easy.

### Pass 9: the arithmetic audit's three (2026-11-23)

On Damian's word ("go ahed with your 3 fixes"), ahead of the Fable
pass. Each is exact: no save changes, and what is new is worked out
again from what is saved, never saved itself.

- **What changed:**
  1. **The memory edge errors keep their log** (`Noise::mem_log`):
     each device's rate and its `gap_log`, beside `mem_rate_`, worked
     out again only when the rate differs, as the cores' and the
     flaky devices' already were. Each failed bit worked out the exact
     log afresh, 56 squarings. The log is kept with the rate it is
     for, so a load, a device replaced or a rate back to 0 needs
     nothing more. The sources' births (`sources.cpp`) still work out
     theirs: one a source born, a few in a million ticks.
  2. **A gap's divide is a multiply** (`GapLog`, `gap_divisor`,
     `gap_quotient`; noise.h). Each kept `gap_log` has its reciprocal
     beside it, (2^64 - 1) / log rounded down, worked out with it. A
     quotient is then the multiply's high half and one compare: the
     multiply falls short of the quotient by under one, so the
     remainder says whether to add one. That holds for every dividend
     and every divisor above 0, so no divide is left anywhere to fall
     back on; a log of 0 is never, as before.
     - Every caller that keeps a log keeps the pair: the cores, the
       cosmic and flaky rates, the edge errors, the weak cells,
       rowhammer, the worn blocks (`blocks.cpp`), the link cuts
       (`links.h`) and the network's corruption (`net.h`). `links.h`
       and `net.h` now include `noise.h` for it.
     - The rest call `geometric_gap(p, r)` as before, which works out
       the pair.
  3. **The ready queues' `% cap`** (`os.cpp`, 8 places, `slot`): a
     slot is head + i, with the head under the capacity and i under
     it too. A full queue is a fatal error before its slot is taken,
     and a load refuses a head or a size out of bounds. So one
     compare and subtraction gives the slot.
  - **The scheduler's divides:** what is left of a turn, worked out at
    each call, takes the quantum already kept for the core's speed
    (`quantum`) instead of dividing again (`quantum16`). Its `* k /
    10` was already a multiply, 10 being a constant. Its `* 10 / k`
    is left: the budget it divides is below 0 after an overrun, so an
    exact form needs the sign apart and a reciprocal for each clock
    level, for one or two divides a quantum.
- **Checked.**
  - **A test** (the noise suite, `test_gap_quotient`): 3,880,470
    quotients by 3,207 divisors, all as the divide gives them.
    - The divisors: 1; each power of two and either side of it;
      2^63 - 3 and + 3; 2^64 - 1, - 2 and - 3; the logs of rates
      from one half to 10^-15 and of rowhammer at full strength; and
      3,000 at random.
    - Each divisor's dividends: 0 and 1; the divisor and either side
      of it; its largest multiple and one under; 2^64 - 2 and - 1;
      2^62, the largest a gap divides; then 300 at random, and 300
      multiples of it at random with one under and one over each.
    - Without the correction step, 1,961,269 of them differ, and the
      gap test fails too.
  - **The suites:** 50 of 50, twice, the determinism suites among
    them. Gate 4's equivalence test is byte for byte on the new build.
  - **Against `build-g5e`, byte for byte:** each world's records
    (its samples among them), windows and genome dumps, and for the
    two of open-3 the world's save at the end. Their reports are the
    same but for timings.
    - **open-3's world** (100 places of 8 MB), from tick 0 to 20,000:
      2.1 million wrong results, 41 million bits failed near an edge,
      1.9 billion cosmic flips, 168,384 rowhammer flips, 72 flaky
      parts and 2 link cuts.
    - **The same with its faults made frequent,** to tick 10,000:
      endurances of 400 to 2,000 ticks, cuts 6,000 times as often,
      disk blocks lasting 20 writes, corruption at 1e-3, a quarter of
      the parts flaky, and visits 250 ticks apart on average. 19,475
      failures, 3,735 parts replaced, 523 respawns, 682 cuts, 7.9
      million wrong results, 7.6 million edge bits and 5.0 billion
      cosmic flips.
    - **The busy world,** 2 seeds of 60,000 ticks, with its 25
      snapshots: 8.4 million messages delivered, at the default
      corruption.
    - **Gate 5's bench part,** its counts: rowhammer's 630,231
      activations past a threshold, and 12,665 flips.
    - **Not seen in a run:** weak cells losing bits and worn blocks
      flipping, which no report counts. Their suites pass, and their
      gaps are drawn by the same function as the rest.
    - **The test's save,** beside the world's, can't be compared. Two
      runs of `build-g5e` differ in 1,150 of its bytes: 4 near the
      start of the counters, and the rest in its last 317 KB. That is
      the observers' last tables, saved as they lie in memory, then
      the seed's result and its running time. In the tables it is one
      byte in each of 100 records of 240 bytes and of 100 of 168, and
      the top bytes of some words: padding or host addresses, by the
      look of it. Against the new build, 1,342 differ, in the same
      parts.
  - **The build:** `build-g5f\bin\nova2.exe`, md5 9b4f6f1e...
- **Measured** (`build-g5e` against `build-g5f`, release builds taking
  turns, nothing else running, 24 host threads; open-3's settings from
  tick 0, one seed):

  | | Before | After | |
  |---|---|---|---|
  | Fading made negligible, 30,000 ticks, wall (three runs each) | 24.1, 24.0, 24.3 s | 21.4, 21.1, 21.7 s | 11% |
  | the same, ticking only (start-up about 5 s either way) | 19 s, 1,580 a second | 16 s, 1,880 a second | 19% |
  | open-3 as it is, 10,000 ticks, wall (two runs each) | 60.5, 59.9 s | 59.7, 57.5 s | 1 to 4% |

  Each pair's windows, records and genome dumps are byte for byte the
  same. More than the audit reckoned (3 to 5%) where fading is out of
  the way. Probably (not measured) because a tick waits for its
  slowest places and those with the most edge errors are among them,
  so the logs no longer worked out come off the tick's length, not only
  its sum. Where fading rules, as in open-3, it is lost in the
  fade's waits, as expected; it shows there once the fades are shared
  out.

### Pass 10: the fade shared out (2026-11-23)

On Damian's word ("go ahead and do the fade spread now"): pass 8's
first item, a big fade shared out over the pool. Only that: each cell
is still worked out and written one at a time (pass 8's second item),
but the loop is shaped for it to go in. The world is the same byte for
byte; nothing new is saved, and the save format is unchanged.

- **What changed:**
  1. **A fade in two parts** (`Noise::fade_plan`, `fade_cells`;
     noise.h). The plan adds each fading device's dose and leaves a
     job for each device with cells to fail: its ranks [done, upto)
     and its stream, counted as failed at once (nothing in the cells'
     loop reads the counts). `fade_cells` fails a job's ranks
     [k0, k1), with k0 a multiple of 64 ranks from the job's start,
     so its first draw is `Rng::at` of that multiple. It works 64
     ranks at a time, their cells first and then their bits, which is
     where the batching and the bytes asked for ahead go in later. It
     changes nothing of the noise's own. `fade` is the two in a row,
     as before.
     - **Chunks of one device at once** write with `_InterlockedOr8`
       and `_InterlockedAnd8`. Two ranks can share a byte but never a
       bit, so each bit comes out the same in any order, and whether
       the byte changed is told by the bit as it was.
  2. **A place's tick in two parts** (`Place::tick_begin`,
     `tick_end`; place.h). `tick_begin` runs the tick as before, but
     a fade of 131,072 cells or more (two chunks) stops before its
     cells, its saved states sorted and its jobs planned. `tick_end`
     joins the chunks' hits in chunk order, notes a change to the
     reset region once if any chunk made one, and does the rest as
     the tick did: the threads hit end, then the edge errors, cosmic
     rays, weak cells, rowhammer, the failed saved states, the draw,
     the wear, the paths, and the next tick. A smaller fade finishes
     its tick as before. `Place::tick` is `tick_begin` that never
     stops; the bench's timed tick fades inline as ever.
  3. **The world's tick** (`World::fade_spread`). After the places'
     run, if no place stopped, nothing more: one pool run, as before.
     Otherwise two more runs: the stopped places' cells in chunks of
     65,536, place by place and device by device, one task each; then
     each stopped place's `tick_end`. A job of one chunk, or a pool of
     one thread, writes its bytes plainly.
     - **Every step moved to `tick_end` was already in the place's
       tick,** in the run where every place's tick goes at once, so
       each touches only its own place's state, as the pool's rule
       says. The one shared thing a place holds, the network, is only
       read there, in a SEND or PING, before the fade. All the places
       to their fades, then the chunks, then the rest is one of the
       orders that run already allowed, and the world's thread does
       nothing between the three runs.
  4. **For tests:** `World::set_fade_spread` sets the chunk and the
     size of fade that stops; `ThreadPool::set_dispatch_reversed`
     runs in reverse every run with no test order of its own, since
     the chunks' runs change length every tick. The world counts the
     fades shared out, their chunks and their cells, for reports
     only.
- **Checked.**
  - **A test** (the fade suite, `test_fade_spread`), to the bit:
    - **A device's fade** in chunks of 64 ranks on 8 host threads,
      every run reversed, against `fade` straight through: 12 ticks at
      0 V, 70,727 cells in 1,119 chunks (309 in the biggest tick), and
      the same memory, the same 24,299 hits in the same order, and the
      same counts and reset-region writes, every tick.
    - **A busy world** of 6 places, 2,000 ticks, on 1 thread with every
      fade in its tick against 8 threads in reverse with fades of
      2,048 cells or more shared out in chunks of 64: 36 fades in
      3,553 chunks, 223,474 of its 306,760 cells, the rest staying in
      their ticks, and 183 threads ended by fading. Each place's
      memory, faded threads, cells faded and reset-region writes the
      same every tick, and the hash at the end.
  - **The determinism suites** run their worlds again on 2 and 24
    threads with fades of 256 cells or more shared out in chunks of
    64, every run reversed, the snapshots' 24-thread resumes too: the
    same hashes and counts. Gate 2's world shared 526 fades in 26,225
    chunks, the fuzz world 7,579 in 238,255. The world suite's
    reversed run shares out its fades too, and the pool suite checks
    the reversed runs.
  - **The suites:** 50 of 50, twice. Gate 4's equivalence test is
    byte for byte on the new build.
  - **Against `build-g5f`, byte for byte,** each pair on the same
    host threads (a save holds its settings, those among them): each
    world's records (its samples among them), windows and genome
    dumps, and the world's save at the end where the run makes one.
    Their reports are the same but for timings. The runs were told
    never to save, report or check memory by the clock, as a slow run
    would at other ticks.
    - **open-3's world** from tick 0 to 20,000, on 1, 2, 8 and 24 host
      threads. The records, windows and genome dumps are also the same
      from one thread count to another.
    - **The same with its faults made frequent** (pass 9's), to tick
      10,000, on 24.
    - **The busy world,** 2 seeds of 60,000 ticks, with its 25
      snapshots: all 55 files.
    - **The late world:** open-3's save at tick 7,523,723 carried on
      2,000 ticks by `nova2 resume` on 24 threads (to 1,020 programs
      and 57,549 threads): its save at the end, 931 MB, and its hash.
    - **The test's save** still can't be compared (pass 9): in the
      faults run 636 of its 53 million bytes differ, in the parts pass
      9 found.
  - **The build:** `build-g5g\bin\nova2.exe`, md5 1d0c72a9...
- **Measured** (`build-g5f` against `build-g5g`, release builds taking
  turns, nothing else running, 24 host threads; "ticking" leaves out the
  start-up, to the second):

  | | Before | After | |
  |---|---|---|---|
  | open-3 from tick 0, 10,000 ticks, wall (two runs each) | 56.1, 57.4 s | 14.3, 14.7 s | 3.9 times |
  | the same, ticking only | 51, 52 s (about 195 a second) | 9, 10 s (about 1,050) | 5.4 times |
  | open-3's late world, 2,000 ticks from its save, the run (two each) | 71.1, 71.6 s (28 a second) | 8.6, 8.7 s (231 a second) | 8.2 times |
  | the same, simulated instructions a second | 5.0 million | 41.7 million | |
  | fading made negligible, 30,000 ticks, wall | 20.6 s | 20.7 s | level |
  | 200 places of 4 MB, 10,000 ticks, wall | 13.5 s | 12.2 s | 10% |

  Each pair's windows, records and genome dumps byte for byte the same.
  With pass 9, open-3's early world ticks about 6 times as fast as on
  `build-g5e` (172 a second), and its late world about 8.5 times (27).
  The cores busy went from 2.1 to 8.4 early. Next, by pass 8's list:
  each cell cheaper (batching and asking for the bytes ahead, which
  `fade_cells` is shaped for), then the cosmic flips and the pool's
  spin.

### Pass 11: each faded cell cheaper (2026-11-23)

On Damian's word ("Yes, do it"): pass 8's second item, each faded cell
cheaper, on pass 10's tree. The world is the same byte for byte;
nothing new is saved, and the save format is unchanged. A profile taken
in the pass showed a cell's cost lay mostly in working the cell out,
not in its byte's cache miss, so a third change went in beside the two
asked for (2 below), for Damian to keep or drop.

- **What changed** (`Noise::fade_cells` unless said):
  1. **The bytes asked for ahead.** Each batch of 64 ranks has its
     cells worked out and their bytes asked for (`_mm_prefetch`, plain
     SSE) one batch before its bits are set. The bits and the hits
     still go in rank order.
  2. **A batch's walks to their cells in step** (the third change). A
     rank's cell is a walk of four-round Feistel steps until the value
     is on the device (`cell_of`). A place's memory is shared among
     its devices, so a device's cells are often a half or a third of
     the Feistel's range: a walk takes two or three steps on average,
     each ending in a branch the processor can't guess. Now the
     batch's 64 walks take each step together, and those still off the
     device are listed for the next without a branch. The cells are
     `cell_of`'s, the one function (`feistel`) doing each step.
  3. **The saved states' blocks** (`saved_blocks`; noise.h,
     place.cpp). When a fade has saved states to look in, the place
     marks a bit for each 256 bytes of its memory that any state
     [lo, hi) has a byte in, and a failed cell in a block whose bit is
     clear skips the binary search. Worked out each fade, never saved;
     the chunks only read it.
  4. **Smaller chunks** (world.h): a fade of 8,192 cells or more now
     stops and goes out in chunks of 4,096 (pass 10: 131,072 and
     65,536). Neither changes the world (pass 10).
- **Tried, kept and dropped** (release builds taking turns, nothing
  else running, 24 host threads; E: open-3 from tick 0, 10,000 ticks,
  the ticking seconds; L: open-3's late save carried on 2,000 ticks,
  the run's seconds; means of two to six runs, every L the same hash):

  | Build | E | L |
  |---|---|---|
  | pass 10 (`build-g5g`) | 8.38 | 7.83 |
  | 1: bytes asked for ahead | 8.21 | 7.37 |
  | 1, two batches ahead | 8.22 | 7.35 |
  | 3: the blocks, alone | 8.35 | 7.68 |
  | 1 and 3 | 8.19 | 7.23 |
  | 2: walks in step, alone | 7.09 | 5.78 |
  | 1 and 2 | 6.95 | 5.37 |
  | 1, 2 and 3 | 6.93 | 5.26 |
  | the same, two batches ahead | 6.92 | 5.27 |

  | 1, 2 and 3: chunk, and the fade that stops | E | L |
  |---|---|---|
  | 131,072, 262,144 | 7.74 | 6.84 |
  | 65,536, 131,072 (pass 10) | 6.79 | 5.17 |
  | 32,768, 65,536 | 6.37 | 4.25 |
  | 16,384, 131,072 | 5.97 | 4.15 |
  | 16,384, 32,768 | 6.01 | 3.80 |
  | 16,384, 16,384 | 5.80 | 3.73 |
  | 8,192, 16,384 | 5.74 | 3.63 |
  | 8,192, 8,192 | 5.67 | 3.53 |
  | **4,096, 8,192 (kept)** | 5.73 | 3.52 |
  | 4,096, 4,096 | 5.66 | 3.53 |
  | 2,048, 4,096 | 5.57 | 3.47 |

  - **Two batches ahead** were no faster than one, with or without
    the walks in step: dropped.
  - **The blocks** gain little alone, as the search was small (below),
    but about 2% late with the rest; kept. On one host thread early
    they looked 3 to 4% slower, though they do nothing there, and a
    profile showed the whole run slower, its start-up too: the thread
    had landed on another kind of core. Pinned to one P core (E for
    3,000 ticks): 23.12, 23.21, 23.12 s against 23.12, 23.21, 23.07
    without the walks in step, and 18.94, 18.88 against 18.64, 18.67
    with them, which can only be the code's layout.
  - **The chunks:** from 8,192 down all within about 3% of each other.
    4,096 keeps pass 10's rule (a fade of two chunks stops) and goes no
    smaller than the gain. Worlds that fade less are level: open-3's
    settings with 200 places of 4 MB, 10,000 ticks, 6.19 to 6.39 s on
    pass 10, 65,536 and 4,096; gate 3's static world, 20,000 ticks,
    3.22 to 3.27 s on all three.
  - **The interlocked writes** don't undo the bytes asked for ahead: 1
    alone gained 5.4% late on 24 threads, no less than on one thread.
- **Where the time went** (the sampler of passes 1 to 8 on a profiled
  build, one host thread, with 1 and before 2):
  - **Early:** of the fade's own time about 73% was working out the
    cells, 27% setting the bits and the loop, and nothing the
    saved-state search: the early fading places have no saved states.
  - **Late:** the fade about 80% of the ticking; the search about 7%
    of the fade, and with the blocks a quarter of that, their own test
    included.
- **Checked.**
  - **Tests** (the fade suite, to the bit):
    - **`test_saved_blocks`:** 119 saved states at the edges of
      256-byte blocks (ending at one, starting at one, a handler's
      frame ending at one inside a state across it, starting a byte
      before one), 12 ticks at 0 V: looking only in the blocks they
      reach gives the same 7,669 hits in the same order, memory,
      counts and reset-region writes as looking for every cell, with
      233 of the states' first and last bytes faded; the 23 blocks
      past a state ending at an edge are clear.
    - **`test_fade_walks`:** 70,727 ranks over 12 ticks, 47,151 of
      them on a device whose Feistel range is twice its cells; each
      tick's memory is the pattern it started from with each rank's
      draw bit at `cell_of`'s cell, and nothing else changed.
    - **`test_fade_spread`** as in pass 10: the same 24,299 hits in
      1,119 chunks, and the busy world's 36 fades in 3,553 chunks.
  - **The determinism suites:** their reference runs, which the
    shared-out runs are held to, are now told to keep every fade in
    its tick. With the new defaults they shared out fades too, and the
    check that the reference shared none failed (every hash matched).
  - **The suites:** 50 of 50, twice. Gate 4's equivalence test is
    byte for byte on the new build.
  - **Against `build-g5g`, byte for byte,** each pair on the same host
    threads, as in pass 10 (records, windows, genome dumps and the
    world's save; reports the same but for timings; no saves, reports
    or memory checks by the clock):
    - **open-3's world** from tick 0 to 20,000 on 1, 2, 8 and 24 host
      threads: 16 of 17 files the same each time, the world's save
      among them, the test's save the one apart. The records, windows
      and genome dumps are the same from one thread count to another.
    - **The same with its faults made frequent** (pass 9's), to tick
      10,000, on 24: the same, and 618 of the test save's 53 million
      bytes differ (636 in pass 10).
    - **The busy world,** 2 seeds of 60,000 ticks with its 25
      snapshots: all 55 files.
    - **The late world:** open-3's save carried on 2,000 ticks on 24
      threads, to 1,020 programs and 57,549 threads: its 931 MB save
      and its hash, 25132543da65ce55.
  - **The build:** `build-g5h\bin\nova2.exe`, md5 29ca4807...
- **Measured** (`build-g5g` against `build-g5h`, release builds taking
  turns, nothing else running, 24 host threads):

  | | Before | After | |
  |---|---|---|---|
  | open-3 from tick 0, 10,000 ticks, wall (two runs each) | 13.3, 13.1 s | 10.7, 10.7 s | 1.24 times |
  | the same, ticking only (to the second; the finer figures above: 8.38 and 5.73 s) | 8, 8 s | 6, 6 s | 1.3 to 1.5 times |
  | open-3's late world, 2,000 ticks from its save, the run (two each) | 7.69, 7.68 s (260 a second) | 3.60, 3.50 s (563 a second) | 2.2 times |
  | the same, simulated instructions a second | 47 million | 102 million | |
  | fading made negligible, 30,000 ticks, wall | 18.2 s | 18.3 s | level |
  | 200 places of 4 MB, 10,000 ticks, wall | 11.8 s | 11.7 s | level |

  Each pair's windows, records and genome dumps byte for byte the same.
  **Passes 9 to 11 together, against `build-g5e`:** open-3 from tick 0
  ticks about 1,700 a second where it ticked about 180 (ticking 55 to
  56 s for 10,000 ticks, now 5.7 to 6), about 9.5 times; with start-up,
  60 s to 10.7. Its late world runs 2,000 ticks in 3.5 s where it took
  about 74 (27 ticks a second to 563), about 20 times, and 5.0 million
  simulated instructions a second to 102 million. Next, by pass 8's
  list: the cosmic flips and the pool's spin (the largest work left with
  fading out of the way), then start-up's 5 s; and the observers' cost
  late, still not measured.

### Pass 12: the cosmic flips and the pool's waits (2026-11-24)

On Damian's word ("sure"): pass 8's third item, the cosmic flips and the
pool's spin and waits, on pass 11's tree. And one found on top, which
the session that asked for the pass let in on its own word, for Damian
to keep or drop: `Place::end_failed_states`, the late world's longest
step (pass 6's "walks every thread for each failed byte"). The world is
the same byte for byte; nothing new is saved, and the save format is
unchanged.

- **Where the time went** (`build-g5h`'s tree, 24 host threads: the
  sampler of passes 1 to 8, and a measuring build that timed each step
  of the world's tick and each place's task by the time-stamp counter;
  E: open-3 from tick 0, L: its late save carried on 2,000 ticks, NF:
  open-3 with fading made negligible):

  | A tick, µs (means) | E | NF | L |
  |---|---|---|---|
  | the whole tick | 563 | 438 | 1,746 |
  | the places' run (run 1) | 374 | 374 | 561 |
  | the fades shared out (runs 2 and 3) | 125 | 0 | 1,102 |
  | the world thread's own steps | 64 | 64 | 83 |
  | run 1: the places' times summed, over 24 | 213 | 212 | 114 |
  | run 1: its longest place | 263 | 265 | 528 |
  | run 1: its last task's end | 368 | 369 | 554 |

  - **Run 1's tail.** A tick's 100 places take 10 to 320 µs each, and
    the last one ended about 100 µs past both bounds (the longest place
    and the sum over 24). Replaying each tick's measured times, places
    taken in order ended at 342 µs; taken heaviest first by the last
    tick's times, at 287; by the tick's own, at 269. Workers that had
    gone to sleep took up to 50 µs to start (21 on average).
  - **The places' own work** (NF, each place's parts timed as the
    benchmark times them): per tick, 2,276 µs of thread time in the
    cosmic phase and 2,390 in execution, of 5,080. The cosmic phase is
    the flips and then the saved states held against the bytes that
    failed (`end_failed_states`), and here nearly all the flips: timed
    apart later, the check was 4 µs of it. The heaviest places are
    heavy with flips: places 42, 24 and 94 spend 110 to 150 µs a tick
    on them, most others about 17 (a flaky device's flips come faster,
    the likely reason; not looked into). So the flips make run 1's
    longest tasks.
  - **Within the flips** (95,000 a tick, about 24 ns of thread time
    each): about 70% of their samples fell on the instruction after the
    flip's `xor`, waiting for its byte, though asked for eight flips
    ahead; about 20% on the instruction after the next byte's
    prefetch; the draws' arithmetic about a tenth. `geometric_gap` was a
    call, and the countdown and the count went through memory at
    every flip.
  - **The world thread's own steps** (E, per tick): the inputs, the
    voltage solve and the links' wear 19 µs; the network step or the
    killer 10; heat 5; the observer's ingest 19, after_tick 6 and
    sampling 3. In L: 29, 44 to 51, 6 and 2.
  - **The late world** is another shape: run 1 is one place, 21, whose
    521 µs a tick are nearly all `Place::end_failed_states` (467 to 472
    µs, timed apart; place 80's, 113 of its tick, is next);
    the fades shared out are 1,765 µs on 62% of ticks, their chunks
    38 ms of thread time a fade tick, spread to within 7% of even; and
    run 3, 18 µs of work, took 60 µs, waiting for sleeping workers to
    wake and leave.
- **What changed:**
  1. **The flips drawn further ahead of being made**
     (`Noise::cosmic_flips`). Up to 1,024 flips are drawn, and their
     bytes asked for, before the oldest is made; most devices take fewer
     in a tick, so their flips are all drawn first and then made one
     after another, and the waits for memory overlap. The draws and the
     flips are each in the order they were, and the countdown left at
     the tick's end is the same; the countdown and the count are kept in
     registers and stored once.
  2. **The draw's arithmetic inlined into the flips' loop**: the fast
     log, the quotient by the reciprocal and the gap
     (`near_log`, `quotient`, `gap_of`, written once in noise.cpp for
     `log2_q56_near`, `gap_quotient`, `geometric_gap` and the loop). The
     same arithmetic; only the fast log's conversion goes through a
     signed value, which for a value under 2^53 is the same.
  3. **Places handed out heaviest first** (world.h, world.cpp). Each
     place's task times its tick by the time-stamp counter; before the
     next run 1 the places are put in order of those times, heaviest
     first, to a quarter of a doubling (a count over 256 steps, then by
     place), and the tasks take them in that order. Each place's tick
     is its own, so no order changes a result (pool.h); the tests'
     dispatch orders still permute it.
  4. **A shorter spin** (pool.cpp): a thread with nothing to do spins
     25,000 cycles, about 7 µs, before it sleeps (pass 3: 200,000,
     about 54).
  5. **The failed bytes' blocks marked** (`Place::end_failed_states`,
     place.h, place.cpp). Each tick every thread's saved registers and
     handler frame were held against every byte that failed, four
     threads at a time: threads times bytes. Now, with many threads
     and more than a few bytes, a bit is set first for each 64-byte
     block holding a failed byte, and for the block 23 bytes before it.
     A state holding a failed byte (24 bytes, or a frame's 20) starts
     between the two, so in one of those blocks, whether it lies within
     one block or across two; a thread neither of whose states starts
     in a marked block is passed by, with two lookups, and for one that
     does the bytes decide as before. The threads go in order, each as
     it stands when it is reached, so the same ones end in the same
     order with the same cause. The marks are cleared after, a block
     past memory's end stands for `kNowhere`, and a byte past memory's
     end (which no state holds) leaves the call to the plain way. When
     the marks pay is a rule from their measured costs (below): with
     few threads the plain way stays.
- **Tried, kept and dropped.**
  - **The flips** (NF, 10,000 ticks, 24 host threads, each place's
    parts timed: the cosmic phase's thread time a tick, summed over
    places, the flips and the 4 µs check after them, which these builds
    left alone; means of two to four runs, taking turns):

    | Build | The cosmic phase, µs a tick |
    |---|---|
    | pass 11 (8 drawn, then made) | 2,283 |
    | a ring, 64 ahead | 2,135 |
    | a ring, 128 ahead | 1,928 |
    | the same with 2 (inlined), 64 ahead | 2,062 |
    | the same, 128 ahead | 1,867 |
    | the same, 256 ahead | 1,692 |
    | the same, 512 ahead | 1,544 |
    | **the same, 1,024 ahead (kept)** | 1,534 |
    | the same, 2,048 ahead | 1,523 |
    | 128 ahead, asked for into L2 (`_MM_HINT_T1`) | 1,876 |
    | 512 ahead, into L2 | 1,567 |
    | a batch drawn, then made, 512 at a time, asked for | 2,484 |
    | the same, 2,048 at a time | 2,493 |
    | a batch drawn, then made, not asked for, 128 | 1,924 |
    | the same, 512 | 1,920 |
    | the same, 2,048 | 1,909 |

    The flips' time fell by a third. Deeper rings kept gaining to
    about 512, though a byte asked for 128 flips ahead should long have
    come: a profile at 128 still had the flip's `xor` waiting. It looks
    as if the bytes asked for are mostly not there in time and what
    gains is the flips made back to back, many misses at once; but a
    batch drawn and then made, the same thing written plainly, was no
    better than 128 without asking and far worse with it. I didn't find
    why. 2,048 is level with 1,024 and twice the stack; 1,024 kept.
  - **Whole ticks** (release builds taking turns, nothing else running,
    24 host threads; E 10,000 ticks and NF 15,000: ticking seconds;
    L: the run's seconds, every L the same hash, 25132543da65ce55;
    means of two, four for 50,000; the rows from four sets of runs, each
    taking turns, a build measured in two sets within 1% of itself):

    | Build | E | NF | L |
    |---|---|---|---|
    | pass 11 (`build-g5h`) | 5.64 | 6.61 | 3.51 |
    | 1 alone: a ring, 16 ahead | 5.56 | 6.52 | 3.56 |
    | a ring, 32 ahead | 5.51 | 6.41 | 3.56 |
    | a ring, 64 ahead | 5.41 | 6.31 | 3.53 |
    | 1 and 2, 1,024 ahead | 5.29 | 6.10 | 3.46 |
    | 1 to 3: and heaviest first | 5.01 | 5.63 | 3.46 |
    | 1 to 3, spin 16 million cycles | 8.87 | 13.03 | 8.37 |
    | spin 4 million | 8.39 | 8.68 | 5.17 |
    | spin 4 million, without 3 | 8.34 | 13.12 | 5.14 |
    | spin 1 million | 6.04 | 6.59 | 3.78 |
    | spin 400,000 | 5.31 | 5.99 | 3.50 |
    | spin 200,000 (pass 3's) | 5.01 | 5.64 | 3.40 |
    | spin 100,000 | 4.88 | 5.48 | 3.41 |
    | spin 50,000 | 4.82 | 5.36 | 3.40 |
    | **spin 25,000 (kept)** | 4.77 | 5.28 | 3.38 |
    | spin 10,000 | 4.76 | 5.34 | 3.40 |
    | no spin | 4.80 | 5.27 | 3.37 |
    | 1 to 4 (`build-g5i` before 5), sets 5 and 6 | 4.80 | 5.32 | 3.40 |
    | **1 to 5: and the failed bytes' blocks marked (kept; place 21's check 467–472 µs a tick, now 68–70)** | 4.76 | 5.30 | 2.94 |

    (The last two rows: two more sets, the two builds taking turns,
    medians of six, L of three; level in E and NF, and L 1.16 times.)

    | Spin (with 1 to 3) | O2 | G3 | E, 8 threads |
    |---|---|---|---|
    | 200,000 | 5.27 | 3.31 | 7.73 |
    | 50,000 | 5.06 | 3.38 | 7.73 |
    | **25,000** | 5.04 | 3.37 | 7.82 |
    | none | 5.17 | 3.44 | 7.74 |
    | 25,000, and 5 (two each, against 1 to 4's 5.04 and 3.31) | 5.04 | 3.34 | |

    (O2: open-3's settings with 200 places of 4 MB, 10,000 ticks; G3:
    gate 3's static world, 20,000 ticks; E on 8 host threads, 10,000.)
  - **Heaviest first** gained 5 to 8% on top of the flips, as the
    replay said it would: run 1 now ends within 35 µs of its bound
    (below). It does nothing in L, whose run 1 is one place.
  - **The spin** was asked to be tuned up or down. Longer was much
    slower: at 4 million cycles (about 1.1 ms, so that no worker ever
    slept) E took 8.4 s, not 5.0. With a thread for every core, a
    worker spinning holds a core the host's other threads then take
    from one with work, is my guess; I didn't measure that. Shorter was
    faster, to about 50,000; below it level. 25,000 is at least as fast
    as 50,000 everywhere at 24 threads; gate 3's static world, the
    world the spin was made for (pass 3), is 1.6% slower than at
    200,000, and on 8 threads E is level to about 1%.
  - **The world thread's own steps alongside run 1:** none of them can
    be, exactly, as the code stands. The inputs, the voltage solve and
    the links' wear go before it: every place's tick reads them (its
    voltage, its links). The network step, the killer and heat read
    every place's tick, so they follow the barrier. The observer's
    ingest takes each place's facts into its tables in place order, one
    observer; after_tick and sampling look at the whole world. Moving
    any of it would be a change to the observer, not to the pool.
  - **The failed bytes' blocks** (5; measuring builds that timed
    `end_failed_states` apart from the flips, 24 host threads, two
    runs each, taking turns; its thread time a tick, summed over
    places):

    | `end_failed_states`, µs a tick | L | place 21 | place 80 | E | NF |
    |---|---|---|---|---|---|
    | each thread against each byte (`build-g5i`) | 639 to 646 | 467 to 472 | 113 | 3.6 | 4.0 to 4.1 |
    | marks with more than 8 bytes | 163 to 166 | 68 to 71 | 16 | 238 | 228 |
    | **marks where they pay (kept)** | 100 to 101 | 68 to 70 | 16 | 3.5 to 3.6 | 3.9 to 4.0 |

    Marking whenever more than 8 bytes failed took open-3's early
    places from 4 µs a tick to 228 and 238: most have a handful of
    threads or none, and about 950 failed bytes a tick, the flips'
    bytes, so setting and clearing the marks cost more than the plain
    way's work. From these runs the marks cost about 9 cycles a byte
    and 5 a thread; the plain way about 2.5 for four threads and a
    byte. So they are used when threads × bytes > 8 × threads + 16 ×
    bytes. With the marks' 16 KB a place (8 MB in 64-byte blocks)
    among the place's other scratch, the fading-negligible world
    ticked 0.8% slower in release builds (medians of seven, 5.34 s
    against 5.29), though its check took the plain way; with them last
    in the place, level (5.30 against 5.32, medians of six). Runs vary
    by 1 to 2% from one to the next, so that may be noise; they are
    last either way.
- **Where the time goes now** (the same measuring build on the kept
  code, 1 to 5; µs a tick, means; in brackets, L with 1 to 4):

  | | E | NF | L |
  |---|---|---|---|
  | the whole tick | 478 | 355 | 1,447 (1,698) |
  | the places' run (run 1) | 288 | 290 | 269 (531) |
  | the fades shared out | 125 | 0 | 1,105 (1,092) |
  | the world thread's own steps | 64 | 65 | 72 (75) |
  | run 1: summed over 24 | 189 | 188 | 86 (110) |
  | run 1: its longest place | 246 | 248 | 230 (513) |
  | run 1: its last task's end | 282 | 284 | 263 (524) |

  - **Run 1** now ends 35 µs past its bound, where it was 100: about
    the time a sleeping worker takes to wake. Its bound is the longest
    place, and the longest places are still the flaky ones' flips.
  - **The flips** are now about 16 ns of thread time each, the cosmic
    phase about 1,530 to 1,730 µs a tick (from one set of builds to
    another), against about 2,400 in execution.
  - **The world thread's steps** are 64 µs of E's 478: 13% of the tick,
    with every worker idle.
  - **Late,** no one place makes run 1 now: its longest place a tick is
    230 µs on the mean, and the heaviest on the mean are places 2 and
    21 at about 90. The fades shared out are three quarters of the
    tick, and the next thing to look at late.
- **Found on top:**
  - **`Place::end_failed_states`, late,** changed (5 above). Reported
    before any change, and let in by the session that asked for the
    pass on its own word, not Damian's: an exact speed-up, and pass 6's
    item. Place 21 has 48,868 threads, and the check was nearly all of
    its tick, the whole of late run 1's length.
  - **Not changed: the path hash** (`run_steps`, an observer's
    measure): a `mix64` on every instruction executed, each on the
    last, about 14% of the places' work in NF's profile. Each depends
    on the one before, a chain the processor can't overlap, and I see
    no exact way to make it cheaper that keeps its values.
- **Checked.**
  - **Tests:**
    - **`test_cosmic_ahead`** (the noise suite): 120 ticks of a 24 KB
      device whose cosmic rate falls from 2e-2 (about 3,900 flips a
      tick, so most are made as later ones are drawn) to 2e-3 (about
      390, all made at the tick's end), the failed bytes listed two
      ticks in three. The bytes, the list in order, the 170,753 flips
      and the 7,099 reset-region writes are held to their values from
      pass 11's loop.
    - **`test_fails_blocks`** (the fails suite): 378 saved states and
      frames at the edges of 64-byte blocks (ending at one, starting
      at one, across one, a frame inside a state across one, a frame
      ending at one, starting a byte before one), each held against a
      failed byte at every one of 4,096 addresses in turn: each state
      holding the byte starts in a marked block, and none a block or
      more away does. The same with every block's first and last bytes
      failed at once. With every byte failed, `kNowhere` is in no
      marked block, and the marks clear again. In a place, 48 sleepers
      and 48 sleeping in a handler, with 370 failed bytes just outside
      their states and at their and their blocks' edges: the marks end
      the same 61 threads in the same order as holding every thread
      against every byte, and the worlds' hashes match; a byte past
      memory's end leaves the call to the plain way.
  - **The suites:** 50 of 50, twice, the determinism suites among
    them (their dispatch orders now permute the heaviest-first order).
    Gate 4's equivalence test is byte for byte on the new build.
  - **Against `build-g5h`, byte for byte,** each pair on the same host
    threads, as in passes 10 and 11 (records, windows, genome dumps
    and the world's save; reports the same but for timings; no saves,
    reports or memory checks by the clock):
    - **open-3's world** from tick 0 to 20,000 on 1, 2, 8 and 24 host
      threads: 16 of 17 files the same each time, the world's save
      among them, the test's save the one apart (pass 9). The records,
      windows and genome dumps are the same from one thread count to
      another, and every report's instructions executed,
      7,808,712,071.
    - **The same with its faults made frequent** (pass 9's), to tick
      10,000, on 24: the same, and 616 of the test save's 53 million
      bytes differ (618 in pass 11).
    - **The busy world,** 2 seeds of 60,000 ticks with its 25
      snapshots: all 55 files.
    - **The late world:** open-3's save carried on 2,000 ticks on 24
      threads: its 931 MB save and its hash, 25132543da65ce55.
  - **The build:** `build-g5i\bin\nova2.exe`, md5 e4e7a869...
- **Measured** (`build-g5h` against `build-g5i`, release builds taking
  turns, nothing else running, 24 host threads unless said):

  | | Before | After | |
  |---|---|---|---|
  | open-3 from tick 0, 10,000 ticks, wall (two runs each) | 10.7, 10.7 s | 9.9, 9.7 s | 1.09 times |
  | the same, ticking only (to the second) | 6, 6 s | 5, 5 s | |
  | the same, CPU time | 96, 102 s | 82, 80 s | 0.81 |
  | open-3's late world, 2,000 ticks from its save, the run (two each) | 3.50, 3.47 s (574 a second) | 2.89, 2.91 s (690 a second) | 1.20 times |
  | the same, simulated instructions a second | 103 to 104 million | 124 to 125 million | |
  | fading made negligible, 30,000 ticks, wall | 18.2 s | 15.8 s | 1.15 times |
  | the same, CPU time | 195 s | 148 s | 0.76 |
  | 200 places of 4 MB, 10,000 ticks, wall | 11.8 s | 10.4 s | 1.13 times |
  | open-3 from tick 0 on 1 host thread, 3,000 ticks, wall | 24.1 s | 20.7 s | 1.16 times |

  Each pair's windows, records and genome dumps byte for byte the same,
  the 1-thread pair's too; every late run's hash 25132543da65ce55. The
  CPU time fell more than the wall time: the shorter spin. **Passes 9 to
  12 together, against `build-g5e`:** open-3 from tick 0 ticks about
  2,100 a second (the session's finer 4.76 s for 10,000 ticks) where it
  ticked about 180 (55 to 56 s), about 11.5 times; with start-up's 5 s,
  60 s to 9.8. Its late world runs 2,000 ticks in 2.9 s where it took
  about 74 (27 ticks a second to 690), about 25 times, and 5.0 million
  simulated instructions a second to 125 million. Next, if wanted: late,
  the fades shared out (three quarters of the tick); early, start-up's
  5 s and the flaky places' flips (run 1's longest tasks); the path
  hash's chain (about 14%) has no exact cut found; the observers' late
  cost still not measured.

### Pass 13: a full world profiled, only (2026-11-24)

On Damian's word ("Yes, profile it"), after runs/open-5 (open-4 with seven
times the sources and cooling, so its places fill) fell from about 475
ticks a second to about 300, with spells at 20 to 30. Nothing was changed.
Passes 9 to 12 were measured on open-3's nearly empty world; a full one
spends its time elsewhere. open-2, as full, ran 175 to 270 a second on its
old build.

- **How:** open-5's save at tick 1,826,900 copied to a scratch folder and
  carried on with `nova2 resume` (the world alone, no observers) on
  `build-g5i`; sampled on `build-profile`, rebuilt from the tree at
  8c55e12. The world there: 64,000 to 83,000 programs, 1.7 to 2.0 million
  threads, 232,000 instructions a tick (99,000 of them idle runs), 1,893
  SPAWNs a tick starting 1,554 threads and 44.5 programs, 600 ALLOCs.
  (open-2 at 21 million ticks: 5.1 million threads, 1,759 threads started
  a tick, 344,000 instructions.)
- **Timed** (2,000 ticks, the same hash b118ac48e90a0e2a each, open-5
  running beside it):

  | Host threads | 24 | 8 | 4 | 2 | 1 |
  |---|---|---|---|---|---|
  | The run | 4.76 s (420 a second) | 5.90 s | 9.11 s | 16.39 s | 34.42 s |

  So a tick is about 17 ms of processor time, 24 threads buy 7.2 times,
  and the world alone gives the live run's rate: the observers aren't the
  main cost. A place is one task, so a tick waits for its heaviest place;
  on 24 threads the workers wait 83% of the time.
- **Sampled** (30,000 ticks, 12 host threads, 92.8 s; shares of the busy
  samples):
  - `Scheduler::tick` 45%: `run_quantum` 38% (`run_thread` 22%, `Os::call`
    17% with `Os::spawn` 8.7%, `Memory::alloc_on` 5.5% for the new stacks,
    `new_thread` 5%, `end_thread` 3.7%), `begin_tick` 4%;
  - `Place::end_failed_states` 18% (`failed_marked`, `end_failed_marked`,
    `end_failed_among`);
  - the shared fades (`World::chunk_one`) about 20%;
  - the interpreter itself (`step_one`, `run_steps`) about 5%; `mix64` 5%;
  - alone on the main thread: `World::network_step` 14.5% of the ticking's
    wall time (`Os::deliver`, `mix64`, `bytes_held`), `fade_spread` 9.6%.
- **What grows with a place's or a program's threads:**
  1. `Os::stack_of` (os.cpp:819) walks every thread of a program. FREE
     calls it for any stack-sized allocation of the caller's (os.cpp:1216),
     SPAWN for any target that isn't the caller's own (os.cpp:892).
  2. `Os::spawn` (os.cpp:889) walks every thread of the caller before
     that, reading each one's saved IP from its stack (`caller_ip`,
     `runs_on`), unless the target holds the caller's IP: twice the
     threads for each program born.
  3. `Scheduler::begin_tick` (sched.cpp:94 to 103) reads `attention` and
     `due` for every thread of the place each tick.
  4. `Place::end_failed_marked` visits every thread of a place with failed
     bytes each tick.
  5. `Os::front` drains stale ready entries; `Os::pick_by_bytes` sums every
     program's allocations twice.
- **The spells are item 1.** The live run's window read 30 ticks a second
  at tick 2,343,566. The copy carried on to tick 2,342,000 (`--save`) and
  sampled for 5,000 ticks took 356 s, 14 a second: `Os::stack_of` under
  `Os::call` (FREE) was 19,897 of about 22,700 busy samples, 88%, one host
  thread busy and the rest waiting. A program with very many threads
  freeing stack-sized allocations walks them all each call.
- **Next, if wanted, in order:** `stack_of` without the walk (what a stack
  is known when a thread starts and ends); SPAWN's walk; the failed
  states by the blocks the threads' saved states lie in; `begin_tick` by
  what is due; the stacks' allocation; delivery off the main thread. Each
  exact, and with the world's and the test's saves still read, so open-5
  can be carried on by the new build.

### Pass 13: what grows with the threads, cut (2026-11-24)

On Damian's word ("Yes, fix them"): the profile's five, on its tree
(8c55e12). `build-g5j` has 1 and 2, `build-g5k` all that held. The
world is the same byte for byte; nothing new is saved, and neither the
world's save nor the test's changes format, so open-5 can be carried on
by either build. Every index added is worked out again when a save is
loaded (`Os::reindex`, `Memory::resum`) and checked by the invariant
checks (`Os::check`, `Memory::check`).

- **Where the time went** (open-5's save at tick 1,826,900, 2,000
  ticks on 24 host threads, open-5 running beside it; a counting build
  and timing copies of the tree and of the new code, each place's parts
  and the network step's timed by the time-stamp counter, taken in
  turns):
  - **The spells** were item 1: a program with very many threads
    freeing stack-sized allocations walked them all each FREE.
  - **The network step,** alone on the main thread, was 546 to 621 µs
    of a tick of about 2,900: delivery 294 to 314 (each message walked
    every thread record of its place twice, about 5 MB), the killer 78
    to 95 (every program record twice, each live one's allocations
    summed twice), the network's own step about 60, and the rest (the
    SENDs posted, the current, the SENSEs answered, the routes planned)
    100 to 140.
  - **`Memory::first_set`** read 97 words a call on the mean (about
    11,000 calls a tick), word by word to the next free page or open
    slab.
  - **`begin_tick`** read about 1.9 million thread records a tick
    across the places, for about 21,000 with an attention bit and 580
    due (the counting build's counts, from 1,600 ticks).
- **What changed:**
  1. **`Os::stack_of` without the walk** (`build-g5j`). `Os::stacks_`
     maps a program's holder and a stack's base to how many of its live
     threads have their stack there: one, but for a stack freed under
     its live thread and handed out again, as it can be with
     `os.stack_held` off. `new_thread` and `end_thread` keep it.
  2. **SPAWN's walk** (`build-g5j`). `stack_of`, now cheap, is asked
     before the walk (each is a pure test, so their order can change),
     and in the walk a thread whose saved IP lies in none of the
     target's pages is passed by without `runs_on`, which can only be
     true there. The walk still visits each of the caller's threads:
     their saved IPs are memory bytes any write can change, and I found
     no exact index of them.
  3. **The failed states by the OS's lists by block.** The OS keeps,
     for each 64-byte block of memory (the marks' blocks), the threads
     whose stack starts in it (`Os::first_in_block`, `next_in_block`).
     A thread's saved registers start at its stack's base and a
     handler's frame at most `os.stack` less `kHandlerFrameBytes` past
     it (`enter_handler`), so a state starting in a marked block belongs
     to a thread whose stack starts there or at most `reach` blocks
     before. `end_failed_marked` gathers those threads in a bitmap and
     takes them in record order with the same test as before: the same
     threads end, in the same order, with the same cause. A list read
     is a random one where the walk of every thread reads in order, so
     the lists are used only with more than `Place::lists_pay` (12)
     threads for each list the failed bytes may read; otherwise every
     thread is walked as before. Of 4, 12 and 32, 12 measured best.
  4. **`begin_tick` by groups.** The OS keeps the least `due` of each 64
     thread records (`Os::due_min`), lowered at once and, when the
     least itself goes up, worked out again from the 64. `begin_tick`
     passes over a group whose least is still to come: `due` is exactly
     the tick its tests of a record would first pass.
  5. **Item 5:**
     - **`Memory::first_set`** finds the next word with a bit set from
       a summary, a bit for each word of the free-page bitmap and of
       each open-slab bitmap (`free_sum_`, `open_sum_`), so 64 empty
       words are passed at once. `alloc_on`, `room_on` and the span
       search all use it.
     - **`Os::deliver`** finds a port's RECV waiters and data handlers
       from keys kept by `note` (1 + the port a thread waits on, 1 +
       its data handler's port, and each port's counts): every change
       of what deliver tested (a thread's state, `recv_port`, its data
       handler, `data_port`) already comes through `note`. It reads the
       keys up to the last of the port's threads, and picks as before:
       the longest waiting, ties to the lowest record. A port past
       `os.ports` (sent from a place with more) is looked for record by
       record as before.
     - **The killer's draw** (`Os::pick_by_bytes`) reads up to the
       highest live program record (`program_top_`, kept as programs
       start and end) and sums each program's bytes once.
     - **`Os::front`**: no cut. Each stale entry is read once and
       dropped, so the work is already the entries queued.
     - **The network step** stays on the main thread: with delivery
       cut it is 190 to 226 µs a tick.
- **The saves:** what a test's save holds that another build must
  match, unchanged here: `sizeof(SeedResult)`; the element size of
  every table written raw (the observers' tables and their HashMaps',
  the loop's `LineageClock` tables); the fixed arrays sized by enums
  (`DeathCause`, `BirthCause`); the counters' list and order, which
  `load_counters` must use up exactly; the observer's settings words;
  and the closing hash over every byte. No counter was added.
- **What it costs:** per place, the lists by block 4 bytes for each
  64-byte block and each thread record, the ports' keys 8 bytes a
  record, the groups' least 8 bytes for 64 records, the summaries a bit
  for each bitmap word: about 2 MB a place of 8 MB. Carrying open-3's
  late save on 200 ticks peaked at 9,878 MB against `build-g5j`'s 9,665
  (twice each). Each is placed last in its class, so no other member
  moves.
- **Timed** (the timing copies taken in turns, three rounds; µs a
  tick; the places' parts summed over the places, as thread time; with
  open-5 beside them a round's numbers vary by up to a tenth):

  | | `build-g5j`'s | 3 to 5 | without 4 | without the summaries |
  |---|---|---|---|---|
  | the places' run (execution) | 12,210 to 12,854 | 11,405 to 11,544 | 11,810 to 12,091 | 11,761 to 11,940 |
  | `end_failed_states` | 5,274 to 5,447 | 4,128 to 4,155 | | |
  | the network step | 570 to 588 | 201 to 218 | | |
  | its delivery | 304 to 314 | 3 | | |
  | its killer | 83 to 95 | 54 to 68 | | |

  Those had `lists_pay` 4. Three more rounds, `end_failed_states` by
  `lists_pay`: 3,904 to 4,124 with 4, 3,831 to 4,243 with 12 (`build-
  g5k`'s), 4,230 to 4,317 with 32, against `build-g5j`'s 5,138 to 5,345;
  with the lists always (the first rounds), 4,209 to 4,322. Place by
  place, place 13 (50,424 threads) went from 153 µs a tick to 101, and
  place 94 (31,936 threads, heavy with flips) was 170 with 4 and 154
  with 12, against 151; with 4, 12 places were more than 1 µs a tick
  slower than on `build-g5j`, with 12, 8, none by more than 4 (places
  of 7,000 to 32,000 threads, up to 11% of their time there).
- **The small worlds,** `build-g5i`, `-g5j` and `-g5k` taking turns,
  three rounds, 24 host threads: the busy world (2 seeds of 60,000
  ticks) 168 to 174 and 157 to 164 µs a tick on each (but `build-g5i`'s
  first round, 147 and 136); open-3 with its faults made frequent,
  10,000 ticks, 3,201 to 3,293 on `build-g5i`, 3,183 to 3,269 on
  `build-g5j`, 3,238 to 3,284 on `build-g5k`: level, within the rounds'
  spread. (`build-g5j`'s check had the busy world at 177 µs against
  `build-g5i`'s 142, one run each while it compiled.)
- **Checked.**
  - **Tests:**
    - **`test_stack_lookup`** (os): 12,000 random calls of a program
      with up to 40 threads, their saved IPs set anywhere, with
      `os.stack_held` on and off: each SPAWN's choice and FREE's answer
      held against the walks they replaced, written out again in the
      test, and the OS's check every 50 steps. It fails on two wrong
      versions.
    - **`test_deliver_keys`** (os): random RECVs and data handlers on
      four ports, threads ended and started, and messages to every port
      and past them: each delivery held against the old walk (the one
      that took it, no one else woken, the interrupts raised), with the
      OS's check every 50 steps. Stopping at the port's first waiter
      fails it.
    - **`test_killer_draw`** (os): programs placed by hand and ended
      at random, each holding what it was given; every step's draw held
      against the old one.
    - **`test_rare_room`** (memory): a memory of 384 words a bitmap
      filled, a page in 300 freed, then 6,000 allocations and frees of
      pages, 8-byte slots and spans: each pick the lowest found by hand,
      page by page, each miss a miss, and the check every 500.
    - **`test_fails_blocks`** (fails), on top of pass 12's: with 100-
      and 200-byte stacks, so frames start blocks past their stack's
      base; with bytes failed by the frames alone (with `reach` 0 it
      fails); through the lists always and through the walk.
    - **`test_due_groups`** (sched): 200 threads sleeping, waiting on
      timers and handlers, grouped by kind, one ending and another
      starting every 9 ticks, the OS's check every tick: 645 groups
      passed over and 12,187 threads woken or timed.
  - **The suites:** 50 of 50, twice, on each build, the determinism
    suites among them. Gate 4's equivalence test is byte for byte on
    both.
  - **Against the build before, byte for byte** (`build-g5j` against
    `build-g5i`, then `build-g5k` against `build-g5j`), each pair on
    the same host threads, as in passes 10 to 12:
    - **open-3's world** from tick 0 to 20,000 on 1, 2, 8 and 24 host
      threads: 16 of 17 files the same each time, the world's save
      among them, the test's save the one apart (pass 9); the records,
      windows and genome dumps the same from one thread count to
      another, and every report's instructions executed,
      7,808,712,071. The reports the same but for their timings.
    - **The same with its faults made frequent,** to tick 10,000, on
      24: the same, and 634, then 718, of the test save's 53 million
      bytes differ.
    - **The busy world,** 2 seeds of 60,000 ticks with its 25
      snapshots: all 55 files.
    - **The late world:** open-3's save carried on 2,000 ticks on 24
      threads: its 931 MB save and its hash, 25132543da65ce55; and
      `build-g5k` on 1 thread, the same hash.
  - **open-5's saves** (the hashes this pass was given): the save at
    tick 1,826,900 carried on 2,000 ticks gave b118ac48e90a0e2a on
    `build-g5i`, `build-g5j` (24 threads and 1) and every timing and
    scratch build of 3 to 5 (24 threads; 1 with `lists_pay` 4); the
    spell's save at tick 2,342,000, 5,000 ticks, 2b113a49cf763361 on
    `build-g5j` and on 3 to 5 with `lists_pay` 4. `build-g5k` gave
    both hashes on 24 threads and on 1 (each world alone, open-5
    beside it).
  - **The builds:** `build-g5j\bin\nova2.exe`, md5 52a8c749...;
    `build-g5k\bin\nova2.exe`, md5 3e3ebf63....
- **Measured** (by the session that asked for the pass, apart from the
  build session's own figures above; `build-g5i`, `-g5j` and `-g5k`
  taking turns, three rounds, 24 host threads, each world alone but
  for open-5, which ran beside every one on `build-g5j`, so a round's
  numbers vary by about a tenth; each world started through
  `build-scratch\world-gate.sh`, one at a time; the run's seconds as
  `nova2 resume` prints them):

  | | `build-g5i` | `build-g5j` | `build-g5k` | |
  |---|---|---|---|---|
  | open-5's save at tick 1,826,900 (1.9 million threads), 2,000 ticks | 6.05, 5.57, 6.12 s | 5.86, 5.37, 6.08 s | 4.70, 4.67, 5.06 s | `-g5k` 0.83 of `-g5j`'s time |
  | the same, open-5 stopped (2026-11-24, one run each) | 4.76 s | 4.88 s | | |
  | the spell: its save at tick 2,342,000, 5,000 ticks | 426.0 s (one run, open-5 stopped) | 11.14, 11.69, 11.62 s (12.55 s then) | 9.29, 9.40, 9.68 s | 34 times, then 0.82 |
  | open-5's save at tick 4,623,600 (2.8 million threads), 2,000 ticks, two rounds | | 7.23, 6.99 s | 6.12, 5.95 s | 0.85 |
  | open-3's late save (tick 7,523,723), 2,000 ticks | | 4.21, 4.24, 3.99 s | 4.22, 4.56, 4.09 s | level |

  - **The hashes** were the ones expected every time: b118ac48e90a0e2a,
    2b113a49cf763361 and 25132543da65ce55, and 9fc0fa368b52aac4 on both
    builds for the save at tick 4,623,600.
  - **Carried on by the next build.** A test's save is read back only
    by a build that keeps its layout, so each build carried on a save
    the one before it wrote, beside that one doing the same, 1,000
    ticks, the records copied to scratch (a carry-on cuts them back to
    the save's lengths): open-5's save at tick 3,667,900 (written by
    `build-g5i`) on `-g5i` and `-g5j`, and its save at tick 4,623,600
    (written by `build-g5j`) on `-g5j` and `-g5k`. Each pair's records,
    windows and genome dump were the same, 15 of 15 files, and the
    reports the same but for their timings.
  - **open-5 itself** went from `build-g5i` to `build-g5j` at tick
    3,667,900 (2026-11-24, 22:29). On `build-g5i` it had spells of
    about 20 ticks a second; on `build-g5j`, over its progress lines of
    ten minutes, 243 to 317 ticks a second with test worlds beside it,
    and no spell. It has not run on `build-g5k`.
  - **What the pass did not do:** the places' run itself, where most of
    a tick's thread time still goes (about 11,400 µs a tick against
    `end_failed_states`' 4,100, by the timing above),
    and a place is still one task, so a tick still waits for its
    heaviest place.
