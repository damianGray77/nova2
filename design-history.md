# Nova 2 — Design

> **Status:** started 2026-11-10. Design only: nothing gets built until the
> design is agreed. Nova 1 (`E:\Projects\nova`) stays as it is and is still
> being worked on; nothing here changes it.
>
> This file holds the **current design only**: what Nova 2 is, decided or open.
> Reasons and history go in the log at the end, not in the spec. (Nova 1's
> `design.md` grew to 300 KB as a phase-by-phase history; see
> `nova1-survey.md`.)

Items are marked **[decided]**, **[settled]** (answered by a later decision, which it names), **[superseded]**, **[proposed]** (agreed in spirit, details
open), or **[open]**.

## 1. Aim

- Nova 2 is a more streamlined successor to Nova 1. **[decided]**
- **Nova 1's real problem [Damian, 2026-11-10]:** its programs were half
  life-forms and half programs, rather than fully one or the other. Its
  variability and complexity were not the problem; complexity is a good
  thing.
  - Claude's reading: this matches the decisions so far. One physics (3.1)
    and no bootstraps (5.1) make Nova 2's programs fully programs, with
    anything life-like left to emerge. Nova 1's life-form rules (ENGULF,
    INJECT, digestion, wounds, bleeding, dormancy, gestation, sleep, the
    death watchdog) are the half to drop. **[settled by 5.1 and 5.9: none of them is in the call table]**
  - **What "streamlined" means [decided 2026-11-10, Damian]:** the design
    of the hardware and the way software interacts with it are what get
    streamlined, not the richness of the world.
  - **Is complexity in the rules a cost? [settled 2026-11-10]** Damian at
    first didn't see it as one: more complex rules should drive more
    complex emergent behaviour. After Claude's answer (below) he agreed
    that this is how physics works too. He added that we can't model down
    to the molecular level, so abstractions are needed, and that no
    ruleset we add will come close to real life's complexity at scale (the
    periodic table, matter in three dimensions, solid, liquid and gas,
    gravity, inertia, angular momentum). Resolved as "variety in things,
    few laws" (3.1).
    - Damian's follow-up: we work above the lowest level, as at the level
      of atoms rather than electrons. So we need more rules to stand in
      for the variety the lower level would have given, and each one must
      be well thought out. Claude: correct, and it tells us where the
      extra rules belong. **[settled: every rule added since is on the hardware side (5.0a, 5.10)]** Stand-in rules belong on the
      hardware side (device ranges, error curves, heat), where they
      replace physics we don't simulate. The program side has no lower
      level: programs are exact at our level. So a rule there that adds
      behaviour is a bootstrap (5.1), not a stand-in.

    Claude's view, for the record:
    - **The evidence doesn't support a general link.** Conway's Life (two
      rules) and Rule 110 (an eight-entry table) can compute anything, and
      Tierra's few instructions gave parasites and hyper-parasites. Most
      complicated rule tables give either noise or nothing.
    - **What matters is what a rule does.** A rule that hands out an
      outcome reduces emergence, because it replaces what would have
      evolved (ENGULF). A rule that adds a trade-off can increase it, as
      Avida's rewarded tasks and Lenski's second resource did.
    - **Rules carry real costs:**
      - their interactions grow roughly with the square of their number
        (Nova 1's dormancy-meets-INJECT bug);
      - each rule is another loophole evolution can exploit;
      - when something interesting or broken emerges, it's hard to tell
        which rule caused it;
      - every rule brings settings, and most of a large settings space is
        dull, which is why Nova 1 needed a settings-search tool.

    Claude's proposed reconciliation: a rule earns its place by creating a
    trade-off (3.2). That's the guardrail in the form Damian has already
    chosen.
- Same goal as Nova 1 (its design.md §1): the smallest set of rules from which
  self-replication, evolution and ecology emerge on their own, in a digital
  medium rather than imitated chemistry. **[settled by 3.1 and 5.4]**

## 2. Approach [decided 2026-11-10]

- **Mechanics designed fresh.** Nova 1's rules (its resources, eating, death,
  sparks, warps and so on) don't carry over by default. Each Nova 2 mechanic
  is designed here from first principles. Nova 1 is a source of lessons, not a
  template.
- **Nova 1's infrastructure reused where it fits.** Damian named three: the
  UI (the viewer and the setup page), determinism, and snapshots. Likely
  others, to confirm as the design reaches them: the hand-written containers,
  the counter-based RNG and hashing, the settings registry, the thread pool,
  the run driver, replay logs, CSV stats, the PNG writer, and the analysis
  tools. **[deferred to building: confirm each item then]**
- **The UI is rendered with DirectX, not GDI [decided 2026-11-11, Damian].**
  The reason is antialiasing: GDI doesn't antialias shapes, and Nova 1's
  viewer looked rough for it.
  - **What to use:** Direct2D for shapes and DirectWrite for text. They're
    DirectX's 2D layer, drawn on the GPU, and what modern Windows apps
    use. They can share a device with the Direct3D 12 that Nova 1 already
    uses for compute.
  - **What the change touches:** Nova 1's viewer draws through one small
    canvas class (`app/canvas.h` and `.cpp`, about 350 lines). Only that
    layer is rewritten; the panels and their logic carry over.
    - The canvas gains coordinates in fractions of a pixel, so lines,
      points and curves can fall between pixels. That's where
      antialiasing shows.
  - **Tests and headless renders** draw to an off-screen bitmap with WARP,
    DirectX's software rasterizer. GPU drivers antialias slightly
    differently, and tests need identical pixels.
  - Very large point sets (a map of life with 100,000+ genomes) may later
    need Direct3D's instanced drawing. Direct2D is enough for maps of
    thousands of places.
- **The engine: places in parallel on CPU threads first; a GPU backend later,
  only if benchmarks call for it [decided 2026-11-11, Damian].**
  - **Per-place parallelism already exists.** Nova 1 ran nodes in parallel
    on its thread pool, with a barrier once per tick and outboxes for
    anything crossing between nodes. It measured 73 million instructions a
    second on one thread and 519 million on 24, for 100 nodes. At 10,000
    nodes the tick was still 98% parallel. Nova 2's places are independent
    within a tick in the same way: they share no memory, messages cross at
    the barrier, and power and heat are solved once per tick.
  - **Ticks per second is the wrong yardstick.** A tick holds as many
    instructions as its length allows (a setting). Nova 1 used 25,000 per
    core per tick. What matters is instructions per second, and with it
    generations per hour. "30 ticks a second" means nothing without the
    world's size and the tick's length.
  - **Why not run places on the GPU:**
    - **Divergence.** A GPU runs 32 lanes on one instruction pointer.
      Thirty-two places each interpreting their own program issue up to 22
      different instructions at once, and the group runs each path in
      turn, often at a tenth of its speed or less.
    - **Scattered memory.** Interpreters hop around memory, and GPUs need
      neighbouring lanes to read neighbouring addresses.
    - **Slow lanes.** A lane is about 5–10 times slower than a P core on
      serial code (5.11).
    - **Nova 1 measured this.** Its real GPU ran even its in-world warps,
      code built for lockstep, slower than the CPU did. The main cost was
      moving data between the CPU and the GPU every tick.
  - **Where speed comes from instead:**
    - **A much simpler interpreter.** Nova 2's instructions are one byte
      each, 22 of them, in flat memory, with no codons, templates, ledgers
      or contests. Decoding is one table lookup, and most instructions
      touch only registers.
    - **Locality.** Nova 1's long runs were limited by memory traffic.
      Nova 2's pages are scaled to programs (5.7), so a place's working
      memory can fit in a core's cache. A place's threads run together, as
      one task.
    - **Long ticks.** More instructions per tick means fewer barriers per
      instruction. Power and heat change slowly next to instructions.
    - **Work stealing** across places, so busy places don't wait on idle
      ones.
  - **Estimate, to be benchmarked:** 150–300 million instructions a second
    per P core, and 2–5 billion across a 24-thread machine. That's 4 to 10
    times Nova 1. At 1,000 places with four cores each and 25,000
    instructions per core per tick, that's 20–50 ticks a second. A smaller
    world or a shorter tick runs proportionally faster.
  - **The GPU's roles:**
    - the UI;
    - possibly the in-world GPU's lanes, if a benchmark shows they gain;
    - the power solve, only at worlds far larger than planned.

    At thousands of places, the physics costs little next to running the
    programs, so moving it to the GPU would add transfers for little gain.
  - **The first benchmark comes with the origin test:** the interpreter on
    one place, then how it scales across cores.
  - **Much larger worlds [Claude, answering Damian 2026-11-11]: the gap
    narrows, but probably doesn't flip.**
    - **What size changes:**
      - A GPU needs tens of thousands of independent threads to run at
        full speed, partly to hide memory latency. At 1,000 places it sits
        mostly idle.
      - At 100,000 places and more, the GPU is fully occupied. The CPU's
        throughput is fixed by its cores, so it just runs fewer ticks a
        second.
      - So the GPU's relative standing improves with size.
    - **What size doesn't change:**
      - **Divergence.** Every group of 32 lanes still holds 32 different
        programs. GPU ray tracers regroup work so that lanes run the same
        code together (Laine, Karras and Aila, 2013, "wavefront path
        tracing"). There, each piece of work is thousands of cycles of
        shading. Here, each instruction is about one cycle, so regrouping
        before every instruction would cost more than the instruction
        itself.
      - **The OS layer.** ALLOC, SPAWN, SEND and routing are branchy and
        serial. They don't suit a GPU.
      - **Transfers.** To avoid Nova 1's cost, the whole world would have
        to live on the GPU between ticks: memory, threads, queues and the
        OS's records. Only views would come back for the observers. That
        means moving the engine, not just the interpreter.
    - **A rough bound (Claude's estimate, unmeasured):** scattered memory
      reads cap a high-end GPU at around 10^10 interpreted instructions a
      second, before divergence, against 2–5 × 10^9 for the CPU. The best
      case is a few times faster, at the cost of a GPU-resident engine.
      The uncertainty is about tenfold either way.
    - **Large worlds suit the CPU too.** One place's memory is small
      (pages scaled to programs, 5.7), so a place processed as one task
      fits in a core's own cache even when the whole world is gigabytes.
    - **Recommendation:**
      - Build for the CPU, with each place's state in one flat, compact
        block. That's best for the CPU's caches, and it's the layout a GPU
        backend would need anyway.
      - Keep the interpreter's core in plain code that can be compiled for
        both. Nova 1 did exactly that, and got the same world bit for bit
        on either.
      - Then benchmark at 1,000, 10,000 and 100,000 places, CPU alone
        against a GPU backend, and decide from the numbers.
      - Nova 1's "auto" mode, which split each round between CPU and GPU
        by measured speed, is the fallback if each wins at different
        sizes.
  - **A CPU-or-GPU setting [decided 2026-11-11, Damian: his idea, in Claude's form
    of one source with two backends, CPU first].** Damian asked for a setting choosing the CPU or the GPU,
    with a separate engine for each. The GPU engine would keep the whole
    world on the GPU, which removes the transfers.
    - **Claude: yes to the setting, but as one source with two backends,
      not two engines.**
      - **Damian's point stands either way:** the GPU side has to be the
        whole engine, not just the interpreter. That was the reason for
        his distinction. A GPU backend is every rule, compiled for the GPU,
        with memory, threads, queues and the OS's records kept there
        between ticks. The only question is whether that engine's code is
        written separately or shared.
      - Two separately written engines would mean writing every rule
        twice: the interpreter, the allocator, the scheduler, SPAWN's
        ownership gift, the queues and routing, power, heat, wear and the
        facts log. Every change would have to be made twice and kept in
        step, and the two would drift.
      - Instead, the rules are written once, in a restricted subset of C++
        that also compiles as HLSL for the GPU. Nova 1 did exactly this for
        its warp interpreter (`warp_isa.inc`, compiled as C++ and as HLSL,
        bound by macros) and got the same world bit for bit on either.
      - Real anchor: scientific codes do the same at scale. LAMMPS, for
        example, runs one source on CPUs or GPUs through Kokkos.
    - **The setting must not change the world.** Both backends must give
      the identical world for the same seed and settings, checked by the
      world's hash, as Nova 1 checked its interpreter. Then the setting is
      only a choice of speed, never part of a world's identity, and a run
      started on one can resume on the other.
    - **What the shared subset demands, a discipline that also helps the
      CPU:**
      - flat arrays and fixed-size records;
      - no allocation, recursion or virtual calls inside a tick;
      - integer and fixed-point arithmetic only (already decided for the
        power solve);
      - an explicit order for everything inside a place, including how its
        cores' threads interleave;
      - no side effects inside `&&` or `?:`, since HLSL evaluates both
        sides (a Nova 1 lesson).
    - **What stays on the CPU in either mode:**
      - the UI and the observers (they read samples and the facts log,
        which cross over at each sample, not every tick);
      - snapshots;
      - rare global work: recomputing routes after a topology change, and
        relinking a respawned place. Their results are uploaded.
    - **The GPU's memory limits world size.** Damian's RTX 5080 has 16 GB.
      100,000 places at 64 KB of memory each is 6.4 GB, which fits. Disks
      on top may not.
      - **Proposed:** disks stay in host memory. DREAD and DWRITE are slow
        and rare in the world (latency calls, 5.9), so batching them across
        once a tick costs little.
      - **Real anchor:** GPUs reading storage through the host, which is
        how most GPU programs handle data larger than their memory.
    - **When:** the CPU backend is built first, in the shared subset.
      - The GPU backend is added only when the benchmarks (above) show the
        CPU is the bottleneck at world sizes we want, and a prototype shows
        a real gain. That's the guardrail (3.0, principle 7).
      - The setting (`engine.device = cpu | gpu`) appears with the GPU
        backend. "auto" can follow if each wins at different sizes.
- **Design first.** The design is hashed out before anything is built.
- **Critique, not encouragement.** Claude critiques proposals, its own
  included, and labels opinions and unproven claims as such.
- **Every number is a setting [decided 2026-11-10, Damian].** Every variable
  in the simulation can be set from configuration; nothing is hard-coded.
  Claude's notes:
  - **Tiers on the setup page [decided 2026-11-10]:** a short main page,
    with everything else under an advanced tier. Damian: this is a matter
    for the interface, not the simulation.
  - **Fixed for the run [decided 2026-11-10, Damian],** as in Nova 1:
    settings never change in the middle of a run.
  - **Hardware as ranges along trade-off curves [decided 2026-11-10]:**
    see 5.0a.

## 3. Principles

### 3.0 The principles, restated [decided 2026-11-11, Damian: adopted as Nova 2's principles; design order step 9]

This is the short list, restated from everything decided in 5. 3.1 and
3.2 keep their full text below. Each principle says where it's decided and
where it's used. Those marked *new* were used in decisions but never
written down as principles.

**What the world is**

1. **Fully programs** [decided, 1]. Programs are programs, not half
   life-forms. Anything life-like has to emerge: eating, parasitism,
   death, cooperation. Used in 5.1, the call table (5.9), and the
   detectors (5.12).
2. **One physics** [decided, 3.1]. The world is its initial conditions
   plus a short list of laws, applied everywhere, all the time. Nothing
   inside the world is scheduled. Test: no law names an emergent thing
   (eating, a parasite, a disaster, death).
3. **What comes from outside is a boundary condition** [decided across
   5.0, 5.0c and 5.10; *new* as a principle].
   - These inputs follow functions fixed before the run: the supply, the
     sources, swells and regional patches, and maintenance visits.
   - They never react to what programs do. Visits react only to the state
     of the hardware.
   - Real anchor: the sun doesn't respond to the biosphere.
4. **Power in, heat out** [decided, 3.1, 5.0]. Power is the driving input
   and heat the sink. Energy is conserved. Without both, the world settles
   into equilibrium and nothing happens.
5. **Luck is part of the world** [decided, 5.0c]. Some seeds are better
   than others, so settings are compared across several seeds.

**How a rule earns its place**

6. **Every choice is a trade-off** [decided, 3.2].
   - Test: name a condition in which the other side wins.
   - A rule earns its place only by creating a trade-off that programs can
     take either side of.
   - Trade-offs stay alive only where conditions vary.
7. **A mechanic is added only to fix a failure seen in a run, or to meet a
   stated test** [the guardrail, 3.3, still standing]. Used for wear
   levelling off by default, reads and writes costing the same, and no
   routing by load.
8. **Variety in things, few laws** [settled, 3.1].
   - Kinds are rows of numbers under the same laws: devices (5.11),
     sources, patches.
   - Each kind is a curve, and each instance a point on it, with small
     scatter, so no device is best at everything (5.0a).
9. **Follow real design, and anchor every choice in real life** [Damian's
   standing preference; *new* as a principle].
   - Every rule cites what real hardware, networks or ecology do.
   - The abstraction rule (5.0) prunes this: a physical quantity stays only
     if some program would feel the world differently because of it.
10. **Stand-ins belong on the hardware side** [settled, 1]. We don't
    simulate below the hardware, so device ranges, error curves, heat and
    wear stand in for the physics we skip. Programs are exact at our
    level, so a rule that adds behaviour on the program side is a
    bootstrap.
11. **When realism and simplicity cost evolvability, prefer realism and
    simplicity, and let tests decide** [*new*, from Damian's choices].
    - Damian chose fire-and-forget SEND knowing it adds an obstacle to
      evolution (5.8). He also dropped seeded ancestors entirely (5.4).
    - The check is 5.5's: if life can't start, or can't find a behaviour,
      tests will show it, and the rule is revisited then.

**What programs can do**

12. **No bootstraps** [decided, 5.1]. No instruction or call hands a
    program a behaviour, and none names another program. Test: its effect
    can be stated in terms of the world alone. Used for every call, from
    NMATMUL to PING.
13. **Random code must be able to stumble on** [decided across 5.9;
    *new* as a principle]. Every byte is an instruction, every call number
    and argument value is valid, and calls fail soft instead of faulting.
    That's how a replicator can be found from static (5.5).
14. **Ownership is physical, and recorded** [decided, 5.3, 5.8, 5.12].
    - The OS records who owns each page. Ownership changes only by
      freeing, or by a gift to a new program at SPAWN.
    - Overwriting uses memory without owning it, and a hijack runs as its
      victim.
    - The dead aren't wiped: their bytes feed new life.
15. **Knowledge is local, and travel starts blind** [decided, 5.8].
    - Programs sense their own place and their neighbours. Anything further
      must come from other programs, or from PING's round trip.
    - The network is fire and forget. Only the receiver can confirm a
      message arrived (the end-to-end argument).

**Events and observation**

16. **Outside events ramp up** [decided, 5.10 (6b)]. An event from outside
    starts gently, so its first effects are its own warning. Some ramps are
    false alarms, which keeps reacting a trade-off.
17. **Facts, not labels** [decided, 5.12].
    - The engine records only facts: what a law did.
    - Labels are observers' patterns: parasite, death, disaster, species.
    - Watching changes nothing, and a label can be redefined without
      rerunning the world.

**Settings and engineering**

18. **Every number is a setting** [decided, 2, 6].
    - Settings are fixed for the run.
    - A law with a strength gets a slider with 0 meaning off, rather than a
      switch. Switches are still welcome for experiments, and new features
      start on.
19. **Settings mean the same at any size** [decided in 5.7 (f) and 5.0c;
    *new* as a principle]. Lengths are counted in links and rates per 100
    places, so a bigger world is bigger, not denser.
20. **Deterministic by construction** [decided, 2, from Nova 1]. The same
    seed and settings give the same world on any number of threads.

**What changed from the first statements (3.1, 3.2, 3.3):**

- **Claude's first draft (3.3) became:**
  - "Material is conserved" became energy conserved (4).
  - "The world never acts on a program's behalf" became no bootstraps (12)
    and boundary conditions (3).
  - The settings budget gave way to "every number is a setting" (18). The
    curves keep that manageable (8).
  - Determinism stays (20).
  - "Nothing grants permanence" was dropped as a rule. Permanence now has
    costs instead: wear, disk writes, and holding the boot block.
- **New since then:** 3, 9, 11, 13 and 19.
- **Decided in 5 but now stated as principles:** 15 and 17.

**Questions for Damian:**

1. ~~Should this list stand as Nova 2's principles?~~ Adopted 2026-11-11, Damian.
2. ~~Principle 11: prefer realism and simplicity over ease of evolution, and
   let tests decide. Is that how you'd put it?~~ Adopted as worded, 2026-11-11, Damian.

### 3.1 One physics [decided 2026-11-10, Damian]

In the real world, earthquakes, volcanoes and thunderstorms are very
different events driven by the same underlying physics. Nova 2's world works
the same way: one underlying engine drives everything that happens in it.
No bootstraps (5.1) is this principle applied to programs.

**Formulation [settled: applied in every decision since]:** the world is its initial conditions plus
a short list of laws, applied everywhere, all the time. Every event is what
those laws do to the state. Nothing is scripted.

**Test [settled: applied in every decision since, e.g. 5.10]:**

- The laws fit on one page.
- No law names an emergent phenomenon: eating, parasite, disaster, collapse,
  or even death.
- Every event the viewer or the stats name is found by an observer in the
  state, not raised by the engine.
- Initial conditions (the map, the seed, where resources start) may vary
  freely; schedules may not. Nova 1's bridges opening every N ticks would
  fail.
- **[settled by 5.0 point 6 and 5.0c (b): a boundary condition]** The
  input from outside the world (the power supply, 5.0) may
  vary over time by a function fixed before the run, as the sun does on
  Earth. That makes it a boundary condition, not a scripted event. Nothing
  inside the world is scheduled.

**Consequences [settled, apart from the 5.2 bullet, superseded]:**

- **The level of the laws is a choice:** cells and instructions, not
  anything below them. Nova 1 rejected simulating real physics as
  infeasible (its §1), and real physics doesn't model earthquakes from its
  forces either.
- **The world needs a driving input and a sink.** Without them it settles
  into equilibrium and nothing happens. Earth's input is sunlight. Nova 2's
  input is power, and its sink is heat (5.0).
- **Disasters need laws that let stress build up and release suddenly**
  (thresholds, as in sandpile models). Without that the world stays smooth,
  and the pressure to script events comes back.
- **Disasters rooted in real hardware and power [Damian, 2026-11-10]:**
  disasters are still needed, rooted in real hardware and power problems,
  roughly at Nova 1's balance. Claude: Nova 1's disasters were events on a
  schedule (`disaster.<kind>_every`), which this principle rules out. They
  were surges, crashes with reboots, burnouts with repair, disk failures,
  moving storms with strikes, brownouts, and warning symptoms beforehand.
  Most of them come out of the power model unscheduled:
  - brownouts from sag;
  - crashes from the cut-offs;
  - burnouts from heat;
  - disk failures from wear on writes;
  - warning signs from falling voltage and rising heat, which programs can
    sense.

  What's left is shocks from outside: surges and storms. Those pass this
  principle as part of the supply (5.0 point 6), for example swings that
  are usually small but occasionally large, or a moving storm field like
  the day sweep. **[decided in spirit 2026-11-10, Damian]** Most disasters
  come down to surges, drops and storms, worked through in 5.10
  (2026-11-11).
- **Variety in things, few laws [settled 2026-11-10, Damian, under 1, "Is complexity in the rules a cost?"].** The real
  world's variety comes from a few laws applied to many kinds of thing at
  enormous scale, not from many laws:
  - the 118 elements are not 118 rules, but one physics acting on
    different numbers of protons and electrons;
  - solid, liquid and gas are not separate rules, but the same forces at
    different temperatures.

  Nova 2 can work the same way. Its contents can be as varied as we like:
  the hardware catalogue, the sources, the map. Its laws stay short. The
  catalogue is Nova 2's periodic table: many kinds, each a set of numbers,
  all under the same power and heat laws.
  - **Why this matters more here than in real life:** our scale is tiny.
    Every law costs computing time on every tick, and time spent on rule
    detail is time not spent on generations. Evolution needs
    generations.
- **Resource kinds become points on the same few quantities** (capacity,
  speed, latency, heat per unit), not separate systems. A GPU is wide, slow
  compute; a disk is big, slow memory.
- ~~**It leans 5.2 towards (b) or (c).**~~ [superseded: 5.2 was settled by 5.8, with ownership recorded by the OS] Absolute ownership and a write contest
  each add a law of their own; no ownership and last-writer-owns don't.
- **Death and disasters become labels** that observers put on patterns in
  the state.

**Risk:** a principle this broad can justify anything ("it's all one
physics"). The test is what gives it teeth.

### 3.2 Every decision is a trade-off [decided 2026-11-10, Damian]

Ideally every decision in the world is a trade-off. There is no perfect
solution.

**Test [settled: applied in every decision since]:** for every choice a program can make and every
hardware model, name a condition in which the other side wins. If there is
none, one option dominates and the choice is dead. A program's choices
include where to live, which device to run on, how fast to clock it, and
what to keep in memory or on disk.

- **Trade-offs stay alive only where conditions vary** in time and place
  (the day sweep, swings, crowding), because the best choice then keeps
  changing. A fixed world settles on one answer.
- **A trade-off on paper can be lopsided in practice.** The numbers decide,
  and only runs can show it.
- **This is also the test for adding a rule [settled: applied in every decision since]:** a rule
  earns its place by creating a trade-off that programs can take either
  side of. That is the guardrail of 3.3 in concrete form.

### 3.3 Claude's first draft (superseded)

One mechanism per job; holdings go with their holder; material is conserved;
nothing grants permanence; the world never acts on a program's behalf; a
settings budget; deterministic by construction. Claude's own critique: most
of it was inherited from Nova 1 rather than derived. "Nothing grants
permanence" drove much of Nova 1's growth, because every protection needed a
matching failure. The settings budget and determinism are process and
engineering rules, not properties of the world. "Conserved" means nothing
until "material" is defined. And it had no success test. The guardrail still
stands: a mechanic is added only to fix a failure seen in a run or to meet a
stated test.

## 4. Design order [settled as a guide; topics were taken up in 5 as they arose; every step done as of 2026-11-11]

Damian chose to start with the foundations; the principles come out of them.

1. Foundations (§5): where life starts, what instructions may do, how
   programs reach each other, what resources exist and in what form.
2. Living: running, copying, mutation.
3. Interaction: what eating and parasitism look like once they emerge.
4. Space: nodes, paths, moving between them.
5. Ending: death, and where matter goes.
6. Environment: variety, change, disasters.
7. Hardware: what the real CPU and GPU do.
8. Observation: what the reused UI needs to show.
9. Principles (§3), restated from all of the above.

## 5. Foundations

### 5.0 Power, the driving input [decided 2026-11-10 and 2026-11-11, Damian; stale labels closed 2026-11-11]

Damian: the fundamental resource is power (electricity), the driving force
behind any digital machine. The universe needs a constant stream of it to
run. Power has its real-life properties: voltage, current and resistance.
Day and night come from the supply rising and falling, as if the minds
behind the universe were balancing load for their own users.

**Abstraction [decided in spirit 2026-11-10, Damian]:** Nova 2 is about
digital life, not physical robots or automata, so its physics is an
abstraction. **Rule [settled 2026-11-11: applied in every decision since, from rejecting AC (point 2) to the swings (point 6)]:** a physical quantity stays only if
some program would experience the world differently because of it, in a way
no other quantity already gives.

**The power model so far:**

1. **Power in, heat out [decided].** In real electronics all the power a
   circuit draws ends up as heat, and the computing happens as it passes
   through. So the books balance as power in = heat out, plus any energy
   stored. Energy is the conserved quantity that Claude's first draft
   lacked (3.3).
2. **DC and Ohm's law [decided 2026-11-10, Damian].** For programs, AC would
   add only two things: transformers between voltage levels (another
   mechanism) and a fast rhythm in the supply, which repeats the day cycle.
   Neither passes the abstraction rule. **[settled by later decisions]** One
   voltage level, with sag along links: Claude's opinion, unless a test
   shows a need for more. No regulators were chosen (5.0a), so nothing
   converts between levels: each source pushes its own voltage (5.0c),
   and it sags along links.
3. **Resistance turns power into heat [decided 2026-11-10, Damian],** in
   links and in whatever draws power. Heat made in a part is current² ×
   resistance. Which way resistance drives heating depends on what is fixed.
   On a fixed voltage, *lower* resistance draws more current and makes more
   heat (power = voltage² / resistance), which is why a short circuit burns.
   For a wire carrying a given current, higher resistance makes more heat.
   Moving power far is cheapest at high voltage and low current.
4. **Voltage sags with path resistance and draw [decided 2026-11-10,
   Damian].** The drop along a path is current × resistance. The source sets
   the voltage, and what is connected sets the current (power = voltage ×
   current). Nodes near a source don't get "first dibs"; their voltage just
   holds up better when everyone draws.
5. **Sources by proximity, like a fire [decided in spirit 2026-11-10,
   Damian].** Power falls off with distance from a source, and programs
   huddle around sources as people do around a fire. The falloff must be
   wide, or the sources many and small, so that ecosystems sprawl instead of
   forming tiny dots.
   - A source is three numbers: its place, its voltage (how hard it
     pushes), and its internal resistance (how much it sags as more is
     drawn, which sets how much current it can give). **[settled by 5.0c: voltage and resistance spreads, with current following]**
     Damian had pictured sources varying in voltage, current and resistance,
     with stiffness as a fourth. Claude: stiffness *is* the internal
     resistance, and the most current a source can give follows from
     voltage ÷ internal resistance. So it is still two numbers plus a
     place. A separate current limit, like a breaker, would be a fourth
     number giving a hard cut-off. **No breakers [decided 2026-11-10,
     Damian].**
   - **The falloff is the links' resistance (point 4), not a separate
     distance law [decided 2026-11-10].** That keeps it to one law and makes
     reach depend on load: a crowded inner ring dims everything beyond it.
   - **Crowds can move in [Damian's idea; Claude: follows from point 4,
     untested].** Damian: once enough programs need power, they could
     slowly close in on a source rather than staying in a ring. Ohm's law
     gives this without a new rule. Heavy draw near a source pulls its
     voltage down, which shrinks the too-hot zone and lets life move
     inward. Two conditions:
     - **The source must be soft.** It needs enough internal resistance to
       sag under load. A stiff source holds its voltage whatever is drawn,
       and keeps a permanent burn zone. So stiffness is what really
       distinguishes one source from another.
       - **Softness belongs to the source, not the wire.** Every real
         source has some internal resistance. A big generator or a car
         battery is stiff: it holds its voltage up to its limit. A small or
         weak battery, or a solar panel in dim light, is soft: it sags as
         more is drawn.
       - A node feels the source's internal resistance plus the
         resistance of its path, added together. The difference is who
         shares it. The source's resistance is shared by everyone drawing
         from that source, so everyone's draw lowers everyone's voltage.
         A path's resistance is shared only by those along that path.
     - **Crowding also makes heat,** which pushes the other way (point 7).
       Damian: devices make their own heat, and so does the source; the
       knock-on effects come later. Claude: all heat comes from resistance
       somewhere (in the source, the links or the devices), so a source's
       own heat needs no separate law. A soft source runs hotter under
       load.

     Whether the result settles, creeps inward or oscillates can't be
     known until it is tested. Moving between nodes belongs to Space.
   - How many sources there are and how big is an initial condition, not a
     law. Claude's opinion: many small ones and a few large ones.
     **[settled by 5.0c]:** a starting number as a setting, a rule that
     creates and retires sources, and sizes skewed so most are small.
   - A high-voltage, low-capacity source reaches far but feeds few. A
     low-voltage, high-capacity source feeds many nearby but fades fast.
     Several sources give gradients and frontiers, so rich and poor regions
     come from the grid itself, in place of Nova 1's 42 per-resource spread
     settings.
6. **Night and swings lower a source's capacity, not its voltage [settled 2026-11-11:
   drops act on capacity (the day sweep, 5.0c's types, 5.10 (4)'s brownouts), and swells on voltage (5.10 (3))].** Damian: night has less power. In real life a power company's
   output follows demand while its voltage is held almost steady, and a
   weak source behaves like a solar panel in dim light: its voltage holds
   under a light load and collapses under a heavy one. So the day sweep and
   the swings scale each source's internal resistance. Night then punishes
   crowding, not presence.
   - **Day and night sweep across the map [decided 2026-11-10, Damian].**
     Its settings are how long a day lasts and how deep the night is.
   - **Users outside the world [decided 2026-11-10, Damian].** The supply
     never reacts to what the world draws (a boundary condition, 3.1).
   - **Random swings, moderate [decided 2026-11-10, Damian]:** enough that
     sources aren't equal, not so much that they remake the landscape or
     kill devices that depend on a fairly steady flow.
     **[settled by 5.0c: fluctuations' size and speed are settings, per source type]** Each source drifts slowly and on its own. Swings
     from tick to tick average out and do nothing. The swings must sometimes
     take some node out of a device's range, or they fail the abstraction
     rule.
     Damian clarified: the swings shouldn't trouble a source's main users,
     but the edges of its habitable zone should feel them. This follows
     from the capacity model without a rule of its own: a dip in capacity
     sags the far ends of paths most, because the drop builds up along
     them. What needs setting is the size of the swings relative to the
     width of the devices' ranges.
   - Option: sources differ in how closely they follow the sweep, and
     steady ones become refuges at night. **[settled by 5.0c's catalogue
     of source types, which includes how closely each follows the day]**
7. **Each kind of device has a voltage range [decided 2026-11-10,
   Damian].** Below its minimum a device shuts down. Above its maximum it
   runs hot, makes errors, and can burn out.
   - **Bigger devices need more power [decided 2026-11-10, Damian],** so
     each source favours some programs over others.
   - **Follow real device design as far as we can [decided 2026-11-10,
     Damian].** In real hardware a bigger device draws more current at
     about the same voltage, and a faster device needs a higher voltage
     (5.0a). So size sets the draw, and speed sets the voltage a device
     wants. Claude's earlier opinion (size sets both, so big devices live
     close in) was wrong for real design and is withdrawn. Zones around a
     source therefore form by speed, not size: fast, power-hungry devices
     close in where the voltage is high, and slow, efficient ones further
     out. Big devices are what make a node sag.
   - Real life (Claude): low voltage causes errors before a shutdown
     (timing margins run out), and heat causes errors before a burnout. So
     both edges make errors first.
   - **Errors are wrong results, not mutations [decided 2026-11-10].**
     Damian: outputs should be affected, but mutation means a permanent
     change to a program. Near either edge of its range, a device
     sometimes gives a wrong result. The error is transient; it becomes permanent only where the
     wrong result is written into code, mostly while a program copies
     itself. Mutation is then what observers call a wrong result that
     landed in code, not a law of its own. That replaces Nova 1's four
     separate sources (copy errors, execution flaws, bit rot and cosmic
     rays).
     - Memory is a device with a range too **[decided 2026-11-10]**: its
       "wrong result" is a bit it fails to hold. Low power makes memory
       fade, which is what gives night its teeth (5.0a).
     - Constraint: the middle of a range must be close to error-free. Above
       about one error per copy, nothing can copy itself faithfully
       (Eigen's error threshold). (A small floor everywhere, for copy errors: decided in 5.10 (8), 2026-11-11, Damian.)
   - **Life forms bands [settled 2026-11-11: a consequence of decided laws, not a rule; only runs show the widths]:** too hot close to a source,
     too dim far out, with a habitable band between. The band's width is
     roughly the device's range divided by the voltage drop per hop.
     Different kinds of device have different ranges, so they get different
     bands, the way life zones form around a hot spring.
   - **Burnout is a heat balance:** a part fails when heat is made in it
     faster than it can leave, until its temperature passes a limit. This is
     the build-up-and-release threshold that 3.1 asks for.
8. **Resource kinds as ways of using power [settled by 5.0a, the P and E decision below, and 5.3's kinds].** P cores are
   fast and power-hungry; E cores are slower and more efficient; a GPU runs
   many lanes at high power; memory needs power to hold its contents, as
   DRAM needs refreshing; a disk doesn't.

**Open:**

- **Is burnout permanent? [decided 2026-11-11, Damian: damage is wear (5.10 (1)), restoring is visits (5.10 (2))]** If devices
  only ever burn out and nothing restores them, the world wears out. Nova 1
  needed node recovery for the same reason. Options (Claude):
  - **(a) Trip and cool:** above a temperature a device stops, and it
    restarts once it has cooled below a lower one, like a thermal cut-out.
    Nothing is lost for good, and it adds one law and no repair law. What
    the device was running is lost, as in a real thermal shutdown; whether
    memory keeps its contents depends on memory's own range. Weakness: it
    leaves no lasting mark on the landscape.
  - **(b) Permanent, with new hardware from outside:** a burnt device is
    gone, and the minds behind the universe add hardware at a steady,
    seeded rate in random places. It is a boundary condition like the
    supply, not a reaction to the world; replacing exactly what burnt would
    be a reaction. It leaves lasting scars and gives slow renewal.
    Weakness: a second input from outside, and the map drifts as new
    hardware lands elsewhere.
  - **(c) Degrade:** each burnout permanently lowers a device's maximum, so
    it becomes a smaller device. Burnt ground near a strong source then
    suits only small devices, a kind of succession. Weakness: it only goes
    one way, so it needs (b) or something else, or the world shrinks to
    small devices.
  - **(d) Wear and repair** (Nova 1): damage builds with heat and heals at a
    rate. That's two laws and a rate, and Nova 1 shows how this grows.

  Claude's opinion: (a) first, adding permanent damage only if a test shows
  the world is too static.

  Damian: all four will probably be needed; to be discussed under Ending.
  Claude's objection, for then: four is two laws for damage ((c), and the
  damage half of (d)) and two for restoring ((b), and the repair half of
  (d)), all for one job. That's the pattern by which Nova 1 grew. At most
  one of each looks justified.
- ~~Night still needs something that keeps going~~: settled by memory
  fading at low voltage (5.0a).
- **Who gets the power within a node [decided 2026-11-11, Damian: nobody
  is given it].** This was the crux that connects to ownership (5.2) and
  resources (5.3). Later decisions answer it:
  - Devices draw power while they work, and every device at a node sees
    the node's voltage (point 4). Nothing hands power out.
  - Who works is decided elsewhere. The fair scheduler shares the cores
    (5.8), GLAUNCH and NMATMUL use the GPU and NPU, and memory draws to
    hold its contents whoever owns it.
  - So getting power means getting time on a device. A program's share of
    the power is its share of device time.
  - In a brownout nothing chooses either: devices below their minimum cut
    out, and memory fades device by device (5.7).
  - Consequence: no program pays for power directly. It feels power as
    conditions (clock speed, errors, cut-outs, heat), and its own draw
    costs everyone at the node a little voltage. That's a commons, like
    overclocking a shared core (5.8).
  - Risk: power is the driving input, yet no program holds or spends it.
    If that weakens selection for using power well, the fix would be a
    cost tied to the voltage drawn. Nothing yet says it's needed.
  - **Damian (2026-11-11):** programs spend power in three ways: by using
    more cores (SPAWN), by overclocking (SETCLOCK), and through heat. Power
    is hidden from programs as it is in real life, so the risk is accepted
    with no fix planned. Claude agrees this answers most of the risk: SPAWN
    and SETCLOCK are levers that selection can act on. What's left is the
    commons: a program's draw costs its neighbours too.
- **Cost:** where links form loops, finding each node's voltage takes a
  linear solve over the graph every tick. That's affordable, but it must be
  done in fixed point to stay deterministic.
- **P and E cores: two types, option (b) [decided 2026-11-10, Damian].**
  Claude first proposed one CPU type whose single
  speed class ran from E-like to P-like. Damian doubted that one curve
  covers the difference, and he was right. The proposal is withdrawn.
  - **Why one number fails.** A real P and E core differ along two axes
    that one number would merge:
    - **How wide the design is:** how much it does per clock, how big its
      caches are, how much area and current it takes, and its cost
      profile per instruction. Nova 1 measured E cores about 15% slower
      at adds and 60–70% slower at divides, searches and system calls.
      That profile is a different *shape*, not just a slower speed.
    - **Where it runs on the voltage-speed curve:** its minimum and
      maximum voltage and its top clock.

    The two are correlated in real chips but not the same: a wide core
    run at low voltage is not an E core. And with speed following the
    node's voltage (5.0a), the clock is set partly by where a core sits,
    so the design width is what the core itself brings.
  - **(a) One type with two axes:** draw width and voltage tuning, and
    blend each instruction's cost between a narrow and a wide table by
    width. Weakness: Nova 1 measured only the two ends. Cores in the
    middle would be an invention, although ARM's three-tier chips (prime,
    performance and efficiency cores) suggest a middle is realistic.
  - **(b) P and E as two types,** each with its own range along its own
    curve, and its own measured cost table. This is how real big-and-
    little chips work, it gives two clear niches, and it's easier to
    observe ("this lineage lives on E cores").
  - **Claude's opinion: (b).** It follows real design and uses only
    measured tables.
  - The GPU stays its own type. Memory's range covers the DDR4-to-DDR5
    kind of difference, and a disk's covers speed against endurance.
- **Link resistance [settled 2026-11-10 in 5.7, "Between places" (3):
  power flows over proximity links, resistance = length × a per-length
  field].** Claude proposed length on the
  map times a thickness drawn from a range, since a real wire's
  resistance grows with length and shrinks with thickness. Damian: that
  leans heavily on how Nova 1 set up its nodes. He liked how that turned
  out, but isn't sure Nova 2 should reuse it. So this waits for the design
  of space. **What the power model already needs from any space design:**
  - discrete places that hold devices, and where sources sit;
  - connections between places, each with a resistance;
  - positions, for the day sweep and for spreading sources;
  - a sense of nearness for heat spreading between places.
- **The world starts cold and warms gradually [decided 2026-11-10,
  Damian].** Heat comes only from heat generators. The world starts cold
  and heats up over time, like turning on a stove-top element, and
  heating and cooling both take time, never happening instantly.
  - Claude: this is how 5.0b works. The heat generators are everything
    with resistance: devices, links and sources. A stove element is just a
    resistor. Heat capacity makes every temperature change gradual, and
    every place starts at the outside temperature.
  - **Cooling is a rate, not a temperature.** A place's cooling is how fast
    its heat leaves to the outside, like how well a room is ventilated. It
    adds no heat and sets no starting temperature.
  - Consequence: early on everything is cool and clean, with few errors and
    no heat cut-outs. The world gets harder as life grows and the heat
    builds up, so history matters.
  - **Better cooling near water [decided in spirit 2026-11-10, Damian].**
    The rest of how cooling is spread was settled in 5.7: water is a heat
    sink, coastal cooling is read off the raster, and inland sinks come
    from the cooling field.
  - **[settled by 5.0b]** If "heat generators" means separate heat-only
    sources (hot spots that heat without carrying power), that's a second
    input to the world and needs its own case. Claude's reading is that it
    doesn't. 5.0b's list of where heat is made, which Damian agreed, has
    devices, links and sources only.

### 5.0a Real devices (reference, 2026-11-10)

Rough figures for real hardware, so that Nova 2's devices can follow real
design (5.0 point 7).

| Device | Voltage | Power | What matters |
|---|---|---|---|
| CPU P-core | ~0.7–1.5 V, higher for higher clocks | desktop chip 65–250 W | few fast cores; speed is bought with voltage |
| CPU E-core | lower clocks and voltages | a few W per core | slower, much less energy per operation |
| GPU | ~0.7–1.1 V | high end 300–575 W, hundreds of amps | many slow, simple cores; big draw at low voltage |
| NPU | low | a few W | very efficient, but only for matrix maths |
| DRAM (memory) | DDR5 1.1 V, DDR4 1.2 V | a few W per module | refreshed every 64 ms, twice as often when hot; loses its contents within seconds to minutes without power, sooner when warm |
| SSD (flash disk) | 3.3 V in; ~15–20 V made inside to write | 2–10 W | writes cost far more than reads and wear cells out; keeps data without power for months to years, less when hot |
| Hard disk | 5 V and 12 V | 5–10 W, a surge to spin up | slow; keeps data without power |

What follows from the table:

- **Logic runs at about 1 V everywhere.** What distinguishes a big device
  from a small one is current, not voltage.
- **Speed costs voltage, and voltage costs power steeply.** A chip's power
  is roughly capacitance × voltage² × clock speed, and the clock it can run
  at rises with voltage, so power grows roughly with voltage³. That's why
  E cores and NPUs are efficient: they run slower and lower.
- **Speed follows voltage, and the edges stay [decided 2026-11-10].**
  Within its range, a device runs faster and hotter near the top and slower
  and cooler near the bottom. Near either edge it makes errors (5.0 point
  7); below its minimum it stops, and above its maximum it overheats. (An
  earlier wording here said "rather than switching off at the edges". That
  was wrong: it was only the fixed speed that was being replaced.)
  - **The speed advantage is real but steep.** Speed rises roughly in step
    with voltage, while power rises roughly with voltage³ and energy per
    operation with voltage². So 10% more speed costs about 33% more power
    and 21% more energy per instruction. Damian's "slight advantage within
    acceptable limits" needs no rule of its own: diminishing returns, heat
    and errors near the top edge put the sweet spot below the maximum.
  - Damian, on the speed trade-off: more variability is good. Claude's
    caveat: variability helps only when it creates a trade-off a program
    could take either side of. This one does (speed against energy per
    instruction). Variability that nothing can exploit is just noise.
  - **Two limits: the voltage range and the heat balance [decided
    2026-11-10].** Real chips have a third, a power cap set by firmware. It
    exists to protect the cooling and the regulators, and heat already does
    that job here.
  - **No firmware [decided 2026-11-10, Damian].** Devices are built to
    work within ranges, and have hardware interrupts through which
    programs interact with them, including overclocking and underclocking.
    Details come later, under Living. Claude's note for then: with no
    regulators a program can't change its voltage, only its clock. Running
    the clock faster than the voltage supports raises errors, as real
    overclocking does. So a program would control its own error rate, and
    with it how much its copies mutate.
  - **P and E cores may be one kind [superseded: withdrawn for two types, (b), below].** A real E core is
    not just a smaller P core. It is a simpler design:
    - it does less per clock, and has smaller caches and a lower top clock;
    - Intel fits about four E cores in the area of one P core.

    Its efficiency is real, but most of it comes from running slower at
    lower voltage (the curve above). The rest comes from the simpler design
    spending less energy per instruction. Measured gains vary: Apple's
    efficiency cores save a lot of energy, while Intel's are as much about
    more cores per area as about energy. So with speed following voltage,
    one kind of core with three numbers covers both: its current draw, its
    voltage range, and how much it does per clock.
  - **Separate designs, same rules [settled by the P and E decision, (b), after Damian's
    question].** Damian: so E cores are still separate, if only in their
    speed profile per instruction? Yes, as designs: a real E core is
    relatively slower at some instructions than others. Nova 1 measured
    this in its `home_v1` E-core column: about 15% slower for adds, 60–70%
    for divides, searches and kernel calls. But a separate design needn't
    mean separate rules. A core design can be a table of numbers: a cost
    per instruction, a voltage range and a current draw. P and E are then
    two rows, and the GPU's lanes could be a third. The instruction costs
    wait for the instruction set (Living).
  - **A catalogue fixed by type, with a range within each type [decided
    2026-11-10, Damian].** The types are fixed (P core, E core, GPU, NPU,
    memory, disk). Within a type there is a range: powerful and low-end
    GPUs, fast and slow memory (DDR4 against DDR5). Nova 1 did this
    fairly well.
    - **Each type is a trade-off curve, and each model a point on it
      (3.2) [decided 2026-11-10].** A stronger model must cost something, such as
      more draw, more heat, or a higher minimum voltage, or it simply wins.
      For example, a high-end GPU has many lanes and a big draw and needs a
      strong source; a low-end one has few lanes but runs on weak power.
      In this world, "price" is paid in power, heat and range, not money.
    - **Ranges drawn along each type's trade-off curve [decided
      2026-11-10, Damian, as Claude proposed below].**
      Damian would rather each type had ranges of variability, as in Nova
      1, than a fixed list of models.
      - **Claude's caveat:** if each number is drawn from its own range
        independently, some devices come out fast, frugal, cool and
        tolerant all at once. Those devices dominate, which breaks 3.2.
        Others come out bad at everything and are never worth using.
      - **[decided 2026-11-10, Damian, as above] Ranges along the curve.** For each device,
        draw one or two position numbers from ranges, such as its size
        and its speed class. Its other numbers (draw, minimum and maximum
        voltage, heat, work per clock) follow from the type's trade-off
        curve, with a little random scatter. That keeps continuous
        variety and luck, without free lunches.
      - **Settings per type:** the ranges of the position numbers, the
        curve's shape (how steeply cost rises with performance), and the
        scatter. The scatter must stay small next to the trade-off, or
        dominant devices return.
- **Memory and disk differ in how they keep things [decided 2026-11-10,
  Damian].** Memory loses its contents at low voltage, and faster when hot.
  A disk keeps its contents without power, but writing is expensive and
  wears it. This is what gives night its teeth. Real behaviour that comes
  with it (Claude):
  - **Memory holds on at lower power than cores need to run.** A sleeping
    computer turns its cores off while its memory keeps refreshing itself
    on a trickle of power. So the cores cut out first and memory last:
    night puts programs to sleep, and only a deeper outage wipes them. (Revised 2026-11-11, Damian: only threads not on a core sleep. The thread running on a core when it cuts out crashes; 5.10 (7).)
    **[decided 2026-11-10]** Memory's minimum voltage sits below the
    cores'.
  - **Memory decays gradually [decided 2026-11-10].** Its cells leak at
    different rates, so at its edge memory corrupts before it empties:
    errors first, then loss. That's the same shape as the other devices.
- **No regulators: error-prone edges, then hard cut-offs [decided
  2026-11-10, Damian].** Devices see their node's voltage directly. Near
  either edge of its range a device makes errors, and past the edge it cuts
  out. This is close to what real regulators do at their own edges (output
  sags as the input nears the regulator's limit, then an under-voltage
  lockout cuts it off). What it drops from real design is only the flat
  middle, where the chip's voltage is held steady whatever the supply does.
  - **High end: heat [decided 2026-11-10, Damian].** Too much voltage makes
    too much heat. Past its heat limit a device doesn't work until the heat
    has gone (a cool-down, with possible damage; damage belongs to
    Ending).
  - **Low end: a hard cut-off [decided 2026-11-10, Damian].** Above it the
    device works; below it, it is off.
  - **A gap between off and on at both ends [decided 2026-11-10].** Damian:
    in practice it's the same thing at both ends, but heat drives the gap
    at the top and the underlying voltage drives the gap at the bottom.
    - **High end:** the cool-down already is the gap, as long as it is
      defined as cooling to a lower temperature (or for a time), not just
      dropping back under the limit. Temperature changes slowly, so this
      is one number.
    - **Low end:** a gap is still needed. A device's own draw moves the
      voltage it sees: when it cuts out, the node's sag eases and its
      voltage rises above the cut-off, so it switches on, draws, sags and
      cuts out again, every tick. That flicker is an artefact of the tick,
      not physics. Real under-voltage lockouts have a gap for exactly this
      reason. One fixed gap for all devices (restart a little above the
      cut-off) is enough; it needn't be a number per device.

### 5.0b Heat [decided 2026-11-10, Damian, as below]

Damian agreed where heat is made, the two temperatures, and thermal runaway,
and kept heat spreading between nodes (see the end of this section).

Already decided:

- all power drawn ends up as heat (5.0 point 1);
- resistance is where it is made (point 3);
- too much heat cuts a device out until it cools (5.0a);
- memory fades faster when hot (5.0a).

**Where heat is made:**

- in devices: their whole draw;
- in links: their loss, split between the nodes at the two ends (over
  water, the loss goes into the sea and leaves the world, 5.7);
- in each source: its internal loss, on its own node.

So a hard-worked soft source heats its own node: get too close to the
fire and you get burnt.

**Where heat lives [decided 2026-11-10, Damian, per the intro above]: two temperatures.**

- **Each device has its own.** It rises with the device's draw and falls as
  heat passes to its node, at a rate set by the device's heatsink (a
  catalogue number).
- **Each node has an ambient temperature.** It rises with heat from its
  devices, links and source, and falls as heat leaves the world, at a rate
  set by the node's cooling. Cooling is a starting condition of the map:
  some places cool well and some badly. A shared node temperature is how
  devices heat their neighbours (Damian's "carry-on effects").
- **Both change gradually** (each has a heat capacity), and heat moves only
  from hotter to cooler. That is what makes the cool-down gap work.
- **Cheaper alternative:** one temperature per node, shared by all its
  devices. One hot GPU would then cut out every device on its node.

**Where heat goes:** out of the world, to a constant outside temperature.
**[settled by 5.10 (4)]** The outside temperature doesn't follow the day: days are
hotter only because more power is drawn. (Regional heat waves vary it as a boundary condition: decided in 5.10 (4), 2026-11-11, Damian.)

**Ambient heat loss [decided 2026-11-10, Damian].** Heat leaves each place
into the air over time, faster the hotter the place is than the outside
(as in real cooling). Damian raised this fearing that life's growth would
turn the world into Venus.

- **No runaway at the scale of the world (Claude).** All heat comes from
  power drawn, and the supply is limited, so the heat made can never exceed
  what the sources deliver. A place settles where heat in equals heat out.
  Its steady rise above the outside temperature is the heat made there
  divided by its cooling. So the world warms as life grows, then levels
  off.
- **Venus is a different mechanism:** its greenhouse comes from insulation
  rising, so cooling falls while the heat input stays the same. Nothing in
  Nova 2 lowers cooling, so no greenhouse can happen unless such a
  mechanism is added.
- **Runaway stays local and bounded:** leakage (a hot device draws more)
  can run away on one device or one crowded node, but the heat cut-out
  stops it. That is the intended build-up-and-release.
- **Settings follow from this:** given how strong the sources are, choose
  cooling so that only places close to strong sources pass the devices'
  heat limits.

**Heat sinks [decided 2026-11-10, Damian, after his idea; see below].** Damian
suggested heat sinks to draw heat out of the system, alongside water edges.

- **A heat sink is a place with very high cooling,** such as a water edge
  or a scattered cold spot. That makes it part of the cooling pattern, a
  starting condition, with no new law.
- **Heat sinks are in [decided 2026-11-10, Damian].**
- **Placed independently of power sources [decided 2026-11-10, Damian].**
  Life then has to choose between being near power and being near
  cooling, which is a trade-off (3.2). Placing sinks next to sources would
  remove it.
- **Fixed for the run, generated once from the seed [decided 2026-11-10,
  Damian],** like water, which is already a fixed heat sink. Together, sinks
  and water make one cooling map, a starting condition with no law of its
  own.
  - Damian's trade-off: this avoids any outside reaction to the world, at
    the cost of some dynamic complexity. Claude: that cost is small. The
    sources come and go, so the geometry between power and cooling still
    changes. A sink far from power today may sit next to a new source
    tomorrow. Fixed sinks also give lineages stable landmarks to
    specialise around, like rivers against the weather.
  - Water edges and sinks are the same kind of thing. Sinks extend cooling
    inland, where there is no water.
  - No cap on how much heat a sink takes is needed. A sink is never a
    perfect place, because power still has to travel to it and loses some
    on the way.
  - Rejected along the way: sinks that operators add where it's hot. That
    would have been the outside reacting to the world (against 3.1 and
    5.0 point 6), unnecessary for safety since heat is bounded by the
    supply, and it would weaken the heat pressure where life gathers.
- **[decided 2026-11-11, Damian, in 5.11 (5): passive, driven by temperature]** Cooling devices in the catalogue, such as fans and
  pumps. These spend power to raise a place's cooling, as in real
  machines, which would make heat something programs can manage. It
  belongs with the hardware catalogue.

**Effects:**

- Decided: errors near the top of a device's range, the heat cut-out and
  cool-down, and memory fading faster.
- **[decided 2026-11-10, Damian: thermal runaway, per the intro above] Leakage.** Real chips leak more current as they get hotter,
  so a hot device draws more and gets hotter still (thermal runaway). One
  rule, "a device's draw rises with its temperature", gives a real
  build-up-and-release (3.1): crowded, hot nodes tip over into cut-outs,
  which is a disaster nobody scripted.
- **[not proposed]** Resistance rising with temperature. It's real (about
  0.4% per °C in copper), but it adds little that leakage doesn't.

**Heat spreads between nodes [decided 2026-11-10, Damian].** Claude had
argued against it. Damian wants it in, possibly with a toggle in case runs
are too slow.

- **Claude's correction:** its cost argument was weak. Heat flowing along
  each link once a tick is cheap next to the voltage solve, which already
  touches every link every tick, and next to running the programs, which
  dominates.
- **A sliding scale [decided 2026-11-10, Damian]:** how much heat a link
  conducts is a setting, and 0 means none. Claude had also argued that a
  switch "turns one physics into two" and goes against "on by default".
  Damian rejected that, rightly: a switch adds composability for
  experiments, the law itself stays the same, and "on by default" means a
  switch starts on, not that there are no switches (§6).

It also passes 3.2: living next to a hot, crowded hub now costs something,
which gives a reason to keep away from one.

### 5.0c Power sources [decided 2026-11-10, Damian, as below]

**Where they sit [decided]:** each source sits on a node and feeds the rest
of the graph through the links (5.0 point 5). Its own node gets the full
voltage, and the source's own heat.

**Spread [decided in spirit, Damian]:** sources appear by chance each tick
(deterministic from the seed), not as a fixed count. Settings control the
spread of voltage, current and resistance across sources, and how variable
their fluctuations are. Claude's notes:

- **The number of sources is only a starting value [decided 2026-11-10,
  Damian].** A rule keeps creating and retiring sources, so the landscape
  stays dynamic. **[settled by 5.7 (f): the world grows with its places]** Settings, per 100 nodes so that
  density doesn't change with map size:
  - how many sources there are at the start;
  - the chance of a new one each tick;
  - their average lifetime.

  The long-run average follows (chance × lifetime), and the setup page
  shows it. Starting above or below it gives a world that thins out or
  fills up.
- **Luck is part of the world [decided 2026-11-10, Damian].** A region, or
  the whole world, can go without power for a long stretch by bad luck,
  and good luck happens too. Some seeds will simply be better than others.
  Claude's note: comparing settings then needs several seeds per setting,
  as Nova 1's trial harness did.
- **Voltage and resistance spreads, with current following [decided
  2026-11-10].** A source's most current is its voltage divided by its
  internal resistance (5.0 point 5), so only two of the three can be set.
- **Fluctuations: how often and how big are settings [decided 2026-11-10,
  Damian].** These are the minor ups and downs of a source's output during
  a run.
- Sizes skewed so that most sources are small and a few are large.

**A catalogue of source types [decided].** Following "variety in things"
(3.1), source types are rows of numbers: voltage, stiffness, how closely
the output follows the day, and how big and how slow the swings are. For
example (the names are for the viewer only; no law uses them):

- **steady:** stiff, ignores the day, small swings. A refuge at night.
- **daylight:** follows the day closely, moderate swings.
- **gusty:** ignores the day, large slow swings.

Each type is a trade-off (3.2): steady but weak, or strong but erratic.

**Appearance and lifetime: (b) [decided 2026-11-10, Damian].** Sources come
and go on a seeded schedule. No wear and tear on the power infrastructure:
it is maintained from outside the simulation by the "power companies".
Running down is a hardware issue, not a supply issue. There will be
occasional brownouts and blackouts from outside, as part of a source's
fluctuations: a source can drop low, or out, for a while. These are
separate from brownouts caused by load (sag), which come from the world
itself.

The options as they were put:

- **(a) Fixed for the whole run.**
- **(b) Sources come and go on a seeded schedule fixed before the run.** A
  source grows, holds, then fades, and new ones appear elsewhere. This is a
  boundary condition (3.1), since nothing in the world affects it. It keeps
  the geography changing, so life keeps having to move and adapt. Tierra
  plateaued in an unchanging world. The real analogy is hydrothermal vents,
  which open and die over decades while their communities move with them.
- **(c) Sources run down with use.** This is the supply reacting to the
  world, which Damian ruled out for the day cycle (5.0 point 6). It's the
  one place a reaction might earn its keep, because it makes overgrazing
  real.

### 5.1 No bootstraps [decided 2026-11-10, Damian]

Behaviours such as eating and parasitism must emerge from programs acting on
the environment directly. No higher-order instruction may hand them a
behaviour ready-made. Nova 1's ENGULF and INJECT are the examples to avoid.

**A test for an instruction [settled: used for every call since, e.g. NMATMUL and PING]:** its effect can be stated
in terms of the world alone (cells, cores, devices, channels, links), never
in terms of another program. ENGULF and INJECT fail this test. COPY, ALLOC
and DIVIDE pass it, as long as they are defined on cells and threads.

**[parked by Damian, 2026-11-10; settled by 5.9]** Does the rule also exclude library-style operations that pass the
test but do a lot in one step, such as Nova 1's 20 GPU op kinds and the
NPU's dot, matvec and matmul? If it does, the NPU has nothing left to be:
those ops were its only role in Nova 1.

Settled by 5.9: the GPU runs the ordinary instruction set in lockstep
lanes, and Nova 1's 20 accelerator ops are dropped. The NPU keeps one
library-style operation, NMATMUL, reached through the OS like disk and
network. It passes the test because it names no program.

### 5.2 How programs reach each other [settled 2026-11-10 by 5.8]

**Outcome:** none of the four options below was taken as it stood. Memory
and disk are allocated by request and protected per page. Isolation breaks
in three real ways: shared pages, injection through input, and physical
faults (5.8). That is closest to (a) with physical breaches, except that
reading and running another's code is possible only within a shared page.

The options as they were put:

Nova 1's private code (a program reads and runs only its own body and waste)
is ruled out by 5.1. Under it, one program can touch another only through a
verb. That is why ENGULF and INJECT existed. So access must be open in some
form, and the open question is what ownership means:

- **(a) Absolute ownership** (Tierra): read and run anywhere, write only to
  your own cells. Parasites can emerge (Tierra's did, from a seeded
  ancestor), but eating can't: a live program's cells can't be taken.
- **(b) No ownership** (Core War, BFF): anyone writes anywhere, and eating is
  overwriting. But nothing is held, so there is nothing for resources to
  belong to.
- **(c) The last writer owns:** writing a cell takes it, along with whatever
  is tied to it (5.3). Eating, theft and parasitism come from read, write
  and execute alone, with no contest rule and no knob. Risk: it may be too
  violent for anything stable to form. Unproven.
- **(d) A graded contest on every write** (Nova 1's surface contest made
  general): more control, but it is a tuned mechanism, the kind 5.1 is
  trying to avoid.

### 5.3 Resources [decided 2026-11-11, Damian: kinds, form, disk allocated like memory, the archive folded into disk, the boot block]

**Kinds:** Nova 1's set is mostly right (Damian, 2026-11-10): memory, P and
E cores, GPU, NPU, disk and archive, plus heat, bandwidth and signals.
Which of these survive 5.1 in full is open (see the NPU question there).

**Form [decided 2026-11-11, Damian; closure below]:** Nova 1 holds resources as ledger counts per body, drawn
from node pools by ALLOC and handed on at DIVIDE. Ordinary instructions
can't touch a count, so moving resources between programs needs a verb,
which 5.1 rules out. Keeping the kinds without the verbs means changing the
form.

**Proposed (Claude):** resources are located in the world, tied to places
(cells, device slots). Holding the place holds the resource, so taking the
place takes the resource by whatever rule 5.2 sets.

**[decided 2026-11-11, Damian; Claude's closure] Later decisions answer the form.**
Resources are specific things, not amounts (5.7):

- Memory and disk are held as allocations: particular pages and disk
  ranges, recorded by the OS against their owner (5.8, 5.9).
- Cores, the GPU and the NPU aren't held at all. They're shared in time:
  turns from the fair scheduler, GPU launches and NPU calls.
- Power isn't held either; it's drawn (5.0, "Who gets the power").
- Resources change hands without verbs, in three ways. Memory is freed and
  claimed again: a dead program's pages go back to the pool, contents and
  all. Or a program is hijacked: the attacker's code runs on the victim's
  threads in the victim's pages (5.8). Or, the third way (decided 2026-11-11, 5.12 (2)): a program gives one of its own allocations to a new program, by starting a thread in it outside its own running code.
- One difference from the proposal above: writing over another program's
  cells doesn't make them yours. The OS's record of who owns a page
  stands. Only freeing changes it, or giving an allocation to a new program at SPAWN (5.12 (2)).
- **How memory is taken, then (Claude, answering Damian 2026-11-11).** FREE
  works only on the caller's own memory, so no call takes another's.
  - **Run as the victim:** code that gets onto a victim's thread (through a
    shared page or an overrun) acts with the victim's ownership. It can use
    the victim's pages in place, with no transfer. Real exploits work the
    same way: they run with the rights of the process they break into.
  - **Kill, then claim:** a victim that faults with no handler loses its
    thread, and when its last thread ends its pages are freed with their
    bytes intact. An ALLOC of the victim's size class then tends to land
    in the gap it left, under first-fit within the class. Crashing a victim
    is far easier than making it call FREE, so this is the likely route.
  - Overwriting cells in a shared page uses them without owning them. The
    owner still holds them and frees them when it dies.

**Kinds left over [decided 2026-11-11, Damian, apart from the archive]:**

- **Signals:** replaced by SEND and RECV on ports (5.9).
- **Bandwidth:** a property of links (5.8, transit), not something held.
- **Archive [decided 2026-11-11, Damian]: dropped as a kind; it becomes
  the slow end of the disk.**
  - What it was: in Nova 1 the archive was a second disk on every node, a
    quarter of the fast disk's size by default. It rotted at a hundredth of
    the fast disk's rate and cost ten times as much per access and per
    codon. ALLOC claimed it separately. Nova 1's records give no reason
    beyond a slow, safe place to keep things. It held no genomes and no
    logs.
  - In Nova 2, each device type is drawn along its own trade-off curve,
    and disk's runs from speed to endurance (5.0). A disk drawn at the
    slow, enduring end is an archive, so no separate kind is needed.
  - Real anchor: tape. It takes seconds to tens of seconds to reach data,
    and it keeps data for decades unpowered, where a solid-state disk is
    microseconds and months to years.
  - So a place can hold more than one disk, and disk's curve reaches that
    far. ALLOC's device argument (5.7) picks the fastest or slowest disk,
    as it does for memory.

**Two disk gaps [found 2026-11-11, Claude].**

- **(1) Disk has no allocation call [decided 2026-11-11, Damian: disk is
  allocated the way memory is].** "Memory and disk are allocated by
  request" was decided (5.8), but 5.9's table had only DREAD and DWRITE,
  which took any disk offset, so any program could read and write the
  whole of its node's disk. Now:
  - ALLOC takes a fourth permission value, disk, and returns a disk
    offset. The device argument picks among the place's disks by speed.
  - Disk is divided into blocks, as memory is into pages. The block size
    is a setting (5.7's sizes), and allocations are placed by size class.
  - Protection is per block. DREAD and DWRITE work on blocks the caller
    holds an allocation in. They stop at the first block it doesn't, and
    return the count done (calls fail soft, 5.9). Within a shared block
    nothing is protected, so a DWRITE that overruns reaches a neighbour's
    data, as an overrun in memory does.
  - FREE takes the permission value too, so it can tell a disk offset from
    a memory address. A program's last thread ending frees its disk as
    well as its memory, and freed blocks keep their bytes. The table stays
    at 16 calls.
  - Real anchor: files. A process can only read and write files it has
    rights to, and deleting a file leaves its blocks unwiped, which is how
    undelete tools work.
  - What disk is for while a program lives: bulk storage beyond memory,
    and a backup. After a brownout that corrupted memory without ending
    the program, a copy on disk lets it repair itself. That's closest to
    what Nova 1's archive was for.
- **(2) Nothing reads a disk back after an outage.** Memory holds through
  an ordinary night, since its minimum voltage is below the cores' (5.0).
  So the disk matters only in a deeper outage, one that wipes memory and
  ends every thread. Afterwards, the only code running is the idle run in
  the reset region, which is static. Nothing can read back what a program
  saved, and its owner's death frees the range anyway. So the disk cannot
  carry a program through an outage, which is the decided reason it exists
  (5.0, "Memory and disk differ").
  - **[decided 2026-11-11, Damian] A boot block.** On a cold start, after an outage that
    wiped memory, the node copies a boot block from the start of its disk
    into the reset region.
  - Real anchor: a PC's firmware loads the disk's first sector into memory
    at power-on and runs it. Boot-sector viruses lived there (Brain, 1986;
    Stoned; Michelangelo).
  - **Damian's worry (2026-11-11):** a program would have to write a boot
    sector that then runs a loader to copy itself from disk into
    executable memory. That might be too much selective pressure. He could
    think of no other reasonable way.
  - **[decided 2026-11-11, Damian; Claude's answer to the worry] Booting is migration from the
    node's own past.** Nothing new is needed past one trigger:
    - **The copy is the hardware's, not the program's.** On a cold start
      the node delivers its boot block to its own port 0, exactly as if it
      had arrived by SEND. The arrivals rule already decided (5.9) then
      applies: the next free core runs it from its first byte, and it must
      ALLOC, copy itself out and SPAWN, as any migrant must.
    - **So any program that can migrate can boot.** The only new behaviour
      is one DWRITE of itself to the boot block. That's the same shape as
      migrating, which is one SEND of itself to port 0. SEND sends a
      program to another place, and DWRITE to the boot block sends it to
      its own place's future.
    - **Only programs bigger than the boot block need a loader:** a
      second stage that DREADs the rest. That's a trade-off, not a hurdle,
      and real master boot records work the same way (446 bytes of code
      that load the rest). Compact genomes boot for free, as they migrate
      best.
    - **The pressure is set by the outages, not by the boot block.** If
      deep outages are local and short, migrants from unaffected neighbours
      refill a wiped node, and dispersal wins. If they are wide and long,
      the boot block pays, and dormancy wins. If they are rare, it barely
      matters. Real ecology has both: after a fire, plants come back from
      seeds in the soil or from seeds blown in. Which pays depends on how
      often and how widely disturbance strikes. Cohen (1966) treated
      dormancy as bet-hedging; Venable and Brown (1988) treated dispersal
      and dormancy as substitutes. So the outage settings under disasters
      decide the pressure. Abiogenesis from static is still the last
      resort.
  - **Details [decided 2026-11-11, Damian]:**
    - **One boot block per node, owned by nobody.** It's a fixed range at
      the start of the node's most enduring disk that the allocator never
      hands out. Any program at the node may DREAD or DWRITE it. That makes
      it the disk's counterpart of the reset region, the one page in memory
      nobody owns. The last writer before the outage wins. Writing it costs
      DWRITE's time and wears the disk, so keeping your copy current
      against rivals has a price (3.2). Its size is a setting.
    - **The trigger is physical.** A cold start is when power returns
      after the voltage fell below the minimum of the memory device that
      holds the reset region, so its contents were lost. An ordinary night
      doesn't trigger it.
    - **Disk starts as static, as memory does.** So a boot block nobody
      has written boots static, and abiogenesis after an outage is
      unchanged. Real anchor: a used disk is full of old data.
  - **Alternatives Claude rejected:**
    - **No recovery from disk.** Outages are survived only by
      migrants. That loses the decided reason for disk.
    - **OS hibernation:** the OS saves all memory to disk as the voltage
      sags and restores it afterwards. Real laptops do this. But it makes
      outages nearly harmless for free, with no trade-off.
    - **A per-program "save me, restart me" call.** This fails 5.1: the OS
      would start a thread for a program, which is the remote thread-start
      that dropping MIGRATE removed.
  - Consequence: the first program to boot has a whole empty node to
    itself, a founder's head start (priority effects, in ecology). This
    answers 5.4's open question about owning the reset region "as
    boot-sector viruses did": not by owning it, but by holding the boot
    block that refills it.

### 5.4 Where life starts [decided 2026-11-11, Damian: (a), random static in the reset region; no seeded ancestors]

- **(a) From random material** (abiogenesis), Nova 1's main mode.
- **(b) From a seeded ancestor**, as in Tierra.

Claude's opinion: (a), with one source of material instead of Nova 1's four
(sparks, vents, decay and cosmic rays).

**[decided 2026-11-11, Damian]: (a).** Life starts as random static in
the reset region, and nowhere else. Every later decision builds on it: the
memory law's static, the reset region, and births from it.

- **No seeded ancestors [decided 2026-11-11, Damian: dropped completely,
  including as a test-only hook].** Claude had proposed a seeded-ancestor
  count (0 = none), then a test-only hook that writes a hand-made
  replicator into the reset region.
  - Cost, accepted: until a replicator arises in the reset region, nothing
    downstream (migration, predation, selection on power) can be tested.
    Under 5.8 that origin is unproven (5.5's risk). Tierra and Avida both
    seed a hand-written ancestor for this reason.
  - So point 4's test comes first and gates everything else: a target
    length for the shortest replicator under the 22 instructions and the
    OS calls, and a test that one arises from static.

**[settled by Damian's answer 1 below: static is part of the memory law]** With power (5.0), that one source could be
physical noise: bits flip more often where a node runs hot or its voltage
sags. Memory that starts random, plus that noise, would then be both the raw
material and the mutation source, under one law.

**Damian's spark [settled by his answers 1–3 below].** As a law, random bits
("static") appear in memory (probably memory only, not disk). Memory is
split, as in real hardware, into executable memory and memory only for
data. Static in executable memory is the spark.

Claude's critique:

1. **Static as its own, uniform law brings back cosmic rays**, one of the
   four sources that 5.0 replaced with wrong results at device edges,
   including memory failing to hold a bit. A uniform rate also hits every
   living program everywhere, so it has to stay under the error threshold
   in the middle of every range, which keeps it too low to be much of a
   source. **Proposed:** keep static inside the memory law we have. (i) Memory
   starts random. (ii) A bit that memory fails to hold reads back random,
   not zero, so static rises with heat and sag. (iii) A small floor
   everywhere for the cosmic-ray part, as a sliding scale with 0 meaning
   off. Real basis: soft errors rise as voltage falls, and DRAM leaks faster
   when hot. Memory only, as Damian leans: keeping things safe is disk's
   side of its trade-off against slow, wearing writes.
2. **Executable versus data fits as a page permission** beside 5.8's
   per-page protection, not as separate memories. Real hardware marks
   pages; only some microcontrollers keep code in its own memory. Whether a
   page may be writable and executable at once is OS policy in real life
   (W^X), not hardware. **Proposed:** the owner chooses, by a call.
   Trade-off: a writable, executable page makes copying and self-changing
   code cheap, but it is what injection through input (5.8 ii) needs.
3. **Static makes material, but nothing runs it yet.** Under 5.8's OS only a
   running thread starts a thread. Options:
   - (a) Starting threads only. Random code faults within a few
     instructions, and a fault stops a thread, so they'd be gone in a few
     ticks with nothing to restart them.
   - (b) **Reset page [Claude's recommendation].** A core with nothing to
     run executes its reset page, and a fault with no handler sends it back
     to the page's start (real: a triple fault resets a PC). The reset page
     is the one executable page nobody owns, and code running in it may
     write to it, or nothing changes there but static. It runs only on idle
     cores, so origins happen in empty or dead places and stop once a place
     fills. Cost: idle cores draw power unless their code sleeps. Real
     cores run the OS's idle loop, not user memory, so this is a stand-in,
     on the hardware side.
   - (c) A chance-based thread start at a random executable address.
     Simplest to tune, but it has no hardware analogue, fires inside living
     programs too, and never stops (Nova 1's sparks).
   Both (b) and (c) need an ownership rule: a thread with no owner that
   makes its first OS request becomes a new program that owns what it gets.
   Open: can a program own a reset page and collect its core's idle time,
   as boot-sector viruses did? (Answered 2026-11-11: no, but it can
   hold the boot block that refills the region after an outage; 5.3, "Two
   disk gaps" (2).)
4. **Whether life starts at all depends on the instruction set and the OS
   calls, not on the spark** (5.5's risk, at the very first step). Tierra
   and Avida seed a hand-written ancestor. BFF (2024) got replicators from
   random code because a replicator there is a few bytes and runs straight
   on other programs' bytes, with no allocation or calls; its authors
   report it happening with and without background mutation, so noise
   wasn't the key ingredient. Under 5.8 a replicator needs at least
   allocate, a copy loop, make the copy executable (unless pages may be
   both) and start a thread, each call costing a trap. **Proposed:** if (a)
   is a requirement, set a target length for the shortest possible
   replicator and test it early. ~~Keep a seeded-ancestor count as a
   starting-condition setting~~ (dropped 2026-11-11, Damian: no seeded
   ancestors at all).

**Resolved [decided 2026-11-10, Damian]: the OS does not zero a page when
it hands it out.** Programs receive leftovers (static and dead programs'
code), which is both raw material and a leak, as Damian wants (see the OS
call table, dead-program pages, §5.9).

**Damian's answers (2026-11-10):**

1. **[decided] No separate static law.** Damian had meant background analog
   noise sneaking into a signal, and dropped it as its own law. **[decided
   2026-11-10, Damian]** Keep (i)–(iii) as part of the memory law: memory
   starts random, lost bits read back random, and a small floor everywhere
   (a sliding scale, 0 off). The reset page needs the floor: on a cool, well-powered node nothing else
   changes its bytes, so code that crashes at once would crash the same way
   forever (the world is deterministic).
2. **[decided 2026-11-10, Damian: "OS chooses"; see the second round
   below and 5.8's OS values]** A two-way setting, "Fixed" or "OS chooses". The
   OS can't choose per program (that breaks 5.8's neutrality); it can only
   apply a fixed rule. Claude's reading, to confirm: "Fixed" is one rule for
   the whole world, and "OS chooses" makes the rule on writable, executable
   pages part of each OS kind, beside page size. Lax kinds make copying
   cheap and injection easy, strict kinds the reverse. That adds a second
   difference between OS kinds, where 5.8 kept all but page size global.
   Either way, each page has an execute bit its owner sets by a call. Since
   5.8 (i) packs allocations, **[settled by ALLOC's permission (5.9) and the PROTECT fix (5.7)]** the permission is asked for at
   allocation and the OS packs only allocations with the same permission
   into one page.
3. **[decided: (b), settled by 5.9's ports, where port 0 is the reset
   region] (b), the reset page.** Damian asked whether code that
   doesn't crash at once would be moved to regular memory. Claude: not by
   the OS. Choosing code because it didn't crash is the OS judging programs
   (5.8, 3.1), and the winner would be an endless loop (`jmp $` is two bytes
   in x86, so static makes it often). **[settled by 5.9's travel rule and the 2026-11-11 no-thread rule]** The code leaves by
   itself: it asks for memory, copies itself out and starts a thread there.
   Whatever leaves can, by that alone, copy itself.
   - **Flaw in Claude's (b):** one isolated page per core is pure random
     search, and the shortest replicator is about 30 bytes (5.9), which
     random search won't find. BFF's replicators built up gradually because
     many programs read and wrote each other's bytes. **[settled by the third and fourth rounds; still
     unproven]** A node's reset pages form one shared region with no owner.
     Every idle core runs in it and code running there can write anywhere
     in it, so it's a small soup. A node has few cores, so the soup is small:
     test this first. Its size is a setting. Open: does a core restart at a
     fixed spot in it or a random one? (Settled in the fourth round: each
     core starts at its own fixed spot.)
   - Code with no owner may touch only the reset region; anything else
     faults.
   - **Second round (2026-11-10).** Damian agreed 2, with each node's OS
     having its own values (5.8). On 3 he asked whether code that makes a
     memory request, when there's space, gets moved to main memory.
     Claude: nothing is moved. A birth, step by step **[settled by 5.9's travel rule and the 2026-11-11 no-thread rule]**:
     1. The reset region starts as static.
     2. A core with no thread to run executes the region from its start.
        This idle running is not a thread: it holds no place in the
        scheduler, and any ready thread takes the core from it.
     3. Most bytes crash within a few instructions, and the core starts
        again. On each start the registers point into the region (a real
        core also starts with set values), so early memory accesses land
        inside it rather than faulting at once.
     4. The region changes by the static floor and by the junk writing
        over it. Bytes that copy themselves spread through the region (the
        BFF route).
     5. Code that makes its first OS call becomes a new program and owns
        what it gets. If memory is short, the call just fails.
     6. If it copies itself into its new memory and asks for a thread
        there, the copy is alive: a program in main memory with a thread
        under the scheduler. The original stays in the region until it's
        written over.
     Steps 5 and 6 are the replicator's own work. A program that got
     memory but no thread joins the open question of pages owned by a
     program with no threads.
     **[decided 2026-11-11, Damian]** The decided fixed rule
     applies: the OS frees the pages of a program with no threads left
     (5.8). A program born in the reset region has only the idle run that
     made it. When that run ends (the core restarts, or a ready thread
     takes it) and the program has started no thread, its pages are freed,
     with their contents left as they are. So a birth must finish within
     one idle run.
   - **Third round (2026-11-10) [settled by the fourth round and later decisions], answering Damian:**
     - **One shared region per node.** Every idle CPU core runs in it at
       the same time, each from its own fixed starting spot (spread
       evenly through the region) and with its own registers. Real
       multi-core chips also start each core at a set address.
     - **CPU cores only, P and E.** GPU lanes can't call the OS (5.9), so
       code there could never make the first call and become a program,
       and real GPUs are started by a CPU. Not the NPU either.
     - **Static rate.** Damian: the region starts as white noise, and a
       few bits (2 or 4, scaled to the page) flip at random each tick.
       That is the floor decided in answer 1, and the floor runs in all
       memory, not just the reset region. At 4 flips per 4 KB page
       (32,768 bits) per tick, a 16-byte program is hit about once every
       64 ticks, so life couldn't hold. **[settled by the fourth round and 5.10 (8)]** Set the rate as a
       chance per bit per tick, so it scales with any page size, low
       enough that a program usually lives out its life unhit. The
       region's churn comes mostly from the junk writing over it, which
       was enough on its own in BFF. If the origin turns out too slow, a
       higher rate inside the reset region could be a separate setting,
       but nothing in real hardware gives a reason for it. The value
       waits on what a tick is, and on a test.
   - **Fourth round (2026-11-10).** Damian: each core starting at its own
     place makes sense. He wants bit flips, but rare, like Nova 1's
     cosmic rays, to be worked out later. **[decided]** One rare floor in
     all memory, with no special rate for the reset region for now; the
     rate comes later. Claude: after the first fill, random bits reach the
     reset region in four ways:
     - the junk code writing over the region (the main churn);
     - memory fading where it's hot or the voltage sags, since lost bits
       read back random;
     - wrong results near device edges;
     - the rare floor, which breaks deadlocks where nothing else changes.
     Consequence (Claude): origins come faster where memory is noisy (hot
     or under-powered places), so life would tend to start in rough places
     and spread to calmer ones. **[accepted 2026-11-10, Damian]**

### 5.5 The discoverability risk [open]

Nova 1 required every world feature to have a mutation-reachable way to
sense and use it (its design.md 6.2), and its verbs did that: one codon
gave a whole behaviour. Built from primitives, eating and parasitism are
multi-instruction programs, so random code finds them later, or within a
run's budget not at all. Tierra's parasites came from a seeded ancestor, not
from random code. BFF and Core War-style systems get replicators from random
code with free overwriting. Claude knows of no result showing predation and
parasitism emerging from random code in a world with several resources.
Argument can't settle this; only a test can.

### 5.7 Space and environment [decided 2026-11-11, apart from the size defaults, which wait on test runs]

Damian liked Nova 1's landscape and substrates, but found its 1D strip of
memory, used as the measure of distance, limiting. He can't think of a
realistic scenario in which programs live in anything other than a 1D
strip, and ideas so far feel arbitrary.

**Two scales in Nova 1:** between nodes, a graph on a 2D landscape; within
a node, a 1D strip of memory, where distance is the difference between
addresses (contact range, template searches, vents, waste).

**Real hardware: addresses are one-dimensional, but distance isn't
(Claude).**

- **RAM is random-access.** Reading any address costs about the same, so
  the difference between two addresses isn't a distance.
- **Real distance is a hierarchy:**
  - the same cache line is almost free;
  - the same memory device is the base cost;
  - another memory device in the same machine costs more, because it means
    crossing the chip's interconnect (non-uniform memory access, roughly
    1.5–2×);
  - another machine costs thousands of times more, over the network;
  - disk is slower still.
- **Inside a chip, cores sit on a 2D mesh,** and the cost between two cores
  depends on where they are. Nova 1 measured a cache line moving between
  cores.
- **One-dimensional distance is real for one thing: scanning.** Searching
  through memory costs per cell scanned.

**What power already needs from space (5.0):** places that hold devices and
sources; connections between them, each with a resistance; positions, for
the day sweep and for spreading sources and sinks; and nearness, for heat
spreading.

**Within a place [decided 2026-11-11 as (b), refined below]:**

- **(a) Nova 1's strip:** distance is the difference between addresses.
- **(b) A hierarchy:** a place holds devices, and each memory device is a
  neighbourhood. An addressed read or write costs by level (same device,
  same place, a neighbouring place, further). Scanning costs per cell
  scanned.
  - A memory device also has its own voltage range, heat and fading
    (5.0a), so a neighbourhood shares a fate: a memory device that browns
    out wipes its whole neighbourhood.
- **(c) A 2D grid inside a place,** like a chip's mesh or Avida's grid.

Claude's opinion: (b). Caveat: within one memory device everyone is equally
near, which makes it a well-mixed population, and spatial structure helps
cooperation evolve (Nowak and May, 1992). Under (b), the structure comes
from having many small devices, so the size of a memory device becomes a
key number.

**Within a place, expanded [decided 2026-11-11, Damian: the four jobs,
the allocator as part of the OS, size classes, and sizes scaled to
programs as a setting, and the device argument's form].**

- **(b) is already being used.** Later sections are built on it without
  its being decided:
  - memory costs by hierarchy level (5.9's costs);
  - one flat address space per node (5.9);
  - packed allocations sharing pages (5.8 (i));
  - the reset region (5.4);
  - the disk reached through calls, and the GPU through pages (5.9).

  What's left is to say exactly what "near" means inside a place.
- **"Near" does four separate jobs.** Nova 1's strip did them all with one
  number. Real machines split them:

  | Job | What decides it in a real machine | Proposal |
  |---|---|---|
  | Access cost | which device the bytes are in | by device, not by distance |
  | Who can touch whom | address order: an overrun runs into the next addresses | Nova 1's strip, kept for this alone |
  | Who shares a fate | which memory device holds you | by device |
  | Who you live beside | the allocator | size classes (below) |

- **(1) Access cost: by device, with no distance inside a place.**
  - In a real machine any address in memory costs about the same, so cost
    depends on the device (its type and its drawn speed, which follows
    its voltage), not on where in it the bytes are.
  - **[decided] Drop the NUMA level from (b).** What it is: a large real
    server has two or more CPU chips (sockets), each with its own memory
    attached. A core reading memory on its own chip pays the base cost;
    reading memory attached to the other chip crosses the link between
    the chips and costs about 1.5–2× as much. That's non-uniform memory
    access (NUMA). Real OSes mostly hide it by putting memory near the
    thread that first touches it. In Nova 2 the fair scheduler puts a
    thread on whichever core is free, so the extra cost would land by
    luck, and a cost a program can't choose to avoid fails 3.2. Avoiding
    it would need a way to pin threads to cores, which is priority by
    another name (5.8 ruled that out).
    - Damian added that it makes little sense with a shared memory pool
      anyway. Claude's note: the shared address space isn't the reason,
      since real NUMA machines share one address space too; NUMA is a
      cost inside a shared pool. The reason is the one above, that the cost
      would fall by luck.
  - Scanning needs no rule: reading costs per byte at the device's speed,
    and CPY per byte copied.
- **(2) Who can touch whom: the address order.** This is the one real
  survivor of Nova 1's strip. An overrun runs into the next addresses, and
  a page holds whatever allocations are packed into it (5.8 (i)). So
  contact is one-dimensional and local, which is real: a buffer overflow
  goes forwards and an underrun backwards. Distance between addresses
  means nothing else.
- **(3) Who shares a fate: the memory device.** A place holds one to a
  few memory devices, each drawn along its type's trade-off curve with
  its own minimum voltage, speed and rate of fading (5.0a). All of a
  place's devices see the place's voltage. In a brownout, the device with
  the highest minimum fades first, and everything in it fades together.
  - The address space lays the devices end to end, and a device boundary
    is always a page boundary.
  - **ALLOC takes a device [decided 2026-11-11].** Real systems expose
    memory tiers the same way: Linux ranks memory into tiers by speed, and
    a program can ask for a tier.
  - **The argument's form [decided 2026-11-11, Damian, as proposed]:** a third
    argument to ALLOC, after size and permission.
    - **0 means any.** The OS takes the fastest device with room in the
      size class, so fast memory fills first unless a program asks
      otherwise.
    - **Counting from 1 picks by speed:** 1 is the fastest device at this
      place, 2 the next, and so on.
    - **Counting from −1 picks from the slow end:** −1 is the slowest.
    - **An index past the end takes the nearest device that exists.** 3 at
      a place with two devices gives the slower one.
    - Why ordered by speed, not by position: a migrant arrives at a place
      with different devices. "1" still means "the fastest here" and "−1"
      "the slowest here", so a program's choice travels with it. Raw
      indices would mean something different at every place.
    - Speed and robustness are two ends of one curve: a faster device
      needs a higher voltage (5.0 point 7), so its minimum is higher and
      it fades first in a brownout. So 1 means fast and fragile, and −1
      slow and steady. The drawn scatter can break the order now and then,
      so SENSE reports each device's numbers.
    - **SENSE per device:** speed, minimum voltage and free bytes, by the
      same index.
    - Every value is valid, as for every call (5.9). A replicator that
      leaves the register holding whatever it held still gets memory, so
      the shortest replicator doesn't grow. Choosing a device on purpose
      is something evolution adds later.
  - Without a choice, which device you land in would be luck again, and
    several devices per place would add noise rather than a niche. With
    one, it's a trade-off (3.2): fast memory that fades first in a
    brownout, or slow memory that holds on.
- **(4) Who you live beside: the allocator [decided 2026-11-11, Damian:
  size classes, as part of the OS].** Where ALLOC puts
  new memory decides who ends up next to whom, so the allocator is
  ecology.
  - **(α) First-fit:** the first gap that's big enough, in address order.
    This is the classic design. Neighbours are whoever was freed or born
    around the same time, and kin end up scattered.
  - **(β) Size classes:** allocations of similar size share pages, with
    first-fit within a class. Modern allocators work this way (slab
    allocators, jemalloc).
    - Kin have the same length, so kin pack together. That gives the
      local structure cooperation needs, which a well-mixed device lacks
      (the caveat above).
    - A parasite shares a page with its host only by matching the host's
      size. That's real too: heap exploits "groom" the heap by allocating
      objects the victim's size to land next to it ("heap feng shui").
    - So mimicry of size can evolve, with no rule of its own.
  - **PROTECT on shared pages [conflict found 2026-11-11; fix decided
    2026-11-11, Damian].** Size classes pack several programs into a page, and execute
    permission belongs to the whole page. So pages are grouped by size
    class and permission. PROTECT, which changes a whole page, succeeds
    only if the caller owns every allocation in it, and otherwise fails
    soft.
    - Consequence: on a strict (W^X) node, a copier must take a whole page
      to make its copy executable. Strict nodes force padding, which is
      safer but dearer. That adds to the trade-off between strict and lax
      nodes (5.4 answer 2).
    - Real anchor: mprotect works on whole pages, which is why JIT
      compilers map whole pages for their code.
  - **(γ) A placement hint:** ALLOC "near this address". It's real
    (mmap's hint), but it hands programs the choice of neighbour, which
    makes targeting a victim trivial.
  - **Claude's opinion: (β).** It's the real modern design, kin clustering
    and the cost of mimicry follow from it, and it needs no new argument
    to ALLOC.
  - The allocator is part of each node's OS, and its policy is the same
    everywhere, like call costs (5.8 kept OS variety to page size and the
    writable-and-executable rule).
- **Sizes are scaled to programs, not copied from real machines
  [decided 2026-11-11, Damian; the sizes are settings].**
  - The shortest replicator is about 14 bytes (5.9), and real memory is
    gigabytes. A 4 KB page would hold hundreds of programs, so every
    program would share a page with everyone, and page size would stop
    being a trade-off.
  - So the page-size list is a short list of powers of two sized to hold
    a few programs, for example 64, 256 and 1,024 bytes. Memory devices
    hold some hundreds to thousands of programs' worth.
  - The numbers wait until programs' real sizes are seen. What's fixed is
    the ratio: a page holds a few programs, and a device many pages.
    - Why wait: 14 bytes is only the shortest possible replicator.
      Evolved programs grow, with handlers, threads and copies of their
      own data, and nobody knows yet whether a typical one will be 30
      bytes or 500. A page list chosen now might hold 50 programs per page,
      or less than one. So the first test runs measure typical sizes, and
      the defaults are set from that.
    - **Settings [decided, Damian]:** the list of page sizes the OS
      values are drawn from (5.8), and the range of memory-device sizes.
      Placeholder defaults: pages of 64, 256 and 1,024 bytes.
- **(a) and (c), finally:**
  - (a) Nova 1's strip survives only as (2), contact by address order.
    That one-dimensionality is real, so it isn't the limit Damian
    disliked: it no longer measures cost or nearness for anything else.
  - (c) A 2D grid inside a place is rejected. Nothing in a real machine's
    memory is two-dimensional to a program, so it would be invented
    locality. The map already supplies two dimensions.
- **What's left mixed:** competition for cores. Every thread at a place
  competes in one fair scheduler, so a place is well mixed for CPU time
  and local only for contact. That's realistic, and places are small; the
  map gives structure at the larger scale.
- **Open:** only the size defaults, after the first test runs.

**Owning and sharing under (b) (Claude, answering Damian, 2026-11-10).**
Damian asked whether (b) means hardware is fixed per node and programs
share it rather than owning it.

- **Hardware is fixed at its place: yes.** Devices are drawn once from the
  seed (by the biome fields below) and stay where they are, until burnout
  or renewal, which belong to Ending.
- **Sharing or owning: the hierarchy doesn't decide it.** That is the
  ownership question (5.2) together with who gets the power (5.0, Open).
- **[settled by 5.3's form, which refines it: overwriting doesn't take ownership] Programs own specific things, not
  amounts.** In Nova 1 a program owned an amount drawn from a pool (500
  millicores, say), which nothing but a special verb could take. In Nova 2
  it would hold particular things: these cells in this memory device, or
  this core while it runs on it. A particular thing can be lost by
  ordinary means, such as being overwritten or pushed off a core, so
  eating and theft can emerge without bootstraps (5.1).
- **Tension with the travel complaint (5.6).** If hardware is fixed at
  places, it can't travel with a program by definition. What travels is
  the program's code and data, and it must find new hardware where it
  arrives, as a real process moved to another machine does. Damian
  disliked exactly that in Nova 1. Under (b) it's physics rather than
  bookkeeping, but it's the same outcome. **Accepted [decided 2026-11-10,
  Damian]:** hardware stays where it is. A program travels to find a node
  with the hardware it wants, as in Nova 1.

**Between places [decided 2026-11-10, Damian, as below; to be judged in
a running world].** The three options were Nova 1's landscape (scattered
nodes, paths by nearness, water and landmasses, bridges), a regular grid of
places, or a hierarchy like a data centre (racks, rows, halls). Under 3.1,
Nova 1's scheduled bridges had to go or be fixed. Damian chose (3), the
landscape rebuilt, with the grid as a test world, but said he'll have to
see it in action. So the choice is provisional, and the first runs judge
it.

**What the map must supply** (collected from 5.0, 5.0b, 5.0c and 5.9):

- places, each holding devices, an OS, and sometimes a source;
- links: power (a resistance) and heat (a conductance) between near
  places, and data (bandwidth, delay and corruption, and SEND's hops)
  along cables. These are two sets of links, (c) below;
- positions, for the day sweep, the drawn fields, and where sources appear;
- water, for cooling (decided), and barriers (decided under "Gradients and
  drop-offs" below).

**The deciding number is how hop counts grow with the size of the world
(Claude).**

- **On anything two-dimensional** (a landscape or a grid), the typical hop
  count grows with the square root of the number of places. Rough
  estimate, untested: with 1,000 places on one landmass, about 20 hops
  between two random places and about 55 at most.
- **In a data-centre tree** it grows with the logarithm. In a real
  three-tier network, two servers in the same rack are 2 links apart, in
  the same row 4, and in different halls 6, at almost any size.
- **This decides whether distance means anything.** With square-root
  growth, a far move takes many hops, neighbours compete and cooperate
  locally (Nowak and May), and a barrier can split a population, which is
  the decided reason for drop-offs. With logarithmic growth, everywhere is
  a few hops from everywhere else. Above the rack the population is
  close to well mixed, and a barrier can add at most a hop or two.

**(1) A data-centre hierarchy: not between places [decided 2026-11-10].**

- It's the most realistic layout for computers, but it contradicts what is
  already decided:
  - sources that appear anywhere and sprawl outwards like a fire (5.0c);
  - a day sweep across the map (5.0 point 6);
  - water and coasts for cooling (5.0b);
  - smooth fields with winding edges ("Gradients and drop-offs").

  A tree has no positions, so there's no "across" for the sweep to cross
  and nowhere to draw a smooth field. Each of these would need a stand-in.
- In a real data centre, power and data run on two different trees fed
  from a handful of utility feeds. A geography of supply doesn't exist
  there to copy.
- **Where it survives: inside a place,** as 5.7(b) already has it (devices
  within a place). The hierarchy stays at the scale where it's real, with
  geography above it.

**(2) A regular grid: a test world only [decided 2026-11-10].**

- For:
  - it's the simplest, with every hop alike;
  - there are published results to compare against (Avida's grid, Nowak
    and May's lattice games);
  - the voltage solve works on a regular sparse matrix;
  - it's easy to draw.
- Against:
  - every place has the same neighbours, so there are no crossroads and no
    backwaters. All variety comes from what the fields paint on it, and
    the layout itself contributes none (3.1, "variety in things");
  - water and coasts need holes in the grid. Once a grid has holes and
    bridges, it's an irregular landscape laid on a lattice;
  - power and heat spread in diamonds (4 neighbours) or octagons (8),
    which is a visible artefact of the lattice.
- Claude's opinion: worth having as a test world, as Nova 1's ring was,
  but not as the world.

**(3) Nova 1's landscape, rebuilt [decided 2026-11-10, Damian; to be
judged running].** Kept from Nova 1: the land-and-water raster (its six
land settings), places scattered on land, links by nearness, and a few
bridges across water. Changed:

- **(a) Places scattered by a density field [decided 2026-11-10, Damian,
  over keeping Nova 1's generator],** drawn with the shared noise
  ("Gradients and drop-offs"), with a minimum spacing between places.
  Dense patches are towns and sparse ones are countryside.
  - **Links follow a fixed rule from the positions,** with no randomness
    of their own: two places are linked if no third place lies between
    them (the Gabriel graph, about 4 links per place on average, with no
    crossings). Within a landmass, every place is reached. Power and data
    use slightly different sets of these links (c).
  - This replaces Nova 1's seven link settings (paths per node, spread,
    hermits, hubs, most paths, connected landmasses, long links) with one
    field. Towns get more and shorter links because they're dense, not
    because a setting says so. Nova 1's hermits become one-place islands.
  - **The cost, accepted:** link counts vary less than under Nova 1's
    generator, and towns are the only source of hubs.
- **(b) Link properties come from geometry and fields:**
  - length from the map;
  - resistance is the length times a per-length value from its own field:
    how well the ground or air between two places carries power ("Link
    resistance" below);
  - heat conductance falls with length;
  - delay grows with length (see (d)).
- **(c) Bridges are fixed for the run (3.1),** generated once from the
  seed, and **carry data, not power [decided 2026-11-10, Damian].**
  Damian: power as discussed was always wireless, a proximity-based cloud
  around the source's node that reaches the places around it with a
  falloff set by resistance. Nothing carries power along a cable, so a
  bridge, being a cable, carries none.
  - **Power is a network over proximity links [decided 2026-11-10,
    Damian],** not a law of distance from the source. Power passes from
    each place to its near neighbours through a resistance set by the gap
    between them, as 5.0 point 5 decided. The network gives two decided
    behaviours:
    - a crowded inner ring dims everything beyond it (5.0 point 5);
    - power lost along the way heats the places it passes through (5.0b).
  - **Two sets of links, both from the same positions [decided
    2026-11-10, Damian]:**
    - **Proximity links carry power, and heat over land.** They're the
      Gabriel graph over all places, water or not. The Gabriel graph
      always contains the shortest gap between any two parts of the
      world, so every landmass has at least one proximity link to
      another. Across a wide sea that link is long, and water (below)
      lets almost nothing through it.
    - **Data links carry SEND.** They're the Gabriel graph worked out on
      each landmass by itself (so each landmass is connected), plus the
      bridges. SEND's hops, delay and routing use only these, and SENSE's
      neighbour list (5.9) lists these.
    - On land the two sets are almost the same links. They differ at
      water: a little power crosses narrow straits and none crosses wide
      seas, while data crosses only where there is a bridge.
  - **Water is a heat sink [decided 2026-11-10, Damian].** Heat doesn't
    cross water from one place to another. Over a link's water stretch,
    heat goes into the water and leaves the world, and so does power lost
    in the water. The good cooling of coastal places (below) is the same
    fact seen from the shore.
  - **Water conducts power as in real life [decided 2026-11-10, Damian]:**
    better than air, but with a falloff. Damian: a live cable in the sea
    doesn't electrify the whole ocean. Claude's working [settled by (c), decided]:
    - **Better conduction on its own can't give a falloff.** In a network
      made only of resistances along the way, lower resistance means less
      falloff, not more. A water stretch that simply conducted better
      would make every strait a power highway.
    - **Real water leaks to ground.** The sea lies on the seabed, which is
      earth, the return path of every circuit. Current put into the sea
      spreads out and returns through the seabed nearby, so the voltage is
      high only close to where it went in. That's why a live cable in the
      sea is dangerous only near it: the sea isn't electrified because it
      shorts the cable out.
      - Seawater conducts about 5 S/m, roughly 100 to 1,000 times better
        than soil. Air hardly conducts at all, so the stand-in medium on
        land has no real value to copy.
    - **So water is a leaky conductor.** It conducts along its length and
      leaks to ground everywhere. That's Ohm's law twice, not a new law:
      a leak is just a resistance to ground. Across water, the voltage
      falls off exponentially over a **reach** set by how well water
      conducts against how fast it leaks.
    - **The catch: water can't carry power across without draining it.**
      A leak can't tell crossing power from any other, and a place's
      contact with the water is both its way across and its way into the
      seabed.
      - Roughly: of the current that enters the water, about a third
        reaches a far shore one reach away and the rest drains to the
        seabed. Across three reaches, about 5% arrives.
      - So if narrow straits are to pass useful power, coasts must drain
        heavily, as a live cable in the sea does. If coasts are to drain
        only a little, straits pass little.
      - Real life is the second for things on land beside the water, and
        the first for a cable in it.
    - **Two numbers to set: the reach, and the shore drain [decided
      2026-11-10, Damian].** The shore
      drain is how much a place bordering water loses into it, through
      the ground between the place and the water line. Crossing follows
      from the two. This replaces the water slider proposed earlier, which
      got its falloff by making water conduct worse than land, against
      real life.
    - **A mild drain and a short reach [decided 2026-11-10, Damian, on
      Claude's recommendation]:** by default, a drain of about one small
      device's draw at full voltage and a reach of one link length. Then:
      - coasts are cool and rich in data (the bridges land there), and pay
        a small power tax, which is a trade-off (3.2);
      - narrow straits pass a trickle, and wide seas nothing.

      A heavy drain would darken every small island, since an island is
      mostly coast, and waste any source that lands near water.
    - **How it's computed [decided 2026-11-10, Damian: the approximation
      is fine]:** once, when the world is made, from the raster.
      - Each place's drain is set by how much water it borders. That's the
        same reading that gives its coastal cooling: the sea takes both
        heat and current from the shore.
      - Each proximity link that crosses water becomes one resistance
        between its two places, rising exponentially with the water's
        width.
      - The per-tick solve keeps its shape: resistances between places,
        plus resistances to ground.
      - The approximation: a narrow strait's shores drain as much as an
        open coast's. Really they drain a little less, because the far
        shore is charged too. Solving the water as a sheet on the raster
        every tick would be exact, but too costly.
  - **Consequences:**
    - islands near a mainland get a trickle of its power;
    - landmasses far apart are separate grids. One whose sources have all
      faded stays dark until a new one appears. Its programs can't draw
      power from across the sea, but they can leave by SEND;
    - water is hard to cross for power but not a wall, as barriers should
      be.
- **(d) Distance in SEND's delay [decided 2026-11-10, Damian; revises
  5.9's hop-only rule].** Delay = hops × a per-hop cost + route length × a
  per-length cost, plus the data size, and the chance of corruption grows
  with length. Without the length term, a long bridge would be one hop, as
  cheap as a link inside a town.
  - The real anchor: in real networks each hop costs a little switching
    time (around a microsecond), and distance costs travel time (light in
    fibre covers about 1 km in 5 µs, so crossing an ocean takes about
    30 ms).
  - A per-length cost of 0 gives back the hop-only rule, for experiments.
  - The hop count still matters for routing.
- **(e) Routing takes the path with the least delay,** with ties broken by
  node id, so it's deterministic.
  - Because the map is fixed, routes can be worked out once at the start,
    as a next-hop table at each place. For 1,000 places that's about a
    million entries (2 MB). For 10,000 it's 100 million, so routes would be
    found on demand and cached instead.
  - Link outages, if Environment adds them, would force a recompute.
- **(f) Lengths are measured in links.** Every length setting (a field's
  feature size, a bridge's longest length, a source's reach) is given in
  units of the average link length. Changing the number of places then
  changes the size of the world, not its ecology per hop. Nova 1 measured
  in fractions of the map's height, so more nodes made the world denser
  rather than bigger.

**Where the day, sinks and sources go under (3):**

- **The day sweep** follows the east–west position. The map is bounded by
  sea, so it doesn't need to wrap.
- **Heat sinks:** places next to water get high cooling (the coast is read
  off the raster; water is a heat sink, decided in (c)), and inland sinks
  come from the cooling field. They're fixed (decided, 5.0b).
- **Sources land by area, not by place [decided 2026-11-11, Damian].**
  Each new source lands at a random point on land and attaches to the
  nearest place. Damian: sources should be spread evenly, while places are
  denser in and near towns. That is what "by area" gives.
  - Even on average, random in detail. In any one stretch of land, sources
    clump and leave gaps by chance, as the luck decision wants (5.0c).
    Evenly spaced sources would need a minimum distance between them,
    which isn't proposed.
  - **What the two mean.** Every place catches the land that is nearer to
    it than to any other place, its catchment.
    - By place, each place has the same chance of the next source,
      whatever its catchment.
    - By area, a place's chance is its share of the land, so a big
      catchment catches more.
    - Example: a town of 10 places packed into 1 km², and one countryside
      place alone with 10 km² around it. By place, the town gets 10 of
      every 11 sources. By area, the countryside place gets 10 of every
      11, so it gets 100 times as many as any one town place.
  - **The real anchor for area:** sunlight and wind fall by area. A solar
    farm needs land, and real cities make little of their own power and
    import the rest.
  - **A sliding scale instead of a switch [decided 2026-11-11, Damian: a slider, default 1].** A place's
    chance of a new source is its land area (the raster cells nearest it,
    counted once) raised to a power. 1 is by area (the default), 0 is by
    place, and above 1 favours the countryside more strongly. One number
    covers both options, as §6 prefers.
  - Consequence: a countryside place covers more land than a town place,
    so it hosts a source more often. Towns are rich in network and poor in
    power per place. The countryside is rich in power and poor in network,
    and its long links make the bands around a source narrow in hops (a
    steep falloff). That's a trade-off from geometry alone, with no rule of
    its own (3.2).
    - In practice the countryside is jackpot or nothing: the place that
      hosts a source is very bright, and its neighbours, at the end of
      long links, are much dimmer.
    - A town rarely gets a source, but when it does the short links
      spread the power evenly across the town. Many places then share
      one source, so a soft source sags.
  - The alternative, per place, gives every place the same chance and makes
    towns better at everything.
  - Small islands get few sources and, unless they're near a mainland,
    long dark spells. That's intended (luck, 5.0c). They also pay the
    shore drain (c) on most of their places. The land settings control how
    many such islands there are.

**Risks and costs:**

- **Power and data share links on land.** They part only at water (c), so
  on land a place that is well linked for data is also well linked for
  power and heat. Fields on resistance and delay loosen the coupling.
  Claude's opinion: leave it until a test shows it matters.
- **The voltage solve** on an irregular planar graph is sparse, at about
  4 links per place. That's affordable for thousands of places in fixed
  point (5.0, Cost). Both link sets are worked out once, when the world is
  made.
- **Settings, a rough count:**
  - land: 6, reused;
  - places: count, minimum spacing, and the density field's settings;
  - bridges: per pair of landmasses, longest, and unbridged;
  - water: the reach and the shore drain;
  - the source-area power;
  - delay: per hop and per length;
  - the per-length resistance field's settings.

  That's about 20, against Nova 1's 44 in "World and land" and "Paths and
  bridges".
- **Unproven: room for barriers in a small world.** At 300 places with
  about 4 links each, a landmass of 50 places is about 7 hops across. A
  field's features must be only a few hops wide to put edges inside it. In
  small worlds, coasts will be the only real barriers.

**Open: none [closed 2026-11-11].**

- ~~The source-area power, as the setting that expresses area against
  place, is proposed.~~ Decided 2026-11-11, Damian: a slider (1 by area, the default; 0 by place; above 1 favours the countryside more).

**Substrates become hardware biomes [settled 2026-11-10 by "Gradients and
drop-offs", which Damian decided for every drawn field, substrates
included].** Nova 1's
substrates were per-node performance levels for eight hardware types,
blended by smooth landscape fields and painted like Minecraft biomes. They
carry over as the source of devices' drawn numbers (5.0a): smooth fields
over the map decide where fast GPUs or large memories cluster.

**On arbitrariness (Claude):** choices feel arbitrary when nothing anchors
them. There are two anchors: what real machines are like, and what a choice
gives programs (a trade-off, 3.2). The hierarchy passes both.

**Gradients and drop-offs [decided 2026-11-10, Damian; how, proposed].**
Every drawn field (power sources, substrates, OS values, everything) needs
both smooth gradients and steep drop-offs, so the map has natural barriers.
Real reason (Claude): barriers split populations, and split populations
drift apart into new kinds (allopatric speciation).

- ~~[proposed, Claude] Shared region borders, with a jump size per
  field.~~ Damian preferred independent noise (below).
- **[decided 2026-11-10, Damian] One noise algorithm** that combines
  gradients and sharp edges, seeded differently for each field it's used
  for (substrates, resources and so on). Consequence (Claude): each
  field's edges fall in different places, so a place can be good for one
  thing and poor for another. Single edges are weaker barriers, and strong
  ones appear where several edges happen to coincide.
- **[the method for the decided one-noise algorithm; details at build] The noise:** two layers of the same smooth noise
  (fractal noise, as Nova 1's landscape fields use), each with its own
  seed. The first is used as it is, for gradients. The second goes
  through a steep step, which makes cliffs along its contour lines, so the
  edges wind like coastlines. Each field has an edge-strength slider (0 =
  smooth only) and a sharpness. It's computed once when the world is made.
- **Barriers are hard to cross, not walls [decided, Damian].**
- **Power is the exception [decided, Damian]:** a place's power is not a
  drawn field; it follows from sources and resistance (5.0).
  - **Link resistance [form settled 2026-11-10 by "Between places" (3);
    edges decided below].** A proximity link's resistance is its length
    times a per-length value: how well the medium between two places
    carries power. Length comes from the map's geometry. The per-length
    value comes from the same noise with its own seed. Edges in it are
    real: separate power grids are joined by thin ties (Texas's grid has
    only small links to the rest of the US). So allow edges.
  - **Edges in resistance are grid borders, on the links [decided
    2026-11-11, Damian, as proposed by Claude, including the strength,
    the spacing and the caveat on what a border divides].**
    - **Resistance can't use the edges as the other fields do.** Every
      other field's values sit on places, and an edge is a jump between
      neighbouring places: an adapted program finds the other side
      different, and that difference is the barrier. Resistance sits on
      links. A step in it would make two plateaus, one side conducting
      well and the other poorly. Power would still flow from the good side
      into the poor one and just fade faster there. That's a texture, not
      a border, and it doesn't separate grids.
    - **So the step layer's contour lines are read as borders.** A
      proximity link that crosses one has its resistance multiplied: it's
      a thin tie. The gradient layer still gives the smooth regional
      variation in how well power travels. It's the same noise, read
      differently because resistance lives on links. That departs from
      how every other field uses the noise, and the reason is the one
      above.
    - **Strength, in plain terms:** crossing a border costs as much as N
      ordinary links. Claude's proposed default is about one habitable
      band's width (5.0 point 7). A source's band then usually stops at a
      border, and only a big source, or one close to the border, leaks
      noticeably across. That's hard to cross but not a wall, as decided.
      - Bands aren't numbered yet, so 10 links is a placeholder until they
        are.
      - Real ties are harder: Texas's ties to its neighbours carry around
        1–2% of its peak demand. At that strength a border would be close
        to a wall, which was decided against.
    - **Spacing:** borders about 20–30 links apart. A large landmass then
      has 1–3 grids and a small one has none, much as a real continent has
      a few (North America has three main grids).
      - A grid region must be large enough to catch sources of its own.
        Sources land by area, so a small fenced-off region would be dark
        most of the time.
    - **Settings:** the gradient's spread and feature size, plus the
      border strength and spacing. Sharpness doesn't apply, since a link
      either crosses a border or doesn't.
    - **What a border divides is the supply, not the population.** SEND
      crosses it freely, because data links ignore power resistance. Each
      side's voltage follows its own sources and its own crowds: a crowd
      on one side doesn't sag the other, and the two sides' supplies rise
      and fall independently. That makes selection differ across a border
      without stopping migration. It's a weaker push towards new kinds
      than a barrier to movement. Barriers to movement come from water,
      distance and the other fields' edges.
    - Heat and data are unaffected by borders.

### 5.8 Running, moving and holding [decided 2026-11-11, transit included]

**Moving between nodes: network operations [decided in spirit 2026-11-10,
Damian].** Movement belongs to network protocols, which programs use the
way they use hardware interrupts. A network operation copies or moves a
program to a destination node over the available links. A destination
several hops away is reached by routing, like real network traffic.
Claude's notes:

- **Copy and move pass the no-bootstrap test (5.1):** both can be stated
  in world terms, as sending cells to a place (and, for a move, releasing
  them here). Copying to a far node is reproducing at a distance.
- **Routing done by the network fits the theme.** The network is
  infrastructure maintained from outside, like the power grid. The cost
  is that finding a way is not something programs evolve. **[settled by
  5.9: the network routes]**
  Accept that, or let programs choose each hop.
- **A destination is only worth choosing if a program can know something
  about it.** Otherwise a multi-hop send is a random jump.
- **Sensing is local; knowledge of far places must be communicated
  [decided 2026-11-11, Damian].** A program senses its own node and the
  nodes directly around it. Anything about distant nodes has to come from
  other programs over the network, something an information-age
  civilisation would invent. Travel starts out blind beyond the
  neighbours. Claude's notes:
  - **It passes 5.1, and it gives reading input a reason to pay.** That's
    the stepping stone injection through input needs (5.8 (ii)).
  - **Expect it to be rare.** In artificial life, explicit signalling has
    evolved where it was rewarded (Avida's tasks, signalling between
    evolved robots), but long-range sharing of information hasn't
    appeared unrewarded in open-ended systems. The world must work without
    it, and it does: spreading by neighbour-to-neighbour SENDs is
    diffusion.
  - **The first stepping stone is already there.** RECV returns the
    sender's id with the data (5.9), and a node that sent something was
    powered and inhabited at that moment. So any message, even a
    meaningless one, is a weak report about a far place.
  - **Leaks to close, so "blind" holds [decided 2026-11-11, Damian]:**
    - **Node ids carry no geography.** The ids are 0 to N−1, shuffled by
      the seed. With ordered ids, "my id + 1" would be a free map. With
      every id in use, probing ids learns only which ones can be reached.
    - **Timing is allowed.** SEND sleeps for the network time (5.9), so a
      sender learns how far away a destination is, as a real ping does.
      That's distance, not conditions, and real networks leak it too.
      Claude's opinion: keep it. (Revised 2026-11-11: SEND is now fire and forget, so this free ping goes; 5.8, "Transit" (6). An explicit PING call replaces it, (8), decided 2026-11-11.)
  - **What the neighbours tell [decided 2026-11-11, Damian]:** SENSE takes a neighbour's
    index (its data links, in a fixed order) and returns the same public
    readings as for its own node: id, voltage, temperature, idle cores and
    free memory. A program's own budget and bytes waiting stay private.
    - Free cores and memory matter most, because a migrant wakes only
      where both are free (5.9).
    - Own-node SENSE gains idle cores and free memory too.
    - Reading a neighbour costs one hop of network time, like a real
      neighbour query, so it is dearer than sensing yourself and cheaper
      than a probe SEND (3.2).
- **Transit has physics:** each link has a bandwidth, a delay and a chance
  of corruption, and sending costs power at both ends. A program in
  transit is data, not running. **[decided 2026-11-11: delay and corruption
  in 5.7, "Between places" (3)(d); the rest in "Transit" below]**

**Transit [decided 2026-11-11, Damian: SEND is fire and forget, (6) (A); failed links reroute, (7); PING, (8)].** This
works through the transit bullet above.

**Already decided:**

- delay is hops × a per-hop cost, plus route length × a per-length cost,
  plus the data's size (5.7 (d));
- corruption grows with length;
- routing takes the least delay, recomputed when links fail, come back,
  or places respawn (5.7 (e), 5.10 (5));
- SEND sleeps for the network time, so timing works as a ping (above).

**Revised by Damian's answers (2026-11-11):**

- UDP's "no acknowledgement" (5.10 (8)) first gave way to errors returned to
  the sender, like email's bounces. Damian: "when the queue is full there
  needs to be a returned error, just like email"; "nothing gets dropped". Then he chose fire and forget for SEND ((6) (A)): nothing comes back beyond the sender's own exit.
- Corruption stays silent.

**(1) Bandwidth: each link has a bandwidth, in bytes per tick [decided
2026-11-11, Damian: "links should vary"; Claude reads that as its own
field].**

- **Where it comes from:** each link draws it from its own field, as
  resistance is drawn from the per-length field (5.7).
- **Why its own field:** power-rich and data-rich routes can then differ.
  A place can be well fed but poorly connected, or the reverse. The risk
  section of 5.7 named the coupling of three physics on one graph, and
  fields on each quantity loosen it.
- **Bridges** carry data only, with high bandwidth (real: undersea cables
  carry most intercontinental traffic). Being few, they are bottlenecks
  all the same.
- **Real anchor:** links vary by orders of magnitude. A fibre backbone
  carries terabits, a copper last mile megabits.

**(2) A message's delay, made precise [decided with (1)].** A SEND is one
message. The decided "plus the data's size" becomes:

delay = hops × per-hop + route length × per-length + size ÷ the route's
narrowest bandwidth + time waiting in queues

- Dividing by the narrowest bandwidth treats a message as pipelined along
  its route. That's how fragmented traffic behaves on real networks: a long
  message is held back by the slowest link, not by every link in turn.

**(3) Busy links: queues, and newcomers dropped when full [decided
2026-11-11, Damian].**

- **Queues:** each link end has a queue of fixed size. The size is a
  setting, in ticks of that link's bandwidth. Messages wait their turn.
- **A message that doesn't fit whole into the next queue is dropped,
  silently.** Only the newcomer is dropped. Messages already waiting keep
  their place (Damian: "not messages that already were sent prior"). This is tail drop, as real routers do it.
- **Routes don't adapt to load,** only to failures and repairs. Real
  routing protocols mostly use fixed link costs, not live load.
- **Real anchors:**
  - routers buffer the same way;
  - the old rule of thumb sizes a router's buffer at about one round
    trip's worth of its link's bandwidth (Villamizar and Song, 1994);
  - Damian first asked for an error to come back, like an email bounce, then chose fire and forget instead ((6)).
- **Big messages are dropped first.** A big message needs more free
  queue space to fit whole.
  - This is a trade-off with no rule of its own (3.2): compact genomes
    travel best (5.9).
  - Real UDP behaves the same way: a datagram bigger than the path's
    packet size is split into fragments, and losing any one fragment loses
    the whole datagram.
- **Sharing:** a flood from one program fills queues that everyone's
  traffic uses. That's another commons, and real enough (congestion
  collapse, denial-of-service floods).

**(4) A network device at each place [decided 2026-11-11, Damian].** Each
place has one network device, a row in the hardware catalogue. It is the
place's router and network interface in one.

- **It obeys the same laws as every device:** a voltage range, heat, wear
  (5.10 (1)), failure, and visits (5.10 (2)).
- **It must be on for the place to send, receive, or forward traffic
  passing through.**
  - So a blackout makes a hole in the network as well as in the power
    grid. Routes recompute around a dark place, as around a failed link.
  - Real anchors: in Puerto Rico after Hurricane Maria (2017) and in Texas
    in February 2021, phone and internet service failed as power failed,
    once the backup batteries ran out.
- **Power:**
  - **A constant draw while it's on.** Real routers draw nearly the same
    power busy or idle.
  - **A small extra draw per byte at the sending and receiving places.**
    That keeps the decided "sending costs power at both ends".
  - Every place pays the constant draw as a base load. Places on busy
    routes pay nothing extra for forwarding, because the network is
    infrastructure (5.8). Their device still has to be on.
- **No speed of its own.** Throughput is limited by the links' bandwidth,
  so the device adds no second bandwidth setting.
- **The device's numbers live in the catalogue:** its range, its draws,
  and its endurance.

**(5) Receiving: fixed inboxes, with newcomers dropped when full [decided
2026-11-11, Damian].**

- **Size:** each comms port has an inbox of fixed size (a setting).
- **Who holds it:** the OS holds it, in the same memory device as the
  reset region (5.10). So it fades in a brownout, like any memory.
- **When it's full,** a new arrival is dropped, silently. What is already
  waiting stays. Real anchor: a full UDP socket receive buffer drops new
  datagrams. (Damian first asked for an error here, then chose fire and forget, (6).)
- **What the program sees:** RECV copies from the inbox into the
  program's buffer. "Bytes waiting" is already sensed (5.9).
- **Port 0 never refuses.** An arrival is always written into the reset
  region, and gets a prompt run on a spare core (5.9). What happens to it
  there is the region's churn, not a refusal.

**(6) What SEND tells the sender [decided 2026-11-11, Damian: fire and
forget, form (A)].**

- **History:**
  - The first proposal was UDP: SEND said "ok", and losses were silent.
  - Damian's answers to (3) and (5) then asked for errors to come back.
  - Claude proposed (a), SEND waiting for the outcome. That turned every
    send into a sleeping thread.
- **Damian (2026-11-11):** he prefers the simplicity of the first design,
  fire and forget. Leave it to the receiver to say whether it got a
  message, or at least use bounces that arrive later, so that SEND stays
  asynchronous. He accepts that this adds an obstacle to evolution.
- **Claude agrees.** The strongest real anchor is the end-to-end argument
  (Saltzer, Reed and Clark, 1984), the principle the internet is built on.
  - Reliability can only be checked at the ends, because the network can't
    vouch for what it delivers. Nova 2 already has this property:
    corruption is silent (5.10 (8)), so an "ok" from the network could
    never mean "arrived intact".
  - Any program that needs to know must hear back from the receiver
    anyway. Errors from the network only add early news of some failures.
  - On the real internet, IP delivers on a best-effort basis, and TCP's
    reliability is built by the two ends on top of it.

**What SEND does now, in both forms:**

- **It returns at once.** It no longer sleeps for the network time.
- **It costs the crossing plus a copy cost per byte,** like CPY, on the
  core. That makes SEND a core-budget call, not a latency call (5.9's
  service costs).
- **It fails at once only for reasons known locally:**
  - no route in the place's own routing table (real: "no route to host",
    reported by the sender's own system);
  - its own exit queue is full (real: the sender's buffer filling,
    ENOBUFS);
  - its own network device is off.

  That local error is the brake on floods, along with the network
  device's power per byte (4).

**The two forms differ only in what happens beyond the sender's own
exit:**

- **(A) Pure fire and forget: the receiver answers, or no one does
  [chosen 2026-11-11, Damian].**
  - Beyond the first hop, nothing comes back. A full queue, a full inbox
    or a failed link loses the message silently.
  - A program that wants confirmation needs a receiver that replies. That
    makes it a two-party protocol, which has to evolve (5.8 expects that
    to be rare).
  - It reverses Damian's earlier answers to (3) and (5), "an error is
    returned, nothing gets dropped", except at the sender's own exit.
  - It's the simplest form, and it's how UDP works. Migration is
    unaffected: arrivals at port 0 were never confirmed, and port 0 never
    refuses.
- **(B) Fire and forget, plus bounces that arrive later [not chosen].**
  - A downstream refusal (a full queue, a full inbox, a failed link)
    sends a bounce back to the sending place. It arrives after the trip to
    the problem and back.
  - The bounce is delivered as an interrupt, a new SETHANDLER kind,
    carrying the destination node, the port and the reason.
  - If the sending thread has no handler for it, or has ended, the bounce
    goes nowhere.
  - Success is silent, as with email.
  - It keeps "nothing is dropped silently" for programs that listen, at
    the cost of a handler kind and the plumbing to route bounces back.
- **Why Claude prefers (A):**
  - A bounce tells the sender only some failures, never success, and
    never corruption. So a program that cares must still build the
    receiver's reply.
  - Bounces then mostly duplicate what the reply protocol already gives,
    for extra machinery.
  - (B) is the right choice if Damian wants "nothing dropped silently" to
    stand as a principle.

**What changes in both forms:**

- **The free ping goes.** SEND no longer sleeps for the network time, so
  timing can't be learned from it. A program learns distance only from
  round trips with a partner that replies (measured with SENSE's time),
  or from its neighbours (SENSE, one hop).
  - That revises the decided "timing is allowed" (above). That decision
    allowed the leak; it didn't require it.
  - It makes "travel starts blind" stronger, which Damian wanted.
  - Real anchor: ping only works because the far end's OS answers echo
    requests. Nova 2 has no such service. One could be added later, as
    an OS that replies, if blindness proves too harsh. (Damian asked for
    this as a command: PING, (8), decided.)
- **Sending becomes cheap,** so floods are cheaper. Three things push
  back:
  - the local "exit full" error;
  - the network device's power per byte;
  - the shared queues (3).
- **Big messages are lost first** under (A), and bounced first under (B).
  Either way compact genomes travel best.

**(7) A failing link: messages reroute [decided 2026-11-11, Damian;
details proposed, Claude].** This replaces "data in flight on a failed
link is lost" (5.10 (5)). It also follows Damian's rule that messages
already sent aren't dropped.

- **Each place keeps a message until the next place has all of it.** If
  the link fails while the message is crossing, the copy at the near end
  goes back into routing from there and takes the new route.
  - Real anchors: on a link, the sending end keeps a frame until the far
    end acknowledges it (Wi-Fi does this). Delay-tolerant networking
    (the Bundle Protocol, RFC 5050) has each hop hold a message until the
    next takes "custody" of it. Mail relays work the same way.
- **Messages waiting in a failed link's queue move to the new route's
  queue at the same place.** They go ahead of new arrivals as room frees
  up, so they're never dropped as newcomers. Until then, they wait where
  they are.
- **When no route remains** (an island cut off, or a region partitioned):
  - **[decided 2026-11-11, Damian]** The message waits at the place until a route
    returns, for up to a lifetime. The lifetime is a setting; 0 means it's
    dropped at once, as real IP does with "no route".
  - Real anchor: a delay-tolerant network holds a bundle until a contact
    appears or the bundle's lifetime expires.
  - The alternative is to drop it at once, which needs no setting, but it
    breaks "messages already sent aren't dropped".
- **When the place holding it loses its network device** (dark, or
  failed), the messages queued there are lost. That's the decided rule
  that a device which cuts out loses its work in flight (5.10 (7)).
  Rerouting covers links that fail; a dead router still loses what it
  held.
- **Effect:** a link failure now delays traffic instead of destroying it,
  unless the place holding the traffic goes dark. Outages become
  slowdowns, partitions become waits, and blackouts are what lose data.
- **Cost:** routes are already recomputed on every topology change (5.7
  (e)). Moving queued messages between queues at one place is cheap.

**(8) PING: a network call [decided 2026-11-11, Damian, as a call].**
Damian suggested PING as a network-level command, now that SEND no longer
gives timing.

- **PING node, timeout → round-trip time in ticks, or −1 for no
  reply.**
  - The calling thread sleeps until the reply comes or the timeout runs
    out. It's a latency call, like RECV waiting.
  - The request and the reply are tiny messages, and they travel through
    the same queues as everything else. So the time includes congestion,
    and a full queue can drop either one, which shows up as a timeout. Real
    pings report "request timed out" under heavy load in the same way.
- **The destination's OS answers by itself.** No program there is
  involved, and no core is needed. Its network device just has to be on.
  - Real anchor: an operating system answers ICMP echo requests in its
    network stack, and "ping" is a standard system tool, not something
    each program writes.
- **What it tells the caller:**
  - whether the place is reachable and powered;
  - how far away it is, as a round trip;
  - how congested the way is.

  It doesn't tell the caller about cores, memory or who lives there.
  Sensing stays local (5.8), and travel stays mostly blind.
- **It costs** the crossing (about 150 cycles) and the thread's time for
  the round trip. Mapping many places means many round trips of waiting,
  so knowledge has a price (3.2).
- **It passes 5.1.** It returns information about a place and names no
  program.
- **The alternative is an echo port, with no new call.** The OS would
  echo back anything sent to a fixed port. Real anchor: the Echo Protocol
  on port 7 (RFC 862).
  - A program would then SEND, RECV with a timeout, and time it with
    SENSE: three calls and some arithmetic.
  - That's much harder to evolve. A measurement few programs ever find
    would mostly go unused.
  - Claude's opinion: the call. Damian agreed (2026-11-11).
- **It's the 17th call.** The call table grows from 16 to 17 (5.9). Where
  it sits in the numbering is the numbering setting's job (5.9).

**Settings:**

- bandwidth: the range, and the field's smoothness (2);
- queue size (1);
- message lifetime when no route remains (1; 0 means dropped at once);
- inbox size (1);
- the network device's numbers, in the catalogue;
- per-hop and per-length delay: already decided.

That's about 5 new settings. PING adds none: its timeout is an argument.

**Risks:**

- **Cost.** Queues per link end, stepped every tick, scale with traffic.
  A flood costs computing time as well as world time. A per-place cap on
  messages in flight may be needed as an engineering limit; that's for
  tests to show.
- **Base load.** If the network device's constant draw is set high, every
  place carries a power floor, and small sources may feed only the
  network. Keep it small, about a tenth of a core's draw, until tests say
  otherwise.

**Questions for Damian:**

1. ~~Bandwidth from its own field?~~ Decided 2026-11-11: links vary (read
   as their own field).
2. ~~Queues when full?~~ Decided 2026-11-11: the newcomer is dropped, and
   messages already waiting keep their place.
3. ~~A network device at each place?~~ Decided 2026-11-11.
4. ~~Inboxes when full?~~ Decided 2026-11-11: the newcomer is dropped, and
   what is already waiting stays.
5. ~~SEND's feedback?~~ Decided 2026-11-11, Damian: fire and forget, (A).
   Nothing comes back beyond the sender's own exit, and receivers reply if
   they choose.
6. ~~When no route remains ((7))?~~ Decided 2026-11-11: it waits for up
   to a lifetime (a setting; 0 means drop at once).
7. ~~PING ((8))?~~ Decided 2026-11-11: a call, the 17th.

**Memory and disk are allocated by request [decided 2026-11-10, Damian].**
A program must ask for memory or disk, and allocations have boundaries.

**Resource allocation like an operating system [Damian's preference;
settled by "An OS on each node" below].** Damian likes allocation through something like an OS layer, but
sees a problem: a program that isn't running can't ask to run, and if an
outside runner decides, the program has no control over the CPU and GPU it
gets.

- **How real systems solve this (Claude).** A program can't ask while it
  isn't running, so it asks while it is running, and the request
  persists. Real programs control their CPU use through settings the
  scheduler reads later: which cores they may use, how many threads they
  run, and when they yield or wait. Nova 1 had a version of this (BX 22,
  "choose P or E cores").
- **So the scheduler is a law:** how a core divides its time among the
  threads waiting for it. Programs control what they get through
  persistent requests (preferred core type, number of threads, yielding).
- **Every control must cost something (3.2).** A free "priority" setting
  would be set to maximum by everyone, so it either costs something or
  doesn't exist. **[decided 2026-11-10 with the fair scheduler]** Each core serves its waiting
  threads in turn, and each thread's state takes memory. More threads
  mean more turns but more memory held, which is a trade-off. Tierra's
  alternative, time in proportion to memory held, invites padding.

**An OS on each node [decided 2026-11-10, Damian].** Each node runs an OS
that mediates its programs' needs and works like a real OS scheduler.
Programs interact with it through interrupts: to ask for memory or disk,
and to use hardware. Claude's conditions and details:

- **Status (2026-11-10):** Damian agreed the fault handler, the fair
  scheduler (below) and calls costing time. He asked for more on
  neutrality, on priority, on clocks and on how much OSes vary. Claude's
  expansions follow each item.
- **Decided 2026-11-10 (Damian), after the expansions:** a neutral OS
  (with the two questions), no priority, per-turn clocks (with none on
  devices that run several programs at once), and limited OS variety
  (page size, perhaps turn length, in two or three kinds by region).
- **[decided] The OS is neutral infrastructure,** maintained from outside
  like the power grid and the network. It acts only on requests, and
  shares time by a fixed policy. It never acts for or against a
  particular program. Every OS service must pass the no-bootstrap test
  (5.1): "allocate N cells" passes, but "kill program P" or "copy program
  P" names another program and fails. An OS that did more (restarting
  programs, tidying up memory for them) would hand out behaviours, which
  are bootstraps.
  - **Expanded: two questions for any OS behaviour.**
    1. **Was it asked for, or is it a fixed rule applied the same way to
       everyone?** Time-sharing, faults stopping threads, and freeing the
       pages of a program with no threads left are fixed rules, so they
       are fine. Anything the OS decides about a particular program on
       its own initiative is not.
    2. **Does the request mention only the asker and the world** (cells,
       pages, cores, nodes), never another program?
  - **Fine:** "give me N cells", "free these cells", "start a thread at my
    address", "send these cells to node Y", "set my fault handler", "what
    is the voltage here?".
  - **Not fine:** "kill P", "read P's memory", "give me P's core", and the
    OS restarting, moving or rescuing programs unasked.
  - **A real example of what to avoid:** Linux's out-of-memory killer.
    When memory runs out, it scores every process and kills the "worst".
    That is the OS choosing who dies, a selection pressure we would have
    designed. The neutral version: when memory is full, new requests
    fail, and nobody is killed.
  - **Nova 1's versions of the same mistake:** the world turning frozen
    programs into spores (dormancy), and killing programs that hadn't run
    for 200 ticks (the watchdog).
- **[decided 2026-11-10] Scheduling like a real fair scheduler:**
  - each thread gets a fair share of its core, as in Linux;
  - a program can prefer a kind of core (P, E, GPU);
  - a thread can step aside or sleep, and wakes on an interrupt;
  - the OS moves threads to idle cores to balance load.

  The main lever is the number of threads: more threads get a bigger
  share, but each thread's saved state takes memory (real "fork bombs"
  show why the cost has to be real).
- **[decided 2026-11-10] No free priority.** Linux lets an ordinary program only
  lower its own priority, never raise it. In Nova 2 lowering it would
  need to buy something, or it's a dead option (3.2). So leave priority
  out unless a reason appears. **[decided]**
  - **Expanded: why the OS shouldn't assign priority either.** Priority
    means some threads get more than a fair share, so something has to
    decide who:
    - **if programs set it,** a free setting is set to the maximum by
      everyone, and means nothing;
    - **if the OS sets it by a rule about behaviour,** the rule picks
      winners. Boosting programs that hold more memory is Tierra's size
      rule and invites padding; boosting old programs rewards seniority;
      boosting programs that wait on input rewards whatever we chose.
      Each is a selection pressure we designed, which is what one
      physics (3.1) rules out;
    - **if nobody sets it,** every thread gets its fair share. This is
      also real: ordinary programs on Linux all run at the same
      priority by default.
  - A fair scheduler already gives the useful part of priority without
    picking winners: a thread that has slept a lot is behind on its
    share, so it is served promptly when it wakes.
- **[decided 2026-11-10] Calls cost time.** Every interrupt request, every switch
  between threads and every fault costs about a system call. So asking
  the OS is never free.
- **[decided] Clocks on a shared core:** each thread runs at its own
  requested clock during its turn. This is Damian's over- and
  underclocking (5.0a), made to work when threads with different requests
  share a core.
  - **Expanded.** Damian isn't sure, and can't picture programs
    overclocking hardware that others are using at the same moment.
    - **On a core they never are.** A core runs one thread at a time,
      and the requested clock applies only during that thread's turn.
    - **This is close to real practice.** Ordinary programs don't set
      clocks directly, but they do give per-thread performance hints
      that the OS applies whenever that thread runs: Windows' efficiency
      mode, Apple's quality-of-service classes (background threads go to
      E cores at low clocks), and Linux's per-task utilisation clamps.
    - **What is shared is the heat.** An overclocker leaves the core hot
      for whoever comes next, and can trip it, stopping every thread on
      it. Overclocking a shared core is a commons problem, which is a
      real conflict between programs and a trade-off, not a flaw.
    - **[decided 2026-11-10] Where it breaks is the GPU,** which runs many
      programs' work at the same moment on one device-wide clock, as
      real GPUs do. Claude's opinion: no clock control on devices that
      run several programs at once. Their speed follows their voltage
      only. The alternatives (the highest request wins, the lowest wins,
      or the average) each let one program impose on the rest.
    - Switching clocks between turns takes real time (tens of
      microseconds), so it can carry a small cost.
- **[decided 2026-11-10, limited as below] Variety in OSes.** The OS's settings vary by node, as
  hardware does: page size, length of a turn, and the cost of calls. One
  set of OS laws, different numbers by place, so a node's OS is part of
  its biome.
  - **Expanded: variety within reason (Damian, 2026-11-10: not much
    variance in the OS layer).** Under the abstraction rule, vary only
    what hardware doesn't already vary:
    - **Page size earns it.** It sets how exposed packed programs are
      and how much padding costs (shared pages, above). Real systems
      differ here: 4 KB on most PCs, 16 KB on Apple's chips, and 2 MB
      "huge pages". Large pages make a node where sharing, and with it
      parasitism, is easy; small pages make one where it's hard. That's
      ecology from one number.
    - **Turn length, maybe.** It changes how threads hand data to each
      other and how quickly a sleeper wakes.
    - **Call cost, no.** It mostly duplicates differences in hardware
      speed.
  - ~~[decided] Two or three OS kinds per world (small, standard and
    large pages), spread over the map by region like the hardware
    biomes.~~ Superseded below.
  - **[decided 2026-11-10, Damian] Each node's OS has its own values,**
    set when the node boots and fixed after that, so every node behaves
    slightly differently. They're drawn from the seed the way hardware is
    drawn: from smooth fields over the map with a little scatter per node,
    plus steep drop-offs (see "Gradients and drop-offs", 5.7). Steps
    rather than slopes for page size and the writable-and-executable rule
    are fine (Damian). The OS never changes its own rules in response to programs
    (5.8's neutrality, 3.1). Why the fields matter: neighbours stay
    similar, so a program can adapt to the OS around it and moving
    nearby is safe, while far moves are a gamble. With independent
    per-node draws, every move would be one.
    - What varies is still limited: page size, the writable-and-executable
      rule (5.4 answer 2) and perhaps turn length. Call cost and the rest
      stay global.
    - Page size comes from a short list (real sizes are powers of two)
      and the writable-and-executable rule is yes or no, so those two
      are steps, not slopes. Expect patches with the odd different node,
      not gradual change.

**The crux: isolation against no bootstraps [settled 2026-11-10 by the
exploits decision below].** If allocations are
protected as a real operating system protects them, no program can write
into another's memory. If programs also can't read or run each other's
memory (real isolation), that is Nova 1's private code, which is exactly
what forced ENGULF and INJECT (5.2). So Nova 2 must depart from real
isolation somewhere, on purpose. Options:

- **(a) Read and run open, writes protected** (Tierra). Parasites can
  emerge. Eating can only be scavenging: claiming what is freed or wiped.
- **(b) An allocation is a claim, not a lock.** Writing into another's
  claim is allowed, so eating emerges as overwriting.
- **(c) Protection is physical.** It holds only while the memory device is
  within its voltage range. Near the edges, errors cross boundaries, and
  heavy access can flip bits in neighbouring cells, as the real
  "rowhammer" fault does.
- **Claude's opinion: (a) with (c).** Both are real behaviours (Tierra's
  open reads, real memory faults). Eating then means pushing or waiting
  for a neighbour's memory to fail, then claiming what's freed. Risk:
  predation may not emerge that way. That's unproven, and (b) is the
  fallback if tests show none.

**Damian's proposal: break isolation through exploits (2026-11-10).**
Programs reach memory or disk they don't own by over- or under-running
pointers. Claude's analysis:

- **That isn't quite how real exploits cross between programs.** In a real
  machine, a pointer run past your own memory into another program's hits
  a page you don't have, and the hardware stops it (a segmentation
  fault). Real overruns are dangerous for two other reasons:
  - **Protection works per page, not per cell.** Pages are blocks of about
    4 KB. Anything inside a page you hold is reachable, whatever it
    belongs to logically.
  - **Victims overrun their own buffers with an attacker's data.** A
    program copies incoming data into its memory without checking the
    length, writes past its buffer, and so overwrites its own code with
    the attacker's. Then the attacker's code runs inside the victim. That
    is how real code injection works.
- **Both can be Nova 2 physics, with no bootstrap [decided 2026-11-10,
  Damian, all three as below]:**
  - **(i) Pages.** Protection is per page (the page size is a setting).
    Allocations are packed, so different programs can share a page, and
    within a shared page nothing is protected. An overrun reaches the
    neighbour in the same page. A program can ask for whole pages and be
    safe, but it pays for the unused rest of each page. That's a
    trade-off (3.2): packed and cheap but exposed, or padded and safe but
    wasteful.
  - **(ii) Injection through input.** A program that takes in data from
    elsewhere (a network message, a shared buffer) and copies it without
    checking can be overrun by it. That gives Nova 1's INJECT emerging
    from ordinary instructions. Risk: it needs victims that read input,
    and early programs may not read input until communication pays.
    Damian: that's fine; predation and parasitism took a while to
    develop in the real world as well. Claude's note: the risk isn't
    slowness but whether the stepping stones exist at all. Reading input
    has to pay for its own sake (communication) before injection through
    input can exist. That belongs to the design of signals and the
    network.
  - **(iii) Physical faults:** option (c) above (voltage-edge errors,
    rowhammer).
- **An access outside your pages faults [Claude's recommendation; Damian
  asked for one, 2026-11-10; agreed by Damian, 2026-11-10].**
  - **A fault is an interrupt.** If the program has set no handler, the
    faulting thread stops, as with a real segmentation fault. If it has
    set one, the thread jumps to it instead, as with a real signal
    handler, and pays the time a trap costs (about one system call;
    Nova 1 measured roughly 2,000 adds).
  - Why: if a failed access did nothing, probing other memory would be
    free and everyone would do it, which breaks 3.2. Stopping outright
    with no way to recover rules out probing entirely. A handler makes
    probing possible but costly, and early random code, which has no
    handlers, dies quickly when it scribbles. Programs can evolve
    handlers later, which is a stepping stone towards exploits.
  - Only the thread stops, not the whole program. A program with several
    threads survives one fault. That's another reason for threads, at a
    cost in memory.
  - Risk: it may be too harsh on the first, sloppy replicators. That's
    unproven, and only a test will show.
  - Passes 5.1: "on a fault, jump to the handler" is stated in world terms.
- **What "eating" becomes:** hijacking (overwriting a neighbour so its
  threads run your code), sabotage (corrupting it until it fails), and
  scavenging (claiming what it frees). Whether that's enough for real
  predation is unproven.
- **Claude's opinion:** (i), (ii) and (iii) together are better grounded
  than its own (a) with (c). They come from how real exploits work, and
  (i) adds a trade-off. Open reads across the whole node (a) would no
  longer be needed: parasites can read and run their hosts' code within a
  shared page.

### 5.9 Instruction set [decided 2026-11-10: 22 instructions and the OS call table, now 17 calls with PING (2026-11-11); service-cost numbers open]

**Current direction (2026-11-10, Damian): a much simpler set, like
TIS-100's,** with MUL, DIV and the OS calls added, "just enough to work
but not to overwhelm our latent space". This replaces the x86 subset
below; see the draft at the end of this section. The x86 notes stay where
they still apply (registers per thread, DOS-style calls, one address space
per node).

**First proposal [superseded by the TIS-100-like set]:** a subset of x86, so
programs are assembly programs: enough to be Turing-complete, none of the
fancier instructions, and a way to reach hardware interrupts (the network
and OS calls).

Claude's critique and corrections **[carried into the decided set: registers per thread, one call instruction, DOS-style calls]**:

- **Registers per thread, not per program.** Each thread has its own set,
  which the OS saves when it switches threads. That is part of what a
  thread costs (5.8).
- **OS calls through INT, not IN/OUT.** On real systems the OS talks to
  hardware with IN/OUT, and programs aren't allowed to; programs call the
  OS with INT (Linux's `int 0x80`) or SYSCALL. All hardware goes through the
  OS here, so one gate covers memory, disk, network, threads, clocks, fault
  handlers and sensing. IN/OUT fault.
- **One address space per node.** Real x86 OSes give each process its own;
  5.8 (i) needs a neighbour's bytes to be reachable in a shared page. Flat
  32-bit addresses over the node's memory, with per-page protection.
- **A subset:** 32-bit, eight general registers, flags, mov, arithmetic and
  logic, compare, jumps, call/ret, push/pop, and `rep movsb`, a two-byte
  copy that matters most for replicators. No floating point, SIMD, segments
  or privileged instructions; anything outside the subset faults as
  invalid, as on real hardware.
- **What x86 brings:** real measured instruction costs for each device
  type's table (5.0a), and a real stack, so injection through input
  (5.8 ii) works the way real buffer overflows do.
- **What it costs:** instructions vary in length, so one flipped bit can
  change how everything after it decodes (like a frameshift). And GPUs run
  their own instruction set, which reopens the parked GPU/NPU question.
- **The number that matters is the shortest replicator,** not Turing
  completeness, which any subset with memory and a conditional jump has.
  Claude's sketch: find its own address (`call`/`pop`), allocate, `rep
  movsb`, start a thread and loop. That's about 30 bytes, nearer 40 if a
  strict W^X rule needs a call to make the copy executable. Random luck
  doesn't find 30 bytes, so a spontaneous origin needs a gradual route
  (5.4, the shared reset region).
- Claude recalls, unchecked, that BFF's authors also got replicators in a
  Z80 emulator (a real 8-bit CPU) with programs on a grid touching their
  neighbours. Check before relying on it.

**Damian's questions and Claude's answers (2026-11-10):**

- **Registers don't lose state.** When a thread's turn ends, the OS
  copies its registers into the thread's save area in memory (about 40
  bytes in 32-bit x86), and copies them back at its next turn, so it
  carries on exactly where it stopped. "Per thread" means a program with
  two threads has two sets, because each is at a different point in the
  code. A program's memory is shared by its threads; registers aren't.
- **DOS-style calls [settled by 5.9's call table].** Damian had the DOS model in mind; what
  matters to him is sending data to an interrupt and getting data back.
  DOS programs did exactly that: a number in a register picks the service,
  other registers carry the arguments, `INT 21h` makes the call, and the
  results come back in registers. Nova 2 keeps that calling style. What it
  drops from DOS is programs going around the OS straight to the hardware
  (IN/OUT, writing to device memory), because 5.8 puts all hardware behind
  the OS. Slow answers (network, disk) come back later: the thread sleeps
  until the reply arrives, or a handler runs (5.8's sleep and wake on
  interrupt).
- **Interrupts coming in aren't instructions.** A fault, arriving data or
  a timer makes the thread jump to the handler address it registered by a
  call. It returns with another call, as Linux signal handlers do, because
  IRET is privileged.
- **The address space is for exploitability, yes**, and for space. With a
  private address space per program, as on modern OSes, a program can
  only name its own memory, so an overrun can only hit itself and shared
  pages can't exist. One address space per node means every address is a
  real place in the node's memory, so running past your data lands in a
  neighbour's. DOS, classic Mac OS, embedded systems without memory
  protection and Tierra's soup all worked this way. It's a deliberate
  departure from modern OSes, chosen for the trade-offs it creates.
- **What "32-bit" means.** It's the width of registers and addresses (4
  GB reachable), not the size of an instruction. x86 instructions vary in
  length: an opcode byte (sometimes two), then optional bytes saying which
  registers or memory, then optional numbers, from 1 to about 11 bytes in
  this subset. There is no 32-bit instruction space to fill.
- **The subset isn't small.** Most of x86's one-byte opcodes are simple
  integer instructions, so the list below covers roughly three-quarters
  of them (Claude's count, about 190 of 256). What's dropped is segments, port I/O, floating point, decimal
  arithmetic and privileged instructions, plus most two-byte (0F)
  opcodes. That matters: if most byte values were invalid, random code
  would die on its first instruction. Valid code mostly dies by touching
  memory it doesn't own instead.

**Proposed subset (Claude), with byte forms, 32-bit forms and the usual
memory operands:**

| Group | Instructions |
|---|---|
| Move | MOV, XCHG, LEA, PUSH, POP, MOVZX, MOVSX, CBW/CWDE, CDQ |
| Arithmetic | ADD, ADC, SUB, SBB, INC, DEC, NEG, CMP, MUL, IMUL, DIV, IDIV |
| Logic and shifts | AND, OR, XOR, NOT, TEST, SHL, SHR, SAR, ROL, ROR, RCL, RCR |
| Control | JMP, Jcc (16 conditions, short and near), SETcc, CALL, RET, LOOP, LOOPE, LOOPNE, JECXZ |
| Strings | MOVS, STOS, LODS, CMPS, SCAS, with REP, REPE and REPNE |
| Flags | CLC, STC, CMC, CLD, STD, PUSHF, POPF, LAHF, SAHF |
| OS | INT (the only way out), NOP |
| Fault as invalid | segment instructions and prefixes, 16-bit size prefixes (66, 67), far jumps and calls, IN/OUT/INS/OUTS, floating point (D8–DF), decimal (DAA, DAS, AAA, AAS, AAM, AAD), HLT, CLI/STI, IRET, INT3, INTO, WAIT, BOUND, ARPL, PUSHA/POPA, ENTER/LEAVE, XLAT, LOCK, other 0F opcodes |

About 60 instructions, counting each conditional family as one, all from
the 8086–386 era. Sleeping is an OS call, not HLT.

**The OS call table [decided 2026-11-10; see the 5.9 header].** This is where Nova
1's verbs could come back, so each call must pass 5.1's test: its effect is
stated in world terms and never names another program. Two design
principles first, both from discoverability (5.5):

- **Every SYS is a real call.** The call number is the operand modulo the
  table size, so a random SYS always lands on something. There is no
  "invalid call" fault. Cost: a mutation to the number silently switches
  one call for another (as Nova 1's codon tables did).
- **Calls fail soft; they don't fault.** A bad size, an address you don't
  own, a full disk: the call returns a failure in A (0 or −1) and the
  thread runs on. Only ordinary instructions fault (touching memory you
  don't own, divide by zero, stack overflow). So random code survives to
  stumble into a working call, and every SYS still costs its ~150 crossing.

Each call reads arguments from A–D and returns its result in A. None names
another program; where programs touch each other it is through physics
(overwriting a shared page, data received into a buffer), never a call.

| # | Call | Arguments → result | Effect, and the cost class |
|---|---|---|---|
| Memory | | | |
| 0 | ALLOC | size, permission, device → address or 0 | Reserve memory or disk. A thread with no owner that calls ALLOC becomes a program and owns what it gets (5.4). Permission is data / executable / both (5.4) or disk, which returns a disk offset (5.3, decided 2026-11-11). Device is 0 for any, 1 for the fastest, −1 for the slowest, among the memory devices or, for disk, among the disks (5.7, "Within a place"). Placed by size class (5.7). Modest. |
| 1 | FREE | address, permission → ok or fail | Give back your own memory or, with permission disk, your own disk range; fails on what you don't own. A program's last thread exiting frees the rest, disk included, with the bytes left in place. |
| 2 | PROTECT | address, permission → ok or fail | Change a page you own: data ↔ executable. On a strict (W^X) node you can't hold both at once, so a copier writes as data then flips to executable before SPAWN; on a lax node it allocates "both" and skips this (5.4 answer 2). |
| Threads | | | |
| 3 | SPAWN | address, a starting value → thread id or fail | Start a thread, on an executable page you own; fails otherwise. Started outside the caller's own code allocation, the thread becomes a new program that takes that allocation [decided 2026-11-11, Damian; 5.12 (2)]. You can't start a thread in a neighbour's pages: injection comes from overwriting their code and their own idle core running it. A stack and saved registers, thousands. |
| 4 | EXIT | — | End the calling thread. Its stack and registers go; its owned memory stays until freed or the last thread exits. |
| Time and power | | | |
| 5 | SLEEP | ticks (0 = until an interrupt) → — | Free the core until the time or an interrupt. |
| 6 | SETCLOCK | level → ok | Scale the core's stock speed (what its voltage supports) up or down in steps, during this thread's turns (5.8; 5.11 (4)). Above stock: faster, more draw, more errors. |
| Interrupts | | | |
| 7 | SETHANDLER | kind, address → ok | Register a handler for faults, arriving data, or a timer (5.8). And low voltage, firing once near a core's cut-off [decided 2026-11-11, Damian; 5.10 (6b)]. |
| 8 | HRET | — | Return from a handler, restoring the interrupted registers. |
| Sensing | | | |
| 9 | SENSE | which → value | Read own temperature, voltage, the time, this node's id, remaining budget, or bytes waiting. Read-only, cheap. How a program perceives the world (5.5). |
| Communication | | | |
| 10 | SEND | node, port, address, length → ok or fail | Copy bytes onto the wire to that node's port and return at once: fire and forget (decided 2026-11-11, Damian). Fails only for local reasons: no route, own exit full, own network device off. Downstream, a message that meets a full queue or inbox is dropped silently; one on a failed link reroutes (5.8, "Transit" (7)). You name a place, not a program; whoever listens there gets them. Port 0 is the reset region (migration); other ports are comms inboxes. Routing is the network's; network cost, high. |
| 11 | RECV | port, address, max length, timeout (0 = poll, −1 = wait) → count | Bytes from a comms port into your buffer. The buffer is ordinary memory, so data longer than the program expects overruns into its own code or a neighbour's: injection through input (5.8). You can't RECV port 0. |
| Storage | | | |
| 12 | DREAD | disk offset, address, length → count | Read the node's disk, which keeps its contents without power (5.0a). Only blocks you hold an allocation in, plus the boot block; it stops at the first other block and returns the count done (5.3). |
| 13 | DWRITE | disk offset, address, length → count | Write it; slow, and it wears the disk (5.0a). The same blocks as DREAD; within a shared block an overrun reaches a neighbour's data (5.3). |
| Devices | | | |
| 14 | GLAUNCH | address, lanes, pages → ok or fail | Run lanes in lockstep on the GPU (5.9); the launching thread wakes when they finish. SYS faults inside a lane. |
| 15 | NMATMUL | operand addresses and shapes → ok or fail | The NPU multiplies the matrices into a result, ready later. Reached only through this call (decided). |
| 16 | PING | node, timeout → round-trip ticks or −1 | **[decided 2026-11-11, Damian; 5.8, "Transit" (8)]** The destination's OS answers if its network device is on. The thread sleeps for the round trip or the timeout. Latency call.

**None of these is a bootstrap.** There is no "eat program X" or "put
myself into X". Predation and parasitism have to be built from SEND, RECV
into a too-small buffer, PROTECT, SPAWN and ordinary stores into a shared
page. That is the whole point of 5.1.

**The four open items, worked through 2026-11-10:**

- **SEND addressing [decided, Damian]: name any node; the network routes;
  latency scales with hops and distance.** You can address any node by id;
  the network carries the bytes hop by hop along its data links, and the
  time is the hop count plus the route's length (plus the data size).
  Length was added 2026-11-10 (5.7, "Between places" (3)(d)). A node
  across a barrier is reachable but far (few links, many hops, long
  bridges), so the map still matters (5.7). Details [settled by the decisions marked in each]:
  - **Node ids are numbers.** A program knows its own (SENSE), and SENSE
    can list its data-linked neighbours (5.7 (c)). It learns distant ids from
    messages: RECV returns the sender's id with the data. So addresses
    spread the way they're used, and a program can also probe by SENDing
    to numbers and seeing what answers. **[decided 2026-11-11]** The ids
    are shuffled by the seed, so they carry no geography, and SENSE reads
    neighbours' conditions as well as their ids (5.8, "Sensing is
    local").
  - **Unknown or unreachable id → fail soft** (the network tried, nothing
    there), not a fault.
  - **Travel: SEND into the destination's reset region [decided in spirit
    2026-11-10, Damian; Claude agrees, over its own MIGRATE idea].** SEND
    moves *data*, not a running program. Damian's mechanism: a program
    SENDs its own bytes to a far node's reset region (5.4) — the open,
    unowned landing area that idle cores execute. There the ordinary
    reset-region rule takes over: an idle core runs it, and if it ALLOCs,
    copies itself out and SPAWNs, it is alive on the new node. No MIGRATE
    primitive, no remote thread-start, no naming another program — SEND
    just writes bytes to an open place, and the destination's own cores do
    the rest. It passes 5.1 more cleanly than a MIGRATE call would (which
    had the OS start a thread for you remotely), and it **unifies migration
    with abiogenesis:** a migrant is simply non-random code arriving in the
    soup. The earlier MIGRATE proposal is dropped.
    - **Gated by real capacity.** A migrant wakes only where there is a
      free core and spare memory. A crowded or full node runs nothing new,
      so life disperses toward empty and underused nodes, not packed ones.
      Travel and colonisation are the same act.
    - **Only complete replicators travel.** The arriving code must restart
      itself from the reset region (find its address, copy out, SPAWN), so
      a fragment or a specialist can't migrate — only self-bootstrapping
      code. A real trade-off: to travel you must carry your whole bootstrap.
    - **Unreliable, and small is better.** In the reset region a migrant
      competes with static and other arrivals and may be overwritten before
      a core runs it; a bigger migrant fits worse and survives the churn
      less. Travel is a gamble, and compact genomes travel best.
    - **Separate from the comms inbox [decided 2026-11-10, Damian].** A
      node has two incoming areas: the **reset region** (migration — idle
      cores run arriving bytes) and the **comms inbox** (communication — a
      program RECVs arriving bytes into its own memory). SEND deposits a
      payload; RECV reads data; they are different acts (Damian), so the
      two areas stay distinct.
    - **Why it earns its place (Claude):** the split gives two different
      ways to spread, not one.
      - **Colonisation** — SEND a whole replicator to the reset region; it
        wakes only where there is free capacity, so it spreads into *empty*
        nodes.
      - **Parasitism / injection** — SEND malformed data to a comms port
        where a running program RECVs it and overruns its buffer, so your
        bytes run in *its* context and steal *its* resources; this spreads
        into *occupied* nodes.
      Two incoming areas, two ecological strategies. Injection (5.8) now
      lives only on the comms side; the reset region only ever takes
      self-contained replicators.
    - **Addressing [decided 2026-11-10, Damian]: ports.** A node has
      numbered ports, as real machines do. SEND names node + port; a
      program RECVs on a port it listens on; **port 0 is the reset region,**
      which idle cores drain and nothing RECVs. The **number of ports is a
      setting, default 16.** Ports give lineages private communication
      channels (agree on a port number), and let a message be aimed at a
      role. SEND and RECV each gain the port argument (four registers each).
      Rendezvous (sender and receiver agreeing on a port) is something
      lineages evolve, as services use well-known ports.
    - **Arrivals get a prompt run [decided in spirit 2026-11-10, Damian;
      mechanism proposed by Claude].** Waiting for an idle core to happen
      upon a migrant is arbitrary: static and the reset region's own churn
      could corrupt it first, and a bigger migrant sits exposed longer, so
      luck, not fitness, decides. Instead, a port-0 arrival **schedules the
      next free core to run it, starting at the payload's first byte.** So:
      - **Size-neutral wake.** Big and small migrants both get run promptly;
        size then matters only at ALLOC (a larger program needs more free
        memory to take root), not as exposure time. This is Damian's "won't
        negatively affect larger programs."
      - **Space is the real gate.** The migrant must ALLOC its own memory to
        take root; on a full node ALLOC fails and it dies. The OS only
        "assigns real memory" if there's room (Damian) — but the migrant
        does the ALLOC itself, the OS doesn't place it (no bootstrap).
      - **Spare core only, no preempting [decided 2026-11-10, Damian].** The
        prompt run jumps ahead of the ambient static soup but does **not**
        preempt a running owned thread. That keeps the scheduler's "no
        priority" (5.8) intact (that rule governs living threads; the reset
        region is a separate pre-life arena), and it bounds the obvious
        attack: flooding a node's port 0 can only soak up its *spare*
        cores, never steal cores from live programs.
      - **This settles the fixed-vs-scattered question:** a migrant runs
        from its own landing address, so it never depends on a core's fixed
        start spot. The static soup (for first origins, 5.4) still runs the
        ambient way, on idle cores with no arrival pending.
      - **Consequence [Claude]:** migration is now reliable wherever there's
        capacity, so spread is gated by free memory and SEND's hop cost, not
        by luck. Colonisation is efficient toward open hardware — which is
        what travel should be.
- **Numbering order [decided 2026-11-10, Damian: one whole-simulation
  setting].** The number → call map is a single setting loaded for the
  entire run (fixed for the run, like every other). Defaults set by test,
  not argument. It matters two ways. (1) **Mutational neighbourhood:** a one-bit flip in a call number
  turns call k into k with one bit changed (k±1, ±2, ±4, ±8). If a flip
  lands on a related, harmless call the program degrades gently; if it
  lands on DWRITE (wears disk) or SEND (dear), a flip is costly. Clustering
  related calls makes mutation gentle; spreading them makes it harsh. (2)
  **Discoverability weight:** every number is equally likely under a random
  SYS, but we can give a key call more than one slot (ALLOC at 0 and 8,
  say), so random code finds it sooner — this directly speeds abiogenesis
  (5.4). Nova 1 did both (shuffled selectors, several codons per op).
  Claude's view: this is a tuning knob, not a principle. Make the number →
  call map a setting, default to extra slots for ALLOC and SPAWN, and set
  the rest by testing origin and mutation rates, not by argument.
- **Service costs [agreed for now 2026-11-10, Damian; tunable].** Real
  operations, in cycles, with one distinction that matters: some calls burn **core
  budget** (the OS does the work on your core), others are mostly **device
  latency** (the thread sleeps and the core runs others).
  - Core-budget calls: SENSE ~crossing only (~150); FREE, EXIT, SLEEP,
    SETHANDLER, HRET a few hundred; ALLOC ~1,000–3,000 (page setup);
    PROTECT ~1,000s (TLB work); **SPAWN ~10,000–50,000** (a new thread:
    stack, saved state, scheduler) — the dear one, so reproduction's cost
    is dominated by making the thread, not copying the bytes; SETCLOCK
    ~10,000–100,000 **and a settling delay** before the new speed applies
    (real DVFS is slow).
  - Latency calls (thread sleeps): RECV while waiting (SEND became a core-budget call, the crossing plus a per-byte copy, 2026-11-11), and PING for its round trip, network time (hop-based,
    ~thousands to millions); DREAD/DWRITE device latency (tens of thousands
    up), DWRITE also wearing the disk; GLAUNCH and NMATMUL a launch
    overhead (~1,000s) plus the device's own run.
  - So the cheapest thing a program can do is sense and compute; touching
    the OS for memory is modest; making a thread, changing clocks, and all
    I/O are dear. That shapes what strategies pay.
- **Dead program's pages [decided, Damian]: left as a corpse; memory is
  never auto-zeroed.** Real memory and file systems don't zero on free; you
  read whatever is there. So:
  - **Death returns a program's pages to the node's free pool with their
    bytes intact** (unowned, reclaimable), it does not wipe them.
  - **ALLOC hands out dirty memory** — a corpse's code, old data, or static.
    A program that wants clean memory zeroes it itself, at a cost.
  - This **resolves the 5.4 question** (does the OS zero a handed-out
    page?): no. Corpses and static are the two sources of raw material, so
    the dead feed new life and abiogenesis (5.4). Realism note: real OSes
    zero-on-alloc for security; we skip it on purpose, because the leak is
    the point.
  - **Smaller open point:** you can't aim ALLOC at a particular corpse (as
    real malloc doesn't let you pick pages), so deliberately eating a chosen
    corpse still needs a shared page or injection, not a call. Belongs to
    Interaction / Ending.

**GPU [settled by "GPU [decided 2026-11-10, Damian]" below].** Damian likes Nova 1's shape: GPUs slower than
CPUs but parallel. Proposal:
- **The same subset on GPU lanes, minus SYS.** Real GPU code can't call
  the OS, so on a GPU, SYS faults (no OS calls at all, GLAUNCH and NMATMUL
  included). The same bytes run on either device, so code can move between
  them and evolution has one language to work in.
- **Lanes run in lockstep groups,** as real GPUs do (warps of 32). Each
  lane has its own registers, but a group shares one instruction pointer.
  When lanes branch different ways, both paths run one after the other,
  with lanes masked off. Fast for code where every lane does the same
  thing (copying or scanning many places at once), slow for branchy code.
  That's the trade-off.
- **A lane knows which lane it is** because the launch call puts its
  number in a register. No new instruction is needed.
- **A CPU thread launches GPU work by a call** (GLAUNCH: start N lanes at
  an address, with a set of pages), with a launch cost, as real launches
  have. A GPU never starts itself.
- **Execution model [decided 2026-11-10, Damian].** Each lane gets its own
  copy of the data registers and starts seeded with its own lane number,
  so identical code touches different memory per lane. Lanes read and write
  only the pages handed to GLAUNCH; the launching CPU thread sets up input
  there before, and reads results after.
- **EXIT ends one lane, not the group [decided 2026-11-10, Damian].** The
  launch returns, and the CPU thread wakes, when every lane has exited.
- **A fault ends the whole launch,** as a fault kills a real GPU job
  (touching memory outside the given pages, divide by zero).
- **GLAUNCH stays its own call, not SPAWN with a flag [decided 2026-11-10,
  Damian].** It carries the lane count and pages and the synchronous
  wake-when-done, which SPAWN doesn't.
- **Correction:** real GPU lanes are much slower than a CPU core on serial
  code, not slightly: lower clocks, no out-of-order execution, small
  caches. Their numbers belong in the GPU's cost table (5.0a).
- This is Nova 1's warp model without the accelerator ops, which removes
  Nova 1's biggest overlap (nova1-survey 3.2). The NPU stays parked: its
  matrix work has no x86 equivalent.

**Third round (2026-11-10).** Damian agreed per-thread registers, the DOS
model and the shared address space, and asked for a TIS-100-like set
instead of the x86 subset.

- **One call instruction, not IN and OUT [settled by the decided set: SYS].** Send and
  receive are two call numbers through one instruction (SYS), like DOS's
  services through `INT 21h`. One opcode keeps the set small. The OS
  always answers at once: done, failed, or not yet. Waiting is the
  program's choice, and that's a trade-off (3.2):
  - **wait:** the thread sleeps without using a core until data arrives
    or its timeout runs out (it hangs forever only if it set no timeout);
  - **poll:** ask again later, paying for each call;
  - **handler:** carry on, and jump to a handler when data arrives,
    paying a trap.
- **Registers versus instructions.** "32-bit" means each register holds a
  32-bit number. Registers are a few slots inside the core, one set per
  thread; instructions are bytes in memory that the core reads. They're
  separate things.
- **Why TIS-100 alone isn't enough.** TIS-100 has no memory addressing, and
  its code isn't data: nodes talk only through ports. A replicator has to
  read its own code as data and write it somewhere else, so the set needs
  loads and stores by address, which TIS-100 lacks.
- **Why simpler is better here (Claude, agreeing on the merits).** In a
  small set where every byte means something, random code runs longer and
  the shortest replicator gets shorter (about 14 bytes in the draft, down
  from about 30 in the x86 subset). Tierra used 32 instructions, Avida 26
  and BFF 10. One byte per instruction also removes x86's frameshift: a
  flipped bit changes one instruction, not how the rest decodes. What it
  costs: x86's measured per-instruction costs. Costs are borrowed by class
  instead (add about one cycle, multiply a few, divide tens, memory by
  hierarchy level, a call a trap). A stack in memory with return addresses
  keeps buffer overflows real.

**First draft set (30 instructions), superseded by the 16-instruction
draft below:**

- **Registers per thread:** A (the accumulator, where results land, as
  TIS-100's ACC), B, C and D (general, also used as addresses), SP (stack
  pointer), IP (readable; writing it jumps there) and flags (zero,
  negative, carry). All 32-bit.
- **Encoding:** one byte per instruction. 5 bits pick the operation (32)
  and 3 bits pick the operand (8). Every byte value means something.
  Combinations that make no sense (writing to a constant) do nothing.
- **Operands:** B, C, D, SP, IP, [B] (the byte at the address in B), [D]
  (the byte at the address in D), and # (the next byte as a small signed
  number, the only two-byte form). The stack holds 32-bit values.

| Group | Instructions |
|---|---|
| Data (5) | MOV s (A ← s), STO s (s ← A), SWP s (swap A and s), PUSH s, POP s |
| Arithmetic (9) | ADD, SUB, MUL, DIV, MOD, INC s, DEC s, NEG (A), CMP s (sets flags from A − s) |
| Logic (6) | AND, OR, XOR, NOT (A), SHL, SHR |
| Control (7) | JMP s (relative: IP + s), JZ, JNZ, JG, JL, CALL s (relative), RET |
| Block (1) | CPY: copy C bytes from address B to address D, costing time per byte (real precedent: x86's `rep movsb`, ARM's newer CPY instructions) |
| OS (1) | SYS s: call number s, arguments in A–D, result in A |
| Other | NOP; the two spare codes are also NOP |

- Jumps are relative and IP is readable, so code works wherever it's
  copied.
- **Shortest replicator in this set (Claude's sketch, 14 bytes):** `MOV
  IP; STO B; MOV #14; STO C; SYS #1` (allocate 14 bytes; A = address)
  `; STO D; CPY; MOV D; SYS #2` (start a thread at A) `; JMP #-12`. Two
  more bytes where a strict node needs a call to make the copy
  executable. Still far beyond luck, so the gradual route (5.4) still
  matters.

**GPU, answering Damian [settled by "GPU [decided 2026-11-10, Damian]" below]:**

- **It interacts through memory.** The CPU thread that launches a GPU job
  gives it pages. The lanes read and write those pages, and when the job
  ends the launching thread is woken and makes any calls (sending results,
  say). Real GPUs work this way: the host does the input and output, and
  kernels compute on buffers. A program needs a CPU thread to drive a GPU,
  so a program that lives only on a GPU can't exist.
- **Lockstep is real.** NVIDIA runs warps of 32 lanes and AMD wavefronts of
  32 or 64. Each lane has its own registers, but a group issues one
  instruction at a time, and a branch that splits the group runs both
  sides one after the other. Another real trade-off: lanes reading
  neighbouring addresses at once is fast, scattered reads are slow.
- **Nova 1 already did this:** its warps ran programs in lockstep lanes.
  What Nova 2 drops is Nova 1's second route, the 20 accelerator ops.
- On a GPU, SYS faults and ends the launch.

**NPU [decided 2026-11-10, Damian]: reached through OS calls**, like disk
and network: "multiply the matrices at these addresses and put the result
here", with the answer coming later. It passes 5.1's test (its effect is on
numbers in memory, and it names no program). This un-parks the NPU half of
the parked library-ops question. Claude's flag **[answered by Damian:
keep it available, and let selection decide]:** the NPU earns its
place only if the world has matrix-shaped work. Its trade-off against the
GPU would be far less power and heat per multiply-add, but that's the only
thing it can do. Until some task in Nova 2 needs many multiply-adds, it's a
device nothing uses (5.5). Damian: selection will decide how programs use
it, as long as it's available.

**GPU [decided 2026-11-10, Damian]:** as above. Lanes act through memory,
in lockstep groups, and SYS faults on a GPU.

**Fourth round (2026-11-10), answering Damian:**

- **Waiting styles are the program's own choice [settled by RECV's timeout and SETHANDLER, 5.9].** Two calls
  give all three: receive with a timeout (0 = poll and return at once, no
  limit = wait until data comes) and set a handler for arrivals. How the
  program is written decides which it uses, so selection chooses.
- **Register size.** The four general registers are 128 bits together.
  With IP and the stack pointer (which programs can't name directly,
  below), a thread's saved state is 24 bytes.
- **What TIS-100 has (13):** NOP, MOV, SWP, SAV, ADD, SUB, NEG, JMP, JEZ,
  JNZ, JGZ, JLZ and JRO (jump by an offset held in a register). It has no
  call or return because it has no stack; programs there are loops.
- **Why the first draft had 5 data and 9 arithmetic instructions:**
  conveniences (INC and DEC, MOD, CMP with flags, NOT, the logic and shift
  operations). They can all be done in a few steps from simpler ones, so
  this draft drops them.

**Second draft set (16 instructions), superseded by the 22-instruction
draft below:**

- **One byte each:** the high 4 bits pick one of 16 instructions, the low
  4 bits one of 16 operands. Every byte value is a valid instruction.
- **One operand, with A implied, as TIS-100's ACC.** TIS-100's MOV takes
  two operands; that would need two bytes per instruction, so here MOV
  loads A and STO stores it.
- **No flags.** As in TIS-100, conditional jumps test A directly. To
  compare two numbers, subtract and test.
- **Registers per thread:** A, B, C, D and IP, all 32-bit. The OS gives
  each thread a stack when it starts it, which is part of what a thread
  costs.

| Code | Instruction | Does |
|---|---|---|
| 0 | MOV s | A ← s |
| 1 | STO s | s ← A. Storing to IP jumps there; storing to a constant does nothing |
| 2 | SWP s | swaps A and s |
| 3 | PUSH s | pushes s onto the thread's stack |
| 4 | POP s | pops into s. POP IP is a return |
| 5 | ADD s | A ← A + s |
| 6 | SUB s | A ← A − s |
| 7 | MUL s | A ← A × s |
| 8 | DIV s | A ← A ÷ s, whole numbers; dividing by 0 faults |
| 9 | JMP s | jumps by s from here. With a register it's a computed jump, TIS-100's JRO |
| 10 | JEZ s | jumps by s if A = 0 |
| 11 | JNZ s | jumps by s if A ≠ 0 |
| 12 | JGZ s | jumps by s if A > 0 |
| 13 | JLZ s | jumps by s if A < 0 |
| 14 | CPY s | copies s bytes from the address in B to the address in D, costing time per byte |
| 15 | SYS s | OS call number s; arguments in A–D, result in A |

**Operands (16):** A, B, C, D, IP; [B], [C] and [D] (the byte at the
address in that register); the constants 0, 1, 2, 4, 8, 16 and −1; and #,
the next byte as a number from −128 to 127. # is the only two-byte form.

- **Against TIS-100:** dropped NOP (MOV A already does nothing), SAV
  (STO does it), NEG (MOV 0, then SUB) and JRO (JMP with a register).
  Added STO, PUSH, POP, MUL, DIV, CPY and SYS. 13 − 4 + 7 = 16.
- **Call and return are idioms, not instructions:** push the return point
  and jump; POP IP returns. Return addresses sit on the stack, so 5.8's
  injection through input can work the way a real stack overflow does.
- **Faults:** touching memory the thread doesn't own, dividing by zero,
  the stack running out of its pages, and SYS on a GPU.
- **No-ops exist without a NOP:** MOV A and STO to any constant do
  nothing, so some bytes are neutral, as some DNA changes are.
- **Dropped on purpose:** AND, OR, XOR and shifts. Shifts are MUL and DIV
  by powers of two; bit masks take several steps. OS call results should
  be plain numbers, not bit masks.
- **Memory operands are one byte wide.** Storing a 32-bit number takes
  several steps. Code is bytes, so reading code as data stays simple.
- **Shortest replicator, 12 bytes (Claude's sketch):** `MOV IP; STO B;
  MOV # 12; SYS 1` (allocate 12 bytes, A = address) `; STO D; CPY # 12;
  MOV D; SYS 2` (start a thread at A) `; JMP # −10`. The call numbers
  are placeholders until the OS call table is designed.

**Fifth round (2026-11-10).** Damian doesn't want to copy TIS-100; he only
wanted to know why the first draft had so many more instructions than the
game. He wants AND, OR, XOR, NOT and bit shifts included, even though they
don't fit in 4 bits.

- **What s is (Claude).** s is the operand: one of a fixed list of
  choices (a register, a byte in memory at the address held in a
  register, or a number). Every instruction works on A and one operand,
  and the result lands in A. Using two other registers goes through A:
  D = B + C is `MOV B; ADD C; STO D`. A few instructions use registers
  implicitly: CPY uses B and D, SYS takes its arguments in A–D. Real
  precedent: the 6502 (Apple II, Commodore 64, NES) is an accumulator
  machine of this kind. The alternative is two operands per instruction
  (`ADD B, C`), as x86 and ARM do, at two bytes per instruction. Claude's
  view: stay with the accumulator, so every instruction is one byte.

**Instruction set, 22 instructions [decided 2026-11-10, Damian; drafted
by Claude].** Damian dislikes that it doesn't fit 4 bits, but accepts it.
It still fits one byte exactly, and the byte is the unit memory, copying
and bit flips work in. What's still open: the OS call table behind SYS,
the cost of each instruction, and whether spare slots can be set to fault.

- **Encoding:** one byte each. The high 5 bits pick the instruction (32
  slots), the low 3 bits the operand (8). Still every byte is valid.
- **Operands (8):** A, B, C, D, IP, [B], [D] (the byte at the address in B
  or D), and # (the next byte, −128 to 127, the only two-byte form). With
  3 bits there's no room for the small constants, so a number costs a
  second byte: adding 1 is `ADD # 1`.
- **10 spare slots** do nothing for now (80 neutral byte values), leaving
  room to add instructions later without changing the encoding. **[decided
  2026-11-10, Damian] They NOP; no crash setting.** Damian: crashes seem
  problematic. So an unused code wastes a thread's turn-step and nothing
  more, as BFF's no-ops do, which lets random code run longer.

| Group | Instructions |
|---|---|
| Data (5) | MOV s (A ← s), STO s (s ← A; storing to IP jumps there), SWP s, PUSH s, POP s (POP IP is a return) |
| Arithmetic (4) | ADD, SUB, MUL, DIV (A ← A op s; dividing by 0 faults) |
| Logic (4) | AND, OR, XOR (A ← A op s), NOT s (A ← s with every bit flipped) |
| Shifts (2) | SHL s, SHR s (shift A left or right by s bits, filling with zeros) |
| Jumps (5) | JMP s (by s from here; with a register, a computed jump), JEZ, JNZ, JGZ, JLZ (if A is 0, not 0, above 0, below 0) |
| Block (1) | CPY s (copy s bytes from the address in B to the address in D) |
| OS (1) | SYS s (call number s; arguments in A–D, result in A) |

- **Not added, and could go in the spare slots later if a need shows:**
  NEG (now `MOV # 0; SUB s`), MOD, rotates, a sign-keeping right shift,
  and one-byte forms with small constants.
- **Shortest replicator, 14 bytes (Claude's sketch):** `MOV IP; STO B;
  MOV # 14; SYS # 1; STO D; CPY # 14; MOV D; SYS # 2; JMP # −12`. Two
  bytes longer than the 16-instruction draft, because the constants now
  cost a byte each.

**Instruction costs [decided 2026-11-10, Damian; drafted by Claude].**
The exact service costs behind SYS wait on the OS call table; the structure
below stands.

- **Unit: cycles,** device-independent. Wall-clock time = cycles ÷ clock
  speed, and clock speed follows voltage (5.0a). One table serves every
  device; a faster core just turns cycles into time more quickly. A turn
  gives a core a budget of cycles (the per-turn clock, 5.8), and a thread
  runs until its budget is spent.
- **Three parts to an instruction's cost:** its own work, a memory-access
  cost if an operand reaches memory, and, for SYS, the OS crossing plus
  the service's own cost.

| Cost (cycles) | Instruction work (register/immediate operands) |
|---:|---|
| 1 | MOV, STO, SWP, ADD, SUB, AND, OR, XOR, NOT, SHL, SHR, all five jumps |
| 3 | MUL |
| 20 | DIV |
| 1 + stack access | PUSH, POP |
| ~2 per byte (streaming) + small setup | CPY |
| crossing + service | SYS |

- **Memory-access add-on [revised 2026-11-11, Damian]** for `[B]`/`[D]`
  and the stack: the latency of the memory device the operand lies on,
  drawn per device along its trade-off curve (5.7), from about **+4**
  cycles for the fastest to about **+12** for the slowest. A register or
  `#` adds nothing. Direct operands reach only the local node; another node
  goes through SYS.
  - It was "the thread's own device +4, another device +12", which
    measures a distance between devices. 5.7 has since dropped that: cost
    is by device, not by distance. The 4-to-12 gap survives, now as a
    choice (ALLOC's device argument) rather than luck.
- **Read and write cost the same [settled by the guardrail (3.3): symmetric until a run shows otherwise].** Real hardware has an
  asymmetry (writes are buffered and don't stall; reads expose latency),
  but it's second-order and it adds a special case. Keep symmetric for now;
  add it only if write-heavy versus read-heavy strategies turn out to
  matter.
- **SYS is a cheap crossing plus the service's own work [revised
  2026-11-10].** Earlier this doc said a trap was "about 2,000 adds"; that
  figure is really a page fault with work or a context switch, not a bare
  syscall. On real hardware the crossing is ~150–500 cycles (a couple of
  memory misses), while calls like allocate or start-thread cost thousands
  because of the work they do. So **crossing ≈ 150**, and each call's own
  cost lives in the OS call table. Reference points (real cycles): ALU ~1,
  branch miss ~15, L1/L2/L3 ~4/12/40, main memory ~200–300, syscall
  crossing ~150–500, page fault or context switch ~1,000–3,000, network
  round trip in the millions.
  - **Why the split matters:** a flat SYS of ~2,000 would dwarf the 4-vs-12
    locality gap (5.7) and flatten the spatial structure. A ~150 crossing
    keeps locality meaningful for ordinary code, and the expensive calls
    are expensive because of what they do.
- **The old "trap ≈ 2,000" in 5.8 (fault → handler) wants the same
  revision:** the crossing is cheap; a page fault is dear because of the
  allocation work, not the jump. To revisit when the OS call table is
  costed.
- **Open:** instruction-fetch cost (only bites if code lives in slow
  memory; ignore for now), the exact crossing figure, and each service's
  cost (with the OS call table).

### 5.6 Known complaint about Nova 1 [settled by 5.7]

When a program moves to another node in Nova 1 (dormant drift, or DIVIDE to a
neighbour), its holdings stay in the old node's pools, and its guests
(parasites) can't go with it at all. Damian wasn't happy with this
(2026-11-09). Resources tied to places (5.3) would travel with whatever
carries the places; the details belong to Space.

**[settled by 5.7]** Damian accepted that hardware stays where it is. A
program travels as data, by SEND into the destination's reset region
(5.9), and finds new hardware there.

### 5.10 Disasters [decided 2026-11-11, Damian]

Damian (2026-11-10, 3.1) asked for disasters rooted in real hardware and
power problems, at roughly Nova 1's balance. Most of them come down to
surges, drops and storms.

Nova 1 had five kinds. Each ran on its own schedule, with scripted
warnings, false alarms and sounds, and together they needed 59 settings.
3.1 rules those schedules out.

This section maps each of Nova 1's disasters onto laws Nova 2 already has.
It proposes the two laws still missing: one that damages hardware and one
that restores it. Together these answer 5.0's open question of whether
burnout is permanent.

**Where Nova 1's disasters go:**

| Nova 1 | Nova 2 | New law? |
|---|---|---|
| Brownout | Sag under load (5.0 point 4), and sources dipping or dropping out (5.0c) | None |
| OS crash and reboot | A deep outage, then a cold start from the boot block (5.3) | None |
| Burnout and repair | Wear, then failure, and maintenance visits that replace what failed ((1), (2)) | Two, decided |
| Disk failure | Wear from writes (decided, 5.0a), then the same failure and visits | None beyond those two |
| Surge | Load rejection, which is emergent, and strikes ((3)) | None |
| Lightning storm | A moving patch where strikes appear, one type of regional event ((4)) | A field on where sources appear (the patch mechanism) |
| Warnings and false alarms | Rising heat, sag, errors and wear, which programs already feel ((6)) | None |
| Sounds | Dropped along with signals (5.3) | — |

**(1) Damage: one law, wear [decided 2026-11-11, Damian].**

- **Every device has a wear count and an endurance** (a catalogue number).
  It fails when its wear reaches its endurance.
- **Wear grows with stress:**
  - **With temperature, steeply.** Reliability engineers use the
    Arrhenius rule; a common rule of thumb is that failure rates double
    for every 10 °C. Accelerated life tests heat parts to squeeze years of
    ageing into weeks.
  - **With voltage above the device's maximum.** A real chip's insulation
    breaks down faster at higher voltage, and a big enough spike breaks it
    at once.
  - **With writes, for disks** (decided, 5.0a). A flash cell survives a set
    number of erase-and-write cycles, about 1,000–3,000 in common consumer
    flash.
- ~~Wear narrows a device's working range as it builds up, so errors start
  sooner at both edges.~~ Revised 2026-11-11, below: wear raises the low
  edge, and the high edge stays where it is.
- **What wear does, by kind of hardware [decided 2026-11-11,
  Damian, mechanism included].** Damian: after enough writes a
  disk's sectors become corrupted, memory slowly starts making errors, and
  CPUs, GPUs and NPUs get slower. Wear was already counted per device;
  what it lacked was a different effect for each kind.
  - **One mechanism gives all three: wear raises a device's threshold
    voltage,** the lowest voltage at which it works.
  - Real anchor: transistors age. Two effects, NBTI and hot-carrier
    injection, raise a transistor's threshold voltage over years, faster
    when hot and at high voltage. Intel's 2024 Raptor Lake instability was
    this, sped up by voltage set too high. The lowest stable voltage crept
    up until chips crashed.

  | Hardware | What wears it | What wear does | Real anchor |
  |---|---|---|---|
  | CPU cores, GPU, NPU | Heat, overvoltage | **Slower at the same voltage.** Speed follows how far the voltage sits above the threshold (5.0a), and the threshold rises. A worn device also cuts out sooner as the voltage sags, and makes errors sooner near its low edge | Ageing chips lose speed at a given voltage. Under the alpha-power law, speed grows with voltage minus threshold |
  | Memory | Heat, overvoltage | **Errors that grow over time, at fixed spots.** Its cells leak at different rates (5.0a, decided). As the threshold rises, the leakiest cells start failing to hold bits even on a normal day, always in the same places | DRAM's weak cells, which hold their charge for less time as they age and heat up |
  | Disk | Writes, per block; heat, for the whole device | **Corrupted sectors.** Each block counts its own writes. Past its endurance, drawn per block around the type's value, its bits start flipping, more so the more it is written. The most-written blocks, such as the boot block, wear first | Flash's limit on erase-and-write cycles, and bad blocks. In 2021 Tesla recalled car screens whose flash chips had been worn out by logging |
  | Link | The current heating it | **Its resistance rises,** so it heats more and wears faster, until it fails | Ageing joints run hot, which is what thermal-imaging inspections of power lines look for |
  | Power source | — | Doesn't wear (5.0c) | — |

  - **Wear levelling: an OS toggle [decided 2026-11-11, Damian; default
    off, "cheap flash"].** Damian: programs don't read disk wear. If
    anything handles wear, it's the OS, as a toggle between cheap flash
    and a modern SSD.
    - **Off (default): cheap flash.** A disk offset maps to the same
      physical block for life, so wear lands where the writing is.
      - The most-written blocks corrupt first, the boot block among them.
      - Blocks shared under size classes (5.3) make that a commons: a heavy
        writer wears out its neighbours' data too.
      - Real anchors: SD cards, USB sticks, and Tesla's worn-out flash
        chips.
    - **On: a modern SSD.** The OS keeps a map from each disk offset to a
      physical block. It moves often-written data onto less-worn blocks,
      so wear spreads across the whole disk. Programs see the same offsets
      throughout.
      - The disk ages evenly: few corrupted sectors early, then many at
        once near the end.
      - Real anchor: SSDs do this in their controller's firmware. Nova 2
        has no firmware (5.0a), so the OS is the right home. That's real
        too: on raw flash with no controller, Linux levels wear in
        software (UBI, JFFS2).
    - **What changes for programs:**
      - With levelling on, the boot block no longer wears out first.
      - A heavy writer no longer ruins its own neighbours' blocks in
        particular. It wears everyone's disk a little instead, so the
        commons spreads from the block to the whole disk.
    - **Cost:**
      - one map entry per block;
      - the OS's own copies, which count as writes, so levelling itself
        wears the disk a little (real: write amplification).
  - **Programs can see wear only partly.** SENSE already reports each
    device's speed (5.7), so a worn processor shows as a slow one. Memory
    errors and bad blocks show only through what they corrupt. SENSE does
    not read disk wear [decided 2026-11-11, Damian].
  - **Cost:**
    - Disks need one write count per block.
    - The leak order of memory cells needs no storage: each cell's rank is
      a hash of its address and the seed. The engine can draw the failing
      cells per page each tick instead of checking every bit.
- **Burnout is the extreme case, not a law of its own.** A device far past
  its heat limit, or hit by a spike far past its voltage limit, wears so
  fast that it fails at once.
- **What failure does:** a failed device stays off until it's replaced.
  - **A core:** the scheduler has one fewer core. Threads are shared out
    in time, so they run on the others (real: taking a core offline).
  - **A memory device:** it loses its contents. Its pages are freed, and
    touching them faults. A program with all its pages there dies; one with
    a fault handler may survive (real: a machine-check exception).
  - **A disk:** it loses its data, and the boot block too if it held it.
    DREAD and DWRITE fail soft.
  - **A GPU or NPU:** its calls fail soft.
- **The heat cut-out still protects (5.0a, decided).** A device past its
  heat limit stops drawing power, so it cools. It wears seriously only
  when it can't cool:
  - when its place is kept hot by its neighbours, its links or its source;
  - when a voltage spike arrives faster than heat can trip it.

  So burnouts happen near hard-worked sources and in crowded hubs, which is
  where 5.0b wants heat to bite.
- **Trade-offs (3.2):**
  - Overclocking (SETCLOCK) is faster now but wears the shared core sooner
    for everyone on it. That's the commons again (5.0), and it's real:
    overclocking shortens a chip's life.
  - Living near a strong source gives power but wears the devices.
  - A backup on disk costs writes, and writes wear the disk (decided).
- **Settings:**
  - endurance for each device type (in the catalogue);
  - how steeply wear rises with heat (in degrees per doubling);
  - how steeply wear rises with overvoltage;
  - the disk's wear per write (decided).

  A slope of 0 turns off wear from that cause.

**(2) Restoring: one law, maintenance visits [decided 2026-11-11, Damian, with both optional extras].**

- **Each place is visited at times drawn from the seed before the run,**
  whatever happens in the world. A visit replaces every failed device at
  the place with a new one, without wear, drawn from the place's own
  fields (5.7's biomes).
  - The new device differs a little from the old one (the catalogue's
    scatter), but the biome stays the same. That fixes the weakness of
    5.0's option (b): the map doesn't drift.
  - New memory and new disks start as static (5.4, 5.3).
  - Working devices are left alone unless they're worn past the
    replacement threshold (next point).
- **Visits also replace worn devices [decided 2026-11-11, Damian;
  default 1].** Damian asked whether the repairs should cover worn
  devices too. Claude: yes, past a threshold.
  - **A slider sets the threshold:** the wear, as a share of endurance, at
    which a visit replaces a device that still works.
    - At 1, only failed devices are replaced. That's the default,
      so wear's effects are felt in full.
    - Lower values replace devices earlier, so wear stays mild and
      failures become rare.
  - **Like for like, with contents copied.** A worn device is replaced by
    one with the same catalogue numbers (size, speed, range) and no wear.
    - A memory device's or disk's contents are copied across first. Bits
      already corrupted stay corrupted.
    - Processors hold nothing between turns, so swapping one only moves
      threads.
    - Real anchors: server "memory sparing" copies a failing memory rank's
      contents to a spare once its corrected errors pass a threshold. A
      failing disk in a RAID array is replaced and rebuilt from the
      others.
  - **This removes the earlier objection** to replacing parts before they
    fail, that it would wipe what lives on them: copying keeps it.
  - A failed device is still replaced by a fresh draw from the place's
    fields, because nothing on it survives to copy.
  - **Disks:** a replacement resets every block's write count, and
    corrupted data is copied as it is.
- **How it runs, step by step [Claude, expanding at Damian's request,
  2026-11-11]:**
  1. **Before the run,** each place draws its own visit times from the
     seed. The gaps scatter around a mean interval, which is the one
     setting. Nothing in the run moves them, and a place stores only the
     tick of its next visit.
  2. **Between visits, failures wait.** A failed device stays dead until
     the place's next visit, however many others fail in the meantime.
     One visit fixes them all, as one technician's round does.
  3. **At a visit,** each failed device is swapped for a new one, drawn as
     the place's hardware was at the start, with no wear. Cores go back to
     the scheduler. New memory and new disks hold static, and so does a
     new disk's boot block. Nothing else at the place pauses, because real
     parts are swapped while the machine runs (hot-swapping).
  4. **Nothing else happens.** Working devices stay, however worn, and
     programs are never touched.
- **What the interval does (illustrative numbers).** With random gaps, a
  failed device waits on average one interval for a visit; with regular
  gaps, half of one. So the share of time a device spends dead is about
  wait ÷ (lifetime + wait). Lifetime depends on stress, so one interval
  leaves different scars in different places:

  | Place | Device life | Interval 100 | Interval 10,000 | Interval 100,000 |
  |---|---:|---:|---:|---:|
  | Cool and quiet | 1,000,000 ticks | ~0% | ~1% | ~9% |
  | Hot hub by a hard-worked source | 20,000 ticks | ~0.5% | ~33% | ~83% |

  So the scars follow stress. A short interval makes a failure a blip
  everywhere. A long one leaves hot places patchy (burnt ground) while
  cool places stay whole.
- **Why visits rather than a repair delay per failure:**
  - A per-failure delay starts its clock when the device fails, so the
    world would decide when repair comes. Visits come when the seed says.
  - Visits also batch failures, as real rounds do.
  - They cost one number per place.
- **Random or regular gaps [decided 2026-11-11, Damian: a slider].** Real
  rounds are regular (a daily walk), and real call-outs are random.
  Regular visits are predictable, so a lineage could learn to time them.
  The regularity slider runs from 0 (random gaps, the default) to 1
  (exactly periodic).
- **The interval across the map [decided 2026-11-11, Damian: a slider].**
  Real repair is faster in cities than at remote sites. A slider ties the
  interval to 5.7's density field, so towns are visited more often. That
  adds to the trade-off between town and countryside with no new field.
  At 0 (the default) the interval is the same everywhere.
- **What visits don't do:**
  - ~~No replacing parts before they fail.~~ Revised 2026-11-11: allowed
    past the threshold, with contents copied ("Visits also replace worn
    devices", above). Real data centres swap disks whose SMART readings
    look bad.
  - **No upgrades.** New hardware comes from the same fields and
    catalogue, so nothing improves over a run. Improvement is real
    (Moore's law), but it is a schedule of progress, and it would make old
    lineages obsolete by decree.
  - **Sources aren't theirs.** The power companies keep those up (5.0c).
  - **Links:** if links wear ((5)), visits replace failed links too, as
    line crews do. Routes are recomputed when a link comes back as well as
    when it goes.
- **Ecology:** wear plus visits make a disturbance regime: how often a
  place loses hardware, and for how long. Ecology treats disturbance as a
  main driver of diversity. The intermediate disturbance hypothesis
  (Connell, 1978) says diversity peaks at moderate disturbance, though
  that claim is contested.
- **The visit interval is a sliding scale,** as Damian prefers to
  switches:
  - **short:** failures barely last, close to 5.0's option (a), trip and
    cool;
  - **long:** failures leave scars that last, close to (b);
  - **none:** nothing is ever replaced, and the world wears out (a test
    setting).
- **Real anchors:** a staffed data centre swaps failed parts within hours
  or days. At the other extreme, "fail in place" designs leave failed
  servers where they are until the whole rack is replaced. Microsoft's
  sealed undersea data centre (Project Natick, 2018–2020) ran for two years
  with its failures left in place.
- **Is a visit a reaction to the world (3.1)?** Its timing isn't: the
  schedule is fixed before the run, as the sources' is (5.0c (b)). What a
  visit does depends only on the hardware's state (what has failed), never
  on programs or demand. Claude's opinion: that gives visits the same
  standing as 5.0c's "power companies" maintaining the supply from outside.
- **Rejected: a scheduled refresh that also replaces working hardware.**
  It's stricter about 3.1, but it would wipe whatever lives on a replaced
  memory device, with no cause in the world. That's a scripted disaster by
  another name.
- **This answers 5.0's open question (is burnout permanent?) with two
  laws, as Claude asked there.** All four of Damian's options are
  present:
  - (a) is the decided heat cut-out;
  - (b), new hardware from outside, is the visits;
  - (c), degrading, is wear narrowing the range;
  - (d), wear and repair, is wear plus the visits.

**(2b) Damian's respawning places [decided 2026-11-11, Damian: "a happy medium between both"; the share
defaults to 1].** Damian's own picture (2026-11-11) was close to visits, but at
the scale of places. A place dies, and a new one is generated somewhere
else on the landscape, with its own links to the places around it.
Visits are more static and more frequent.

- **Both can exist together.** Visits repair devices, and respawning
  moves whole places. Real anchors: technicians swap parts every day,
  while whole data centres close and new ones are built elsewhere over
  years.
- **Respawning on a schedule meets the objection to the refresh above.**
  Tearing down a place where programs live is a scripted wipe.
- **Proposed: only dead places move.** When a visit finds every device at
  a place failed, it doesn't repair the place. It tears it down and builds
  a new one elsewhere:
  - The new place goes at a random point on land, drawn by the density
    field (5.7 (a)), with new hardware from the fields there.
  - The Gabriel rule links it to the places around it, and the old
    place's neighbours relink among themselves.
  - It takes over the dead place's id, so the number of places stays the
    same. In real networks, too, an address is reused by a new machine.
- **What this gives:**
  - Nothing living is wiped, because nothing can live on a place whose
    memory has all failed.
  - Over a long run, the most punished ground is abandoned, and the
    network slowly rebuilds itself elsewhere.
  - The density field keeps towns and countryside, on average, where they
    were.
- **A slider instead of "every device":** a visit tears a place down when
  at least this share of its devices has failed.
  - 1, the proposed default: only wholly dead places move.
  - Lower values also evict survivors. That's a disaster with a cause in
    the world, a mostly ruined place.
  - Above 1: never.
- **Cost:** small once links can fail ((5)). The voltage solve and the
  routes must already handle a changing graph, and linking a new place by
  the Gabriel rule touches only its neighbourhood.
- **Risk:** whole places fail only where stress is extreme, so with short
  visit intervals this may almost never happen. The two settings trade
  against each other: frequent visits keep places alive, and rare visits
  let them die and move.
- Claude's opinion: include it, with the share at 1.

**(3) Surges [decided 2026-11-11, Damian].**

- **Load rejection is emergent and needs no law.** When many devices cut
  out at once, from heat trips or failures, the draw falls and the sag
  eases. The voltage then rises at every place sharing the source.
  - Devices that sat near their maximum are pushed past it. They heat up
    and cut out in turn, which raises the voltage again.
  - That's a cascade, which is what 3.1 asks for: stress builds, then is
    released suddenly.
  - Real anchors: a power plant that loses its load sees its voltage and
    speed jump. Cascades like the 2003 North American blackout spread as
    lines and generators tripped one after another.
  - Stiff sources barely move with load, so they rarely cascade. Soft
    sources let crowds move in (5.0 point 5), but they cascade. That's a
    trade-off between sources with no rule of its own.
- **A strike is a type of source, not a new law.** It's an entry in the
  source catalogue (5.0c): a very high voltage, a short life (one tick by
  default), and a moderate internal resistance, because it carries limited
  charge.
  - It enters the voltage solve like any other source. Its spike falls off
    along the links by resistance and spreads further through dense,
    low-resistance towns. Real surges do the same, travelling down power
    lines to devices far from where the lightning hit.
  - Its own loss heats the place it strikes. Over water, the shore drains
    it (5.7).
  - All its effects come from laws already decided. Voltage past a
    device's maximum makes errors (memory loses bits, which is what Nova
    1's strike did by randomising codons), heat and wear. A big enough
    spike makes devices fail outright.
- **A source's own surge [decided 2026-11-11, Damian].** Damian
  counts sources among the hardware that has both failure states ((7)).
  - A source's outage is already decided: it can drop low or out for a
    while (5.0c).
  - Its surge is new: its voltage can swell above its normal level for a
    while, as part of its fluctuations.
  - Real anchor: the power-quality standard IEEE 1159 names both sags and
    swells. A swell is a rise to between 1.1 and 1.8 times the normal
    voltage, lasting from half a cycle to a minute.
  - This revises 5.0 point 6 ("swings lower a source's capacity, not its
    voltage"). Drops stay on capacity, and swells act on voltage.
  - Sources still don't wear (5.0c: the power companies maintain them).

**(4) Regional events: one mechanism, a catalogue of patches [decided
2026-11-11, Damian].** Damian asked for regional events on top of
individual ones:

- widespread power spikes, where an area runs hot for a time;
- brownouts across an area;
- network outages.

Storms already needed a moving patch on the map, so one mechanism serves
them all.

- **What a patch is:**
  - It forms at a seeded time and place, with a radius in links
    (5.7 (f)).
  - It drifts along a heading that turns a little, or stays put.
  - It lives for a drawn time, ramping up and down.
  - While active, it scales one quantity for everything inside it.
- **Why it passes 3.1:** everything about a patch is fixed before the
  run, like the day sweep, and it scales only quantities that are already
  boundary conditions or rates.
- **Patch types are rows in a catalogue,** as source types are (3.1,
  "variety in things"):

  | Type | What it scales inside the patch | Real anchor |
  |---|---|---|
  | Storm | The chance that strikes appear ((3)); 0 outside storms | Thunderstorms, and lightning surges on lines |
  | Swell | Sources' voltage, upward: the area runs hot | Regional overvoltage, as on lightly loaded long lines at night (the Ferranti effect) |
  | Brownout | Sources' capacity, downward, as night does | Rolling blackouts and load shedding (California 2000–01; South Africa since 2007) |
  | Heat wave | The outside temperature, upward, so cooling weakens | London's July 2022 heat wave took Google's and Oracle's data centres there down when their cooling failed |
  | Network outage | The chance that links are cut ((5)) | Hurricanes taking out a region's lines (Puerto Rico, Maria, 2017) |

- **Individual events are the same things at one place:** a single
  source's own dips and swells ((3), 5.0c), a single strike, a single cut.
  A patch is many of them at once, correlated in space and time.
- **Damian's "an area runs hot for a time" is the swell or the heat
  wave.** They differ in cause:
  - A swell pushes everything powered in the area past its voltage, which
    brings errors, heat and wear.
  - A heat wave weakens every place's cooling, so the places working
    hardest trip first.
- **Warnings come free.** A patch ramps up and moves, so its edge reaches
  some places first, and the neighbours' readings (SENSE, 5.7) show it
  coming.
- **Settings:** about 18 for five types:
  - movement, shared by all types: speed, turn, lifetime (3);
  - per type: how often, how big, how strong (3 each).

  Nova 1 had 11 settings for storms alone.
- 5.0b proposed an outside temperature that doesn't follow the day. A
  heat wave varies it regionally, still as a boundary condition.
- **Why storms only affect power:** real storms also add noise to data
  links. That would need a law of its own, so it's left out. Lines down
  come from the network-outage type and from link wear ((5)).

**(5) Network outages: links fail and are repaired [decided
2026-11-11, Damian, routing around failures].** Damian wants network outages
that stop travel between places until they're repaired. That answers
"do links wear too?" with yes.

- **A link fails in three ways.** Each is repaired at the next visit to
  either of its places (2).
  - **Wear** under load, by the wear law (1). A link carries current and
    turns part of it into heat (5.0b). Real grids fail this way: in 2003,
    heavily loaded lines sagged into trees and tripped, pushing their load
    onto other lines, which tripped in turn.
  - **Cuts from outside:** each link has a small seeded chance per tick
    of being cut, growing with its length. Real anchors: most fibre cuts on
    land come from digging ("backhoe fade"). Roughly 150–200 undersea cable
    faults happen each year worldwide, mostly from fishing and ships'
    anchors. Bridges carry data only, so they never wear, but they can be
    cut.
  - **Network-outage patches** raise the cut chance over an area (4).
- **What an outage does:**
  - A failed link carries neither power nor data, and the data in flight
    on it reroutes from the near end (decided 2026-11-11, Damian; 5.8, "Transit" (7)).
  - Routes are recomputed (5.7 (e)), so traffic goes around the gap if
    there's a way, at the cost of more delay.
  - Travel stops only where no way is left: a cut bridge isolates an
    island, and a regional outage can cut off a whole area.
  - Real anchor: the internet routes around damage, and a network splits
    only when every path between two parts is cut.
  - **Routing goes around a failed link [decided 2026-11-11, Damian].**
    That's real, and it keeps outages local unless they're wide.
- **Cost:** each change needs about one shortest-path search per place,
  and it changes the graph under the voltage solve.
- **Settings:**
  - the link-wear slope (0 means links never wear);
  - the cut rate per link length (0 means no cuts).

**(6) Warnings [decided 2026-11-11, Damian].** Nova 1 scripted symptoms before each
disaster, with false alarms. In Nova 2 the laws produce them:

- heat rises before a burnout;
- voltage sags before a cut-out;
- errors rise and processors slow as wear raises a threshold;
- a storm's strikes show up first as voltage spikes at neighbouring places
  (SENSE on a neighbour, 5.7).

False alarms come free: a hot spell that cools before anything fails.

- ~~One addition, proposed: SENSE reads a disk's wear, as real SMART data
  does.~~ Rejected 2026-11-11, Damian. If anything handles disk wear, it's
  the OS, through the wear-levelling toggle ((1)). Wear in every kind of
  device stays hidden, and shows only as slowness, errors and corruption.

**(6b) Symptoms before large events [decided 2026-11-11, Damian, after his
request].** Damian: larger events, such as power outages, need
symptoms that programs can detect, so they have a chance to react.

**What programs can already sense (5.7, 5.9):**

- their own node's voltage, temperature and the time;
- each device's speed;
- their neighbours' voltage, temperature, idle cores and free memory, one
  hop away;
- SEND's timing, which works as a ping.

A trend is something a program has to notice for itself, by keeping
earlier readings and comparing.

**Which large events give warning now:**

| Event | What shows first | Warning now? |
|---|---|---|
| Night | Voltage falls as the sweep arrives; the time can be read | Yes |
| A source reaching the end of its life | It fades (5.0c: grows, holds, fades) | Yes, if the fade isn't too quick |
| A source dropping out (one of its fluctuations) | Nothing; the drop can be sudden | **No** |
| Regional brownout or blackout, swell, heat wave | Nothing, unless the patch ramps up | **Only through the patch's ramp, whose length isn't set** |
| Storm | Strikes at neighbours first as it moves in | Partly |
| Regional network outage | Routes detour, so pings rise and SENDs fail | Partly |
| A single cut link | Nothing | No (real cuts by digging give none either) |
| Cascade | Voltage rising and neighbours tripping, fast | Yes, briefly |
| Burnout | Temperature rising | Yes |
| Failure from wear | Slower, more errors | Yes |
| Deep outage, then a cold start | The drop that causes it | Only if that drop ramps |

**Every event from outside ramps up [decided 2026-11-11, Damian].** This covers source
drop-outs, swells, the patches and storms.

- **How it ramps:** each event's strength rises from nothing to its peak
  over a lead time drawn per event. It then holds, then eases.
- **This doesn't add separate symptoms.** The ramp is the event itself,
  starting gently, as part of the same boundary condition (3.1). Nova 1
  scripted its symptoms as separate effects ahead of a scheduled
  disaster; this replaces that.
- **What a program feels early:** the first part of a drop-out or a
  brownout is a sag. The first part of a swell or a heat wave is a rise
  in temperature. Neighbours on the side the event comes from feel it
  first.
- **False alarms:** a drawn share of ramps peak low and fade. Reacting to
  every dip, by sleeping or leaving, then has a cost, which keeps the
  reaction a trade-off (3.2) rather than a free win. Nova 1 used 25%.
- **The lead must stand out from normal swings.** A ramp's early part has
  to grow past a source's ordinary fluctuations (5.0c), or nothing could
  tell it from noise. So the lead's depth is set relative to the swing
  size.
- **Real anchors:**
  - Grids often sag before they fail. In Tokyo's voltage collapse of July
    1987, on a hot day, voltages sank for about twenty minutes before the
    collapse.
  - Storms are seen coming.
  - Brownouts precede blackouts.
  - A sudden drop with no warning is real too: a tree on a line. That's
    why the lead is drawn from a range whose low end can be short, not
    fixed.
- **Settings:** 3 (the lead time's shortest and longest, and the
  false-alarm share), shared by all outside events. Nova 1 had a lead
  range per disaster, plus a false-alarm share.

**A low-voltage interrupt, the hardware's own alarm [decided 2026-11-11, Damian].** This was
mentioned as an option in (7); Damian's point makes the case for it.

- SETHANDLER gains one kind. It fires once when a core's voltage falls to
  within a margin of its cut-off. The margin is a setting.
- The handler can call SLEEP, so the thread is off the core when the cut
  comes and survives (7).
- **Real anchors:**
  - the PDP-11's power-fail trap, which gave the OS a few milliseconds to
    save state;
  - ACPI's low-battery event;
  - a UPS signalling "on battery" to the server it feeds.
- **Why it's worth having:**
  - Polling costs a SENSE call (about 150 cycles, 5.9) every time it
    checks, all the time, and still misses a fast drop between checks.
  - A handler costs nothing until it fires, and it reaches a thread in
    the middle of its work.
  - The trade-off: it fires late, near the cut-off, so it suits programs
    that want to work until the last moment. Polling and trend-reading
    see further ahead, at a constant cost.
- **It passes 5.1:** an interrupt only delivers information, like the
  timer. It decides nothing for the program.
- **Cost:** no new call, one more handler kind, and 1 setting.

**(7) Damian's two failure states per device (2026-11-11).** His original
idea was that every device has two failure states: an outage or crash
(power too low) and a surge (power too high), each with its own effects
on the device. He asked whether the power laws already give them.

- **Claude: yes. They are the two ends of every device's voltage range**
  (5.0 point 7, 5.0a), applied by one law with each type's own numbers.
  Near either end a device makes errors. Below its range it cuts out.
  Above its range it heats, trips until it cools, and now wears (1). No
  per-device rule is needed, and none should name "outage" or "surge"
  (3.1).

  | Device | Below its range (outage) | Above its range (surge) | Lost when it cuts out |
  |---|---|---|---|
  | P and E cores | Off; back on a little above the cut-off (5.0a) | Wrong results, heat, a trip until cool, wear; a big spike fails it | The registers of the thread running on it **[gap]** |
  | Memory | Fades: errors first, then loss, reading back random (5.0a, 5.4) | Bits it fails to hold, heat, wear | Its contents, gradually (decided) |
  | Disk | Off; DREAD and DWRITE fail soft; its data is kept | Errors, heat, wear | A write in flight **[gap]** |
  | GPU | Off; GLAUNCH fails soft | Wrong lane results, heat, wear | The lanes in flight **[gap]** |
  | NPU | Off; NMATMUL fails soft | Wrong products, heat, wear | The multiply in flight **[gap]** |
  | Power source | Drops low or out for a while (decided, 5.0c); everything it feeds sags | Swells above its normal voltage for a while ((3), proposed); everything it feeds runs hot | Nothing; sources don't wear (5.0c) |
  | Link | Fails: worn, cut, or in a network-outage patch ((5)); no power or data crosses it | No range of its own; heavy current heats and wears it | Nothing: the data on it reroutes from the near end (5.8, "Transit" (7)) |

  Damian (2026-11-11): "device" here means every kind of hardware,
  sources included. The last two rows were added for that.

- **The gap: what a device loses when it cuts out.** 5.0a says "night puts
  programs to sleep". That quietly assumes a thread on a core is saved
  when the core cuts out. Real hardware can't do that: a sudden loss of
  power loses the registers, and that's a crash.
  - Real machines sleep safely only because they get warning and act
    before the power goes: a laptop suspends, and a UPS tells a server to
    shut down. The PDP-11 even had a power-fail trap that gave its OS a few
    milliseconds to save state.
- **[decided 2026-11-11, Damian] A device that cuts out loses its work in flight and
  keeps only what it stores.** That's what "off" means, so it needs no new
  law.
  - **Core:** the thread running on it at that moment ends, as a fault
    with no handler would, because there's no power to run one. Threads
    not on a core keep their saved registers in memory and survive as long
    as memory holds. That covers threads that are queued, asleep, or
    waiting on a call.
  - **GPU:** the lanes in flight are lost. The launching thread was
    asleep, so it survives, and it wakes to a failure.
  - **NPU:** the multiply in flight is lost, and NMATMUL reports failure.
  - **Disk:** a write in flight is torn, leaving the block part new and
    part old. Real file systems keep journals because of torn writes. A
    read in flight fails.
- **Effects:**
  - **Night is a crash for whatever is running on a core when the core
    cuts out, and a sleep for everything else.** A program that senses its
    voltage falling (SENSE, 5.9) and calls SLEEP in time survives. That
    gives the warnings (6) a use, and it's a trade-off (3.2): keep
    computing as the light fades, or sleep early and stay safe.
  - **It rebuilds Nova 1's crash from physics.** Nova 1 deleted a drawn
    30–80% of a crashed node's programs. Here the share lost is the threads
    on cores at that moment out of all threads, so it follows crowding with
    no setting. A crowded node loses few; a sparse one loses everything
    that was running.
  - **Several threads hedge the risk:** a thread queued off a core
    survives. Threads still cost a SPAWN and core time.
- **Risk: early life.** A fresh replicator from the reset region has one
  thread and senses nothing. In an empty node it has a core to itself, so
  the first cut-out kills it.
  - Life gets through the night only once a node holds more threads than
    cores, or once a lineage learns to sleep. That's an Allee effect:
    small populations fail.
  - Places under steady sources (5.0c) never cut out, so they are refuges.
  - It has to be tested alongside the origin test (5.4). If it fails, the
    fallback is the pause that 5.0a assumed.
- **A power warning: decided in (6b), 2026-11-11.** SETHANDLER would gain a
  kind that fires as the voltage nears a core's cut-off. Real versions
  are the PDP-11's power-fail trap and ACPI's low-battery event. It's
  barely easier to evolve than polling: one registration plus a SLEEP in
  the handler, against a SENSE, a compare and a SLEEP in the loop. So it
  isn't needed at first.

**(8) Where mutations come from [decided 2026-11-11,
Damian].** Damian wants mutation from three sources:

- cosmic rays, as classic random bit flips rather than Nova 1's
  protection by NOP padding;
- a small chance of corruption when copying;
- a small chance of corruption when travelling between places ("we use
  UDP").

- **Cosmic rays: already decided (2026-11-10, 5.4)** as the rare floor in
  all memory.
  - Each bit has a chance per tick of flipping. It's a sliding scale with
    0 meaning off.
  - The flips are classic: no shielding, no ray energy, no absorption.
  - The rate still waits on a test.
  - Real anchor: soft errors in DRAM and SRAM from cosmic-ray neutrons.
    The rate rises with altitude: a few times higher in Denver than at sea
    level, and hundreds of times higher at airliner altitude.
  - Memory only, as decided. Real cosmic rays also hit logic, but memory
    holds far more bits.
- **Copying: a floor on wrong results [decided 2026-11-11, Damian].** 5.0 point 7 decided
  that wrong results happen near the edges of a device's range, and that
  "the middle of a range must be close to error-free".
  - For copying to carry a small chance of corruption everywhere, "close
    to" becomes a small floor: every result a device gives has a tiny
    chance of being wrong, even mid-range.
  - A copy is many results (each byte CPY moves, each store), so it
    collects errors in proportion to its length.
  - **Why not a rule for copying alone:** no law may name an emergent act
    (3.1), and a rule on CPY alone would favour copy loops that avoid it.
  - **Real anchor: silent data corruption.** Meta and Google (both 2021)
    found that a few faulty cores per thousand or so machines give wrong
    results now and then under normal conditions, with nothing logged. In
    real hardware the errors are concentrated in a few bad parts. So the
    floor could vary per device, through the catalogue's scatter: most
    devices nearly clean, a few flaky. That's an option.
  - **Constraint:** the floor plus cosmic rays must stay under the error
    threshold, well under one error per copy of a typical program (5.0
    point 7, Eigen).
  - **Setting:** the floor per device type (catalogue), with 0 meaning
    none.
- **Travel: already decided (5.7 (d)).** SEND's chance of corruption grows
  with route length. UDP fitted at first: SEND returned "ok" once the data was on the
  wire, with no acknowledgement. (Revised 2026-11-11, Damian: refusals now
  came back as errors, like email; then Damian chose fire and forget: nothing comes back beyond the sender's own exit (5.8, "Transit"). Corruption stays silent.)
  - **A real detail:** UDP carries a checksum, and a datagram that fails
    it is dropped, so on real networks most corruption becomes loss. IPv4
    lets the checksum be switched off, and Nova 2 copies that case:
    corruption arrives as it is, so it can become mutation. Programs can
    evolve their own checks.
  - **Loss:** a message that meets a full queue or inbox is dropped
    silently. One on a failed link reroutes (5.8, "Transit" (7)).

**Settings, a rough count:** about 33, plus catalogue entries, against
Nova 1's 59.

- wear: 2 slopes, plus endurance for each device type and each disk
  block's write endurance in the catalogue;
- visits: 4 (interval, regularity, tie to density, replacement
  threshold);
- respawning places: 1 (the share failed);
- strikes: one row in the source catalogue;
- a source's swells: 2 (how often, how big);
- regional events: about 18;
- links: 2 (wear slope, cut rate);
- error floor: one number per device type, in the catalogue;
- wear levelling: 1 toggle (default off, cheap flash);
- symptoms: 3 (lead time, shortest and longest; false-alarm share), plus
  the low-voltage interrupt's margin (1).

That makes about 33.

**Risks:**

- **Cascades may happen too often, or never.** That depends on how stiff
  the sources are, on the devices' ranges, and on how much margin devices
  keep. Only runs can show which.
- **Wear makes old places worse than new ones.** A place that has been
  crowded for a long time carries worn devices (slower, error-prone) until they fail
  and are replaced. That's intended (succession, 5.0 (c)), but it could
  make long-lived lineages pay for their own success.
- **The reset region sits on one memory device.** If that device fails,
  where does the region go? [Decided 2026-11-11, Damian] It lives on the place's most
  enduring memory device and moves to the next one if that fails.
  (Claude's note: a visit that replaces the failed device doesn't move it back.)

**Questions for Damian:**

1. ~~Damage as one wear law (from heat, overvoltage and disk writes) that
   narrows a device's range until it fails, with burnout as its extreme?~~
   Decided 2026-11-11, Damian.
2. ~~Restoring by maintenance visits?~~ Decided 2026-11-11, Damian, with
   the regularity and density sliders.
3. ~~Strikes as a type of source, and regional events as a catalogue of
   patches ((4))?~~ Decided 2026-11-11, Damian.
4. ~~Do links wear too?~~ Decided 2026-11-11, Damian: wear, cuts and
   patches, with traffic routing around a failed link ((5)).
5. ~~Can SENSE read a disk's wear?~~ No (Damian). Instead, an OS
   wear-levelling toggle, defaulting to cheap flash ((1)).
6. ~~Does a device that cuts out lose its work in flight ((7))?~~ Decided
   2026-11-11, Damian: yes.
7. ~~Respawning places ((2b))?~~ Decided 2026-11-11, Damian.
8. ~~Swells on a source's voltage ((3))?~~ Decided 2026-11-11, Damian.
9. ~~A floor on wrong results ((8))?~~ Decided 2026-11-11, Damian.
10. ~~Wear raising the threshold voltage, giving slower processors, memory
    errors at fixed spots, corrupted disk blocks and hotter links ((1))?~~ Decided 2026-11-11, Damian.
11. ~~Visits replacing worn devices past a threshold, like for like with
    contents copied, defaulting to 1 (failed only) ((2))?~~ Decided 2026-11-11, Damian.
12. ~~Every outside event ramps up over a drawn lead time, with a share of
    false alarms ((6b))?~~ Decided 2026-11-11, Damian.
13. ~~A low-voltage interrupt as a new SETHANDLER kind ((6b))?~~ Decided 2026-11-11, Damian.

### 5.11 The hardware catalogue [decided 2026-11-11, Damian]

This section gathers every kind of hardware and its numbers in one place.
It's Nova 2's periodic table (3.1, "variety in things"): many kinds, each a
set of numbers, all under the same laws. The laws stay in their own
sections; this lists only what each kind has to state. Every number is a
setting (2), and the defaults wait for tests. The real figures below are
anchors, not defaults.

**(1) The common row: what every device states.** These are the same
fields for every kind, so the laws treat them alike.

| Field | Meaning | Where it's decided |
|---|---|---|
| Count per place | How many of this kind a place holds | (6) a, decided |
| Position on the curve | One or two numbers (size, speed class) drawn from ranges; the rest follows from the kind's trade-off curve, with a little scatter | 5.0a |
| Voltage range | Threshold (lowest working voltage) and maximum; faster parts need a higher threshold | 5.0 point 7, 5.0a |
| Speed against voltage | How speed rises as the voltage sits further above the threshold | (6) b, decided |
| Draw | Idle draw and working draw. Working draw scales with size and with voltage² × speed | 5.0a |
| Heat | Heat capacity, heatsink rate to the place, heat limit, and the cool-down point below it | 5.0b, 5.0a; limits in (6) c, decided |
| Leakage | How much more it draws as it gets hotter | 5.0b |
| Errors | The floor in mid-range, and how errors rise towards each edge | 5.0 point 7, 5.10 (8) |
| Wear | Endurance; the slopes for heat and overvoltage are world settings | 5.10 (1) |
| Lost at a cut-out | What it holds in flight | 5.10 (7) |

One global number sits outside the rows: the restart gap above a cut-off,
the same for every device (5.0a).

**(2) The kinds.**

| Kind | Its own numbers, beyond the common row | Its trade-off curve | Real anchor |
|---|---|---|---|
| P core | Cost per instruction class (the base table, 5.9) | Speed against draw, with a high threshold | Desktop P cores: about 0.7–1.5 V, several watts to tens of watts each |
| E core | Its own cost table: about 15% slower on simple operations, 60–70% slower on divides, scans and calls (Nova 1's measurements) | Lower speed and lower threshold for much less energy per instruction | Intel fits about four E cores in the area of one P core |
| GPU | Lane count; lockstep group width; cost per instruction per lane; launch overhead. No clock control (5.8) | Lanes against draw: many lanes need a strong source | High-end cards draw 300–575 W at about 1 V, hundreds of amps |
| NPU | Multiply-adds per tick; launch overhead; reached only by NMATMUL | Far less energy per multiply-add than a GPU, but nothing else | Phone and laptop NPUs: a few watts |
| Memory | Size; access latency (the +4 to +12 cycle add-on, 5.9); fade rate; the spread of its cells' leak rates | Speed against holding on: fast memory has a higher threshold and fades first (5.7) | DDR4 at 1.2 V, DDR5 at 1.1 V; refreshed every 64 ms, twice as often above 85 °C |
| Disk | Size; access latency and cost per byte; write endurance per block | Speed against endurance; the slow, enduring end is the archive (5.3) | SSDs: microseconds, a few thousand writes per cell. Tape: tens of seconds, decades of keeping |
| Network device | Idle draw (about a tenth of a core, 5.8); draw per byte sent or received | None yet. One kind, no speed of its own (5.8) | Routers draw nearly the same busy or idle |
| Cooling device | Passive, driven by temperature ((5), decided) | Cooling against draw | Fans and pumps |

**Not devices, but hardware all the same.** These are stated here so the
catalogue is complete:

| Kind | Its numbers | Where they come from |
|---|---|---|
| Link | Per-length resistance; bandwidth; endurance under current; cut rate per length | Map fields (5.7, 5.8); endurance and the cut rate are settings (5.10 (5)) |
| Bridge | Bandwidth; cut rate. It carries no power | 5.7, 5.8 |
| Power source | Voltage; internal resistance (stiffness); how closely it follows the day; the size and speed of its swings; its swells; its lifetime (grows, holds, fades); and the skew of sizes, so most are small | 5.0, 5.0c, 5.10 (3) |

- **Source types are catalogue rows (5.0c):** steady, daylight, gusty,
  and the strike (5.10 (3)), which is a very high voltage for one tick.
- **Sources don't wear,** because the power companies maintain them.
- **The regional patch types** (storm, swell, brownout, heat wave,
  network outage) are rows of an environment catalogue (5.10 (4)), not
  hardware.

**(3) How a place gets its hardware (all decided).**

- **At the start,** each kind's numbers come from its biome field over
  the map (5.7, "Substrates become hardware biomes"), plus a small
  scatter.
- **At a visit (5.10 (2)):**
  - a failed device is replaced by a fresh draw from the same fields;
  - a worn one past the threshold is replaced like for like.
- **A respawned place** draws everything fresh where it lands (5.10
  (2b)).

**(4) The clock: SETCLOCK against speed following voltage [decided
2026-11-11, Damian].** Two decisions meet here, and how they combine has never been
written down:

- "Speed follows voltage" (5.0a);
- SETCLOCK's over- and underclocking (5.9).

The proposal:

- **The stock speed.** A core's speed at the place's voltage, given by
  its speed curve ((6) b), is its stock speed. It is what the voltage
  supports.
- **SETCLOCK scales the stock speed in steps,** for example from half to
  one and a half times, in tenths. The step size and the range are
  settings.
- **Above stock,** the core runs faster than the voltage supports. Its
  draw rises with the speed, and its errors rise with how far it is past
  stock. Real anchor: overclocking past what the voltage supports
  crashes, or silently miscomputes.
- **Below stock,** it runs slower, draws less, and errs less. Real
  anchor: underclocking for stability and battery life.
- **The thread's request applies during its turn (5.8).** GPUs and NPUs
  have no clock control, so they run at stock.
- **Wear lowers the stock speed** by raising the threshold (5.10 (1)). A
  worn core runs slower at the same request. That's Damian's "CPUs get
  slower".

**(5) Cooling devices [decided 2026-11-11, Damian: (a)].** 5.0b left
them for the catalogue. A cooling device spends power to raise its
place's cooling rate, as fans and pumps do.

- **(a) Passive, driven by temperature [Claude's recommendation].**
  - The device runs harder as its place gets hotter: its cooling and its
    draw both rise with the place's temperature. No program controls it.
  - Real anchor: thermistor-controlled fans. A thermistor is a passive
    part, so this needs no firmware (5.0a).
  - Its trade-off: a place with cooling holds more heat-making hardware,
    but it pays a base load that grows in the hot hours. The cost falls
    on everyone there, as the network device's does.
  - It obeys the common row: its own range, wear and failure (fans are
    moving parts and wear out), and visits.
  - When a hot place's cooling fails, the place overheats until the next
    visit: a slow disaster with a cause.
- **(b) Controlled by programs.** A call would set a place's cooling
  level, as SETCLOCK sets a clock.
  - That makes cooling a commons: one program pays everyone's power to
    protect its own core.
  - It costs a new call, and "who sets it" has the same problem 5.8 found
    with GPU clocks: several programs share one device.
- Claude prefers (a). It adds no call, it's real, and it gives places a
  trade-off between power and heat that programs live with rather than
  set.

**(6) Gaps found while gathering [decided 2026-11-11, Damian, as proposed].**

- **(a) Counts per place.** Each kind's count is drawn from its biome
  field, within a range.
  - At least one P or E core, one memory device, one disk and one network
    device per place. Births need a core; the reset region needs memory;
    the boot block needs a disk; and nothing reaches a place without its
    network device.
  - GPUs, NPUs and cooling devices may be absent. Many real machines have
    no GPU or NPU.
  - Real anchors: a server holds a few CPUs, several memory modules,
    several disks, and zero to eight GPUs.
- **(b) The speed curve.** Speed rises with how far the voltage sits above
  the threshold, in one shape per kind.
  - The real form is the alpha-power law: speed grows roughly as
    (voltage − threshold)^α ÷ voltage, with α about 1.3 for modern chips.
  - A simpler stand-in, speed in proportion to (voltage − threshold) up
    to the maximum, keeps the same direction with fewer numbers.
  - Claude's opinion: the simpler one, with its slope as the kind's
    number.
- **(c) Heat limits per kind,** taken from real parts:
  - cores: about 100 °C (CPUs throttle near there);
  - GPUs: about 85–90 °C;
  - memory: 85 °C, where real DRAM doubles its refresh;
  - disks: about 70 °C, where SSDs throttle.

  The cool-down point sits a little below each limit (5.0a).
- **(d) Default ratios between kinds, until tests say otherwise:**
  - E core against P core: Nova 1's measured ratios (above).
  - GPU lane against P core on serial code: about 5 to 10 times slower.
    That's Claude's estimate from lower clocks, in-order running and small
    caches; real lanes are much slower, not slightly (5.9).
  - NPU against GPU: about 5 to 10 times less energy per multiply-add.
    That's Claude's estimate from vendors' efficiency figures.
- **(e) Units for wear.**
  - Endurance is counted in ticks at reference conditions. Wear builds
    faster when hotter or overvolted (5.10 (1)).
  - Disk blocks count writes.
  - Links count ticks at a reference current.

**Settings.** The catalogue is mostly numbers per kind: about ten fields
for each of nine kinds, plus the source types. That's the advanced tier on
the setup page (2). New world settings: the clock steps and range (2), and
the minimum counts (4).

**Risks:**

- **Too many numbers to tune by hand.** Nova 1 needed a settings-search
  tool for the same reason. The curves help: each kind is set by its
  curve's shape and a few ranges, not by every field separately.
- **Dominant devices.** If the scatter is large next to the curve, some
  draws come out better at everything (5.0a's caveat). Keep the scatter
  small, and check by looking at the drawn devices before a run.

**Questions for Damian:**

1. ~~Counts per place: at least one core, memory device, disk and network
   device, with GPUs, NPUs and cooling optional ((6) a)?~~ Decided 2026-11-11, Damian.
2. ~~The clock: SETCLOCK scales the stock speed the voltage gives, in steps,
   with errors above stock ((4))?~~ Decided 2026-11-11, Damian.
3. ~~Speed rising in proportion to the voltage above the threshold (Claude),
   or the real alpha-power curve ((6) b)?~~ Decided 2026-11-11, Damian: linear above the threshold.
4. ~~Cooling devices: passive and driven by temperature (Claude), or set by
   a call ((5))?~~ Decided 2026-11-11, Damian: passive.
5. ~~Heat limits and default ratios from the real figures in (6) c and (6)
   d?~~ Decided 2026-11-11, Damian.

### 5.12 Observation [decided 2026-11-11, Damian; observer-side methods are revisable starting points]

This section covers what the reused viewer and stats need to show
(design order, step 8). Nova 1's viewer, setup page, determinism and
snapshots are kept (2).

**Nova 1's viewer, as a starting point:**

- a map with 22 colourings;
- a memory strip (by owner, lineage, heat, stamp, codon);
- charts;
- a phylogeny;
- a genome browser over the genebank;
- the "map of life" (genomes placed by PCA);
- a novelty measure;
- milestone events (first replicator, parasite, colonist and so on).

**(1) Principles [decided 2026-11-11, Damian].**

- **Watching changes nothing.** Observers only read. The world's hash is
  the same with or without them, as in Nova 1, where a test checked it.
- **Every named event is found by an observer in the state (3.1).** The
  engine never raises "disaster", "parasite", "death" or "species". Those
  are labels observers put on patterns (3.1, consequences).
- **[decided 2026-11-11, Damian] The engine records only physical
  facts.** A fact is something a law did to the state, logged with a tick
  and a place. A label is a pattern across facts, and only observers make
  labels. The rule of thumb: if a law acted, it's a fact; if it's a
  pattern, it's a label.
  - **Facts logged one by one,** because each one matters:
    - a device failed or was replaced;
    - a link failed, was cut, or came back;
    - a place respawned;
    - a source appeared or retired;
    - a patch formed or ended;
    - a cold start booted the boot block;
    - **a program was created:** its parent, the cause (SPAWN, an arrival,
      a boot, or static) and its genome's hash. An arrival's record names
      its sender, and a boot's names the boot block's last writer, so
      sends to port 0 and boot-block writes need no separate entries;
    - **a program ended, and why** (exit, fault, a crash at a cut-out, a
      failed memory device, no thread after its idle run). Its parent link
      would otherwise vanish with it, and descent needs it.
  - **Frequent facts are counted per sample,** not logged one by one:
    threads started, threads crashed at cut-outs, messages sent, dropped
    and stranded.
  - **Observers also read sampled, read-only views of the state,** because
    some labels are states rather than events. The parasite test compares
    a thread's owner with the owner of the page it's running in. The
    sample interval is a setting.
  - **Observer memory is saved beside each snapshot,** outside the world's
    hash. Nova 1 didn't save it, so a resumed run restarted its novelty
    model. In Nova 2 that would break the family tree at every resume.
  - **Why (Claude, agreed by Damian):**
    - It is 3.1's test in practice.
    - Labels can be redefined and re-run over an archive without
      rerunning the world.
    - An observer bug can't corrupt the world.
    - Nova 2 has no disaster rules, so its disasters can only be found by
      watching. Nova 1 could announce its disasters because they were
      scheduled.
  - **Real anchors:**
    - Systems keep raw event logs, and monitoring and security tools
      apply detection rules on top to name "incidents".
    - Avida's analysis mode works out what saved genomes do after the
      run.
  - **Volume:** one record per program birth and death, about 32 bytes
    each. A busy world may write megabytes per second, so the log is
    compressed and append-only.

**(2) A gap found first: what counts as an individual [decided 2026-11-11,
Damian: (b)].** Lineages, populations, births and deaths all need an answer, and
the design doesn't give one.

- **How it stands now.** The shortest replicator does ALLOC, CPY and
  SPAWN (5.9). SPAWN starts a thread of the same program, in pages the
  same program owns. So every local copy is another thread of one
  program, not a new program.
  - A lineage on a node grows as one ever-larger program: a colony.
  - New programs arise only from the reset region: static, an arrival at
    port 0, or a boot.
- **Why that's a problem:**
  - **No turnover.** A colony's memory is freed only when its last thread
    ends, so old copies' pages are never recycled while any thread lives.
    In Tierra, by contrast, each daughter is its own creature, and the
    reaper frees it alone.
  - **Shared fate.** One broken copy can FREE the colony's code, since
    every thread owns everything.
  - **Selection blurs.** Variants inside a colony share memory and
    ownership. They compete only for core time.
- **(a) Keep it.** A program is everything one owner holds, a clonal
  colony, like an aspen grove. A program that wants separate offspring
  sends a copy to its own place's port 0.
  - That uses existing physics, but local birth then needs a spare core
    and the network device's power. Under crowding, births stall.
- **(b) Where a thread starts decides what it is [decided 2026-11-11,
  Damian].** SPAWN takes no new argument. The rule depends only on
  the start address:
  - If it lies in the allocation that holds the caller's running code,
    the new thread is a thread of the same program.
  - If it lies in any other allocation the caller owns, that allocation
    passes to a new program, and the thread starts there as that
    program's first.
  - Real anchor: threads run inside their process's own image, and
    running a different image makes a new process (exec, CreateProcess).
    Linux's `clone()` makes threads and processes with one call, and its
    flags choose what is shared.
  - **Why not a flag in a register:** the decided 14-byte replicator (5.9)
    never sets C. A flag would read whatever value was left there, and
    that value changes from one generation to the next. Deciding by place
    is stable.
    - The replicator copies itself into a new allocation and starts a
      thread there, so it makes separate offspring with no extra bytes.
    - A birth from the reset region runs in memory nobody owns, so its
      first SPAWN into its new allocation makes a new program too
      (5.4's step 6).
    - A program that wants more threads of its own starts them in its own
      code. That's the evolved choice.
  - **It changes 5.3:** ownership now also changes when a program hands
    one of its own allocations to a new program at SPAWN.
  - **It passes 5.1:** it names no program, because the child doesn't
    exist yet, and the parent gives only what it owns.
  - **What it gives:** each copy lives and dies on its own. Its pages
    free when its last thread ends, so memory turns over as in Tierra.
    A lineage is a family of programs.
- Claude prefers (b). Under (a), the world's turnover would hang on whole
  colonies dying, by crashes at nightfall or by fading.

**(3) Lineages and genomes [decided 2026-11-11, Damian: lineage by descent, genome groups. The
genome follows from the individual, (2); the grouping method is a revisable starting point].**

- **A program's genome is the bytes of the allocation it started in,** as
  they were at its birth. Its id is a hash of those bytes. The genebank
  saves each new genome once, with when, where and its parent.
- **Lineage is tracked by descent [decided 2026-11-11, Damian].** The
  parent is recorded at birth, from physical facts, so nothing is guessed
  afterwards:
  - a SPAWN that makes a new program gives the spawner;
  - a port-0 arrival gives its sender;
  - a boot gives the boot block's last writer;
  - static gives no parent: a new origin.
- **No similarity matching.** Corruption in transit doesn't break a link:
  the sender is still known, so the corruption is a mutation.
- **Where the maker and the code differ,** descent still records the
  maker. That happens in three cases:
  - scavenging, where a program starts a dead program's code;
  - hybrids, where a migrant is partly overwritten in the reset region
    before it runs;
  - parasite code copied along inside a host.

  The detectors in (4) label these (scavenger, parasite). They don't
  change the tree.
- **Genomes are grouped by similarity [decided 2026-11-11, Damian; method
  a revisable starting point, Claude].** Descent answers "who came from whom". Groups answer
  "which genomes are alike", which is roughly what species are. Both are
  kept, side by side.
  - **What groups are for:**
    - population counts per group;
    - diversity over time;
    - territories on the map;
    - colours on the map of life.

    And where a child lands in a different group from its parent, that
    flags a big jump: a large mutation, a hybrid, or code taken from
    someone else.
  - **Method (proposed): greedy clustering.** Each group keeps its first
    genome as its representative. A new genome joins the first group whose
    representative it matches closely enough, or starts a new group.
    - Closeness is the share of 3-byte runs two genomes have in common.
      Nova 1's map of life hashed 3-codon runs the same way.
    - The threshold is a setting.
    - A new genome is compared with each group's representative, not with
      every genome, which keeps it cheap.
  - **Real anchors:**
    - Microbiologists group bacteria by DNA similarity: about 97% for the
      marker gene that defines working units, and about 95% across the
      whole genome for a species.
    - The clustering tools they use (CD-HIT, UCLUST) work greedily, as
      above.
  - Groups are an observer's view and change nothing in the world.
- **Genomes are real code,** so the genome browser shows them
  disassembled in the 22-instruction set (5.9). Nova 1's codons had no
  such reading.

**(4) Detectors: labels found in the facts [parasite decided 2026-11-11, Damian; the rest a starting set, revisable at any time because labels belong to observers ((1))].** Each is an
observer rule, never an engine rule.

| Label | Found when | Notes |
|---|---|---|
| Replicator | A program has a descendant whose genome matches its own | Nova 1's first milestone |
| Colonist | The first program of a lineage at a place, born from an arrival | |
| Parasite | A thread runs code in pages owned by another program | **Decided 2026-11-11, Damian.** A precise physical signal: the thread's owner and the owner of the page at its IP differ. That is hijacking (5.3). Found in the sampled state views ((1)) |
| Scavenger | A program runs code it found in memory it allocated, bytes left by a dead program | Corpses are not zeroed (5.9) |
| Booter | A program born from a boot block | 5.3 |
| Sleeper | A program that SLEEPs, or uses the low-voltage interrupt, before cut-outs, and survives them | 5.10 (7), (6b) |
| Communicator | A program whose behaviour follows data it RECVs from another lineage | Hard to detect well; start with "RECVs, then acts" |
| Partnership | Two lineages that regularly send to each other's ports | |
| GPU user, NPU user, disk user, pinger | It calls GLAUNCH, NMATMUL, DWRITE or PING and gains from it | |
| Cascade | Cut-outs that spread from place to place within a few ticks | A disaster label (3.1) |
| Blackout, outage | A region dark, or cut off, for a time | Labels on patches and failures |

Deaths are counted by cause, from the facts:

- the last thread exited;
- a fault;
- a crash at a cut-out;
- a memory device failed;
- no thread at the end of an idle run.

**(5) The viewer: what changes [decided 2026-11-11, Damian].**

- **Map.** Places at their positions on the landmasses, water, bridges,
  and links drawn by what they carry.
  - **Overlays:** sources as icons by type; patches as moving discs;
    borders (5.7) as lines.
  - **New colourings,** beside Nova 1's population, births, lineage
    territories and biome fields:
    - voltage and temperature;
    - draw;
    - wear and failed devices;
    - network device down;
    - queue fill, and drops on links;
    - power and data flow along links;
    - cooling;
    - age since the last visit.
- **Place inspector (new).** Clicking a place shows:
  - its devices, each with its numbers (speed, threshold, wear,
    temperature, on or off);
  - its threads on cores;
  - its queues and inboxes;
  - its OS values (page size, the writable-and-executable rule).
- **Strip, per memory device.** Pages coloured by owner, lineage,
  permission (data, executable, both), size class, and errors (weak
  cells, 5.10 (1)), with the reset region marked. A disk strip shows each
  block's wear.
- **Charts.**
  - Populations of programs and threads.
  - Births and deaths by cause.
  - Power drawn and heat.
  - Wear, failures and visits.
  - Traffic, drops and stranded messages.
  - Labelled disasters.
  - Diversity and novelty.
- **Phylogeny, the genome browser, and the map of life.** These are kept,
  with genomes disassembled. The map of life places genomes by their byte
  patterns, as Nova 1 did with codons.
- **Events.** The facts and the labels as one timeline, filterable, each
  linked to its place on the map.

**(6) Stats (CSV) [a revisable starting point: stats are observer output, (1)].** One row per sample. Columns:

- world totals: programs, threads, births and deaths by cause, power
  drawn, heat, failures, visits, traffic and drops;
- per-place samples, at a slower interval, for the archive the viewer
  reads.

The sample intervals are settings. Nova 1's RunArchive already reads
per-place columns this way.

**Cost:**

- The facts are an append-only log, cheap next to running programs.
- Detectors and charts run on samples, apart from the tick loop where
  possible.
- The parasite check is the one detector that must look at threads
  often. Sampling it (every so many ticks) keeps it cheap, at the cost of
  missing brief hijacks.

**Questions for Damian:**

1. ~~What counts as an individual: (a) a program is everything one owner
   holds, with local copies as threads; or (b) a thread started outside
   the caller's own code allocation becomes a new program that takes that
   allocation (Claude)?~~ Decided 2026-11-11, Damian: (b), a new program.
2. ~~Does the engine log only physical facts, leaving every label to
   observers?~~ Decided 2026-11-11, Damian: yes, with sampled state views, counted frequent facts, and observer memory saved with snapshots.
3. ~~Should lineage be tracked by exact descent from the facts?~~ Decided
   2026-11-11, Damian: by descent, with no similarity matching.
   Genomes are grouped by similarity (decided 2026-11-11, Damian); the
   greedy method in (3) is a revisable starting point.
4. ~~The detectors listed in (4), with a parasite defined as a thread
   running in pages another program owns?~~ Decided 2026-11-11, Damian: the parasite definition. The other detectors are a starting set, redefined freely.
5. ~~The viewer changes in (5): the map with links and overlays, a place
   inspector, a strip for each memory device, and disassembled genomes?~~ Decided 2026-11-11, Damian.

## 6. Working rules

- New features are on by default, not behind an off switch. **[decided]**
  (Damian's standing rule for Nova 1, carried over)
  - **Switches are welcome [decided 2026-11-10, Damian].** "On by default"
    means a switch starts on, not that there are no switches. Switches add
    composability: experiments can turn a law off to see what it does, and
    the law itself stays the same. Where a law has a strength, a sliding
    scale with 0 meaning off is preferred to a separate switch.
  - Claude's note: Nova 1's tests ran on old defaults
    (`Config::set_fallback`), so they tested a different world from the
    one that runs. Nova 2's tests should run the default world, and use
    switches only when testing a law on its own.
- No backward compatibility while in beta: no legacy defaults, no loading old
  files. **[decided]** (the same)
- **No external dependency without Damian's approval first [decided
  2026-11-11, Damian; carried over from Nova 1].** That covers any
  library, package or required installed tool.
  - **Already approved:** the Windows SDK's platform libraries chosen in
    2: Direct2D, DirectWrite, Direct3D 12 with its WARP rasterizer, and
    the system shader compiler Nova 1 used (`d3dcompiler_47`).
  - **Would need asking:** for example, the newer DXC shader compiler,
    which is a separate redistributable rather than part of Windows.
- **C++ written like C with classes [decided 2026-11-11, Damian].**
  Claude's reading, to confirm:
  - structs, classes and free functions;
  - simple templates for containers only, as in Nova 1's `Array<T>`;
  - no exceptions, no RTTI, no inheritance hierarchies, and no template
    metaprogramming.

  This matches the subset the rules must keep anyway to compile as HLSL
  (2).
- **No C++ standard library [decided 2026-11-11, Damian; the same line as
  Nova 1].**
  - Nothing from `std::`: no containers, algorithms, strings or
    iostreams. Containers, the RNG, hashing, config parsing and the test
    harness are hand-rolled, and Nova 1's can be reused (2).
  - Thin C-runtime headers (`cstdint`, `cstddef`, `cstdio`, `cstdlib`,
    `cmath`) and platform headers (`windows.h`, intrinsics) are allowed.
    In Nova 1's words, "the line is no-STL, not no-C-runtime".

## Log

- 2026-11-10: The notes folder is `E:\Projects\nova2` (Damian). Nova 1
  surveyed (`nova1-survey.md`).
- 2026-11-10: Approach decided (Damian): reuse Nova 1's infrastructure where
  it fits (the UI, determinism and snapshots named), design the mechanics
  fresh, design before building. Claude drafted principles, a design order
  and three foundation questions.
- 2026-11-10: Damian asked for critique only. Claude critiqued its own draft
  principles (§3). Damian chose to start with the foundations: Nova 1's
  resource kinds are mostly right, but behaviours must emerge with no
  bootstrap verbs (5.1). Claude set out what that rules out and what it
  leaves open (5.2–5.5).
- 2026-11-10: Damian parked the ops question and set the first principle,
  one physics (3.1). Claude proposed a formulation, a test and its
  consequences.
- 2026-11-10: Damian made power the fundamental resource and driving input,
  with voltage, current and resistance, and day and night from the supply
  varying (5.0). Claude listed what it settles and what is open.
- 2026-11-10: Damian kept voltage, current and resistance, with resistance
  making heat, voltage sagging with path resistance and draw, and burnout
  from too much heat (5.0 point 1). Claude corrected which way resistance
  drives heating.
- 2026-11-10: Damian chose DC, outside users with no reaction to the world,
  and day and night sweeping across the map. He asked for abstraction over
  physical detail. Claude proposed the abstraction rule and set out what
  different power sources would give (5.0 points 2–7).
- 2026-11-10: Damian decided that night lowers the supply, that each device
  has a minimum and a maximum voltage, that the supply swings moderately at
  random, and that sources work by proximity like a fire, with ecosystems
  sprawling. Claude proposed that night and swings lower capacity, not
  voltage, that the falloff is link resistance, and one error law for both
  edges of a device's range. §5.0 was restructured into the model so far
  and what is open.
- 2026-11-10: Damian confirmed that resistance sets the falloff, that bigger
  devices need more power, that swings should reach the edges of a zone
  but not its core, and suggested that crowds could close in on a source.
  He questioned errors as mutations. Claude proposed errors as transient
  wrong results that become permanent only when written into code, showed
  that closing in follows from Ohm's law if a source is soft, and set out
  four options for burnout.
- 2026-11-10: Damian agreed that errors are transient unless written into
  code, asked for real device design to be followed, and said devices and
  sources make their own heat and that all four burnout options may be
  needed. Claude added real device figures (5.0a), withdrew its "size sets
  voltage" opinion, explained source stiffness, and raised regulators as an
  open conflict between real design and gradients.
- 2026-11-10: Damian: devices get both, speed that follows voltage within
  their limits and errors at the edges. Claude corrected its misleading
  wording, quantified the speed curve, suggested two limits rather than
  real chips' three, and proposed P and E cores as one kind.
- 2026-11-10: Damian chose no regulators (error-prone edges, then hard
  cut-offs), no firmware (programs will clock devices through hardware
  interrupts), and two limits. Claude showed that stiffness is the internal
  resistance, not a fourth number for a source, asked for hysteresis at the
  cut-offs, corrected "an E core is just a smaller P core", and noted that
  overclocking would let programs control their own mutation rate.
- 2026-11-10: Damian: no breakers; the high end cuts out on heat with a
  cool-down, and the low end is a hard cut-off. Nova 1's real problem was
  half life-form, half program; complexity itself is good. Claude explained
  why only the low end needs a restart gap, proposed core designs as rows
  of one table, and raised what "streamlined" means and whether the
  guardrail stands.
- 2026-11-10: Damian: restart gaps at both ends (heat at the top, voltage
  at the bottom). What gets streamlined is the hardware and how software
  interacts with it. Disasters stay, rooted in real hardware and power. A
  catalogue fixed by type with ranges within each type. Memory fades at
  low voltage, faster when hot, while a disk keeps its contents. A new
  principle: every decision is a trade-off (3.2). Claude disagreed that
  complexity in rules drives emergence, mapped Nova 1's disasters onto the
  power model, and proposed the trade-off principle as the test for rules.
- 2026-11-10: Damian agreed the catalogue's trade-off curves and both
  memory behaviours. Disasters were deferred to Environment, mostly
  surges, drops and storms. On complexity he agreed, noting that
  abstraction is unavoidable and nothing we build will approach real
  life. Claude proposed "variety in things, few laws" (3.1).
- 2026-11-10: Damian: stand-in rules are needed because we work above the
  lowest level; each must be well thought out. Claude placed stand-in rules
  on the hardware side only. Damian asked to stay on power: heat, and how
  power sources work. Claude drafted heat (5.0b) and power sources (5.0c).
- 2026-11-10: Damian agreed the heat model and kept heat spreading between
  nodes. For sources: spawned by chance per tick, a catalogue of types,
  lifetimes on a seeded schedule (b), no wear on the infrastructure, and
  outages from outside. Every number is a setting. Claude dropped its cost
  objection to spreading heat, proposed a strength of 0 in place of a
  toggle, plain-terms spawn settings, two of the three spreads, and tiers
  of settings, and asked whether settings can change during a run.
- 2026-11-10: Damian: switches are welcome for experiments ("on by default"
  means initially on); a sliding scale for heat spreading. A starting
  number of sources, with a rule creating and retiring them. Luck is part
  of the world. Voltage and resistance spreads. Fluctuation frequency and
  size are settings. Settings are fixed for a run. He prefers ranges per
  device type to named models. Claude withdrew its "one physics into two"
  argument and proposed drawing ranges along each type's trade-off curve.
- 2026-11-10: Damian agreed ranges along trade-off curves. Claude listed the
  remaining power items: P and E as one type, link resistance, and node
  cooling.
- 2026-11-10: Damian doubted that one curve covers P and E cores, so Claude
  withdrew the merge and set out two options. He questioned reusing Nova
  1's node layout, so link resistance waits for Space, with what power
  needs from it listed. The world starts cold and warms gradually from
  heat generators, with better cooling near water.
- 2026-11-10: Damian chose P and E as two types. Worried about a
  Venus-like runaway, he asked for ambient heat loss and heat sinks.
  Claude showed that cooling bounds the world's heat (there's no global
  runaway, since nothing lowers cooling), and proposed heat sinks as
  high-cooling places placed independently of sources.
- 2026-11-10: Damian adopted heat sinks, independent of sources, coming and
  going, and proposed that operators place them where heat needs them.
  Claude flagged that this reacts to the world (an exception to 3.1 and to
  5.0 point 6), that it's not needed for safety, and that it damps the
  pressure where life gathers. Claude offered scheduled sinks at
  geographic places instead.
- 2026-11-10: Damian settled it: heat sinks are generated once from the
  seed and stay fixed, like water. Claude agreed: the dynamics lost are
  small because the sources still move.
- 2026-11-10: Damian started on the environment: he liked Nova 1's
  landscape and substrates, but found the 1D memory strip limiting. Claude
  set out the real hierarchy of distance, three options within a place,
  and the choices between places, and proposed substrates as hardware
  biomes (5.7).
- 2026-11-10: Damian asked whether (b) means fixed hardware that programs
  share. Claude: hardware is fixed, but ownership is a separate question,
  and proposed that programs own specific things rather than amounts.
  Claude flagged that fixed hardware can't travel, which conflicts with
  the 5.6 complaint.
- 2026-11-10: Damian accepted fixed hardware, with movement through
  network operations (copy or move, routed over links), allocation of
  memory and disk by request, and an OS-like layer. He raised the problem
  that a program that isn't running can't ask to run. Claude: requests
  persist, the scheduler is a law, and every control needs a cost. The
  crux is that real isolation brings back Nova 1's private code, with
  three options set out (5.8).
- 2026-11-10: Damian proposed breaking isolation through exploits (pointer
  overruns). Claude explained how real exploits cross between programs
  (per-page protection, and victims overrunning their own buffers with an
  attacker's data), and proposed pages, injection through input, and
  physical faults as Nova 2 physics, with out-of-page access faulting.
- 2026-11-10: Damian adopted all three: shared pages, injection through
  input, and physical faults. Slow emergence of predation is acceptable.
  5.2 is settled by 5.8. Claude noted that injection through input needs
  communication to pay first.
- 2026-11-10: Damian asked for a recommendation on out-of-page access.
  Claude: a fault is an interrupt, so the thread stops unless it has set a
  handler, and a handler costs a trap. Damian decided on an OS per node,
  working like a real scheduler, reached through interrupts. Claude
  proposed conditions: a neutral OS, a fair scheduler, threads costing
  memory, no free priority, calls costing time, per-turn clocks, and OS
  settings varying by node.
- 2026-11-10: Damian agreed the handler, the fair scheduler and the cost
  of calls, and asked for more on neutrality, priority, clocks and
  variety. Claude expanded on each: two questions for neutrality, why no
  one should set priority, per-thread clocks as in real QoS hints (but
  not on GPUs), and variety limited to page size, perhaps turn length.
- 2026-11-10: Damian agreed all four expansions.
- 2026-11-10: Where life starts (5.4). Damian proposed static in memory as
  the spark, with executable and data memory. Claude: fold static into the
  memory law (random start, lost bits read random, a small floor), make
  executable a page permission, and add a way for code to run: a reset page
  on idle cores. Whether life starts at all rests on the instruction set.
- 2026-11-10: Damian dropped static as its own law, suggested a "Fixed" or
  "OS chooses" setting for executable pages, leaned to the reset page, and
  proposed an x86 subset with interrupts (new 5.9). Claude: survivors leave
  by themselves, not by the OS; make the reset pages one shared soup; calls
  through INT; registers per thread; one address space per node.
- 2026-11-10: Damian agreed the memory static (i)–(iii) and the
  executable setting, with each node's OS having its own values. Claude
  proposed drawing those from smooth fields, fixed for the run; explained
  a birth step by step, register saving, DOS-style calls and the shared
  address space; and proposed a subset list, an OS call table and GPU
  lanes running the same subset in lockstep, without INT.
- 2026-11-10: Damian: OS values are fixed at boot; every field needs smooth
  gradients and steep drop-offs; static as bit flips per tick; a
  TIS-100-like set instead of x86; the NPU through OS calls. Claude
  proposed shared region borders, a one-shared-reset-region design on CPU
  cores, a per-bit static rate (4 flips per page per tick would destroy
  life), one SYS instruction, a one-byte draft set, and GPUs acting
  through memory.
- 2026-11-10: Damian chose one noise algorithm, seeded per field, with
  power following from resistance; rare static like Nova 1's cosmic rays,
  rate later; GPU and NPU agreed. Claude proposed the two-layer noise,
  two calls giving every waiting style, and a 16-instruction TIS-100-like
  draft with a 12-byte replicator.
- 2026-11-10: Damian agreed the noise and the rough-places consequence,
  and asked for AND, OR, XOR, NOT and shifts. Claude explained the
  accumulator operand and proposed 22 instructions in a 5-bit + 3-bit
  byte, with 10 spare no-op slots and a 14-byte replicator.
- 2026-11-10: Damian agreed the 22-instruction set.
- 2026-11-10: Damian questioned the cost of syscalls and whether reads cost
  more than writes. Claude corrected its own number: a bare syscall crossing
  is ~150–500 cycles, not ~2,000 (that's a page fault or context switch),
  and split SYS into a cheap crossing plus the service's work, so locality
  still matters. Read/write kept symmetric for now. Costs drafted, proposed.
- 2026-11-10: Damian confirmed the instruction costs. (The OS call table
  reply was stopped twice by a safety filter on Opus 5.5; switched to Opus
  4.8 for it.)
- 2026-11-10: Claude drafted the OS call table (§5.9): 16 calls, every SYS
  valid (modulo), calls fail soft instead of faulting, none a bootstrap.
  Open: SEND addressing, numbering order, service costs, and what becomes
  of a dead program's pages.
- 2026-11-10: Damian: on a GPU, EXIT ends one lane (the launch returns when
  all exit); GLAUNCH stays its own call. GPU execution model written into
  §5.9.
- 2026-11-10: The four open call-table items. SEND names any node, the
  network routes, latency is hop-based (decided); travel left open (Claude
  leans a MIGRATE call). Numbering → a setting plus a test. Service costs
  anchored to real cycles, with core-budget vs device-latency split. Dead
  pages left as a corpse, memory never auto-zeroed (decided), resolving the
  5.4 zeroing question.
- 2026-11-10: Travel. Damian: SEND a program into the destination's reset
  region; it wakes if a core and memory are free. Claude agreed, over its
  own MIGRATE idea — cleaner on 5.1, and it unifies travel with
  abiogenesis. MIGRATE dropped. Numbering is one whole-simulation setting.
  Service costs agreed for now. Open: whether the reset region is also the
  comms inbox.
- 2026-11-10: Damian kept the reset region and the comms inbox separate
  (SEND deposits, RECV reads — different acts). Claude noted the payoff: two
  spread strategies, colonisation (reset region, empty nodes) and injection
  (comms RECV overflow, occupied nodes). Ports proposed for addressing.
- 2026-11-10: Ports adopted — count a setting (default 16), port 0 the
  reset region, other ports private comms channels. Damian: a port-0
  arrival should schedule a core promptly so static can't corrupt it and
  big programs aren't penalised. Claude's mechanism: run on the next free
  core from the payload's first byte, no preempting live threads; the
  migrant still ALLOCs its own space, so a full node turns it away. Settles
  fixed-vs-scattered; migration now reliable where there's capacity.
- 2026-11-10: Damian: migrants run on spare cores only, never preempting a
  live thread. The OS call table is now settled bar the service-cost
  numbers (tunable). Next: the map between places (5.7), which SEND's hops
  and the barriers depend on.
- 2026-11-10: The map between places (5.7). Claude moved "Between places",
  the hardware biomes and "Gradients and drop-offs" from 5.8 back into
  5.7. Analysis: hop counts grow with the square root of size on a 2D map
  and with the logarithm in a data-centre tree, which decides whether
  distance and barriers mean anything. The data-centre layout was rejected
  between places (it contradicts the sweep, sprawl, coasts and fields) and
  kept inside a place. A grid was offered as a test world only. Claude
  recommended Nova 1's landscape, rebuilt: a density field and the Gabriel
  rule in place of seven link settings, fixed bridges that carry data but
  not power, lengths in link units, sources landing by area, and adding
  length to SEND's delay (a revisit, Damian's call). Five questions open.
- 2026-11-10: Damian answered the five map questions. The landscape
  rebuilt, with a grid as a test world and no data-centre layer between
  places, accepted provisionally: he'll have to see it in action. Density
  field and Gabriel rule chosen over Nova 1's generator. Bridges carry
  data, not power: Damian said power was always wireless, a proximity
  cloud with a resistance falloff. Length added to SEND's delay (revising
  5.9). Sources land by area for now, perhaps as a switch. Claude read
  "wireless" as a network over proximity links, not a distance law (which
  5.0 point 5 ruled out), giving two link sets from one geometry: power
  and heat over proximity links that may cross water, data over each
  landmass's links plus bridges. Proposed a water slider and a
  source-area power in place of switches. Open: confirm the reading.
- 2026-11-10: Damian confirmed power as a network over proximity links
  (not a distance law), the two link sets, and data links as proposed.
  Water is a heat sink: heat doesn't cross it. Water should conduct power
  as in real life, better than air, but with a falloff, since a live cable
  in the sea doesn't electrify the ocean. Claude: better conduction alone
  gives less falloff, not more, so the falloff must come from a leak to
  the seabed, as in real water. The catch is that water can't carry power
  across without draining the shores, so straits passing useful power and
  coasts draining little can't both hold. Proposed water as a leaky
  conductor with two numbers, a reach and a shore drain, defaulting to a
  mild drain and a one-link reach, replacing the water slider. Open: the
  drain's weight.
- 2026-11-10: Damian agreed to water's defaults: a mild shore drain (about
  one small device's draw) and a reach of one link length.
- 2026-11-11: Damian asked what "by area" and "by place" mean for
  sources. Claude explained catchments with a worked example and the real
  anchor (sunlight and wind fall by area). Damian accepted the water
  approximation. On edge strength, Claude proposed reading the resistance
  field's step contours as grid borders on the links that cross them,
  because a step in values only makes plateaus. Proposed defaults: a
  border costs about one band's width in links (10 as a placeholder) and
  borders sit 20–30 links apart. Noted that a border divides the supply,
  not the population, since SEND crosses it.
- 2026-11-11: Damian chose sources by area: an even spread of sources,
  with places denser in and near towns. Claude noted that this is even on
  average and random in detail, as the luck decision wants. Grid borders
  in the resistance field adopted as proposed (about one band's width per
  crossing, borders 20–30 links apart), and Damian accepted that a border
  divides the supply rather than the population. "Between places" is
  settled apart from the source-area power as a setting.
- 2026-11-11: Sensing (5.8). Damian: sensing is local, knowledge of far
  places must be communicated (something an information-age civilisation
  would invent), and travel is blind beyond the neighbours. Claude agreed
  (it gives reading input a reason to pay), warned that unrewarded
  long-range signalling is rare in artificial life, noted that any
  message is already a weak report of a live sender, and proposed
  shuffled node ids, keeping SEND's timing as a ping, and SENSE reading
  neighbours' voltage, temperature, idle cores and free memory for one
  hop of network time.
- 2026-11-11: Within a place (5.7), expanded at Damian's request. Claude
  split "near" into four jobs: access cost (by device; drop NUMA, since the
  scheduler would make it luck), contact (address order, the one real
  survivor of Nova 1's strip), shared fate (the memory device, with ALLOC
  choosing a device), and neighbours (the allocator; size classes
  recommended, which cluster kin and make size mimicry pay, as in heap
  feng shui). Sizes to be scaled to programs, not real machines. 2D grid
  rejected. Four questions open.
- 2026-11-11: Damian agreed the leak fixes (shuffled ids, timing kept),
  the neighbour readings, all four within-a-place points with the
  allocator as part of the OS, size classes, and sizes scaled to programs
  as settings. He asked what the cross-socket cost is, what argument ALLOC
  would take, and why sizes wait. Claude explained NUMA, proposed a device
  argument ordered by speed (0 any, 1 fastest, −1 slowest, clamped at the
  ends) so a choice means the same at every place, with SENSE per device,
  and explained that evolved program sizes are unknown until test runs.
  Open: the device argument's form, and the size defaults.
- 2026-11-11: Damian agreed NUMA's removal, the speed-ordered device
  argument, and leaving the size defaults to the first test runs. He
  added that NUMA makes little sense with a shared memory pool. Claude
  noted that real NUMA machines share one address space too, so the
  reason recorded stays the scheduler making the cost luck. "Within a
  place" is settled apart from the size defaults.
- 2026-11-11: A pass over open items, at Damian's request. Closed with
  pointers where a later decision clearly settles them: the supply as a
  boundary condition, one voltage level, source numbers and sweep-
  following (5.0c), heat-only sources, cooling's spread, library ops (5.1),
  5.4 answers 2 and 3 and the core start spot, the 5.6 complaint, routing,
  OS allocation, the 5.8 scheduler, priority, call cost, GPU clock,
  OS-variety and fault-handler items, the isolation crux, the NPU flag,
  substrates as biomes, and the 5.7, 5.8 and 5.9 headers. Proposed for
  Damian: who gets the power (nobody is given it; it follows device time),
  the form of resources (specific allocations, nothing held for time-shared
  devices), archive (keep or drop), (a) for where life starts, and freeing
  a reset-region birth with no thread. Two conflicts found: PROTECT on
  pages that size classes share, and 5.9's +4/+12 memory add-on measuring a
  distance 5.7 dropped. Fixes proposed for both.
- 2026-11-11: Damian's answers to the pass. Decided: nobody is given power
  (5.0), and he added that programs spend it by using more cores,
  overclocking and heat, while power stays hidden from programs as in real
  life, so the risk is accepted. Also decided: life starts as random static
  in the reset region (5.4 (a)); a reset-region birth with no thread has
  its pages freed; signals become SEND and RECV and bandwidth a property of
  links (5.3); and the memory add-on becomes a per-device latency of about
  4 to 12 cycles (5.9). Damian's "No" on 5.4 is read as dropping the
  seeded-ancestor setting, to confirm. Claude proposed a test-only hook
  instead. He asked whether stealing memory needs an injected FREE. Claude:
  FREE works only on your own memory. Theft is running as the victim, or
  killing it and claiming its freed size-class gap (5.3). He asked what
  PROTECT is; Claude explained it, and the fix stays proposed. He didn't
  know what Nova 1's archive was. Claude looked it up: it was a slow,
  durable second disk. Claude proposed folding it into the slow end of
  disk's curve. Found two disk gaps: no disk allocation call, and nothing
  reads a disk back after an outage. A boot block was proposed for the
  second. Burnout moves to disasters, the next topic.
- 2026-11-11: Damian accepted the PROTECT fix and folding the archive into
  the slow end of disk. He dropped seeded ancestors completely, the
  test-only hook included, so the origin test gates everything else (5.4).
  Disk is allocated the way memory is (5.3): ALLOC's disk permission, the
  device argument among disks, blocks and size classes, protection per
  block, FREE with a permission, and freed bytes kept. 5.9's ALLOC, FREE,
  DREAD and DWRITE rows were updated. On outages he worried that a boot
  sector which must run a loader would be too much selective pressure,
  but saw no other reasonable way. Claude proposed booting as migration
  from the node's own past: on a cold start the boot block is delivered to
  port 0 like an arrival, so any program that can migrate can boot with
  one DWRITE, and only programs bigger than the block need a loader. The
  outage regime, not the boot block, sets the pressure (dispersal against
  dormancy; Cohen 1966, Venable and Brown 1988). Details proposed: one
  unowned boot block per node on its most enduring disk; a cold start
  triggered by the reset region's memory losing its contents; disk starting
  as static. Hibernation and a restart-me call were rejected.
- 2026-11-11: Damian accepted the boot block as migration from the node's
  own past, with its details (5.3). Disasters next, at his request. Claude
  wrote 5.10. It maps Nova 1's five scheduled disasters (59 settings) onto
  existing laws: brownouts are sag and source dips, crashes are cold
  starts, disk failure is decided wear, surges are load rejection
  (emergent cascades) plus strikes, and warnings come from the laws. It
  proposes two laws for the rest. Damage is one wear law, driven by heat
  (Arrhenius), overvoltage and disk writes; it narrows a device's range
  until it fails, and burnout is its extreme. Restoring is maintenance
  visits at seeded times that replace failed devices from the place's
  fields, with the interval as a sliding scale. These cover all four of
  5.0's burnout options with two laws. Strikes are a source type, and
  storms are moving patches where they appear. Questions: links wearing
  too (Claude: yes), and SENSE reading disk wear (as SMART does). About 11
  settings.
- 2026-11-11: Damian agreed the wear law (5.10 (1)). He asked for more on
  maintenance visits. Claude expanded 5.10 (2): how a visit runs step by
  step, what the interval does (the share of time dead is about wait ÷
  (life + wait), so scars follow stress), why visits beat a per-failure
  delay, random against regular gaps, one interval against a density
  field, and what visits don't do (no early replacement, no upgrades, no
  sources, links if they wear). Damian's original idea was two failure
  states per device, outage or crash and surge. Claude: both are the two
  ends of every device's voltage range, already in the laws. The one gap
  is what a device loses when it cuts out. 5.0a's "night puts programs to
  sleep" assumed a running thread is saved, which real hardware can't do.
  Proposed in 5.10 (7): work in flight is lost. The thread on a core
  crashes, while queued and sleeping threads survive. This rebuilds Nova
  1's crash share from crowding and rewards sensing voltage and sleeping
  in time. Its risk is early single-threaded life (an Allee effect), and
  the fallback is a pause.
- 2026-11-11: Damian accepted maintenance visits and both optional
  extras: a regularity slider and an interval tied to the density field.
  He said visits mirror his own idea of places dying and respawning
  elsewhere with new links. Claude proposed that only dead places respawn
  (2b), with a share-failed slider defaulting to 1, so no living program
  is wiped. He said "device" means all hardware, sources included, so 5.10
  (7) gained Source and Link rows, and sources gain voltage swells (IEEE
  1159), revising 5.0 point 6. He asked for network outages, regional
  brownouts and widespread spikes. Claude folded storms into one mechanism,
  a catalogue of seeded patches (storm, swell, brownout, heat wave,
  network outage; 5.10 (4)). Links fail by wear, by seeded cuts that grow
  with length, and by outage patches, and are repaired at visits. Claude
  recommends routing around a failed link (5.10 (5)). On mutation: cosmic
  rays are already decided as the memory floor, with classic flips and no
  NOP shielding. Travel corruption is already decided; UDP fits, with the
  checksum off as IPv4 allows. Copy corruption is proposed as a small floor
  on wrong results for every device, anchored on silent data corruption
  (Meta, Google 2021), rather than a rule on copying (5.10 (8)). About 28
  settings, against Nova 1's 59.
- 2026-11-11: Damian: every kind of device needs wear. A disk's sectors
  corrupt after enough writes, memory slowly makes errors, and CPUs, GPUs
  and NPUs get slower. He asked whether repairs should cover this too.
  Claude noted that wear was already per device; what it lacked was a
  different effect for each kind. Claude proposed one mechanism: wear
  raises a device's threshold voltage (NBTI and hot-carrier ageing;
  Intel's 2024 Raptor Lake Vmin shift). That gives slower processors at
  the same voltage (the alpha-power law) and memory errors at fixed spots,
  as the leakiest cells (decided per-cell leak) go first. Disks wear per
  block by writes, with no wear levelling, so the most-written blocks
  (the boot block among them) corrupt first. Links wear by rising
  resistance. Sources don't wear. "Narrows the range at both edges" was
  revised to "raises the low edge". Visits gain a replacement threshold
  slider: worn devices are replaced like for like with contents copied
  (memory sparing, RAID rebuild). That removes the earlier objection to
  early replacement. Default 1, failed only.
- 2026-11-11: Damian decided respawning places ("a happy medium between
  both"; the share defaults to 1), regional events, source swells, network
  outages with routing around failures, the mutation sources (including the
  error floor), and crashes for threads running on a core when it loses
  power. He rejected SENSE reading disk wear: if anything handles wear,
  it's the OS. So wear levelling is an OS toggle between a modern SSD and
  cheap flash, defaulting to cheap flash. Claude noted that real SSDs level
  wear in firmware, but Nova 2 has no firmware. Linux levels raw flash in
  software (UBI, JFFS2), so the OS is a real home for it. Levelling's own
  copies count as writes (write amplification). Still open in 5.10: wear
  raising the threshold voltage per kind of device ((1)), the replacement
  threshold for worn devices ((2)), and where the reset region goes if its
  memory device fails.
- 2026-11-11: Damian accepted wear raising each device's threshold
  voltage, so processors slow, memory errs at fixed spots, disk blocks
  corrupt and links' resistance rises. He also accepted visits replacing
  worn devices past a threshold, like for like with their contents copied
  (default 1, failed only). 5.10 is decided apart from where the reset
  region goes if its memory device fails.
- 2026-11-11: Damian accepted the reset region's fallback: it lives on
  the most enduring memory device and moves to the next if that fails.
  He then asked for detectable symptoms before large events such as power
  outages, so programs can react. Claude audited the large events. Source
  drop-outs had no warning, and the patches' ramps had no set length.
  Claude proposed 5.10 (6b): every outside event ramps up over a drawn
  lead time that stands out from normal swings, with a false-alarm share.
  The ramp is the event itself starting gently, not a scripted symptom.
  Real anchor: Tokyo's 1987 voltage collapse, about twenty minutes of
  sinking voltage on a hot day. Claude also proposed a low-voltage
  interrupt as a new SETHANDLER kind (PDP-11 power-fail trap, ACPI, UPS
  signals). It lets a thread SLEEP before a cut-out without polling.
  About 33 disaster settings.
- 2026-11-11: Damian accepted both symptom proposals (5.10 (6b)). Every
  outside event ramps up over a drawn lead time, with a false-alarm share.
  A low-voltage interrupt joins SETHANDLER's kinds (5.9 updated). 5.10,
  disasters, is decided in full, at about 33 settings against Nova 1's 59.
- 2026-11-11: Transit, at Damian's request. Claude wrote the transit
  analysis into 5.8. (1) Each link has a bandwidth from its own field,
  decoupled from resistance; the alternative is one shared thickness.
  (2) Delay's size term becomes size ÷ the narrowest bandwidth on the
  route, plus time in queues. (3) Each link end has a fixed queue with
  silent tail drops. Routes adapt only to failures. Big messages drop
  first, as fragmented UDP datagrams do. (4) Each place has one network
  device in the catalogue, router and interface in one, under the usual
  device laws. It must be on to send, receive or forward, so blackouts
  become network holes (Maria 2017, Texas 2021). It draws a constant
  amount, plus a little per byte at the two ends. (5) Each port has a
  fixed inbox in OS-held memory, dropping arrivals when full. (6) SEND's
  sleep includes the queues along the route at sending, so it shows
  congestion. It returns ok with no delivery report, and fails only when
  the destination is unreachable (as ICMP reports) or the sender's own
  exit is full. About 4 new settings.
- 2026-11-11: Damian's transit answers. Link bandwidth varies ("links
  should vary", read as its own field). A full queue returns an error to
  the sender, "just like email". The network device at each place is
  decided. A full inbox returns an error: "nothing gets dropped". He asked
  for more on SEND's feedback. Claude rewrote the transit section: every
  refusal the network knows about reaches the sender, and only corruption
  stays silent. That replaces the UDP-style silent loss in 5.10 (5) and
  (8), and SEND's row in 5.9. Proposed for (6): (a) SEND waits for the
  outcome and returns ok or one of four reasons (unreachable, busy, full,
  lost). The timing still works as a ping, and nothing new is needed.
  Delivery confirmation becomes cheap, but each message in flight holds a
  sleeping thread. Refusals leak a little about far places. The
  alternative is (b), email's own fire-and-forget with bounces as a later
  interrupt, but that breaks the decided ping timing. Claude recommends
  (a). Bounces travel outside the queues. Port 0 never refuses.
- 2026-11-11: Damian chose fire and forget for SEND over waiting for the
  outcome, for its simplicity, and accepts the obstacle to evolution: the
  receiver can reply if it wants. Claude agreed, citing the end-to-end
  argument (Saltzer, Reed and Clark, 1984): corruption is already silent,
  so only the receiver can confirm delivery anyway. Of the two forms,
  Damian took (A), pure fire and forget, over (B), bounces later. He
  clarified that the only things dropped are newcomers that meet a full
  queue or inbox, never messages already waiting (tail drop). His earlier
  "an error is returned" for (3) and (5) is withdrawn.
  - SEND now returns at once, costing the crossing plus a per-byte copy (a
    core-budget call). It fails only for local reasons: no route, own exit
    full, or own network device off.
  - Beyond the sender's own exit nothing comes back. The free ping goes,
    which makes "travel starts blind" stronger; distance comes from
    partners' replies or from neighbours.
  - Data on a link that fails is still lost, as decided in 5.10 (5), now
    silently.
  - The 5.9 SEND row and service costs, the 5.8 timing note, and 5.10 (5)
    and (8) were updated.
- 2026-11-11: Damian decided that data on a failing link reroutes rather
  than being lost, and suggested PING as a network-level command. Claude
  wrote two new subsections in 5.8, "Transit":
  - (7) Rerouting. Each place keeps a message until the next place has all
    of it, as link-layer retransmission and DTN custody transfer (RFC 5050)
    do. Messages queued for a failed link move to the new route's queue
    ahead of new arrivals. With no route left, a message waits for up to a
    lifetime (a setting; 0 means drop at once, as IP does), as a proposal.
    Messages held by a place whose network device goes dark are still lost
    (5.10 (7)). Outages become slowdowns and partitions become waits; only
    blackouts lose data.
  - (8) PING, proposed as call 16: node and timeout in, round-trip ticks or
    −1 out. The destination's OS answers if its network device is on. The
    request and reply go through the same queues, so PING shows congestion
    and times out when a queue is full. It reveals reachability, power and
    distance, not resources. Claude prefers it to an echo port (RFC 862),
    which would be much harder to evolve.
  - Updated: the 5.9 table (SEND row, PING row), the service costs, the
    5.8 timing note, and 5.10 (5), (7) and (8).
- 2026-11-11: Damian accepted PING as a call, the 17th (5.9 updated), and
  stranded messages waiting up to a lifetime when no route remains (a
  setting; 0 means drop at once). 5.8 is decided in full, transit
  included.
- 2026-11-11: Damian confirmed the resource-form closure (5.3).
  Memory and disk are owned allocations. Cores, the GPU and the NPU are
  shared in time. Power is drawn. Resources change hands only by being
  freed and claimed again, or by hijacking. Overwriting doesn't transfer
  ownership. 5.3 is decided in full.
- 2026-11-11: A pass over 5.0's six stale [proposed] labels, at Damian's
  request. All were settled by later decisions: the abstraction rule (used
  throughout), a source as two numbers (5.0c), night and swings (drops on
  capacity, swells on voltage), source drift (5.0c), life forms bands (a
  consequence, not a rule), and resource kinds as ways of using power
  (5.0a, P and E, 5.3). 5.0 is now headed decided. The transit bullet in
  5.8 is marked decided too. About 45 other [proposed] labels remain
  elsewhere, many already superseded.
- 2026-11-11: A pass over the remaining [proposed] labels, at Damian's
  request. The legend gains [settled] (answered by a later decision, which
  it names) and [superseded].
  - 37 were settled by later decisions, each now naming the decision.
    Among them: the 3.1 and 3.2 tests, two temperatures, leakage, heat
    sinks, the spark and birth steps, the OS call table, the GPU, and
    waiting styles.
  - 2 were superseded: "P and E may be one kind" and the x86 subset. So
    was one bullet in 3.1's consequences (5.2 leaning towards (b) or (c)).
  - 2 were deferred to building: the reuse of Nova 1's infrastructure,
    and the exact two-layer noise method.
  - 1 needs Damian: the source-area power as a slider (1 by area, 0 by
    place) rather than a toggle (5.7).
- 2026-11-11: Damian chose a slider for source-area power (1 by area,
  the default; 0 by place; above 1 favours the countryside more). 5.7 is
  decided apart from the size defaults, which wait on test runs. Nothing
  in the design now waits on Damian except numbers that need runs.
- 2026-11-11: The hardware catalogue, at Damian's request. Claude wrote
  5.11. It sets out the common row every device states (count, position on
  its curve, voltage range, speed against voltage, draw, heat, leakage,
  errors, wear, what is lost at a cut-out). It gives each kind's row and
  curve: P core, E core, GPU, NPU, memory, disk, network device and
  cooling device, plus links, bridges and source types. It also records
  how places get hardware. Proposed:
  - how SETCLOCK and speed-follows-voltage combine: SETCLOCK scales the
    stock speed in steps, with errors and draw above stock, and wear
    lowers stock;
  - cooling devices: passive and driven by temperature (thermistor fans),
    with no call, rather than a commons call;
  - gaps: minimum counts per place (a core, memory, disk and network
    device; GPU, NPU and cooling optional), the speed curve (linear above
    the threshold, rather than the alpha-power law), heat limits from real
    parts, default ratios between kinds, and units for wear.
- 2026-11-11: Damian accepted all five catalogue proposals (5.11):
  - minimum counts per place (a core, memory, disk and network device;
    GPU, NPU and cooling optional);
  - SETCLOCK scaling the stock speed in steps, with errors and draw above
    stock, and wear lowering stock;
  - speed linear above the threshold;
  - passive, temperature-driven cooling devices;
  - heat limits and default ratios from real parts.
  The catalogue is decided.
- 2026-11-11: Observation, at Damian's request. Claude wrote 5.12.
  Principles: watching changes nothing; labels are observers' (3.1); the
  engine logs only physical facts (failures, cuts, respawns, patches,
  boots, program creation and end with cause, thread starts, port-0
  sends, boot-block writers). A gap was found first: the design never
  said what an individual is. SPAWN makes threads of the same program, so
  local copies form one colony with no turnover and a shared fate. Claude
  proposed (b): a thread started outside the caller's own code allocation
  becomes a new program that takes that allocation, decided by place
  rather than by a register flag, because the decided 14-byte replicator
  never sets C. This revises 5.3's ways of changing ownership. Also
  proposed: genomes as the bytes of the start allocation, hashed;
  lineage by exact descent from the facts, with similarity as a fallback;
  detectors including parasite (a thread running in pages another
  program owns); deaths by cause; viewer changes (map with links and
  overlays, a place inspector, per-device strips, disassembled genomes);
  CSV columns.
- 2026-11-11: Damian asked what tracking lineage by exact descent means.
  Claude explained: the parent is recorded at birth, from the physical
  act, rather than inferred later from similar code. Claude also
  corrected its own wording: corruption in transit doesn't break the
  link, because the sender is still known. Maker and code differ only in
  scavenging, hybrids and parasite code riding in hosts. Damian decided
  descent, with no similarity matching. The mismatched cases are left to
  the detectors (scavenger, parasite).
- 2026-11-11: Damian: genomes should be grouped by similarity (read from
  dictated "Gmail"). Recorded in 5.12 (3): descent gives the tree (who
  came from whom), and similarity groups give species-like clusters
  (which genomes are alike), kept side by side. A child in a different
  group from its parent flags a big jump. Method proposed: greedy
  clustering against each group's first genome, by the share of 3-byte
  runs in common, with the threshold as a setting (anchors: 97% and 95%
  similarity conventions in microbiology; CD-HIT and UCLUST).
- 2026-11-11: Damian chose (b) for the individual. A thread started
  outside the caller's own running-code allocation becomes a new program
  that takes that allocation, and one started inside it is a thread of
  the same program. Copies made by ALLOC, CPY and SPAWN are therefore
  separate programs with their own deaths and memory turnover.
  Ownership in 5.3 gains a third way to change hands, a gift at SPAWN.
  The 5.9 SPAWN row is updated.
- 2026-11-11: Damian asked to discuss facts-only logging, then agreed.
  The engine records only physical facts: device, link, place, source,
  patch and boot transitions, and program creation and end with parent,
  cause and genome hash. Frequent facts are counted per sample.
  Observers also read sampled read-only state views, because the parasite
  test is a state, not an event. Observer memory is saved beside
  snapshots, outside the hash, so the family tree survives a resume.
  Rationale: 3.1's test, labels can be redefined offline, observer bugs
  can't corrupt the world, and emergent disasters can only be found by
  watching. Anchors: monitoring and detection rules over raw logs;
  Avida's analysis mode.
- 2026-11-11: Damian accepted the parasite definition: a thread running
  code in pages another program owns, found in the sampled state views.
  The other detectors in 5.12 (4) stand as a starting set. They can be
  revised at any time, because labels belong to observers. Only the
  viewer changes remain open in 5.12.
- 2026-11-11: Damian accepted the viewer changes (5.12 (5)), so 5.12,
  Observation, is decided. The remaining observer-side methods (genome
  grouping, the other detectors, the CSV columns) are revisable starting
  points, because labels and stats belong to observers. Design order step
  8 is done; what's left to decide is test numbers, and step 9,
  restating the principles.
- 2026-11-11: Design order step 9, at Damian's request. Claude restated
  the principles as 3.0: twenty principles in five groups (what the world
  is, how a rule earns its place, what programs can do, events and
  observation, settings and engineering), each pointing to where it's
  decided and used. New as principles: boundary conditions (3), real
  anchors with the abstraction rule (9), realism and simplicity over
  ease of evolution, tested (11), random code stumbling on (13), and
  settings meaning the same at any size (19). The section also says what
  became of Claude's first draft (3.3); "nothing grants permanence" was
  dropped in favour of costs. Damian is asked to confirm the list and
  the wording of 11.
- 2026-11-11: Damian adopted the twenty restated principles (3.0),
  including 11 as worded. Every step of the design order is now done. What
  remains open needs test runs: the origin test (5.5), the size defaults
  (5.7), the service and instruction-fetch costs (5.9), and the tuning of
  disaster rates and catalogue numbers.
- 2026-11-11: Damian raised two architecture points.
  - **The UI is rendered with DirectX for antialiasing (decided).** Claude
    specified Direct2D and DirectWrite. Only Nova 1's canvas class (about
    350 lines) is rewritten, and it gains coordinates in fractions of a
    pixel. Tests render through WARP so their pixels are identical.
  - **Nodes in parallel, possibly on the GPU, to beat Nova 1's 30 ticks a
    second (proposed).** Claude answered that per-node parallelism already
    exists on CPU threads: Nova 1 measured 73 to 519 million instructions
    a second on 1 to 24 threads, and was 98% parallel at 10,000 nodes. The
    GPU suits interpreting programs badly, for three reasons: 32-lane
    divergence, scattered memory, and slow lanes. Nova 1 found its real
    GPU slower even on warps, mostly from transfers. Ticks per second is
    the wrong yardstick; instructions per second is the right one. Speed
    comes from the simpler 22-instruction interpreter, locality, long
    ticks and work stealing. The estimate is 4 to 10 times Nova 1, to be
    benchmarked with the origin test. The GPU is for the UI, possibly the
    in-world lanes, and the power solve only in very large worlds.
- 2026-11-11: Damian said the DirectX UI is a Nova 2 choice, not a
  general preference. He asked whether a much larger map changes the GPU
  question. Claude added the analysis to the engine note in 2.
  - **What size changes:** a GPU needs tens of thousands of threads, so it
    is idle at 1,000 places and fully occupied at 100,000 or more, while
    the CPU's throughput stays fixed.
  - **What size doesn't change:**
    - divergence. Wavefront regrouping (Laine, Karras and Aila, 2013) pays
      for thousands-of-cycle shading, not for one-cycle instructions;
    - the serial OS layer;
    - transfers. A GPU win would need the whole engine resident on the
      GPU.
  - **Rough bound (an unmeasured estimate):** the best case is a few times
    the CPU.
  - **Recommendation:**
    - build for the CPU, with each place's state in one flat block;
    - keep the interpreter core compilable for both, as Nova 1 did;
    - benchmark at 1k, 10k and 100k places, and decide from the numbers;
    - use Nova 1's "auto" CPU/GPU split as the fallback.
- 2026-11-11: Damian proposed a CPU-or-GPU setting, with a separate engine
  for each, so that a GPU engine keeps the whole world on the GPU and
  avoids transfers. Claude agreed with the setting but proposed one source
  with two backends rather than two engines, since writing every rule
  twice would drift.
  - **One source:** the rules are written once, in a C++ subset that also
    compiles as HLSL. Nova 1 did this with `warp_isa.inc` and got
    bit-identical results. Anchors: LAMMPS through Kokkos.
  - **Same world on both:** the setting must not change the world, so
    hashes must match on either backend. It then chooses only speed, and
    a run can resume on the other backend.
  - **Discipline of the subset:** flat arrays; no allocation, recursion or
    virtual calls in a tick; integer and fixed-point arithmetic; explicit
    order within a place; no side effects in `&&` or `?:`.
  - **What stays on the CPU:** the UI, observers, snapshots, route
    recomputes and respawn relinking.
  - **GPU memory:** the RTX 5080 has 16 GB, enough for 100,000 places'
    memory. Disks stay in host memory, with DREAD and DWRITE batched once
    a tick.
  - **When:** CPU first. The GPU backend and its setting
    (`engine.device`) come only after benchmarks show a need and a
    prototype shows a gain (principle 7).
- 2026-11-11: Damian explained that he said "two engines" because Claude
  had said avoiding transfers means moving the whole engine to the GPU,
  not just the interpreter. Claude agreed: the GPU backend is the whole
  engine, resident on the GPU. The only difference is whether its code is
  written separately or shared. 2's wording was made explicit.
- 2026-11-11: Damian accepted the engine plan.
  - Places run in parallel on CPU threads first.
  - The rules are one source in a subset of C++ that also compiles as
    HLSL, so a GPU backend can follow. That backend is the whole engine
    resident on the GPU, and it must give the identical world (the same
    hash).
  - The GPU backend and the engine.device setting are added only when
    benchmarks at 1k, 10k and 100k places show the CPU is the bottleneck
    and a prototype shows a gain.
- 2026-11-11: Damian added three working rules from Nova 1 (6).
  - No external dependency without his approval first. The DirectX
    platform libraries chosen in 2 are approved; DXC, for example, would
    need asking.
  - C++ written like C with classes. Claude's reading: structs, classes
    and functions, with simple templates for containers only; no
    exceptions, RTTI, inheritance hierarchies or metaprogramming. This is
    to confirm.
  - No C++ standard library. Thin C-runtime and platform headers are
    allowed, the same line as Nova 1.
  He plans to have Fable and Astra review the design document for
  consistency before building starts.
- 2026-11-11: At Damian's request the design was split before the review
  by Fable and Astra.
  - This file was renamed design-history.md and kept word for word; new
    log entries continue here.
  - A clean design.md (1,935 lines, 94 KB, against 5,537 lines and
    334 KB) holds only the current design, reorganised by subject into
    14 sections with their own numbering.
  - Six gaps found while writing it are listed in its 14.2 rather than
    settled silently:
    - first OS call against first ALLOC making ownerless code a program;
    - a decided core-kind preference with no call;
    - rowhammer decided but unspecified;
    - the order of threads within a place;
    - turn length per OS;
    - confirming the "C with classes" reading.
  - SENSE's row was merged with its later additions (idle cores, free
    memory, per-device and neighbour readings).
  Also: Damian's rule that no external dependency is added without his
  approval was saved as a global preference in the memory database.
- 2026-11-11: Damian settled the six gaps found in writing the clean
  design.md, all as Claude recommended:
  - (1) ownerless code becomes a program at its first ALLOC. Before that,
    SENSE and SEND work and other calls fail soft; a SEND from
    ownerless code gives its arrival no parent;
  - (2) a new call, SETCORE (any, P only, E only), as a strict per-thread
    pin like Linux affinity. It falls back to any if every core of that
    kind fails, which makes 18 calls;
  - (3) rowhammer specified: per-page access counts per tick, a
    per-device threshold, flips in adjacent pages, a slider (anchors:
    Kim et al. 2014, about 139k activations per 64 ms on DDR3, about 10k
    on newer chips);
  - (4) thread order within a place is rotating quanta (anchors: Kendo,
    DMP 2009);
  - (5) turn length is one world setting;
  - (6) "C with classes" is confirmed: namespaces, classes with RAII,
    references, const and constexpr, enums, operator overloading for
    small maths types, and simple container templates. No exceptions,
    RTTI, inheritance hierarchies, engine virtuals, lambdas or
    metaprogramming.
- 2026-11-11: Two design reviews came in: Astra's
  (design-review-codex-2026-11-11.md) and Fable's (design-review-fable.md).
  Claude merged them into design-review-triage-2026-11-11.md, with 30
  decisions (R1–R30), specs and text fixes. Damian accepted all of
  Claude's recommendations ("design fatigue"), approving WIC for the UI
  among them. Seven reverse earlier decisions:
  - R3: port 0 can refuse;
  - R5: a program's record begins at SPAWN;
  - R6: the reset region stays put;
  - R7: hand-placed programs are allowed in tests;
  - R14: the disk is raw and unowned;
  - R18: DIV by zero returns 0;
  - R20: the GPU and NPU are parked.
  design.md is being revised accordingly. Its pre-revision copy is in
  Claude's scratchpad, and this history keeps the original.
- 2026-11-11: design.md's revision is complete. All 30 decisions are
  applied through sections 2–14, and section 14 now lists what each build
  gate must show, the decisions still open, and what was settled. The
  specs are written as appendices:
  - A: the instruction and call contract (encoding, operands, every
    instruction, threads, stacks and handlers, what every argument value
    means, SENSE's readings), with the shortest replicator traced: 18
    bytes on a lax place, 24 on a strict one with 64-byte pages;
  - B: lifecycles for idle runs, programs, threads, allocations, devices
    and pending operations;
  - C: the order of a tick and its equations;
  - D: the laws on one page (sixteen laws, six boundary inputs) and a
    rough settings count of about 290, against Nova 1's 385;
  - E: the default world's placeholder numbers and the ratios to watch.
  The section-4 text fixes are applied. The instruction-code order in A.1
  and the SENSE reading order in A.4 are Claude's placeholders, fixed
  before the first run.
- 2026-11-11: Claude drafted the gate 1 build plan
  (build-plan-gate1.md): what gate 1 delivers, the gate 1 world (places
  with no links, a steady supply), the code layout and toolchain, what
  comes from Nova 1, twelve steps each with a check, tests, the numbers to
  fix before the first origin run, risks and size. Planning found three
  design gaps, awaiting Damian's yes:
  - (1) the origin test needs SPAWNed threads to run, but the gate table
    builds the scheduler at gate 2. Proposed: gate 1 builds every call that
    works within one place on a steady supply (the scheduler, SLEEP,
    SETCORE, SETCLOCK's speed and errors, fault and timer handlers, the raw
    disk), so random code meets the real call table and its costs;
  - (2) SYS takes the call number mod 16 with 16 calls, which leaves no
    room for the extra ALLOC and SPAWN slots 9.2 and 10.6 rely on.
    Proposed: a slot table (a setting), 18 slots by default;
  - (3) SENSE's index modulo an empty list (no neighbours) divides by zero.
    Proposed: it reads 0.
- 2026-11-11: Damian approved the gate 1 build plan ("Yes, approve all"):
  the three design fixes, the toolchain (MSVC, with CMake and Ninja from
  the Visual Studio install, and the Windows SDK) and the parts reused from
  Nova 1 (build-plan-gate1.md, 4.2). Applied to design.md:
  - the gate table (2): gate 1 builds every call that works within one
    place on a steady supply (memory, the scheduler, SLEEP, SETCORE,
    SETCLOCK's speed and errors, fault and timer handlers, the raw disk),
    on places with no links between them. Gate 2 gains the boot block's
    cold start; gate 3 keeps the network and data handlers. Until gate 3,
    SEND and PING fail as with the network device off, and RECV waits on
    inboxes that never fill;
  - SYS picks a slot (the operand mod the slot count) in a numbering table,
    both settings. The default is 18 slots: the 16 calls, then a second
    ALLOC and a second SPAWN (9.2, A.2, appendix E);
  - an index into an empty list reads 0 in SENSE (A.4);
  - a new convention in A.1: "x mod n" reads x as unsigned 32-bit.
- 2026-11-11: Build step 3 (memory) fixed the allocator's details, added to
  design.md 9.3 as [details at build]. These are Claude's choices within
  the design, open to Damian's veto:
  - size classes: 8, then two per doubling up to the page (a setting gives
    powers of two only);
  - larger allocations take whole pages, up to memory.max_alloc
    (placeholder 4,096 bytes), which is appendix A's "largest size class";
  - first fit by address throughout;
  - PROTECT on a multi-page allocation changes only the page named;
  - the reset region takes whole pages at its device's start, and only idle
    runs may touch it, since programs hold nothing there;
  - SENSE's "free memory" counts free pages and free slots.
- 2026-11-11: Build step 4 (the interpreter) fixed which writes count as
  results for the error floor, added to design.md 7.5 as [details at
  build]: one per value written (registers, memory, the stack, or IP by
  STO, SWP, POP or a taken jump; SWP writes two). The neutral bytes write
  nothing and so can't go wrong. The E core's cost table placeholders are
  18, 55 and 528 sixteenths for simple work, MUL and DIV, against the P
  core's 16, 48 and 320; CPY costs 74 + 37 per byte, against 64 + 32.
- 2026-11-11: Build step 5 (the OS) ran appendix A's two replicators in
  test worlds and matched the traces register by register, children
  included, with the out-of-memory death as described. Details fixed while
  building, added to design.md (9.4 and after A.4) as [details at build]:
  a thread that ends while its program lives on gives its stack back;
  SENSE's units and device order; SETCLOCK in tenths of stock; the handler
  frame's layout and fault codes; a zero timer period clearing its handler;
  idle runs' refused calls answering 0; call costs including the crossing,
  with E cores paying about 1.6 times; and program ids unique in the world
  by place and count.
- 2026-11-11: Build step 6 (the scheduler). A hand-placed replicator now
  fills a two-core place by itself (140 copies in 60 ticks), the same way
  on two runs. Details fixed while building, added to design.md 9.5 as
  [details at build]:
  - time is kept at each core's stock speed, and a thread at clock k gets
    k/10 of the cycles;
  - overruns are debt, taken from the thread's next quantum (so a SPAWN
    keeps its thread off the core for about a tick);
  - a thread leaving mid-quantum leaves the rest to the next ready thread,
    which pays the switch;
  - at a turn's end the thread requeues only if someone is waiting;
  - one ready queue for unpinned threads and one per pinned kind, ordered
    together;
  - interrupts are delivered at quantum starts (low voltage, then data,
    then timers); timers fire at tick starts; an interrupt wakes only SLEEP
    0 waits;
  - ready threads take cores from idle runs at quantum starts.
- 2026-11-11: Build step 7 (the reset region, idle runs and the raw disk).
  A replicator placed in the reset region is found by an idle run, its copy
  becomes a program from static, and its line made 67 copies in 30 ticks.
  Details fixed while building, added to design.md (9.3, 10.2) as [details
  at build]:
  - the start-spot step must share no factor with the region's size;
  - idle runs run at stock speed, and their cap counts every cycle;
  - halted cores look for a reason to start at each quantum;
  - halted is the core's state, kept through threads using the core;
  - disk results land at the barrier, with the latency (placeholder 20,000
    cycles plus one per byte) paid from the next quantum;
  - an idle run's DREAD is forgotten if a thread takes its core first.
- 2026-11-11: Build step 8 (the noise floors). Error rates and cosmic-ray
  flips match their settings within statistical bounds, rates of 0 give
  nothing, and the same seed gives the same flips. Details fixed while
  building, added to design.md 7.5 as [details at build]:
  - the error rate at clock level k is the floor times 2^((k − 10) ÷ d),
    with d placeholder 1 tenth: 32 times at 1.5 times stock;
  - a change of level redraws the countdown;
  - geometric draws use a fixed-point log2, in integers only;
  - the cosmic floor counts bit-ticks per memory device, covers memory but
    not disks, and its flips in the reset region count as writes to it;
  - edge errors wait for the varying supply at gate 2.
- 2026-11-11: Build step 9 (places, the world, threads and snapshots).
  Places are built from settings and filled with static, and `nova2 run`
  and `nova2 resume` work. The world comes out the same on 1, 2 and 24
  host threads and in reversed dispatch order, and a snapshot taken at
  tick 20 resumes to the unbroken run's hash at tick 40.
  - Details fixed while building, added to design.md (2, 7.3): gate 1's
    hardware draw; the world hash covering exactly the saved state, field
    by field; snapshots at the barrier, with only run.* changeable on
    resume. Nova 1's thread pool came across as is, and its snapshot
    container with a payload in place of its world.
  - A finding for the origin test: static faults within a few
    instructions, so halted cores wait for the retry, and the retry sets
    the search rate. In 48 places over 200 ticks there were 820 idle runs
    and 278,000 instructions at the placeholder retry of 100 ticks, against
    47,000 runs and 15 million at 1 tick. At gate 1 nothing draws power, so
    halting saves nothing yet. Claude proposes a 1-tick retry for the
    origin test, to be agreed with the other numbers before the first run
    (build-plan-gate1.md 7).
- 2026-11-11: Build step 10 (facts and observers). The facts log
  reconstructs the family tree exactly, generation depths and lineages
  agree with it, and a watched world hashes the same as an unwatched one.
  Details, added to design.md 13.3 as [starting point]:
  - every parentless birth founds a lineage;
  - stages 1 and 2 are a lineage's first faithful birth, and its first
    faithful birth from a faithfully born parent;
  - the Scavenger and Hijack heuristics;
  - genome groups at 0.8 of shared 3-byte runs;
  - "crowded" means free memory under a tenth;
  - distinct paths are counted in a 65,536-entry table.
  Nova 1's compressibility estimate came across, made for bytes and
  without its lambda. Birth images are now hashed like genomes, so the two
  compare.
- 2026-11-11: Build step 11 (the origin-test runner and the first
  benchmark). The results are in build-plan-gate1.md 11.
  - The interpreter: 85 M instructions a second on one thread, 1.15
    billion on 24, about 2 to 4 times below the design's estimate.
  - Static and replicator worlds scale about 6 times; the per-access
    counters cost under 1%.
  - A dry run of the origin test (2 seeds, 5 × 10⁸ instructions each,
    nothing seeded): random code made 59 programs, none of them faithful
    copiers, all dead by faults. The regions' compressibility fell from
    0.97 to about 0.85.
  - The stage criteria live in the core (lineage_stage), so they're
    tested.
  - Per-access counters for data accesses (not fetches) count activations
    of a page different from the context's last, and once per page for
    CPY.
- 2026-11-11: Build step 12 (the gate 1 views), and with it gate 1's
  build. `nova2 view` opens a config or a snapshot and runs the world live,
  or renders one frame headless to a PNG.
  - On screen it draws with Direct2D and DirectWrite; headless, into a WIC
    bitmap, software-rendered.
  - The views: places as tiles, the place inspector, memory page by page
    (by owner, lineage, permission or size class), and three charts.
  - A headless render matches the stored tests/golden/view_gate1.png pixel
    for pixel, and a snapshot draws exactly as the world it was saved from.
  - The frame is new and small rather than a port of Nova 1's 5,000 lines.
    Nova 1's setup page, widgets, genome browser and phylogeny come when
    needed.
  - Direct2D, DirectWrite and WIC joined the code-rules allowlist, as
    approved.
- 2026-11-11: The first origin run started (runs/origin-1).
  - Damian chose to run with Claude's four recommendations and tune from
    the results ("a run's going to give us some concrete answers, more
    than just hypothesizing about the numbers"):
    - the budget is 10 million ticks a seed, which replaces design 10.6's
      10¹² instructions: once life appears, programs spend most of their
      time paying for SPAWN, so an instruction budget would stretch the
      seeds that succeed;
    - the idle-run retry is 1 tick, now the default;
    - the error floor stays at 10⁻⁵;
    - the 10 seeds run side by side, 2 host threads each.
  - The rest of build-plan-gate1.md 7 as proposed. Each seed's samples go
    to runs/origin-1/samples.
- 2026-11-11: The first origin run finished: FAIL, with no seed at stage
  1 (results in build-plan-gate1.md 11).
  - 230,233 births from static across 10 seeds of 10 million ticks, none
    faithful; almost all had an empty genome.
  - Random code finds ALLOC and SPAWN but almost never copies itself
    between them.
  - The reset regions stagnate: under 1% of 21.5 billion idle runs took a
    new path.
  - One program in seed 3 spawned about 1,200 threads into its own code.
- 2026-11-11 to 12: The lever trials (runs/pilot-1; results in
  build-plan-gate1.md 11). 10.6's first three levers, and an experimental
  switch that starts new threads with B, C and D at their start address
  (`os.thread_regs = start`), were tried alone and together, 8 seeds of
  500,000 ticks each.
  - Together they gave the first births by programs, but no program copied
    8 bytes or more into a child.
  - The apparent stages 1 to 3 were an artifact. First, empty genomes
    matched. Then, on a lax place, 4-byte gifts inside the parent's page let
    children run the parent's code in place, with a one-byte genome.
  - A faithful birth now needs a genome of at least 8 bytes
    (`observe.min_genome`). This is Claude's change to the measure, for
    Damian to confirm.
- 2026-11-12: The second origin run started (runs/origin-2): all the
  trialled levers at full length, 10 seeds of 10 million ticks.
- 2026-11-12: Lever 4 piloted (runs/pilot-2; results in
  build-plan-gate1.md 11), on Damian's go-ahead. The experimental switch
  `os.alloc_d` makes a successful ALLOC leave its address in D too.
  - It aims about 16 to 19 times as many copies at new memory.
  - With every earlier lever, it made 408 copies of region code.
  - Still no program copied itself.
- 2026-11-12: The second origin run failed (results in build-plan-gate1.md
  11). Seed 5 reached stage 1 and no seed went further.
  - In seed 5, one place's reset region grew copier code from tick 6
    million. Run as a program, one of its genomes (47 bytes) copies itself,
    child included, only with lever 4's `os.alloc_d`.
- 2026-11-12: The third origin run (runs/origin-3): run 2's settings plus
  `os.alloc_d`, at full length. It failed, but closely: stage 4 in 3 of 10
  seeds against a pass mark of 5, and stage 5 in two of them.
  - Faithful births went from 3 to 305,890.
  - Only lax places got past stage 1; every strict place stayed at stage 0.
  - `nova2 origin` now makes its output folders. Run 3 lost its genome
    dumps because they didn't exist.
- 2026-11-12: Run 3's stage 4s checked (runs/origin-3c). They were met by
  one surviving program in a place of 4 or 6 (seeds 7 and 9), or by members
  none of which were born faithful (seed 6). Most copiers are relays, one
  child each. Seed 9 grew one real population late: 100 of a place's 132
  programs in 100,000 ticks, with 209 genome groups.
  - Proposed for Damian to decide: stage 4 to need a minimum number of
    living members, most of them copies (faithful, or at least half like
    the parent), holding the share for origin.span ticks counted from when
    it first did.
- 2026-11-12: Stage 4 tightened, as proposed, on Damian's go-ahead. He had
  wanted a longer lineage criterion for Nova 1 too.
  - A lineage holds its place at a check when it has 20 living members or
    more (`origin.members`), at least half of them born as copies
    (`origin.copies`), and they are at least 10% of the place's programs.
  - A copy is a faithful birth, or a near one: a program wrote 8 bytes or
    more into its child, at least half like its own genome (3-byte runs in
    common, as the groups use).
  - Stage 4 is a hold of 100,000 ticks without a break, checked at every
    observer sample. One failed check ends a hold, and the next starts
    afresh. The span used to count from the lineage's first birth, and the
    share was checked only at that moment.
  - Run 3's settings are rerun to score them under it (runs/origin-3d).
- 2026-11-12: Damian confirmed the 8-byte minimum for a faithful birth
  (`observe.min_genome`), made after the lever trials.
- 2026-11-12: Run 3's settings scored under the tightened stage 4
  (runs/origin-3d): FAIL, stage 4 in 1 of 10 seeds. Seed 9's growing
  population in place 13 is the one pass: it held 100 of 132 programs, 77
  of them born as copies, for 100,000 ticks. Seeds 6 and 7, which passed
  before, never held a place. Stage 5 still comes at the same check as
  stage 4, since it counts groups made before establishing too.
- 2026-11-12: Relays traced in place (seed 9 snapshots). They don't copy.
  A program runs past the end of its block on a lax page and SPAWNs its own
  block. R15's test ("the allocation holding the caller's IP") misses this,
  so the block is given away while in use, and the parent faults. The
  observer counted these hand-overs as faithful copies, because a block's
  written extent stays with it. In 100,000 ticks of seed 9, 11,636 of
  11,639 births were hand-overs. Seed 9's place 13 population is real
  copying: ALLOC, CPY, SPAWN from several threads. Proposed for Damian:
  close the hole in SPAWN for page-shared execution, and stop counting a
  birth into the parent's own block as a copy.
- 2026-11-12: The hand-over gap closed, on Damian's go-ahead. A thread
  running in no allocation of its own (lax page sharing) now runs on every
  allocation its program holds in that page, so SPAWN there starts a
  thread. The observer counts a birth into the parent's birth block as a
  hand-over, never a copy. Run 3's settings run again as run 4
  (runs/origin-4).
- 2026-11-12: Run 4 (runs/origin-4): FAIL, stage 4 in 1 of 10 seeds.
  Hand-overs fell to 898, faithful births from 401,118 to 42,315, and run
  3d's stage 3s in seeds 1, 6 and 7 went with them. Seed 2 is the pass, and
  real copying: a 17-byte copier (ALLOC 17, CPY 117 bytes from its own
  start, SPAWN through `SYS IP` when its address is right) began at
  5,595,091 in a lax 1,024-byte place and held 151 of 280 programs for
  100,000 ticks. It relies on the experimental `os.thread_regs = start`
  and `os.alloc_d`. Strict places still never reach stage 1. The run used
  about 70% of 10.6's 10^12-instruction budget a seed.
- 2026-11-12: Stage 5 counts only the groups a lineage makes after it
  reaches stage 4, and seeds run on past it, on Damian's go-ahead. Before,
  it counted groups made while the lineage was taking its place, so it came
  at the same check as stage 4 and stopped the seed there. Reaching stage 4
  is now for good. The report says whether each seed's established lineage
  lasted. Run 5 runs run 4's settings to 10.6's full budget, 10^12
  instructions a seed (runs/origin-5).
- 2026-11-12: Run 5 (runs/origin-5), run 4's settings to 10.6's full
  budget of 10^12 instructions a seed: FAIL, stage 4 in 1 of 10 seeds.
  Run 4's worlds, extended to 12.1 to 16.3 million ticks. Seed 2's copier
  family held its place to the end, 9.2 million ticks after taking it, but
  made no new groups while the place was crowded, so no seed reached stage
  5. Nothing else held a place in the extra ticks. The other copiers are
  chains: each member leaves one child, so one failed copy ends the
  lineage, and most died out within a few thousand ticks. Levers 1 to 4
  are all in use. Lever 5, the instruction set, is the last before the
  design review.
- 2026-11-12: Lever 5 (the instruction set) drafted, on Damian's go-ahead
  after run 5. A new command, `nova2 trace`, runs one genome alone in a
  sandbox place. It showed that seed 2's copier works in only a third of
  pages (its ALLOC's permission is its address mod 3), and that seed 1's
  chain copiers do their SPAWN past their own blocks, outside the recorded
  genome. Two experiments, both off by default: `cpu.top`, spare codes made
  TOP (a jump to the start of the allocation holding it), and
  `os.spawn_top`, a SPAWN that makes a new program sends its caller back to
  the start of its block. In the sandbox each turns a one-child copier into
  one that loops (20 and 27 faithful children against 1). IP wrapping at an
  allocation's end was left out: any code that doesn't fault would loop
  forever, and nothing at gate 1 ends such a thread. A pilot of three
  variants (TOP in one spare code, in four, and spawn_top), 10 seeds of 5
  million ticks, runs in runs/pilot-3.
- 2026-11-13: The lever 5 pilot (runs/pilot-3, 5 million ticks a seed):
  `os.spawn_top` works and TOP doesn't. With spawn_top, seed 1 reached
  stage 4 at 3,135,300, the first on lax 64-byte pages and the first within
  5 million ticks: a lineage of 245,132 births over 730 generations, many
  children per member instead of run 5's one-child chains. It held its
  place to the end, but after 3.5 million ticks it all but stopped copying,
  and nothing ends its members, so the place froze and no stage 5 followed.
  Seed 3 boomed and busted: seven lineages of up to 130,810 births each
  died out or dwindled. Its copier, traced, is a 9-instruction ALLOC, CPY,
  SPAWN loop that works only in a third of pages (ALLOC's permission is
  its address mod 3). spawn_top had 582,583 faithful births across the
  seeds against run 5's 75 at the same tick. TOP in one spare code reached
  stage 2 in two seeds, and in four codes only stage 1, with twice run 5's
  threads alive and 3 faithful births: it gives junk code loops sooner than
  it helps copiers. One seed in 10 is still short of the pass mark of 5.
- 2026-11-13: Run 6 (runs/origin-6), run 5's settings plus lever 5's
  `os.spawn_top`, to 10.6's full budget: FAIL, stage 4 in 1 of 10 seeds,
  the same count as run 5. Seed 1's lineage (the pilot's) held its place
  for 11.1 million ticks, but froze: 95 members throughout and 54 faithful
  births in its last 9.2 million ticks, so no stage 5. Run 5's seed 2
  copier never appeared, because spawn_top changed seed 2's world before
  it. Stage 3 came in 5 seeds and faithful births rose 3.5 times, but the
  other copiers boomed and died out. With 10.6's five levers spent, next is
  the design review with Damian: frozen places with no way to end a
  program, booms that bust, the B mod 3 permission trap and barren strict
  places, the three experimental switches every copier leans on, and no
  snapshots to trace a lineage in.
- 2026-11-13: The design review after the origin test's five levers
  (design-review-origin-2026-11-13.md), all seven proposals accepted by
  Damian. Lever 5's experiments, `os.spawn_top` and TOP, are dropped:
  spawn_top broke 9.1, principle 12 and principle 9, and didn't change the
  count at stage 4. `os.thread_regs = start` and `os.alloc_d` become the
  rules (A.3, A.4), with ALLOC's result in D on a failure too, so a copier
  in a full place still faults (A.5). A lax place has no no-execute bit,
  so its data pages are "both" (9.3); before, the copiers found worked
  only in a third of pages. A.5's replicators, rewritten for the new
  conventions, are 14 bytes on a lax place and 20 on a strict one.
  Turnover stays at gate 2; strict places being barren at gate 1 is
  recorded as a finding; snapshots of seeds are to be built; the pass
  mark stays until the changes are tested. Pilot 4 (runs/pilot-4) tests
  them against run 5.
- 2026-11-13: Pilot 4 (runs/pilot-4), run 5's settings under the design
  review's rules, 10 seeds of 5 million ticks: stage 4 in 4 of 10 seeds
  (1, 3, 5 and 6) and stage 5 in 3, within a third of 10.6's budget. Run 5
  had none at that tick, and run 6 had one at its end. Faithful births
  were 3,123,484 against run 5's 75. Run 5 already had `os.thread_regs`
  and `os.alloc_d`, so the change is a lax place's data pages running and
  a failed ALLOC setting D to 0. The copiers traced lean on the first:
  seed 5's 51-byte copier takes its block's permission from its own
  address mod 3 and now works at 2 addresses in 3, not 1; seed 1's runs
  code past its own block. Holds no longer freeze: most children die by
  faulting, so members come and go, and later lineages took over the
  holding places in seeds 1 and 5. Booms and busts still happen in seeds
  3 and 6. Strict places stay barren. Next is run 7, to the full budget
  with place snapshots.
- 2026-11-13: Run 7 (runs/origin-7), pilot 4's settings to 10.6's full
  budget with place snapshots: PASS, stage 4 in 5 of 10 seeds, exactly
  the pass mark. Its first 5 million ticks repeated pilot 4 row for row,
  so seeds 1, 3, 5 and 6 carried their stage 4 in. The fifth, seed 8,
  came at 15,415,200, with 1.4% of its budget left: a 91-byte copier (a
  23-byte unit repeated) took 132 of its place's 147 programs in 25,000
  ticks, then stopped, with 362 of 365 threads waiting for a message that
  never came. Pilot 4's "holds don't freeze" didn't last: copying stopped
  in seed 1 after 12 million ticks, in seed 5 after 6 million and in seed
  6 after 8 million, and the holding lineages stayed, mostly sleeping
  until an interrupt. Only seed 3 was still copying at the end, with
  lineages taking turns in four places. Seed 2's late copier (10 bytes)
  filled its place but copied too badly to hold it. The test passes as
  written, narrowly, and three of the five holds end asleep. Whether that
  is gate 1's pass, whether stage 4 should ask for copying during the
  hold, and whether to confirm with more seeds are for Damian.
- 2026-11-14: Run 8 (runs/origin-8), run 7's settings for seeds 11 to 20,
  to confirm run 7's narrow pass: PASS, stage 4 in 6 of 10 seeds (13, 14,
  15, 18, 19 and 20), each with at least a quarter of its ticks left.
  With run 7, stage 4 in 11 of 20 seeds against a mark of 10, and stage 5
  in 6. Every seed reached stage 1 and 19 of 20 stage 3. The holds split
  about evenly: in 5 of the 11 stage 4 seeds (3, 15, 18, 19 and 20)
  lineages were still copying and taking turns at the end; in 6 (1, 5, 6,
  8, 13 and 14) copying had stopped and the place slept, most threads
  waiting for an interrupt, full or not. Strict places stayed barren.
  Gate 1's origin test passes on 20 seeds; whether a full place keeps
  turning over is gate 2's question, and these runs answer it
  "sometimes".
- 2026-11-14: Gate 1's origin test passes, accepted by Damian: stage 4 in
  11 of 20 seeds within 10.6's budget (runs 7 and 8), against a mark of
  10. With the build done, the first benchmark and the side measurements
  (faults weren't too harsh for the first copiers; the reset regions kept
  changing, compressibility about 0.84 falling to about 0.77; the
  per-access counters cost under 1%), gate 1 is complete. Turnover in a
  full place, where half the holds fell asleep, is carried to gate 2.
- 2026-11-14: Gate 2's build plan (build-plan-gate2.md) drafted, and its
  five design gaps accepted by Damian, as proposed:
  1. The supply before gate 4 is a source per place, a voltage behind an
     internal resistance that the place's own draw sags through the
     one-tick lag, varying by swings, drop-outs and a day at each place
     (each at its own phase), all drawn before the run. Power draw comes
     with it; heat stays at gate 4. Chosen over a voltage simply scheduled
     because R1 and 4.3 make crowding matter, which is gate 2's question;
     the day is brought forward (option a over b) because night is the
     design's main source of cut-outs (7.6).
  2. A thread ends when a bit of its saved state fades (appendix B's
     "fading past use"), so threads asleep for good in faded memory don't
     hold it until a cold start.
  3. A cold start reboots the place's OS: every program ends, with a new
     death cause "cold start", and then the boot block runs.
  4. Port 0's queue and prompt runs come forward from gate 3, for the boot
     block.
  5. 7.6's fallback, pausing instead of crashing at a cut-out, is built as
     a switch, off by default.
  Applied to design.md 2, 7.6, 9.4, 10.5 and 13.1. The numbers in the
  plan's section 7, the pass mark among them, are still to agree before
  the first gate 2 run.
- 2026-11-14: Gate 2's baseline: runs 7 and 8 rerun with the turnover
  measure on, their stages repeating exactly. Gate 1's world turned over
  73 of 1,164 counted windows of 200,000 ticks (6%), and 31 of 342
  crowded ones (9%). Its places hold a fixed set of residents, living
  about 1.6 million ticks, with offspring dying young around them: 99.9%
  of the dead in counted windows had been born in the window.
- 2026-11-14: Gate 2's numbers (build-plan-gate2.md 7) agreed by Damian,
  with the baseline in. The pass mark stays at 9 in 10 counted windows
  and 9 in 10 crowded ones, for a new reason: a window starts with about
  150 programs, so it turns over almost exactly when its residents' mean
  life is under about 200,000 to 250,000 ticks, and the share counts the
  full places that turn over. (The first draft's reason, that gate 1 had
  about half its holds still copying, didn't survive the baseline.)
  Added: a floor of 100 crowded windows from at least 5 seeds, so a world
  that empties its places can't pass on a few windows; and windows that
  turn over with a cold start in them count, decided before the runs,
  with the report saying how many did. Fade's retention just below the
  threshold set at 1,000 times its retention at 0 V, rising evenly on a
  log scale. The rest as proposed.
- 2026-11-14: Gate 2's supply built (build-plan-gate2.md step 3), and two
  readings of section 7's drop-outs agreed by Damian: their lengths are
  spread evenly on a log scale between 10 and 1,000 ticks, so half are
  shorter than 100 (spread evenly in ticks, most would be long); and a
  drop-out that isn't a blackout leaves between nothing and half of the
  capacity, rather than taking up to half. Also from step 3: a device
  below its threshold draws nothing, since constant draws would otherwise
  pull a weak source to 0 V, and the network device gets a range for it
  (600 to 1,300 mV, placeholders like the disk's).
- 2026-11-14: Gate 2's cut-outs built (build-plan-gate2.md step 4), and
  six readings of 7.6 agreed by Damian, all as built. I/O is in flight
  from the barrier that did it until the next tick's device states, and
  a disk cut-out cuts it at one point drawn along all of it. A failed
  read answers 0 but leaves what it brought in the buffer. DREAD and
  DWRITE wait for as long as the disk is out, since gate 1's calls have
  no limit of their own. The pause switch puts the thread back in the
  ready queue, where it may resume on another core. A core that comes
  back starts a fresh idle run. The crash rule ends the thread part way
  through its turn at the tick boundary, so threads whose turns line up
  with the ticks get through; ending the thread that ran last was the
  other reading. Also from step 4: on section 7's supply the P cores
  cycle at night (out 20.7 ticks a time, 4.1% of their ticks), since at
  a fifth of capacity their own draw moves the voltage by more than the
  restart gap. It's left as it is, bounded by the off-time.
- 2026-11-14: Gate 2's edge errors and memory fade built
  (build-plan-gate2.md step 5), and seven readings agreed by Damian, all
  as built. Edge errors are relative to the floor: within 50 mV of an
  edge the total doubles every 10 mV closer, so 32 times the floor at
  the edge, and a kind with no floor has none (a base rate of their own
  was the other reading). The clock scales the floor but not the edge
  part. Memory's edge errors are bits that fail to hold, at the cosmic
  floor times the same factor, and like flips they only corrupt a saved
  state: only fading ends a thread. A cell is a bit. A device fades only
  while it's cut out and below its threshold, holds from there to its
  restart, and the restart refreshes it. A varying dip adds up as a
  dose, each tick at V counting as 1000^(-V / threshold) ticks at 0 V. A
  thread's saved registers count only while it's off its core, and a
  handler's frame counts on or off. Also from step 5: on section 7's
  supply 3.9 million cells faded in 400,000 ticks but ended only 1
  thread, so the devices that faded held almost no threads off their
  cores (which places they were wasn't checked).
- 2026-11-14: Gate 2's cold start and boot block built
  (build-plan-gate2.md step 6), and eight readings agreed by Damian, all
  as built. A cold start empties port 0's queue, the old OS's, before the
  boot block joins it at the head. A block nobody wrote boots as static,
  with no parent. The last writer is the last DWRITE to land with its
  first byte in the block, whatever its length, so a rival writing one
  byte there becomes the boot's parent though the code stays; a write
  torn back to nothing restores the writer before it. Start spots keep
  stepping through a reboot, so the block lands wherever the first core
  to start is, cut short near the region's end. The observer keeps the
  record of a writer that died as the block's last, so its boot carries
  the lineage on, and a boot brings a lineage back from extinction. The
  disk barrier checks that a request's thread is still waiting before
  any bytes move. A boot's code stays in the region where it landed, and
  idle runs that later copy it make births of static, with no parent:
  only the prompt run carries the cause "boot". Also from step 6: step
  4's crash test now uses a drop-out to a fifth, which cuts out the P
  cores but not memory, since a blackout now reboots the place; and on
  section 7's supply the 6 cold starts in 400,000 ticks found nothing
  alive and no written block.
- 2026-11-14: Gate 2's determinism, snapshots and cost checked
  (build-plan-gate2.md step 7), and eight readings agreed by Damian, all
  as built. The invariant checker is a function the tests call, not part
  of the tick, so a run neither checks nor pays for it; the checked build
  is the Debug test build, and the release build has no tests. The fuzz
  world runs with every test run (three runs of 8 places, about 7
  seconds) rather than as a soak on its own. Its crowded runs, and the
  determinism world, raise the disk's threshold to 900 mV, above the
  cores', so the disk cuts out with I/O in flight: a test setting, the
  default unchanged. The ready-queue check goes one way, allowing entries
  left by threads that have since run or ended, as the queue drops those
  lazily. The benchmark times a tick's parts with the time-stamp counter
  behind a switch only it turns on, the memory part split into fading
  then the cosmic-ray floor. Fading's cost is left as it is: about 55 ns
  a failed cell, half a place-tick on the fuzz world's supply but nothing
  on section 7's, where gate 2's parts cost 2.5% of a place-tick (1.2% on
  the steady supply). Also from step 7: static alone made one birth in
  the fuzz world's 4,000 ticks, so the crowded runs, with programs placed
  by hand, are what put threads through cut-outs, fading and cold starts.
- 2026-11-15: Gate 2's views built (build-plan-gate2.md step 8), and
  eight readings agreed by Damian, all as built. The stored image the
  view test checks is now gate 2's, at 1,280 by 800, the window's
  default, since the inspector no longer fits at gate 1's 960 by 600;
  gate 1's image stays in tests/golden, unused. The inspector runs the
  window's full height, with the charts in two rows under the places and
  the memory. Tiles dim with the voltage and, beyond the plan, get a red
  corner when a device is cut out, because a place with everything off
  draws nothing, its voltage climbs back to the source's, and dimming
  alone would show it bright. The voltage chart plots the mean and the
  lowest over each sample, from 0 to the source's voltage, with the
  thresholds as faint lines named in its legend, so a drop-out shorter
  than a sample still shows; the observer keeps 4 bytes a place a sample
  for it. Loading a snapshot works out the cores' speeds and the tick's
  capacity, which aren't saved, so an opened snapshot shows them as they
  were. "Births from boots" counts only boots of a block a program wrote;
  a static boot shows only as a cold start. The turnover chart is the
  world's, over the last 24 windows that began with a program alive. The
  inspector's drop-out line shows the drop-out the last tick was in, or
  else the next tick's, and a device's off-time left counts the next tick.
- 2026-11-15: Gate 2's runs (build-plan-gate2.md step 9), and seven
  readings agreed by Damian, all as built. **Gate 2 fails its pass
  mark.** Under gate 2's world, 283 of 848 counted turnover windows turned
  over (33%), and 130 of 279 crowded ones (47%), against the 9 in 10
  agreed in advance; gate 1's world, the baseline, turned over 6% and 9%.
  The floor was met: 279 crowded windows from 9 seeds. The origin
  regression reached stage 4 in 8 of 20 seeds, against half (the
  baseline 11); at the baseline's rate 8 or fewer comes about 13% of the
  time. The windows that didn't turn over failed on their residents (562
  of 565 lost under half their starters). They are gate 1's frozen places
  again: seed 5's longest hold sat with 263 programs, no births and no
  deaths, every thread asleep or waiting for an interrupt, until a cold
  start ended them all. Cold starts made a third of the turnover, and 7
  of the 8 first stage-4 lineages died out in a window with one. So gate
  2's answer is the one section 8 predicted: no, not by the physics alone
  at these rates. The 9.4 test world agrees: a memory-taker filling every
  place of seed 1 with sleeping threads lasted until its place's first
  blackout or near-blackout, 1.7 million ticks on average, and no
  shallower drop-out, night, swing or cut-out ended one. Its fill is built
  by hand at tick 0 through the OS's own SPAWN and ALLOC, proved equal to
  the code's own fill on a steady supply, because the code's first thread,
  alone on its P core while filling, died at night-time cut-outs in most
  places. The test world is one seed with a taker in each of its 48
  places, run to the origin test's budget. A taker's end is its last
  thread's death, with that thread's cause, and the report names the
  drop-out it ended in or after, since a blackout ends it by fading two
  ticks before the cold start; threads lost before then are put down to
  fading by elimination. A full place isn't quiet, as section 8 assumed:
  its cores run idle runs on static all the time. The pass mark's three
  settings (origin.turnover_share, origin.crowded_min,
  origin.crowded_seeds) are section 7's numbers, the windows needed
  rounded up. The 20-seed run kept snapshots, which the hand check used.
  Next, by sections 8 and 10, is Damian's decision: the reserve lever (a
  visit that reboots a place, 7.8) or another rule, and for the origin
  regression the pause switch or a milder supply.
- 2026-11-15: The design review of gate 2's fail
  (design-review-gate2-2026-11-15.md), its four proposals accepted by
  Damian. First, frozen places: the saved-state rule is widened from
  "fades" to "fails", so a thread ends when any bit of its saved state
  goes wrong by the physics (a fade, a cosmic-ray flip or an edge error),
  while an overwrite still only corrupts it. Its anchor is Linux's
  memory-failure handling, which can kill a process as soon as an error
  is found in memory nobody is reading. At the cosmic floor of the runs,
  a thread's 192 bits of saved registers take a wrong bit about every
  5,200 ticks it spends off a core. It's built behind a switch,
  os.saved_state_fails, off by default, and tried in a pilot of seeds 1
  to 10 to 5 million ticks against runs/gate2-1. It becomes the rule only
  if the pilot bears it out. The reserve lever and more outages were
  turned down on the runs' own arithmetic: windows with a cold start
  turned over 97% of the time and others 24%, so the mark would need a
  cold start in about 9 windows in 10, and places that rebooted that often
  wouldn't refill (boot blocks carried life across 57 times in 24,211 cold
  starts). A low-memory killer was turned down because 416 of the 565
  failing windows had a tenth or more of memory free. Second, the origin
  regression is left to the rerun, with the pause switch next if it fails
  again. Third, one cluster of cold starts (about 25 a place a seed
  against section 7's 7) is traced and recorded. Fourth, the pass mark
  stays.
- 2026-11-16: The widened saved-state rule built and tried
  (build-plan-gate2.md step 10), and its eight readings agreed by Damian,
  all as built. It went in behind a switch, os.saved_state_fails.
  Memory's edge errors and the cosmic-ray floor list every byte they
  fail a bit of. After the memory step, each thread whose saved state
  holds such a byte ends, cause "memory", in record order. Its saved
  state is exactly fading's: its registers while off its core, and a
  handler's frame on its core or off. An edge error fails its bit
  whatever the bit reads back, as a faded cell does. The lookup scans
  every thread record when any bit fails. That's about 1 µs a place-tick
  at the runs' cosmic floor of 10^-6, the floor's part going from 520 to
  1,560 ns, about 4% of section 7's place-tick. A faster index was left
  until the rule had earned it. With the switch off, two of gate 2's
  first run's seeds reproduced byte for byte to 200,000 ticks, against
  that run's own binary. The 9.4 test world rerun (runs/taker-2,
  added to the agreed pilot) saw all 48 takers end by memory failing,
  after 37,442 ticks on average against 1.69 million, as the rule's
  arithmetic says: 192 bits at 10^-6 a tick is a wrong bit about every
  5,200 ticks a sleeper. The pilot (seeds 1 to 10 to 5 million ticks)
  reached each stage in as many seeds as gate 2's first run, or more,
  with far thinner populations. Claude proposed it too short to judge
  crowding, which came only after 5 million ticks. The cluster of cold
  starts, traced from tick 0 with a scratch probe, is a brown-out reset
  loop, the physics as written and not a bug. With nothing drawing, the
  voltage is back at the source's after the off-time, so every device
  restarts at once. At under 1% of capacity the restart's draw pulls it
  below every threshold, and the place cold-starts every 21 ticks until
  the drop-out ends.
- 2026-11-16: **Gate 2 passes**, at its second run (runs/gate2-2), and
  the widened saved-state rule becomes the design's. Damian agreed the
  run's three readings. The run was gate 2's first run again, all 20
  seeds with section 7's numbers and the pass mark agreed in advance,
  with the widened rule on and nothing else changed. 586 of 586 counted
  windows turned over (the first run 283 of 848), and 448 of 448 crowded
  ones (130 of 279), from 13 seeds. The origin regression reached stage
  4 in 13 of 20 seeds (8; gate 1 11). A hand check recomputed every
  window's flags from the rule and matched the report. Every one of the
  140,784 programs alive at a counted window's start died within it, so
  with the rule on the measure can't fall short. It stays as it is,
  since gate 2's question was whether full places freeze, and what lasts
  is lineages, by copying: holds over a million ticks in 8 seeds, up to
  3.6 million, and 17.1 million faithful births (7.8 million). Programs
  died 2.67 times a program-day (1.27), faults still most of it; the
  rule ended 45 million threads. Strict places stayed at stage 0, as at
  gate 1. The rule is now stated in 9.4, recorded in 14.1, and named in
  appendices A to D. It's on by default, with the switch kept so gate
  1's world and the baseline can still be run, and every earlier run
  keeps its own binary. The run took 17.1 hours against 11.1. Seed 3
  slowed about ninefold per instruction as its places crowded and ran at
  16 million instructions a second against 37 to 70 for the rest. The
  cause, traced the same day, was the observer's genome grouping (the
  next entry); the rule's lookup is too cheap to explain it. Next, by the
  build plan's section 10, is gate 3: does life spread?
- 2026-11-16: Seed 3's slowness traced and fixed, and Damian agreed the
  three readings (build-plan-gate2.md section 11, "Seed 3's slowness,
  traced"). The places weren't slow. Place 34, replayed from its
  snapshots, cost 11 to 12 µs a tick early and late. The observer's
  genome grouping was: 13.2's greedy grouping compared each new genome
  with every group's reference in turn, one thread after each tick's
  barrier. Seed 3 made 1.4 million genomes in 191,000 groups, and the
  grouping took 96 to 99% of the observer's time, about 11 of the run's
  17 hours. The fix is an index of each 3-byte run's groups. A genome
  joins only a group sharing at least ceil(threshold × n) of its n runs,
  so only the groups on its n − ceil(threshold × n) + 1 rarest runs'
  lists are checked, in group order, with the same comparison. That
  picks the scan's group every time, so no result changes and 13.2
  stands as written. A test checks it against a scan written apart, at
  four thresholds, and fails when the filter is broken. Seven places
  replayed with the old and new observers put 173,821 genomes in the same
  groups, with the observer 25 to 38 times faster. Seed 3 rerun in full
  matched gate2-2 byte for byte: samples, windows, genome dump, report
  and the places in its snapshots. Its last tenth took 1.7 times its
  first, where gate2-2's took 9. Gate 3 starts without this cost.
- 2026-11-16: Gate 3's build plan (build-plan-gate3.md) drafted, and
  Damian agreed its eight design gaps as proposed ("Agree all"), now in
  the design. (1) Gate 3 builds the map's data side: land and water,
  places by density with shuffled ids, data links (the Gabriel graph on
  each landmass) and fixed bridges, each link's length, bandwidth and
  delay, and the fields for hardware and OS values. Proximity links,
  water as a conductor, grid borders, the cooling field and the day's
  sweep wait for gate 4, and the supply stays gate 2's (2, 6.1). (2)
  The gate 3 runs stay at 48 places a seed at gate 2's budget of 10^12
  instructions: at 1,000 places that budget buys under a million ticks,
  and stage 4 took 5 to 15 million. Test worlds at 1,000 and 10,000
  places test the mechanics at full size (14.1, appendix E). (3) A
  lineage spreads when it settles a place it arrived at: stage 4's hold
  there, with the same numbers, its first member a colonist, and only
  copies born there counted. An arrival carries its sending program for
  the observers, as a boot carries its block's last writer, so the
  colonist's lineage carries on (13.3, 14.1). (4) A neighbour SENSE
  costs a round trip outside the queues, answered by the neighbour's
  OS, and reads the neighbour as it was at the barrier the request
  landed; every reading but the id is 0 if either network device is
  off. Neighbours are data neighbours (11.1, A.4). (5) An arriving
  message goes to the longest-waiting RECV first, with no interrupt;
  otherwise every thread with a data handler for its port gets the
  interrupt pending, raised once however many messages join (9.5). (6)
  Inboxes are lost at a cold start, with port 0's queue, and kept whole
  until then, replacing 11.5's fade, which had no cells to reach; a
  network device that cuts out drops everything queued at it; a message
  whose next hop is dark is dropped at its queue's head and counted
  stranded, 11.6's lifetime of 0 until gate 5 (11.4, 11.5). (7) Time on
  the network is in whole ticks, at least one a hop, a hop at most per
  barrier, and messages meeting at a queue join by their source place's
  id, then their order there (11.3, appendix C). (8) Each value gates 1
  and 2 drew evenly is drawn as before, then dealt out to the places in
  the order of its own field, nudged by a small scatter, so the world's
  mix stays gate 2's and only where values sit changes; map.fields turns
  them off, and world.map is landscape, none, grid or ring (6.1, 6.2,
  7.3, 9.6). The plan's numbers and pass mark (its section 7) are agreed
  before the first gate 3 run.
- 2026-11-16: Gate 3's step 2, the map, built and checked (build-plan-gate3.md
  11), and Damian agreed its three build choices ("Agree, start step 3").
  (1) Every landmass is bridged: after the bridges of up to 2 a pair, no
  longer than a quarter of the map's height, a landmass still apart is
  joined to the rest by its shortest crossing, whatever its length (a
  join). Section 7 asked for both, and they can't both hold when a
  landmass's nearest crossing is longer; at 48 places 5 of 6 seeds needed
  a join. (2) The longest bridge stays in the map's height, as section 7
  gives it, an exception to 6.1's lengths in average link lengths. (3)
  Two placeholders joined section 7's numbers: the fields' noise scale,
  3 across the map as the land's, and bandwidth spread evenly on a log
  scale by its field's rank. With no map and the fields off, gate 2's
  world reran seed 5 to 1.6 million ticks byte for byte (6.1, 7.3).
- 2026-11-16: Gate 3's step 3, links, routes and the network device, built
  and checked (build-plan-gate3.md 11), and Damian agreed one change ("Yes,
  at least 1 KB"): a link end's queue never holds less than the message
  size limit. At 8 ticks of the narrowest bandwidth, 16 bytes a tick, a
  queue held 128 bytes against a 1 KB limit, so a message bigger than a
  link's queue could never cross it: about 16% of land links were closed to
  a 200-byte message and 75% to a 1 KB one. Big messages are still dropped
  first when queues fill, but no link is closed to them (11.5, the plan's
  section 7).
- 2026-11-16: Gate 3's step 4, SEND, RECV, PING, arrivals and handlers,
  built and checked (build-plan-gate3.md 11). Damian settled three choices
  before it was built. (1) A SEND or PING to its own place is delivered
  locally, after the network device's latency, crossing no link; so a PING
  to itself takes twice the latency ("Deliver locally"). (2) An idle run
  waits on its core for its SEND's transmission and its neighbour SENSE's
  round trip, holding the core; a ready thread can still take it, which
  ends the run ("Wait, holding the core"). (3) A PING's request and echo
  are 8 bytes each, a new placeholder in the plan's section 7 ("8 bytes")
  (11.2).
- 2026-11-17: After gate 3's step 7 (the test worlds, build-plan-gate3.md
  11), Damian agreed one change ("Yes, change it"): a static run that
  starts in the bytes an arrival last landed in the reset region, while
  all of them are as they landed, takes that arrival's sender as its
  parent, as the arrival's own prompt run does; a later landing over any
  of them, or any other change to them, ends that. A program it makes is
  born from the arrival too: a colonist if it's its lineage's first member
  at the place, and not a copy made there. Before, 13.2 gave every static
  birth no parent. In the test worlds a core's next static run after an
  arrival starts region.step (13) bytes into it, and static runs took up
  the migrants' landed bytes and began lineages of their own: with them,
  every place had a colony of the migrant's code, while the migrant's
  lineage alone reached 29% to 52% of them. So 13.3's measure missed most
  of the spread (13.2, 13.3).
- 2026-11-17: Damian kept 10.2 as designed after gate 3's step 7 ("Keep
  10.2"): a ready thread can still take the core from a prompt run, as from
  any idle run. In the sensing test losing the core was not the main reason
  arrivals failed (28% to 37% of those that did nothing; most of the rest
  found no room on the device they asked for), though in the blind test it
  was (73%). And with 13.2's change above, an arrival that loses its core
  leaves its bytes for a static run to carry on its sender's lineage (10.2,
  13.2).
- 2026-11-17: After gate 3's step 8 pilots and flood test
  (build-plan-gate3.md 11), Damian changed two rules ("Build and pilot both
  but NOT off by default"). (1) An idle run may not SEND: its SEND fails
  soft, so code leaves a place only in a program's SEND; a setting
  (os.idle_send) brings the old rule back (9.2, 10.3, 10.4). (2) Arrivals
  from other places hold at most a set share of a place's cores at once
  (region.arrival_share, half), rounded down but at least one, so static
  keeps the rest; a boot block is never held back (10.4). Before, a SEND
  straight from an idle run's start sent the run's own bytes, and landed at
  a port 0 those bytes ran and SENT themselves on: in the flood test over
  99% of idle runs' SENDs sent their own bytes, mostly to a port 0, and as
  the queues filled, arrivals took every core that came free, so static
  runs' share of the cycles fell below 2% and the origin of copying starved
  (1 seed of 10 at stage 1 by tick 2 million, against gate 2's 6). Both
  are piloted, each alone and together, before the 20 seeds run.
- 2026-11-19: **Gate 3 passes**, at its second run (runs/gate3-2).
  Damian confirmed it ("Yes, passed"). The run was the 20 seeds with
  build-plan-gate3.md 7's numbers and the pass mark agreed in advance
  (2026-11-17), on the build of 2026-11-18 with the observers'
  redesign; the first run (runs/gate3-1) was stopped with 11 seeds done,
  slowed late by a bug in the observers. A lineage settled a place
  beyond the one it began in, in 17 of the 20 seeds (the mark is 5):
  9,187 settlements of 1.57 million colonies founded. The three seeds
  without (3, 6 and 18) never got going before their budget ran out.
  The origin regression reached stage 4 in 18 of 20 seeds (the mark is
  10; gate 2 13), and stage 5 in 17. 57,803 of 57,808 counted windows
  turned over, and 50,251 of 50,255 crowded ones, from 17 seeds (the
  mark is 90% of each). Strict places never passed stage 3, so all the
  life at stages 4 and 5 is in lax places. A scan of the run's 43 world
  saves found no corruption, and one thing left for Damian to decide: a
  thread's saved registers are written at its stack's address after its
  program gave the stack away or freed it (14.2). The overnight changes
  of 2026-11-19 (forgetting for a run with no end, 13.2; the window
  never waiting on a seed; the streamed save) are still for him to
  confirm (14.1, build-plan-gate3.md 11 step 8).
- 2026-11-19: **A live thread's stack is in use** (9.4, A.4). Damian
  chose it of three ("I'll go with your recommendation"; the others were
  to leave it as the world's physics, or to end a thread when its stack
  leaves its program). A SPAWN into the stack of a live thread of the
  caller's program, the caller's own included, starts another thread of
  that program and gives nothing; a FREE of it fails. It extends R15 (an
  allocation in use is never given away). Before, the OS went on writing
  a thread's 24 bytes of saved registers at its stack's address after
  the program had given the stack to a new program or freed it, into
  memory another program held, on a strict place too; and the thread's
  end could free a later allocation of its program that had landed
  there. The bug scan of gate 3's worlds found it: 48 of 1,015,757
  threads in the run's 43 saves. A switch (os.stack_held, on by default)
  brings the old rule back, so gates 1 to 3's runs can be run as they
  were; they were all made under the old rule. A world saved under the
  old rule and carried on under the new keeps its threads in that state
  until they end.
- 2026-11-19: Damian confirmed the overnight changes of 2026-11-19 as
  built ("Yes, they stand"): forgetting for a run with no end
  (observe.forget, 13.2; build-plan-observe.md 3.5), and viewer step 10
  (build-plan-viewer.md): the window never waits on a seed, the world's
  save is streamed, saves and progress lines come by the clock, `nova2
  check` reads a saved world through the engine's checks, and a run's
  window writes its own picture to a file. He had approved them in
  advance the night before, to be told in the morning.
- 2026-11-19: **Gate 4's plan agreed** (build-plan-gate4.md). Damian
  agreed its ten design gaps, its pass mark and the stack rule's pilot as
  Claude leaned ("Agree as leaned"). Nothing is built yet, and the plan's
  other numbers (its section 7) are still to agree before the first run.
  What was agreed, with what it was chosen over:
  - **What "the physics shapes it" means** (14.1). Each place, in each
    window of 200,000 ticks, is hot, dim or fair, by the core time it
    lost to heat trips and to cut-outs; the gate judges the programs on
    each kind of ground, with the regressions beside it. The two named
    checks (creep on soft sources, heat settling or oscillating) are
    reported with fixed definitions, not judged, since either outcome is
    an answer. Chosen over judging the regressions alone, which a physics
    that changes nothing would pass, and over judging whether life
    follows sources as they come and go, which rewards short-lived
    sources against the origin.
  - **The runs are at 48 places a seed** (2), with a band about 3 links
    wide, and test worlds at 1,000 places for the geometry. A 10-link
    band has no room on a map about 7 links across in 5 to 14 landmasses.
    Chosen over 200 places at four times the budget (about a day and a
    half, and no longer like with like).
  - **Sources are many and small,** about one for every four places, and
    dark ground is left to luck (2, 4.2). Chosen over a few large ones,
    and over guaranteeing every landmass a source.
  - **The day's span is a setting,** by default a whole day across the
    map (4.3). Chosen over a fraction of a day, where the whole map has
    night together.
  - **The constant draws** stay constant inside a device's range and rise
    as (voltage ÷ maximum)² above it (4.4, appendix C). Chosen over
    leaving them constant, which would let a swell heat only working
    cores, and over scaling them with voltage² everywhere, which would
    change every place's draw from gate 3's.
  - **The low-voltage interrupt** (9.5, 12.4): a thread takes it once,
    when it starts a quantum on a core whose voltage is within the margin
    of the core's cut-off, and again only after the voltage has been back
    above the margin. As written before, it went to the thread on the
    core as the voltage neared the cut-off, which in a crowded place is
    rarely the thread on it when it cuts out. Also considered: every
    thread with a handler, which would wake sleepers that were safe.
  - **A heat trip of the reset region's memory ends in a cold start**
    when it cools (5, 10.5). Chosen over letting it come back with
    whatever fading left.
  - **Swells and drop-outs start and end at once** until gate 5 brings
    ramps (2, 4.3). Chosen over bringing ramps forward.
  - **No SENSE reading of a device's temperature** (12.4). A new reading
    would renumber SENSE's for every evolved program.
  - **The forms left "details at build"**, as a set: the solve's formats
    and sweeps (appendix C), sources' schedules and landing (4.2), water
    crossings and shore drains (6.1), grid borders (6.2), cooling,
    leakage, heat between places and fade when hot (5), and cooling
    devices (7.7).
  - **The stack rule's pilot** (14.1): gate 4's regressions are the first
    20 seeds under 9.4's rule that a live thread's stack is in use, so a
    10-seed pilot of gate 3's world under that rule runs first, to tell
    its effect from the physics'. Chosen over rerunning gate 3's 20
    seeds (about 9 hours), which comes back to Damian only if the pilot
    shows a difference.
- 2026-11-20: **Four mechanics chosen for gate 5** (Damian), from the
  "chaos" mechanics discussed: flaky parts, straits that part and
  rejoin, an out-of-memory killer with a watchdog, and migration out of
  full places, each a setting with an off so a run can compare with and
  without. The same day **replication currents replaced migration**
  ("agreed on the weakness of the current and yes this should replace
  migration"): migration would have moved programs unasked, against 9.1,
  and gave nothing like wind or ocean currents. He dropped an OS update
  that reshuffles the call numbers, and didn't choose memory compaction
  or messier networks.
- 2026-11-20: **9.1 amended** (Damian; `build-plan-gate5.md` 2, gap 14).
  A fixed rule may act on a program by what it holds, or on a thread by
  how it runs, the same way for all, never by its id, lineage or kind.
  It reverses part of his decision of 2026-11-10, whose reason was that
  an OS choosing who dies is a selection pressure designed in, and gate
  2's turning down of a low-memory killer. Why now: late worlds thinned
  to one program a place with hundreds of threads (gate 3's seed 15) or
  over a thousand (`runs/open-2`), a failure the runs showed, which the
  design's rule for adding a mechanic asks for. Two rules use it, both
  at gate 5:
  - **the out-of-memory killer:** a place crowded (free memory under a
    tenth) for a day loses one program, drawn by the bytes it holds,
    stacks included, so a big holder is likeliest but never certain;
    cause "out of memory". Drawn by bytes rather than always the biggest
    holder (Damian's first words), so the second biggest isn't the safe
    size to be;
  - **the watchdog:** a thread that runs a million cycles without a call
    takes a new fault, "watchdog", which a handler can catch.

  Chosen over the watchdog alone with 9.1 kept, which leaves fan-out
  untouched since its threads call, and over a fair share of cores by
  program, which reverses 9.5's "the main lever is the number of
  threads". The risk 9.1 guarded against stays, so gate 5 has a
  comparison run with the four mechanics off.
- 2026-11-20: **Gate 5's plan agreed** (build-plan-gate5.md). Damian
  approved everything as Claude leaned: its fifteen design gaps, its two
  choices about runs and its numbers, the pass mark among them. Its
  numbers marked "(gate 4)" are worked again from gate 4's run and
  agreed then. Nothing is built yet. What was agreed, with what it was
  chosen over:
  - **What "stays interesting" means** (14.1): whether new kinds keep
    taking ground late in each run, by first leads (a genome group
    leading a place for the first time, at stage 4's numbers) in most
    late eras of 2 million ticks, in enough seeds; with the four
    regressions beside it, and the three named checks (cascades, reading
    input and injection, worn ground) reported with fixed definitions.
    Chosen over the regressions alone, which never test the question;
    over beating gate 4's run by the same measure, which could fail a
    world that stays interesting; and over 12.5's succession, one
    mechanism.
  - **The runs are at 48 places a seed,** at gate 4's budget (2). Chosen
    over four times the budget, and over a few long seeds at 200 places.
  - **Wear above a device's maximum doubles per 100 mV** (7.4, appendix
    C). Chosen over appendix C's straight line, which can't break a
    device with a spike without ruining it with a swell, and over a
    breakdown voltage, a law of its own.
  - **Worn memory grows weak cells** by its leak rank, which lose bits
    while it is on (7.4). Chosen over the raised threshold alone, which
    dropped 7.4's fixed spots.
  - **A link wears by its loss,** so its rising resistance speeds its
    wear (7.4). Chosen over wear by current alone, and by its ends'
    temperatures.
  - **Replacing the reset region's device is a hot swap,** not a cold
    start (7.8, 10.5). Chosen over a cold start, the reboot visit in a
    narrow form.
  - **A fresh draw takes the place's dealt values** (7.3), and a
    respawned place looks up its values in a table kept at setup. Chosen
    over even draws from each range, and over keeping the old values.
  - **A dark next hop is a failed link,** and a message's lifetime runs
    from when it lost its route (11.6). Chosen over keeping gate 3's drop
    for dark places.
  - **Rowhammer counts over a refresh window,** as appendices C and E
    had it (9.8). Chosen over 9.8's count a tick, which would make the
    threshold about one activation a tick.
  - **The falling cosmic-ray rate stays off** in the judged runs, a
    lever if gate 5 fails (2, 7.5). Chosen over turning it on.
  - **The forms left to build,** as a set: wear's arithmetic, failure,
    disks and levelling (7.4, 7.6), visits and respawning (7.8), cuts and
    rerouting's order (11.6), ramps (12.4), patches (12.3), strikes
    (12.2), rowhammer's counting (9.8) and the observers' incarnations
    (13.1).
  - **Flaky parts** (7.5): 2% of cores and memory devices with a floor 3
    to 100 times their kind's, a core's rising with wear. It settles what
    7.5 and 14.2 left open. Chosen over a spread on every device, and
    over a flaky region through the fields.
  - **Straits** (11.6): half the straits part and rejoin by turns, in
    spells of 2 to 20 million ticks drawn before the run. Chosen over
    `map.unbridged` alone, and over slow-mending cuts.
  - **The killer and the watchdog,** as 9.1's amendment above.
  - **Replication currents** (12.6): blocks of 256 bytes copied by
    address downstream along a heading that drifts by season, landing in
    the neighbour's reset region; weak by default. Chosen over landing
    in free memory, where nothing could run it, and at port 0, a prompt
    run for every block.
  - **The runs:** gate 4's ground record carries each place-window's
    leading group and lineage, so gate 4's run is gate 5's comparison
    with no rerun; and one comparison run of 20 seeds has the four
    mechanics off. Chosen over a rerun of gate 4's world (9 to 13
    hours), and over a run for each mechanic (40 to 56 hours).
- 2026-11-20: **No 20-seed run until gates 4 and 5 are both built**
  (Damian: "I want to hold off on doing the 20 seed run until both gates
  are fully built"). Gate 4's 20 seeds run first, then gate 5's numbers
  that depend on them are worked out, then gate 5's runs. So gate 5 is
  built before gate 4 is judged.
- 2026-11-20: **One 20-seed run for gates 4 and 5 together** (Damian:
  "we only need a single 20 seed test that is a combined gate 4+5. I
  don't think we need 2"). It replaces the order set earlier that day,
  gate 4's 20 seeds first and then gate 5's. `runs/open-2` stays on its
  own exe and keeps the host until that run needs it. What follows
  (build-plan-gate5.md, status and step 15):
  - **Gate 4's mark is read from the combined run's ground record.**
    Gate 5's wear, disasters and four mechanics act on the same ground,
    so it is reported as how the physics shapes life in gate 5's world,
    not as gate 4's physics alone. Beside it, the same measure on
    place-windows gate 5 left alone (fresh ground, no patch, no kill).
    Claude proposed judging the mark on the whole run as agreed,
    reporting the untouched reading, and making a run of gate 4's world
    (9 to 13 hours) only if the mark fails; that is for Damian.
  - **Gate 5's numbers that depend on gate 4** are worked from gate 4's
    pilot (10 seeds to 2 million ticks), its build's test worlds and
    gate 3's DWRITE rates, instead of gate 4's 20 seeds.
  - **The ground-record columns** agreed earlier that day for reading a
    separate gate 4 run have no such run to read. They were already
    built and tested when the change came, so they stay in: ten columns
    at the end of each ground row, observers only, with gate 4's first
    twelve as they were. Gate 5's step 1 starts from them. Gap 1's
    comparison of late change against gate 4's world now needs a
    full-length run of gate 4's world.
  - **The 1.6-million-tick reference** from gate 4's build, for gate 5's
    equivalence test, stands, and was made that day
    (`runs/gate4-reference`: seed 1, the same on 2 threads and across
    two builds). Seed 1 has no life by that tick, so it checks physics
    and static only.
  - **The comparison run stays** (Damian, asked whether the single run
    dropped it too: "Keep the comparison"). So the runs are the one
    combined 20-seed run and the comparison run with the four mechanics
    off, as agreed earlier that day. Claude had raised turning off every
    gate 5 strength instead, gate 4's world at full length, and leaned
    against it: the four's part, the killer's above all, would be mixed
    with wear's and the disasters'. The agreed form stands.
  - **Gate 4's pilot may run beside `runs/open-2`,** small and at low
    priority (Damian, asked whether the half-hour pilot might: "Yes,
    beside open-2"). It is the earliest check that gate 4's world still
    makes life, and the source of gate 5's numbers that depend on gate
    4. Gate 5's pilot and the test worlds weren't asked about.
- 2026-11-20: **Storage: less stored before any compression** (Damian,
  after the runs' disk use was measured). The build of gate 5 does these
  first, and they hold for the combined run of gates 4 and 5 and its
  comparison run (build-plan-gate5.md 7):
  - a finished seed keeps only its final save pair; world saves write
    only records in use; finished lineages get a row only if they
    lasted, the rest counted; samples are written once, in binary, with
    CSV on request;
  - **every second sample is written,** a row each 200 ticks ("we can
    probably get away with halving the sampling to every 200 ticks").
    The stage 4 and settling clocks are checked at each sample and
    start again at one failed check, so sampling itself at 200 would
    have let those holds come sooner and eased the origin and spread
    regressions. So the observers still sample every 100 ticks and only
    the written rows halve ("Agreed"): no measure changes. Chosen over
    sampling at 200, with or without those clocks kept at 100;
  - **genomes are kept by the existing rule** ("Remembering extinct
    lineages is kind of the point, but I would want it limited to
    genomes that actually had at least 3 generations of descendants.
    There's no point is saving what is basically just random noise").
    Asked whether that meant one generation deeper than the rule built
    on 2026-11-18, a grandchild, he said "Keep existing rule".
    `observe.keep_generations` stays 3, with `observe.forget` off;
  - **kept genomes are stored as differences from their parent,** with
    a whole copy at least every 16 steps along a line ("I like the idea
    of storing a key frame every 16 changes, I think it is worthwhile
    to keep in");
  - **the periodic snapshots of each place are dropped;** the stage 4
    and end-of-run ones stay ("Agreed").
- 2026-11-20: **How gate 4's mark is judged in the combined run**
  (Damian: "Agreed", to Claude's proposal). On the whole combined run;
  the same measure on the ground gate 5 left alone is reported beside
  it and brought to him if the two disagree; a run of gate 4's world
  alone, every gate 5 strength off, is made only if the mark fails.
- 2026-11-20: **Programs' calls are counted by kind** (Damian: "Yes,
  add it"). From the combined run of gates 4 and 5, and so its
  comparison run, each written sample carries the world's totals of the
  calls programs' threads make, by kind, all 16; SENDs to a port other
  than 0; and the calls that succeed, by kind, if that is cheap. The
  parent session traced 2,100 of `runs/open-2`'s kept genomes, 100 for
  each million ticks:
  - from tick 1,000,000 on, 96 to 98% call SPAWN and 94 to 97% ALLOC;
  - SETHANDLER fell from 39% in ticks 1 to 5 million to 16%, and SEND
    from 24% to about 10%, all of it to port 0 but 3 genomes';
  - every other call (SENSE, FREE, PROTECT, DREAD, DWRITE, RECV, PING,
    SLEEP, SETCLOCK, SETCORE) sat at 1 to 6%, what random bytes give,
    and none rose; kinds of call a genome fell from 2.9 to 2.4.

  Its reading: one of those calls rising above about 5% in the combined
  run and not in the comparison run would be a direct sign that gates 4
  and 5 gave programs something worth sensing or keeping. The counts
  are reported. Whether any is judged is Damian's to decide, and he
  hasn't been asked.
- 2026-11-20: **Gate 4's numbers chosen, and its mark changed before
  any judged run** (Damian, by way of the map-design session; the
  detail is in build-plan-gate4.md 7 and 11, and this entry is what
  gate 5's plan takes from it).
  - **The numbers:** the set that follows from 7's own rules with
    nothing tuned ("rules only stays closer to my original vision"):
    heatsinks 0.98 and 0.124 W a degree, the cores' heat capacities
    19.6 and 2.48 J a degree, sources 100 to 800 W, links 2.5
    milliohms; the sources' stiffness unchanged. Gate 5's equivalence
    test compares with `runs/gate4-reference-d`, made at them;
    `runs/gate4-reference`, at the placeholders, is kept as it was.
    Gate 4's pilot is running beside `runs/open-2` at them.
  - **Why the mark changed,** plainly: in the one seed with life under
    gate 4's physics (1.6 million ticks, under numbers not then
    chosen), the place the replicators filled had no heat trips before
    them and lost 42 to 50% of its core time to trips after. So hot
    windows started with 334 programs and fair ones with 3.6: the
    first wording was failed by a factor of 90, the wrong way round. A
    crowd heats its own place and sags its own source, so ground named
    by what happened to it follows the load. Ground with nothing
    living on it was never hot.
  - **The naming:** ground is named by what a full place gets there,
    with every core of that place kept busy and nothing else living,
    worked from the seed and its sources, and again as sources open
    and end. Hot is over a quarter of its core time lost to heat
    trips; dim is over a quarter lost to cut-outs, or its reset
    region's memory off over a quarter of the time; fair otherwise. A
    crowd can no longer change the name of its own ground.
  - **The direction:** the mark is met if fair and hot ground differ
    by at least a factor of two in the programs their place-windows
    start with, whichever way, and fair and dim ground likewise; both
    must hold, and each direction is reported ("Either direction",
    asked which of two wordings). The rest stands: the seeds that
    reach stage 4, lax places, at least 100 place-windows of each kind
    from at least 5 seeds, and the regressions.
  - **It is an easier test than the first.** The mark as first worded
    and named (ground by what happened to the place in the window;
    fair at least twice hot and twice dim) is reported beside it and
    not judged, as is the reading on the ground gate 5 left alone.
  - **The classes** are worked after the run by a tool, a class for
    each place every 2,000,000 ticks, from the seed.
  - **Not settled:** under gate 5 a place changes over a run, so
    whether its class is worked on the place as built or as it stands
    at that tick. Claude's lean, in build-plan-gate5.md step 15 and
    not put to Damian: as built, on the first map, with only what is
    fixed before the run at that tick (the sources' schedules and the
    straits' partings); wear, failures and patches out; a respawned
    place's later windows left out and counted.
- 2026-11-20: **The index that finds a genome's group isn't saved**
  (Damian: "Yes, drop it"). It was over half of a long seed's test
  save (3.3 GB of gate 3's seed 2's 6.0 GB), and is built again from
  the kept genomes when a seed is carried on. No measure changes.
- 2026-11-20: **Gate 5's numbers that waited on gate 4, agreed**
  (Damian: "Agree as leaned"). Gate 4's pilot ran (`runs/gate4-pilot`,
  seeds 1 to 10 to tick 2,000,000 at its agreed numbers), and the rows
  of build-plan-gate5.md 7 marked for it were worked again from the
  pilot and from gate 4's measuring tool. The temperatures and draws
  are measured; the lifetimes are those put through the wear rule by
  hand, since wear isn't built; the link currents are estimates.
  - **Changed: the links' reference current is 30 A,** the draw gate
    4's own link row is worked at, where the row had "the draw of an
    average place". It was written when a full place drew 23 W;
    measured, one draws 45 W and the sources are twice the size, so at
    15 A every link would have worn four times as fast as meant.
    Chosen over the pilot's own mean draw, 14.5 A.
  - **Changed: a strike is 20 V behind about 3 mΩ,** by the row's own
    rule (ten times a large source's rated current), a large source
    now being 500 to 800 W.
  - **Left as it was: device endurance, 200 million ticks.** It does
    what its row asks at both ends (an idle place's devices last about
    600 million ticks, hot ground's cores 4 to 9 million). The
    measurements add that on fair ground a full place of 8 cores wears
    out its E cores in about 19 million ticks and its P cores in about
    62 million, since an E core at 1,000 mV rests as hot as a P core
    at 1,100. Left so, because worn ground late in a run is what the
    check on old, worn places reads. Chosen over 400 million.
  - **Stand:** wear's reference (45 °C, a doubling per 10 °C), the
    threshold's rise (50 mV) and the cascade window (10 ticks).
  - **Disk blocks stand at 10,000 writes,** though not worked as the
    row said: no record counts DWRITEs. Gate 3 had about 80 boots a
    place over a run. Checked at gate 5's pilot from the calls counted
    by kind.
  - Still open: how a place's ground class is worked in the combined
    run.
- 2026-11-20: **A place's ground class is worked on the place as
  built** (Damian: "Yes, as built"; left open in the two entries
  above). In the combined run a place changes as it goes: it wears, its
  devices fail and are replaced, straits part, and it may respawn. Its
  class, for gate 4's mark, is worked on the first map with each device
  as dealt and unworn, and with only what is fixed before the run at
  that tick: the sources' schedules and the straits' partings, which
  cut power for 2 to 20 million ticks.
  - Left out: wear and failures, which come from load, so that naming
    by them would let a crowd change its own ground's name again, the
    thing the new naming was made to stop; and a finished seed keeps
    no saves to read them from. Patches too: they are shorter than a
    window.
  - A replaced device keeps the values its place was dealt, so it
    changes nothing.
  - A respawned place lands at a new point, and when it moves comes
    from the run. Its windows from then on are left out of the mark
    and counted; if a seed has more than a few, the classing is worked
    again with the respawns applied before the mark is read.
  - Chosen over the place as it stands at that tick, worn and failed.
  - It asks the classing tool for the straits' partings at the tick.
- 2026-11-20: **A message lifetime of 0 reroutes nothing** (Damian,
  asked whether to keep "message lifetime 0 means no rerouting at all"
  as built: "Yes, keep it"). Gap 8, agreed earlier that day, says a
  message whose next hop is failed or dark moves to a new route if
  there is one, and that a lifetime of 0 gives gate 3's drops exactly.
  The two pull apart at 0: gate 3 dropped a message for a dark place
  even when another way existed.
  - **What holds:** routes are worked out once a tick, at the
    barrier's end, so a blocked message can reroute at the next
    barrier at the soonest. A lifetime of 0 leaves it none, and it is
    dropped where gate 3 dropped it. From a lifetime of 1 a blocked
    message waits and reroutes.
  - **Why:** it keeps gate 3's world exactly at a lifetime of 0, which
    the gate 4 equivalence test checks to the byte, and 11.5 already
    called gate 3's rule "11.6's lifetime of 0".
  - It touches only worlds run at 0: the equivalence test and the
    regressions. The judged run's lifetime is 1,000 ticks.
  - Chosen over rerouting at a lifetime of 0 too, which would have
    broken the equivalence with gate 4's world.
  - In `design.md` 11.6, and `build-plan-gate5.md` gap 8 and 11, step 5.
- 2026-11-21: **A place's incarnation is read from the log of
  respawns, not held in each record** (Damian: "Yes, log only"). Gap 7,
  agreed on 2026-11-20, said a place's records carry its incarnation.
  The build of respawning (step 6) left it out, so that no record
  changes size, and kept in the world the incarnation and the tick of
  each place's first respawn, which is what gate 4's mark needs to
  leave a respawned place's later windows out.
  - **What holds now:** every respawn is logged as a fact, as 13.1
    already listed, with its tick, its place, its new incarnation and
    its new point. A record's incarnation is the count of its place's
    respawns up to its tick. Step 11 builds the log, whether or not
    the pilot shows a respawn.
  - **Why:** nothing is lost by reading it from the log, and the
    storage changes of 2026-11-20 were made to shrink what is written.
  - Chosen over a field in every record, and over the build's first
    offer, to add it only if the runs showed respawns.
  - In `design.md` 13.1 and 7.8, and `build-plan-gate5.md` gap 7 and
    step 11.
