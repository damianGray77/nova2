# Nova 2 — Design

> **Status (2026-11-11):** revised after two external reviews (Astra and
> Fable). Every decision they raised is settled (R1–R30). What remains open
> is listed in 14: numbers and outcomes that need runs, and specifications
> to finish at each build gate (2). The program-facing rules are specified
> in the appendices (A–E). Nothing gets built until the design is agreed.
> Nova 1 (`E:\Projects\nova`) stays as it is; nothing here changes it.
>
> This file holds the **current design only**. Every decision's history,
> reasoning, the options rejected, and the dated log are in
> `design-history.md`, the original working document, kept word for word.
> When this file and the history disagree, this file is the current
> design. Report the disagreement as a bug.

Everything here is decided unless it's marked otherwise:

- **[open]** waits on a test run or a later choice (all listed in 14);
- **[starting point]** is an observer-side method that can be changed at
  any time without touching the world (13);
- **[details at build]** is a method whose exact form is fixed when it's
  built. Changing it changes the world, so it's fixed before any run;
- **[gate N]** marks a mechanism built only at gate N (2).

Real-world figures are anchors, not defaults. Section numbers in this file
refer to this file.

## 1. Aim

- Nova 2 is a more streamlined successor to Nova 1, with the same goal: the
  smallest set of rules from which self-replication, evolution and ecology
  emerge on their own, in a digital medium rather than imitated chemistry.
- **Nova 1's real problem:** its programs were half life-forms and half
  programs. Nova 2's programs are fully programs. Anything life-like has to
  emerge: eating, parasitism, death, cooperation. Nova 1's life-form rules
  are dropped:
  - ENGULF and INJECT;
  - digestion, wounds and bleeding;
  - dormancy, gestation and sleep as rules;
  - the death watchdog.
- **"Streamlined"** applies to the hardware and the way software meets it,
  not to the richness of the world. Complexity in the world is good;
  complexity in the rules is a cost (3, principle 8).

## 2. Approach and working rules

**How the design is made**

- **Mechanics are designed fresh.** Nova 1 is a source of lessons, not a
  template.
- **Design first:** nothing is built until the design is agreed.
- **Critique, not encouragement.** Claude critiques proposals, its own
  included, and labels opinions and unproven claims as such.
- **External review.** The design was reviewed by Astra and Fable on
  2026-11-11. The 30 decisions that came out of it (R1–R30, in
  `design-review-triage-2026-11-11.md`) are applied throughout.

**Nova 1's infrastructure is reused where it fits:**

- the UI frame, widgets, charts and setup page (the drawing layer is
  replaced, below);
- determinism;
- snapshots.

Other candidates are confirmed one by one when building **[open]**:

- the hand-written containers;
- the counter-based RNG and hashing;
- the settings registry and the thread pool;
- the run driver, replay logs and CSV stats;
- the analysis tools.

**Build order: five gates (R19).** Everything in this design stays in the
design, but each mechanism is built only when its gate comes. That is
principle 7 applied to the build.

| Gate | The question | What is built for it |
|---|---|---|
| 1 | Does a replicator arise from static? | The instruction set (8); every call that works within one place on a steady supply (9.1–9.5): memory, the scheduler, SLEEP, SETCORE, SETCLOCK's speed and errors, handlers for faults and timers, and the raw disk; the reset region (10.1–10.3); places with no links between them; the noise floors; the region measures (13.1); the facts log; the origin test (10.6) |
| 2 | Does a full place keep turning over? | Memory fade, cut-outs and the crash rule (7.6), edge errors (7.5), a varying supply (a source and a day at each place, below), power draw, the boot block's cold start (10.5), with port 0's queue and prompt runs for it (10.4) |
| 3 | Does life spread? | The map (6), links, SEND, RECV and PING (11), arrivals at port 0 from other places (10.4), the network device, handlers for arriving data |
| 4 | Does the physics shape it? | The voltage solve (4), heat (5), the day's sweep across the map, sources coming and going, swells, the low-voltage interrupt, cooling devices |
| 5 | Does it stay interesting? | Wear and visits (7.4, 7.8), respawning places, regional events and strikes (12), network outages and rerouting (11.6), rowhammer (9.8), the wear-levelling and replacement options; and four mechanics Damian chose on 2026-11-20: flaky parts (7.5), straits that part and rejoin (11.6), the out-of-memory killer and the watchdog (9.1), and replication currents (12.6) |

- **Why gate 1 has every call that works within one place (approved
  2026-11-11).** The origin test needs a SPAWNed thread to run, so the
  scheduler can't wait for gate 2. More widely, a call stubbed at gate 1 but
  live later would make the origin test answer a different question from
  the one the world asks: SETCLOCK, for example, costs 10,000–100,000
  cycles of a run's cap.
- **The supply before gate 4 [2026-11-14, `build-plan-gate2.md` 2; agreed
  with Damian].** Gate 2 needs a supply that varies, before the map and
  the voltage solve exist.
  - **Each place has a source of its own:** 4.2's model reduced to one
    place, a voltage behind an internal resistance. The place's own draw
    sags it, through appendix C's one-tick lag, which for one source and
    one place is a single division, not a solve. So crowding matters from
    gate 2 (R1).
  - **It varies by 4.3's swings and drop-outs, and a day at each place:**
    capacity rising and falling over the day's length, each place at its
    own phase drawn from the seed. All of it is drawn before the run and
    never reacts to the world (principle 3).
  - **Power draw comes with it** (7.1, appendix C). Heat stays at gate 4, so
    temperature is constant and leakage a fixed factor.
  - **Gate 4 replaces it** with sources on the map, the voltage solve and
    the day sweeping across the map. Swells (and the heat trips they
    cause) come then too, and ramps at gate 5.
- **The map at gate 3 [2026-11-16, `build-plan-gate3.md` 2; agreed with
  Damian].** Gate 3 builds the parts of the map (6) that data needs, and
  the fields that make places differ:
  - land and water, places by density with their ids shuffled, data links
    and bridges, and each link's length, bandwidth and delay (6.1);
  - the fields for the hardware and OS values (7.3, 9.6).

  Proximity links and what they carry, water as a conductor, the
  resistance field and grid borders, the cooling field and the day's sweep
  come with gate 4, which is the first to need them. The supply stays gate
  2's until then. On land the two Gabriel graphs are almost the same links
  (6.1), so gate 4 adds the water crossings without moving what gate 3
  built.
- **The world at gate 4 [2026-11-19, `build-plan-gate4.md` 2; agreed with
  Damian].**
  - **The runs are at 48 places a seed,** at gate 3's budget, so the
    regressions compare like with like. A 10-link band has no room on a
    map about 7 links across, so in those runs the links resist enough
    for a band about 3 links wide. They show the kinds of ground place by
    place (14.1), not the geometry of bands.
  - **Test worlds at 1,000 places,** with placed replicators and the
    10-link band, show bands, creep and heat at the size the design is
    for (R7: evidence about the mechanism, not about life).
  - **Sources are many and small at that size** (4.1), about one for
    every four places, with lives long against the time to stage 4. Dark
    ground is left to luck (4.2).
  - **Swells and drop-outs start and end at once.** Ramps come at gate 5
    (12.4), for both.
  - **Two switches:** `supply.model`, `sources` (the default from gate 4)
    or `place` (the supply before gate 4, above); and `heat.on`. With the
    supply by place, heat off and `os.stack_held` off (9.4), gate 4's
    engine gives gate 3's world.
- **The world at gate 5 [2026-11-20, `build-plan-gate5.md` 2; agreed with
  Damian].**
  - **The runs are at 48 places a seed,** at gate 4's budget, judged on
    each seed's late eras (14.1). Long runs are the open-ended run's job
    once gate 5 passes, watched rather than judged; the full-size map
    waits for the optimization work after gate 5. Chosen over four times
    the budget (one and a half to two days, and no longer like with
    like), and over a few long seeds at 200 places of 4 MB (too few to
    carry a mark across seeds, principle 5).
  - **The falling cosmic-ray rate stays off** in the judged runs (7.5),
    so the regressions compare like with like. It is a lever if gate 5
    fails its measure, and a candidate for the open-ended run after.
  - **The four mechanics Damian chose on 2026-11-20** (flaky parts,
    straits, the out-of-memory killer and the watchdog, replication
    currents) are each a setting with an off. They are on in the judged
    run, and one comparison run of 20 seeds has all four off, to tell
    what they do from what life does. Damian kept it when the runs
    became one combined run, below ("Keep the comparison").
  - **With every gate 5 strength off,** and the message lifetime 0
    (11.6), gate 5's engine gives gate 4's world.
  - **One 20-seed run judges gates 4 and 5 together** [Damian,
    2026-11-20: "we only need a single 20 seed test that is a combined
    gate 4+5. I don't think we need 2"; earlier that day he had held the
    20-seed runs until both gates were built]. It is made once both are
    built. Gate 5's numbers that depend on gate 4 are worked from gate
    4's pilot and build. `runs/open-2` keeps the host, on its own exe,
    until the run needs it; gate 4's pilot may run beside it, small and
    at low priority [Damian, 2026-11-20: "Yes, beside open-2"].
- **Calls before their gate.** Until gate 3, SEND and PING fail as they do
  when the place's network device is off, and RECV waits on inboxes that
  never fill. They charge their real costs. From gate 3 they're live (11).
- **The GPU and NPU are parked (R20).** They're designed, but not built
  until a gate names the condition in which using them wins.
- The viewer's views follow the same gates. Most of 13.4 shows things that
  won't exist before gate 3.

**Settings**

- **Every number in the world is a setting, with one boundary (R24).**
  - **Structural constants are part of the design,** not settings:
    - the instruction width (8 bits), the opcode table and its encoding;
    - the register count and width;
    - the call table's shape and argument conventions;
    - page sizes being powers of two.
  - **Everything else is a setting:** rates, sizes, catalogue values, field
    parameters.
  - **Setup checks the constraints between settings** and refuses a world
    that breaks one. For example, the reset region must fit its memory
    device; thresholds must be ordered (memory below cores, 4.4); heat steps
    must be stable (appendix C).
- **Settings are fixed for the run.**
- **The setup page has a short main page,** with everything else in an
  advanced tier.
- **A law with a strength gets a slider.**
  - Usually 0 means off. Where a slider's natural "off" is a different
    value, the slider says so: the respawn share and the replacement
    threshold are off above 1 (7.8).
  - Switches are still welcome, for experiments.
- **New features are on by default,** once their gate is built. Tests run
  the default world, and use switches only to test a law on its own. Nova
  1's tests ran on old defaults, so they tested a different world from the
  one that ran.
- **Hand-placed programs are allowed in unit tests and test worlds, never in
  runs (R7).**
  - A test world checks mechanics: SPAWN, the disk, queues, conservation,
    snapshot/resume and thread-count equivalence.
  - Its results never count as evidence that life arises.
  - Runs, the experiments about spontaneous life, never seed anything
    (10.1).
- **No backward compatibility while in beta:** no legacy defaults, and no
  loading old files.

**Code rules**

- **No external dependency without Damian's approval first.** That means
  any library, package or required installed tool.
  - **Already approved:** the Windows SDK platform libraries Direct2D,
    DirectWrite and WIC (the Windows Imaging Component, approved 2026-11-11)
    for the UI; Direct3D 12 and its system shader compiler
    (`d3dcompiler_47`) for a future GPU backend.
  - Anything else is asked first, such as the separate DXC compiler or
    D3D11On12.
- **C++ written like C with classes.**
  - **Allowed:**
    - namespaces;
    - structs and classes, with constructors and destructors (RAII);
    - references, `const` and `constexpr`;
    - enums;
    - operator overloading for small maths types;
    - simple templates for containers.
  - **Not allowed:**
    - exceptions or RTTI;
    - inheritance hierarchies;
    - virtual functions in the engine;
    - lambdas;
    - template metaprogramming.
- **No C++ standard library:** nothing from `std::`. Thin C-runtime headers
  (`cstdint`, `cstddef`, `cstdio`, `cstdlib`, `cmath`) and platform headers
  (`windows.h`, intrinsics) are allowed. In Nova 1's words, "the line is
  no-STL, not no-C-runtime".
- **The engine style (R22).** Tick code uses:
  - flat arrays and fixed-size records;
  - no allocation, recursion or virtual calls inside a tick;
  - integer and fixed-point arithmetic only;
  - an explicit order for everything inside a place (rotating quanta, 9.5).

  That keeps the engine fast on the CPU and portable to a GPU later. The
  code isn't required to compile as HLSL now: the confirmed C++ features
  aren't HLSL anyway. If a GPU backend is ever approved, the engine is
  ported then.

**The UI is drawn with Direct2D, not GDI (R23).**

- The reason is antialiasing.
- **On screen:** Direct2D draws into a window target, and DirectWrite draws
  the text. No Direct3D is involved.
- **Off screen:** headless renders and tests draw into a WIC bitmap. That is
  software-rendered, so the pixels are identical on every machine, and it
  gives the pixel read-back the tests need.
- **Later, if needed:** very large point sets may need Direct3D's instanced
  drawing. That would need D3D11On12, which isn't approved yet.
- **The size of the work.**
  - Nova 1's canvas layer (`app/canvas.*`, 348 lines) holds all its GDI
    drawing, and is what Direct2D replaces.
  - About 70% of the viewer (`app/viewer.cpp`, 2,783 lines) and the view
    layer that feeds it (`core` `view.*`, about 2,800 lines) name Nova 1's
    concepts, and need reworking for Nova 2.
  - So the UI work is nearer 5,000 lines than 350. The frame, widgets,
    charts and the settings-driven setup page carry over.

**The engine: places run in parallel on CPU threads.**

- **Parallel by place.** Places are independent within a tick:
  - they share no memory;
  - messages, arrivals and disk I/O complete at a barrier once a tick;
  - power and heat are solved once a tick (appendix C).

  Nova 1 already ran this way. Its best was 898 million instructions a
  second on 24 threads, on a 1,000-node landscape.
- **What to measure:**
  - instructions per second;
  - bytes processed (one CPY can move thousands);
  - simulated time per second;
  - reproductive throughput (births per second at capacity).
- **Where the speed comes from:**
  - a much simpler interpreter (8);
  - locality: a place's memory fits a core's cache;
  - long ticks;
  - work stealing.

  Claude's estimate, to be benchmarked, is 2–5 billion instructions a
  second on a 24-thread machine. That's 2 to 6 times Nova 1.
- **The whole workload counts, not just the interpreter:**
  - the voltage solve;
  - topology changes and routing;
  - heat;
  - memory noise;
  - allocation;
  - observers and the archive.

  Storage needs a budget too (13.5).
- **The first benchmark** comes with gate 1. It measures:
  - the interpreter on one place;
  - its scaling across cores;
  - the cost of the per-access counters (rowhammer, memory draw).
- **The world hash and snapshots [details at build, step 9; 2026-11-11].**
  - **Every place's whole state** is saved field by field, never as raw
    structures, so padding can't reach it. The world's hash combines each
    place's hash with the seed and the tick.
  - **The hash covers exactly what a snapshot holds.** Access caches and
    report counters are left out: they change no result.
  - **Snapshots are taken at the barrier.** A world is rebuilt from its
    settings, then its state is read into it. So a resumed world keeps its
    settings, and only `run.*` can change.
  - **The network [details at build, step 5; 2026-11-16]:** with a map,
    the network's state follows the places', in the snapshot and in the
    hash:
    - each link end's queue, the message it's sending and what's on its
      wire;
    - every message on its way, with its bytes, and the deliveries due;
    - which places' network devices are dark.

    The routes aren't saved. They follow from the dark places, so a
    resumed world works them out again as they're asked for, and gets the
    same ones. A snapshot whose network fails its own check doesn't load.
    Each place's part includes its comms ports' inboxes, its neighbour
    SENSE requests, the threads waiting in RECV or for a PING's echo, and
    each core's wait for an idle run's SEND. Its outbox is empty at the
    barrier, since every SEND has joined the network by then.
- **A GPU backend is a later option, not a plan.**
  - It would be the whole engine resident on the GPU, to avoid per-tick
    transfers. Its best case is an unmeasured "a few times the CPU".
  - It's considered only if benchmarks at 1,000, 10,000 and 100,000 places
    show the CPU is the bottleneck, and a prototype shows a real gain.
  - It would have to give the identical world (the same hash), so a setting
    `engine.device` would only choose speed.
  - Snapshots are kept backend-neutral, so a run could resume on the other
    backend.
  - Nova 1's real GPU ran its lockstep warps slower than the CPU in mature
    worlds, mostly because of transfers. On a young world it was twice as
    fast (15.2 s against 31.4 s).

## 3. Principles

Twenty principles, adopted 2026-11-11. Each names where it's used.

**What the world is**

1. **Fully programs.** Anything life-like has to emerge (1).
2. **One physics.** The world is its initial conditions plus a short list
   of laws, applied everywhere, all the time. Nothing inside the world is
   scheduled.
   - Test: no law names an emergent thing (eating, a parasite, a disaster,
     death). And the laws fit on one page (appendix D).
   - Every event the viewer names is found by an observer (13).
3. **What comes from outside is a boundary condition.** The supply, the
   sources, the regional events and the maintenance visits follow functions
   fixed before the run. They never react to what programs do. Visits react
   only to the state of the hardware.
4. **Power in, heat out.** Power is the driving input and heat the sink, and
   energy is conserved. Without both, the world settles into equilibrium
   and nothing happens.
5. **Luck is part of the world.** Some seeds are better than others, so
   settings are compared across several seeds.

**How a rule earns its place**

6. **Every choice is a trade-off.**
   - Test: for every choice a program can make and every hardware model,
     name a condition in which the other side wins.
   - A rule earns its place only by creating a trade-off that programs can
     take either side of.
   - Trade-offs stay alive only where conditions vary in time and place.
7. **A mechanic is added only to fix a failure seen in a run, or to meet a
   stated test** (the guardrail).
8. **Variety in things, few laws.** Kinds are rows of numbers under the same
   laws: devices, sources, patches. Each kind is a trade-off curve and each
   instance a point on it, with small scatter, so that no device is best at
   everything. Every law costs computing time, and time spent on rule detail
   is time not spent on generations.
9. **Follow real design, and anchor every choice in real life.** Every rule
   cites what real hardware, networks or ecology do.
   - The abstraction rule prunes this: a physical quantity stays only if
     some program would feel the world differently because of it, in a way
     no other quantity already gives.
   - Laws work at the level of cells and instructions, nothing below.
10. **Stand-ins belong on the hardware side.** Device ranges, error curves,
    heat and wear stand in for the physics we don't simulate. Programs are
    exact at our level, so a rule that adds behaviour on the program side is
    a bootstrap.
11. **When realism and simplicity cost evolvability, prefer realism and
    simplicity, and let tests decide.** The origin test (14) is the check.

**What programs can do**

12. **No bootstraps.** No instruction or call hands a program a behaviour,
    and none names another program.
    - Test: its effect can be stated in terms of the world alone (cells,
      cores, devices, ports, links).
13. **Random code must be able to stumble on.** Random code has to keep
    running long enough to find a working call:
    - every byte is an instruction;
    - every call number and argument value is valid;
    - calls fail soft instead of faulting.
14. **Ownership is physical and recorded.**
    - The OS records who owns each page.
    - Ownership changes only by freeing, or by a gift to a new program at
      SPAWN.
    - Overwriting uses memory without owning it, and a hijack runs as its
      victim.
    - The dead aren't wiped: their bytes feed new life.
15. **Knowledge is local, and travel starts blind.**
    - Programs sense their own place and their neighbours. Anything further
      comes from other programs, or from PING.
    - The network is fire and forget. Only a receiver can confirm a message
      arrived (the end-to-end argument).

**Events and observation**

16. **Outside events ramp up.** Their first effects are their own warning,
    and some ramps are false alarms.
17. **Facts, not labels.**
    - The engine records only facts: what a law did.
    - Labels are observers' patterns.
    - Watching changes nothing.

**Settings and engineering**

18. **Every number in the world is a setting,** apart from structural
    constants (2).
19. **Settings mean the same at any size.** Lengths are counted in links and
    rates per 100 places, so a bigger world is bigger, not denser.
20. **Deterministic by construction.** The same seed and settings give the
    same world on any number of threads, and after a snapshot and resume.

**Risk:** a principle as broad as "one physics" can justify anything. Its
test is what gives it teeth.

## 4. Power

Power (electricity) is the world's driving input. Day and night come from
the supply rising and falling, as if the minds behind the universe were
balancing load for their own users. The physics is an abstraction, pruned by
the abstraction rule (3, principle 9).

### 4.1 The model

1. **Power in, heat out.** All power drawn ends up as heat. Energy is the
   conserved quantity.
2. **DC and Ohm's law, at one voltage level.** There are no regulators and
   no transformers.
   - Each source pushes its own voltage, and it sags along the links.
   - AC is left out: for programs it would add only transformers and a fast
     rhythm, which repeats the day.
3. **Resistance turns power into heat,** in links and in whatever draws
   power.
4. **Voltage sags with path resistance and draw.** The drop along a path is
   current × resistance. Places near a source don't get "first dibs";
   their voltage just holds up better when everyone draws.
5. **Power spreads from sources over proximity links (6.1),** not by a law
   of distance.
   - The falloff is the links' resistance, so reach depends on load: a
     crowded inner ring dims everything beyond it.
   - Programs gather around sources as people do around a fire.
   - The falloff must be wide, or the sources many and small, so that
     ecosystems sprawl instead of forming tiny dots.
6. **The voltage solve, with a one-tick lag (R12).** Devices aren't linear:
   their draw depends on voltage, speed, temperature and state. So each
   tick:
   - every device is treated as a conductance worked out from the previous
     tick's voltage and state;
   - every source is a voltage behind its internal resistance;
   - the network is then one sparse linear solve, in fixed point, warm-started
     from the last tick.

   A part of the graph with no source sits at 0 V. The lag is the world's
   physics, not an approximation to hide. Appendix C gives the equations
   and the update order. At about four links per place, the solve is
   affordable for thousands of places.
7. **A minimum off-time after a cut-out.** A device that cuts out stays off
   for at least this long (a setting), as real undervoltage lockouts do.
   Together with the restart gap (4.4), it stops a place's own sag making
   its devices flicker on and off.

### 4.2 Sources

- **A source is a place, a voltage, and an internal resistance (its
  stiffness).**
  - The most current it can give is voltage ÷ internal resistance.
  - There are no breakers.
  - Its own internal loss heats its place, so a hard-worked soft source
    burns those too close to it.
- **Softness may let crowds move in (a hypothesis).** Heavy draw near a soft
  source pulls its voltage down, which cools the devices near it, so life
  might creep inward.
  - But more current also raises the source's own internal loss, which heats
    its place. The net effect isn't known.
  - A stiff source holds its voltage, and keeps a permanent burn zone.
  - Whether a crowd settles, creeps in or oscillates is for runs to show
    (14).
- **A catalogue of source types.** Each type is a row of numbers:
  - voltage;
  - stiffness;
  - how closely it follows the day;
  - the size and slowness of its swings;
  - its swells.

  The starting types are steady (stiff, ignores the day, small swings, a
  night refuge), daylight, gusty, and the strike (12). Names are for the
  viewer only. Each type is a trade-off: steady but weak, or strong but
  erratic.
- **Sizes are skewed,** so most sources are small and a few are large. The
  spreads of voltage and resistance are settings, and current follows from
  them.
- **Sources come and go on a schedule seeded before the run.** A source
  grows, holds, then fades.
  - Settings, per 100 places: how many there are at the start, the chance
    of a new one each tick, and their average lifetime. The long-run
    average is chance × lifetime.
  - Real anchor: hydrothermal vents open and die, and their communities
    move with them.
  - **In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
    agreed with Damian]:** a source's landing point, type, size, birth,
    lifetime, swings, drop-outs and swells are a function of the seed and
    the tick alone. It grows over the first tenth of its life and fades
    over the last tenth, by its capacity. Lifetimes are spread evenly on
    a log scale from a quarter of the mean to four times it.
- **Sources land by area.** Each new source lands at a random point on land
  and joins the nearest place.
  - A place's chance is its catchment area raised to a power, set by a
    slider: 1 is by area (the default), 0 is by place, and above 1 favours
    the countryside more.
  - Consequence: the countryside is rich in power and poor in network,
    and towns the reverse.
  - Real anchor: sunlight and wind fall by area.
  - **In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
    agreed with Damian]:** the point is a random land cell of the raster,
    weighted so that a place's chance is its catchment's area raised to
    the power. The point is kept, for respawning (7.8).
- **Sources don't wear.** The "power companies" maintain them from outside
  the world.
- **Luck is part of the world.** A region, or the whole world, can go
  without power for a long stretch.

### 4.3 The day, swings and swells

- **Day and night sweep across the map from east to west.** The settings
  are the day's length and the night's depth.
  - **The day's span [2026-11-19, `build-plan-gate4.md` 2; agreed with
    Damian]:** a setting, how much of a day lies between the map's east
    and west edges. The default is a whole day, so it is always noon
    somewhere and the world's total supply stays about level.
- **The supply never reacts to what the world draws** (3, principle 3).
- **Night and swings lower a source's capacity, not its voltage.** They
  scale its internal resistance, as a solar panel in dim light holds its
  voltage under a light load and collapses under a heavy one. So night
  punishes crowding, not presence.
- **Random swings are moderate.** Each source drifts slowly and on its own.
  - The edges of its habitable zone feel the swings most, because the drop
    builds up along the paths.
  - The swings must sometimes take some place out of a device's range.
  - The setting is the swings' size relative to the width of the devices'
    ranges.
- **Drop-outs:** a source's capacity can drop low, or to nothing, for a
  while: a blackout from outside. It ramps down first (12.4). The drop-out
  rate, depth and length are settings for each source type.
- **Swells:** a source's voltage can rise above its normal level for a
  while, from 1.1 to 1.8 times normal, as in the power-quality standard
  IEEE 1159. Swells also ramp up (12.4).
- **Until gate 5 brings ramps,** drop-outs and swells start and end at
  once [2026-11-19, `build-plan-gate4.md` 2; agreed with Damian].

### 4.4 Devices on the supply

- **Each kind of device has a voltage range** (7.1).
  - Below its threshold it cuts out.
  - Near either edge it makes wrong results.
  - Above its maximum it heats, and past its heat limit it trips until it
    has cooled.
    - A working core heats there by its own law: its speed stops rising
      at the maximum, and its draw goes on rising with voltage².
    - The constant draws (memory's holding draw, the disk's, the network
      device's and a halted core's) stay constant inside the range, and
      rise as (voltage ÷ maximum)² above it [2026-11-19,
      `build-plan-gate4.md` 2; agreed with Damian].
- **Speed follows voltage.** Within its range a device runs faster and
  hotter near the top, and slower and cooler near the bottom.
  - Speed is in proportion to (voltage − threshold) (7.5), and working
    power to voltage² × speed.
  - With a threshold of 0.7 at a voltage of 1.0, 10% more speed costs about
    17% more power.
  - The sweet spot sits below the maximum, with no rule needed.
- **Bigger devices draw more current at about the same voltage,** and
  faster devices need a higher voltage. So the zones around a source form
  by speed: fast, hungry devices close in, and slow, efficient ones further
  out. Big devices are what make a place sag.
- **There are two limits:** the voltage range and the heat balance. There is
  no firmware power cap.
- **The gap between off and on:**
  - **at the high end,** the heat cut-out holds until the device has cooled
    to a lower temperature;
  - **at the low end,** one fixed restart gap for all devices, plus a
    minimum off-time (4.1). A device restarts a little above its cut-off,
    and not before the off-time has passed.
- **Memory and disk keep things differently.**
  - Memory loses its contents below its threshold, and faster when hot. It
    decays gradually: its cells leak at different rates, so errors come
    first, then loss.
  - A disk keeps its contents without power, but writing to it is slow and
    wears it.
  - Memory's threshold sits below the cores'. Cores cut out first, and
    memory last. Setup checks this for every drawn device, but wear raises
    thresholds, so a worn memory device can end up above a fresh E core.
- **Life forms bands:** too hot close to a source, too dim far out, and a
  habitable band between. A band's width is roughly the device's range
  divided by the voltage drop per hop. Different kinds get different bands.
  This follows from the laws above; it isn't a rule.

### 4.5 Who gets the power

- **Nobody is given power.** Devices draw it while they work, and every
  device at a place sees the place's voltage.
- **Life raises draw (R1).**
  - **A core draws working power only while it runs something.** A core
    with no thread runs the reset region (10.2). When that run faults or hits
    its cap, the core halts at idle draw until something new is written to
    the region, or a slow retry comes round. Real anchor: a machine with
    nothing bootable halts.
  - **Memory draws a holding power always, plus a power per access.** Real
    DRAM draws several times more when it's read and written than when it
    only self-refreshes. So busy programs raise their place's draw, and an
    empty place draws little.
- **Who works is decided elsewhere:** the scheduler shares the cores, and
  memory is accessed by whatever runs. So a program's share of the power is
  its share of device time and memory traffic.
- **Programs spend power four ways:**
  - more threads (SPAWN), which keep cores busy;
  - overclocking (SETCLOCK);
  - memory traffic;
  - heat.

  Sleeping sheds load: a core whose threads all sleep halts at idle draw.
  Their draw costs everyone at the place a little voltage, which makes
  power a commons. Power stays hidden from programs, as in real life.
- **In a brownout nothing chooses.** Devices below their threshold cut out,
  and memory fades device by device.

## 5. Heat

- **Where heat is made:**
  - in devices: their whole draw;
  - in links: their loss, split between the places at the two ends. Over
    water, the loss goes into the sea;
  - in each source: its internal loss, on its own place.
- **Two temperatures.**
  - **Each device has its own.** It rises with the device's draw and falls
    as heat passes to its place, at the device's heatsink rate (a catalogue
    number).
  - **Each place has an ambient temperature.** It rises with heat from its
    devices, links and source, and falls as heat leaves the world, at the
    place's cooling rate.
  - **Both change gradually.** Each has a heat capacity, and heat moves only
    from hot to cool.
- **Cooling is a rate, not a temperature.** It's how fast a place's heat
  leaves to the outside, faster the hotter the place is than the outside
  (Newton's law of cooling).
- **The outside temperature is constant.** It doesn't follow the day: days
  are hotter only because more power is drawn. Regional heat waves raise it
  for a while (12.3).
- **The world starts cold** (at the outside temperature) and warms as life
  grows. Early on everything is cool and clean, and the world gets harder
  as heat builds, so history matters.
- **No runaway at the scale of the world.** The heat made can never exceed
  what the sources deliver, and cooling grows with temperature.
  - A Venus-style greenhouse needs cooling to fall, and nothing in Nova 2
    lowers cooling.
  - Runaway stays local and bounded. Leakage (below) can run away on one
    device or one crowded place, but the heat cut-out stops it.
  - That bounds heat; it doesn't make it settle. A time-varying supply and
    cut-outs can keep a place oscillating. The heat update is clamped so a
    large rate can't overshoot (appendix C).
- **Leakage:** a device draws more as it gets hotter, which is real thermal
  runaway. Crowded, hot places tip over into cut-outs: a disaster nobody
  scripted.
- **The cooling map is fixed for the run,** generated once from the seed.
  - Water is a heat sink, and places on the coast cool well. Coastal
    cooling is read off the land raster.
  - Inland heat sinks come from a cooling field.
  - Sinks are placed independently of power sources, so life has to choose
    between power and cooling.
- **Heat spreads between places** along proximity links on land. Conductance
  falls with length, and a slider sets how much (0 means none). Heat doesn't
  cross water; it goes into it. Living next to a hot, crowded hub costs
  something.
- **Effects of heat:**
  - the cut-out and cool-down;
  - memory fading faster;
  - leakage;
  - wear (7.4).

  Heat causes no errors directly (R26). Errors come from the voltage edges
  and the clock (7.5).
- **Tuning:** given the sources' strength, choose cooling so that only
  places close to strong sources pass the devices' heat limits.
- **Cooling devices** are in the catalogue (7.7).
- **In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
  agreed with Damian]:**
  - a place's cooling rate is the base rate × its cooling field's value
    × (1 + the coast factor × the share of water among the raster cells
    bordering its catchment);
  - a land link's heat conductance is the setting ÷ its length in average
    links;
  - leakage is counted from the outside temperature, so a cold world
    draws what it did before heat was built;
  - a memory cell's retention halves for each 10 °C its device is above
    45 °C (both are settings);
  - a heat trip needs no off-time: the device comes back at its cool-down
    point. A thread ended by one has the cause "heat";
  - a heat trip of the memory device holding the reset region ends in a
    cold start when it cools (10.5).

## 6. Space

Two scales: a map of places, and the inside of a place. Hardware is fixed at
its place. A program travels as data (11) to find the hardware it wants.

### 6.1 The map

This is Nova 1's landscape, rebuilt. It's provisional: the first runs judge
it. A regular grid is kept as a test world only. A data-centre hierarchy
isn't used between places, because it has no positions for the day, the
fields or the coasts.

- **Land and water** come from Nova 1's land raster and its six land
  settings.
- **Places are scattered by a density field,** with a minimum spacing. Dense
  patches are towns, and sparse ones countryside.
- **Two sets of links, both worked out once from the positions:**
  - **Proximity links** carry power, and heat over land. They're the
    Gabriel graph over all places: two places are linked if no third lies
    between them. That gives about four links per place, with no crossings.
  - **Data links** carry SEND. They're the Gabriel graph worked out on each
    landmass by itself, plus the bridges.

  On land the two sets are almost the same links. They differ at water.
- **So links come in three kinds:**
  - **power only:** proximity links that cross water;
  - **data only:** bridges;
  - **both:** most links on land, which fail as one thing.

  Only links that carry current wear (7.4). Any link can be cut (11.6).
- **What a link has:**
  - its length, from the map;
  - its resistance: length × a per-length value from its own field;
  - its heat conductance, which falls with length;
  - its bandwidth, from its own field (11.3);
  - its delay, which grows with length (11.3).
- **Bridges** are data links across water, generated once from the seed and
  fixed for the run. They carry no power, because power is a proximity
  network, not a cable. Settings cover bridges per pair of landmasses, the
  longest bridge, and how many landmasses stay unbridged.
- **Water is a leaky conductor and a heat sink.**
  - Heat doesn't cross water from place to place: it goes into the water
    and leaves the world.
  - Power across water falls off exponentially over a **reach**, because
    the sea leaks current to the seabed. Real anchor: a live cable in the
    sea is dangerous only near it.
  - Each place bordering water pays a **shore drain** to the sea. Its size
    is read off the raster, from how much water the place borders.
  - Each proximity link that crosses water becomes one resistance between
    its two places, rising exponentially with the water's width.
  - Defaults: a drain of about one small device's draw at full voltage,
    and a reach of one link length.
  - **In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
    agreed with Damian]:** a link's water width is the part of it that
    lies over water cells, and its resistance is its land resistance ×
    e^(width ÷ reach). A place's shore drain is a conductance to ground:
    the drain setting × the share of water among the raster cells
    bordering its catchment.
  - Consequences:
    - coasts are cool and rich in data, and pay a small power tax;
    - narrow straits pass a trickle of power, and wide seas pass none;
    - islands near a mainland get a trickle;
    - distant landmasses are separate power grids, joined only by data;
    - small islands have long dark spells.
- **Lengths are measured in links.** Every length setting is in units of the
  average link length, so adding places makes the world bigger, not
  denser. The one exception is the longest bridge, which is in the map's
  height [2026-11-16, `build-plan-gate3.md` 11 step 2; agreed with Damian].
- **The day sweep** follows east–west position. The map is bounded by sea,
  so the sweep doesn't wrap.
- **Settings, roughly 20:**
  - land (6);
  - places: count, spacing, and the density field;
  - bridges;
  - water: reach and drain;
  - the source-area power;
  - delay: per hop and per length;
  - the resistance and bandwidth fields.

  Nova 1 had 44 settings for the same job.
- **The switch `world.map`** [2026-11-16, `build-plan-gate3.md` 2; agreed
  with Damian]: `landscape` (the default); `none`, places with no links,
  as at gates 1 and 2; and `grid` and `ring`, for test worlds only.
- **The map in detail [details at build, step 2; 2026-11-16; the joins
  agreed with Damian]:**
  - **Places:** darts land on land cells chosen by the density field. Each
    has a spacing of r ÷ √d at its cell, where d runs from 1 to
    `map.density`, evenly on a log scale with the field's rank over the
    land. Two darts keep the mean of their spacings apart. r starts loose
    and shrinks by 0.85 until every place fits. Setup refuses a map whose
    closest places would be under a quarter of a raster cell apart. The ids
    are then shuffled.
  - **Data links:** each landmass's Delaunay triangulation, keeping an edge
    if both angles facing it are acute, so no other place of the landmass
    lies in the circle on it. A landmass of two places is linked. One the
    triangulation can't join (all its places on a line) is joined by its
    shortest gaps.
  - **Bridges:** from the triangulation of all the places, shortest first:
    up to `map.bridges` a pair of landmasses, none longer than
    `map.bridge.longest`, and no place at the end of two. Then each
    landmass still apart is joined to the rest by its shortest crossing,
    whatever its length (a join), so every landmass is bridged. The longest
    bridge is in the map's height, as `build-plan-gate3.md` 7 gives it.
  - **A link's numbers:** the average link length is that of the links on
    land. The bandwidth field is read at a link's middle, ranked among the
    links on land, and spread evenly on a log scale from the least
    bandwidth to the most.
- **Risks:**
  - On land, a place well linked for data is also well linked for power
    and heat. Fields loosen the coupling, and it's left alone until a test
    shows it matters.
  - In small worlds, coasts may be the only real barriers.

### 6.2 Fields, biomes and borders

- **Every drawn field** has both smooth gradients and steep drop-offs, so
  the map has natural barriers. Barriers split populations, and split
  populations drift apart into new kinds (allopatric speciation). That
  covers hardware biomes, OS values, density, cooling, resistance and
  bandwidth.
- **One noise algorithm, seeded differently for each field.** Each field's
  edges fall in different places, so a place can be good for one thing and
  poor for another. Strong barriers appear where several edges coincide.
- **Method [details at build]:** two layers of the same fractal noise.
  - The first is used as it is, for gradients.
  - The second goes through a steep step, which makes cliffs that wind like
    coastlines.
  - Each field has an edge-strength slider (0 means smooth only) and a
    sharpness.
  - How a field sets each place's hardware and OS values is in 7.3.
- **Barriers are hard to cross, not walls.**
- **Hardware biomes:** smooth fields over the map decide where fast GPUs,
  large memories and so on cluster (7.3). These are Nova 1's substrates,
  carried over.
- **Power is not a drawn field.** A place's power follows from sources and
  resistance.
- **Grid borders, on the links.** The resistance field's step contours are
  read as borders: a proximity link that crosses one has its resistance
  multiplied, making it a thin tie between grids.
  - In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
    agreed with Damian]: a link crosses a border when its ends lie on
    opposite sides of a step of the field, and its resistance is raised
    by the border's strength, in links.
  - Strength: crossing a border costs as much as about one habitable band's
    width in ordinary links (10 links, as a placeholder).
  - Spacing: borders about 20–30 links apart, so a large landmass has one
    to three grids. A grid region must be large enough to catch sources of
    its own, because sources land by area. A small fenced-off region would be
    dark most of the time.
  - What a border divides is the supply, not the population. SEND crosses
    freely, and so does heat. Each side's voltage follows its own sources
    and crowds.
  - Real anchors: Texas's grid has only thin ties to the rest of the US.
    North America has four interconnections: Eastern, Western, Texas and
    Québec.
  - At the default 1,000 places, a map is about 32 links across. That's
    room for only one to three bands, grids or barriers (appendix E).

### 6.3 Within a place

A place holds devices: cores, memory devices, disks, a network device, and
perhaps cooling, plus a GPU and an NPU if they're ever unparked (7).

- **One flat address space per place** lays the memory devices end to end.
  A device boundary is always a page boundary.
- **Each device's address range is fixed for the place's life.** A
  replacement keeps its predecessor's size (7.8), so live pointers into
  other devices never move.
- **Address 0 is never valid:** the first page is reserved. ALLOC returns 0
  for failure.
- **The disks form one offset space of their own,** laid end to end with the
  most enduring disk (the highest write endurance) first, so offset 0 is
  its boot block (10.5).

"Near" does four separate jobs inside a place:

| Job | Decided by | Rule |
|---|---|---|
| Access cost | which device the bytes are in | each device's own latency, paid by memory operands and by every instruction fetched (8.4); no distance inside a place, and no NUMA level |
| Who can touch whom | address order | an overrun runs into the next addresses, and a page holds whatever allocations are packed into it |
| Who shares a fate | the memory device | in a brownout, the device with the highest threshold fades first, all of it together |
| Who lives beside whom | the allocator | size classes (9.3) |

- **NUMA is dropped.** The fair scheduler puts a thread on whichever core is
  free, so a cost by socket would land by luck, and a cost a program can't
  avoid fails principle 6. SETCORE (9.5) lets a thread pin itself, but a
  pin is a chosen, costed act; an unpinned thread would still pay a
  per-socket cost by luck (R27).
- **Choosing a device.** ALLOC's device argument is ordered by speed, so a
  choice means the same at every place:
  - 0 means any: the fastest with room;
  - 1 means the fastest, 2 the next, and so on;
  - −1 means the slowest;
  - an index past the end takes the nearest device that exists.

  Fast memory has a higher threshold and fades first. So 1 means fast and
  fragile, and −1 slow and steady. Every instruction pays the latency of
  the memory it's fetched from (R11), so code in fast memory runs faster.
  That's what makes fast memory worth its fragility.
- **A place is well mixed for core time and local only for contact.** Every
  thread at a place competes in one fair scheduler. That's realistic, and
  places are small; the map gives structure at the larger scale.
- **Sizes are scaled to programs, not copied from real machines.**
  - A page holds a few programs, and a memory device holds hundreds to
    thousands of programs' worth.
  - The page-size list and the range of device sizes are settings, with
    placeholder pages of 64, 256 and 1,024 bytes. Page sizes are always
    powers of two (a structural constant, 2).
  - **[open]** The real defaults wait until the first runs show how big
    evolved programs are.

## 7. Hardware

This is the hardware catalogue: Nova 2's periodic table. Many kinds, each a
set of numbers, all under the same laws.

### 7.1 The common fields

The same fields under the same laws, so every kind is treated alike.

| Field | Meaning |
|---|---|
| Count per place | How many of this kind a place holds (7.3) |
| Position on the curve | One or two numbers (size, speed class) drawn from ranges. The rest follows from the kind's trade-off curve, with a little scatter |
| Voltage range | Threshold (lowest working voltage) and maximum. Faster parts need a higher threshold |
| Speed against voltage | Linear above the threshold, with a slope per kind (7.5) |
| Draw | Idle draw, working draw, and for memory a draw per access. Working draw scales with size and with voltage² × speed (appendix C) |
| Heat | Heat capacity, heatsink rate to the place, heat limit, and the cool-down point a little below it |
| Leakage | How much more it draws as it gets hotter |
| Errors | The floor in mid-range, and how errors rise towards each edge (7.5) |
| Wear | Endurance. The slopes for heat and overvoltage are world settings (7.4) |
| Lost at a cut-out | What it holds in flight (7.6) |

**Which fields each kind has** ("–" means the field doesn't apply):

| Kind | Range | Speed | Draw | Heat | Leakage | Errors | Wear | Lost at cut-out |
|---|---|---|---|---|---|---|---|---|
| P core, E core | yes | yes | yes | yes | yes | yes | yes | yes |
| Memory | yes | latency only | yes, plus per access | yes | yes | yes (bits not held) | yes | contents, gradually |
| Disk | yes | latency only | yes | yes | yes | yes | per block, and the device | a write in flight |
| Network device | yes | – | yes | yes | yes | – | yes | queued messages |
| Cooling device | yes | – | yes | yes | – | – | yes | – |
| Link | – (no range of its own) | – | its loss | its loss heats its ends | – | corruption by length (11.3) | if it carries current | data crossing it |

Outside the fields, one global number: the restart gap above a cut-off,
with its minimum off-time (4.4).

### 7.2 The kinds

| Kind | Its own numbers | Its trade-off curve | Real anchor |
|---|---|---|---|
| P core | Cost per instruction class (the base table, 8.4) | Speed against draw, with a high threshold | About 0.7–1.5 V; several to tens of watts each |
| E core | Its own cost table: about 15% slower on simple operations, 60–70% slower on divides and calls (Nova 1's measurements on a Core Ultra 9 285K). Costs are kept in sixteenths of a cycle, so a 15% penalty on a 1-cycle instruction is expressible | Lower speed and threshold, for much less energy per instruction | Intel fits about four E cores in one P core's area |
| Memory | Size; access latency (+4 to +12 cycles, 8.4); holding draw and draw per access; fade rate; the spread of its cells' leak rates; rowhammer threshold (9.8) | Speed against holding on: fast memory has a higher threshold and fades first | DDR4 at 1.2 V, DDR5 at 1.1 V; refresh doubles above 85 °C; active DRAM draws several times its self-refresh power |
| Disk | Size; block size (the unit of wear); access latency and cost per byte; write endurance per block | Speed against endurance. The slow, enduring end is the archive | SSD: microseconds, a few thousand writes per cell. Tape: tens of seconds to reach data, decades of keeping |
| Network device | Idle draw (about a tenth of a core); draw per byte sent or received | **[open]** One kind, no curve yet (principle 8 asks for one); no speed of its own (11.4) | Routers draw nearly the same busy or idle |
| Cooling device **[gate 4]** | Cooling and draw, both rising with the place's temperature (7.7) | Cooling against draw | Thermistor-controlled fans |
| GPU **[parked]** | Lane count; lockstep group width (32); cost per instruction per lane, about 5–10 times slower than a P core on serial code; launch overhead | Lanes against draw | High-end cards: 300–575 W at about 1 V |
| NPU **[parked]** | Multiply-adds per tick; launch overhead | About 5–10 times less energy per multiply-add than a GPU, and nothing else | Phone and laptop NPUs: a few watts |

- **Heat limits,** from real parts: cores about 100 °C, memory 85 °C, disks
  about 70 °C, GPUs 85–90 °C.
- **The GPU and NPU are parked (R20).**
  - Neither has a condition yet in which using it wins. Lanes can't
    reproduce or travel, and the world has no matrix-shaped work.
  - Their designs are kept (9.5), and they're built only when a gate names
    that condition.
  - The ratios given for them are Claude's estimates.

**Hardware that isn't a device:**

| Kind | Its numbers | Where they come from |
|---|---|---|
| Link | Per-length resistance; bandwidth; endurance under current; cut rate per length | Map fields (6.1); endurance and cut rate are settings |
| Bridge | Bandwidth; cut rate; no power | 6.1 |
| Power source | Voltage, stiffness, day-following, swings, drop-outs, swells, lifetime, size skew | 4.2, 4.3; the strike type, 12.2 |

The regional event types belong to an environment catalogue (12.3), not
here.

### 7.3 How a place gets its hardware

- **Each kind's numbers come from its biome field over the map** (6.2),
  plus a small scatter. One or two position numbers are drawn, and the rest
  follow from the kind's curve.
  - The scatter must stay small next to the curve. Otherwise some devices
    come out better at everything and dominate.
  - Setup checks the drawn devices before a run, including that memory's
    thresholds sit below the cores' (4.4).
- **Counts per place come from the same fields,** within a range per kind.
  - **Every place has at least one core (P or E), one memory device, one
    disk and one network device.** New life needs a core, the reset region
    needs memory, the boot block needs a disk, and nothing reaches a place
    without its network device.
  - **Cooling devices may be absent,** as GPUs and NPUs would be on many
    real machines.
  - A place may hold several memory devices and several disks.
- **Replacements keep the size (R17).**
  - A visit replaces a failed device with a fresh draw of everything but its
    size, which stays the same, so addresses and disk offsets never move.
  - It replaces a worn one like for like (7.8).
  - A respawned place draws everything fresh where it lands.
  - **What "fresh" means, now that values are dealt by rank [2026-11-20,
    `build-plan-gate5.md` 2; agreed with Damian].** One device or one
    place can't be dealt again without moving every other. So a
    replacement takes the values its place was dealt (a core's kind, a
    memory device's latency and with it its threshold), with fresh draws
    of what is the device's own: its endurance, its leak ranks, its
    rowhammer threshold, its error floor (7.5) and its error countdowns.
    A respawned place reads each field at its new point, with the
    scatter, and takes the value the world dealt to the field value
    nearest its own, from a table kept at setup. So the world's mix of
    values drifts only as places move, the order by speed that ALLOC's
    device argument uses doesn't change at a replacement, and the reset
    region stays on the device with the lowest threshold (10.2). Chosen
    over drawing evenly from each range, as gates 1 and 2 did, and over
    keeping the old values.
- **At gate 1, with no map fields [details at build, step 9; 2026-11-11]:**
  - **Cores:** a count drawn evenly between the settings' bounds, each an E
    core with a set chance.
  - **Memory devices:** a count drawn the same way. The memory is shared
    out equally in whole kilobytes. Each device draws a latency evenly in
    its range, and its threshold follows the latency in a straight line
    from fast memory's to slow memory's. So the reset region always lands
    on the slowest memory.
  - **One disk** of the set size.
  - **The OS values:** a page size drawn evenly from the list, and strict
    with a set chance.
- **From gate 3, from the fields [2026-11-16, `build-plan-gate3.md` 2;
  agreed with Damian].** Each value gates 1 and 2 draw evenly has a field
  of its own instead: the core count, the E cores, the memory device count
  and latencies, the page size, and strict or lax.
  - The values are drawn for all the places as before, then dealt out to
    the places in the order of that field's value at each, nudged by a
    small scatter. So the world's mix of values stays gate 2's, and only
    where they sit changes: neighbours alike, with barriers between.
  - Each field is 6.2's two layers, seeded for that field, with its own
    edge strength and sharpness.
  - A switch (`map.fields`) turns them off, for gate 2's independent
    draws.
  - **In detail [details at build, step 2; 2026-11-16]:** a value that
    comes with another is dealt among the places with the same other:
    - a place's pattern of E cores, among the places with the same core
      count, in order of how many E cores;
    - its set of memory latencies, among the places with the same device
      count, in order of their sum.

    So the mix of cores with their kinds, and of devices with their
    latencies, is gate 2's too. The scatter nudges each place's field value
    by up to `map.field.scatter` of the field's range at the places, either
    way. A world with no map has no fields.

### 7.4 Wear [gate 5]

- **Every device has a wear count and an endurance.** It fails when its wear
  reaches its endurance.
- **Wear grows with stress:**
  - **With temperature, steeply.** A common rule of thumb is that failure
    rates double for every 10 °C (Arrhenius). The setting is doublings per
    10 °C, so 0 means no heat effect.
  - **With voltage above the device's maximum.** A big enough spike breaks a
    device at once. The slope is a world setting.
    - **In doublings, as heat is [2026-11-20, `build-plan-gate5.md` 2;
      agreed with Damian]:** wear is multiplied by 2^(v × (V − Vmax) ÷
      100 mV) above the maximum, where v is doublings per 100 mV and 0 is
      off. A swell wears, and a strike breaks a device at once, whatever
      the heat numbers. In a straight line it couldn't do both: a slope
      that let a strike break a device would ruin every device a swell
      touched. Real anchor: gate oxide wears out exponentially faster with
      voltage (time-dependent dielectric breakdown). Chosen over the
      straight line, and over a breakdown voltage of its own, against
      "burnout is wear's extreme, not a law of its own".
  - **With writes, for disks, per block.** A catalogue number.
  - A slope of 0 turns off wear from that cause.
- **Wear raises a device's threshold voltage.** Transistor ageing does this
  (NBTI and hot-carrier injection). Intel's 2024 Raptor Lake instability
  was this effect, sped up by too much voltage.

  | Hardware | What wear does |
  |---|---|
  | Cores (and a GPU or NPU, if unparked) | **They slow down.** Speed follows how far the voltage sits above the threshold, so at the same voltage a worn device runs slower. It also cuts out sooner as the voltage sags |
  | Memory | **Errors grow over time, at fixed spots.** The leakiest cells fail first, even on a normal day. Real DRAM's weak cells do the same. The engine keeps a seeded permutation from leak rank to cell, so finding the failing cells costs per failing cell, not per cell (appendix C) |
  | Disk | **Sectors go bad.** Each block counts its writes. Past its endurance, drawn per block around the type's value, its bits start flipping, more as writing goes on. The most-written blocks wear first, the boot block among them |
  | Link (only one carrying current) | **Its resistance rises,** so it heats more and wears faster, until it fails |
  | Power source | Doesn't wear (4.2) |

- **Memory's weak cells [2026-11-20, `build-plan-gate5.md` 2; agreed with
  Damian].** Fade acts only while memory is cut out, so on its own it
  makes no errors on a normal day. As a device wears, the leakiest cells
  by its leak rank turn weak: their share rises in proportion to its
  wear, to a set share at the end of its life. While the device is on, a
  weak cell loses its bit, reading back random, at a set chance a tick
  (about once a refresh window). The flips are drawn as the cosmic-ray
  floor's are, a count a tick and then which weak cells, so they cost
  per flip and land on the same spots every time. Two settings: the weak
  share at the end of life, and a weak cell's chance a tick. Chosen over
  the raised threshold alone, which would have dropped the fixed spots.
- **A link wears by its loss [2026-11-20, `build-plan-gate5.md` 2; agreed
  with Damian]:** a tick's wear is its loss ÷ its loss at the reference
  current when new, which is (I ÷ I_ref)² × (R ÷ R₀). Its resistance
  rises in proportion to its wear, to a set multiple at the end of its
  life, so its loss and its wear rise with it: the feedback above. Real
  anchor: lines fail by heating (2003's sagging lines, 11.6). Chosen over
  wear by current alone, which has no feedback, and over wear by its
  ends' temperatures, which would follow its neighbours' crowds.
- **When a disk counts as failed.** A disk fails when its device wear
  reaches its endurance, or when its first block (on the most enduring disk,
  the boot block) passes its write endurance. A visit then replaces it
  (7.8). A worn boot block is therefore always repaired eventually.
- **Burnout is wear's extreme, not a law of its own.** A device far past its
  heat limit, or hit by a spike far past its voltage limit, wears so fast
  that it fails at once.
- **The heat cut-out still protects.** A device past its limit stops
  drawing, so it cools. It wears seriously only when it can't cool:
  - when its place is kept hot by its neighbours, links or source;
  - when a spike arrives faster than heat can trip it.

  So burnouts happen near hard-worked sources and in crowded hubs.
- **Wear levelling is an OS toggle, defaulting to off ("cheap flash").**
  - **Off:** a disk offset maps to the same physical block for life, so wear
    lands where the writing is. The disk is raw and shared (10.5), so heavy
    writers wear everyone's data.
  - **On ("modern SSD"):** the OS remaps blocks so wear spreads across the
    whole disk. The OS's own copies count as writes (write amplification).
  - Real anchors: cheap flash (SD cards; Tesla's 2021 recall of worn-out
    flash chips), and Linux's UBI and JFFS2, which level raw flash in
    software.
- **In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
  agreed with Damian]:**
  - **Wear's arithmetic:** wear is in fixed point, in ticks at the
    reference conditions; the heat and voltage factors come from tables
    worked at setup, as the cosmic rate's are, so the tick code stays in
    integers. Each device's endurance is drawn evenly on a log scale from
    half to twice its kind's, so like devices under like stress don't
    fail on the same tick. The threshold rises in proportion to wear, to
    the set rise at the end of life. A device wears whether on or off,
    since a cut-out or tripped device still sits at its place's voltage
    and temperature; a failed one wears no more.
  - **Disks:** a block is 512 bytes, the boot block's size. Each block's
    write endurance is drawn evenly on a log scale from half to twice the
    disk's value. Past it, each bit written to the block lands wrong with
    a chance that grows in proportion to the writes past its endurance.
    The flips come when the block is written, at the barrier's disk I/O,
    not each tick (appendix C).
  - **Wear levelling, when on:** the OS keeps a map from each disk block
    to the block holding it. When a block's writes pass the
    least-written block's by a set gap, the OS swaps the two blocks'
    contents and places in the map: two writes, counted as wear, at no
    thread's cost. The disk fails when the block holding offset 0 passes
    its endurance, wherever levelling has put it. Levelling is one world
    switch, as every OS value but the page size and strictness is (9.6).
  - **Links [details at build, step 5; 2026-11-20,
    `build-plan-gate5.md` 11]:** each link's endurance is drawn evenly
    on a log scale from half to twice the links' value, as a device's
    is, so the links of one source don't fail on one tick. Link wear
    follows the world's wear rate as device wear does, and a link
    endurance of 0 turns link wear off alone. The rise in resistance is
    taken in 4,096 steps over a life, so the solve's conductances
    change at most that often.
  - **As built [details at build, steps 2 and 3; 2026-11-20,
    `build-plan-gate5.md` 11]:**
    - every kind's threshold rises with wear, not only a core's; worn
      memory fades as new memory would at a voltage lower by the rise;
    - a worn core draws by its worn speed, so it draws less as it
      slows;
    - a failed device's ticks are counted as failed, not as cut out or
      tripped, so those counts keep their meaning;
    - a threshold rise so large that a worn device could never restart
      under its maximum is refused when the world is built;
    - a write of any length inside a block is one write to it, and a
      write across blocks is one to each; a write torn by a cut-out
      still counts;
    - "past its endurance" means more writes than it: a block of
      endurance N takes N clean writes. The chance a bit lands wrong is
      capped at one in two;
    - a worn boot block fails its disk even with wear by time off;
    - levelling's swaps are whole-block writes: they can flip bits on a
      worn block, and never set off another swap. Ties for the
      least-written block go to the lowest-numbered.
- **Programs see wear only partly.**
  - SENSE reports a device's speed, so a worn processor shows as slow.
  - SENSE reports each device's nominal threshold, not its worn one (R25).
  - Memory errors and bad blocks show only through what they corrupt.
- **Units:**
  - device endurance: ticks at reference conditions;
  - disk blocks: writes;
  - links: ticks at a reference current.
- **Trade-offs:**
  - overclocking is faster now, but wears the shared core for everyone;
  - living near a strong source gives power but wears the devices;
  - a backup on disk costs writes, and others can overwrite it.

### 7.5 Speed, the clock, and errors

- **Speed is linear above the threshold:** in proportion to (voltage −
  threshold), up to the maximum. The slope is the kind's number. (The real
  alpha-power law was considered; the simpler form keeps the direction with
  fewer numbers.)
- **Speed is cycles per unit of simulated time.** A tick is a fixed span of
  simulated time (R9), so a core's cycles in a tick are its speed × the
  tick's length. A faster core does more in the same tick.
- **The clock.** A core's speed at its place's voltage is its **stock
  speed**, what the voltage supports.
  - SETCLOCK scales it in steps, for example from half to one and a half
    times in tenths. The step and the range are settings.
  - **Above stock,** it's faster, draws more, and makes more errors, rising
    with how far past stock it runs. Real overclocking beyond what the
    voltage supports crashes or silently miscomputes.
  - **Below stock,** it's slower, draws less, and makes fewer errors.
  - **The setting applies during the requesting thread's own turns** (9.5).
    So a program controls its own error rate, and with it how much its
    copies mutate.
    - Turns are slices of time (R9), so overclocking buys more cycles in
      your own turn, and underclocking costs you cycles.
    - The extra heat and wear fall on everyone at the core.
  - **The cost of switching.** SETCLOCK pays its own cost once (9.7).
    Switching between threads at different levels adds a small fixed switch
    time to that context switch (a setting), not the full SETCLOCK cost.
  - **Wear lowers the stock speed** (7.4).
- **Errors are wrong results, not mutations.**
  - Near either edge of its range a device sometimes gives a wrong result.
  - A mutation is what observers call a wrong result that landed in code,
    mostly during copying.
  - Memory's "wrong result" is a bit it fails to hold. A lost bit reads
    back random.
  - **What counts as a "result":** each instruction's result written to a
    register or memory, and each byte CPY moves.
    - **In detail [details at build, step 4; 2026-11-11]:** one result per
      value written: to A–D, to memory, to the stack, or to IP by STO, SWP,
      POP or a jump that's taken. SWP with a register or memory writes two.
      The neutral bytes write nothing and so can't go wrong: MOV A, STO A,
      SWP A, a store to #, POP #, and the spare slots. A wrong register
      result has one of its 32 bits flipped; a wrong byte, one of its 8.
- **A small floor, even in mid-range.** Every result a device gives has a
  tiny chance of being wrong: a floor per kind, in the catalogue, with 0
  meaning none.
  - A copy is many results, so it collects errors in proportion to its
    length. That's what makes copying imperfect.
  - Real anchor: silent data corruption. Meta and Google (both 2021) found
    a few faulty cores per thousand or so machines miscomputing under
    normal conditions.
  - The engine draws the number of results until the next wrong one and
    counts down, rather than drawing once per result (appendix C).
  - **Flaky parts [gate 5; 2026-11-20, `build-plan-gate5.md` 2; agreed
    with Damian; one of the four mechanics he chose that day].** Each core
    and memory device is flaky with a set chance (2%), drawn when it is
    built and again when it is replaced (7.3). A flaky device's floor is
    its kind's times a factor drawn evenly on a log scale between two
    settings (3 to 100); in memory the factor multiplies the cosmic-ray
    rate at that device, a falling rate included. The rest keep their
    kind's floor, so most places are as before and a few are stark.
    - **With wear,** a core's floor rises in proportion to its wear, to
      a set multiple of its own at the end of its life (10 times; 1 is
      none).
      Memory's wear makes errors by its weak cells (7.4).
    - SENSE doesn't read it: a program finds a flaky part only by its
      errors, as a real fleet does. The observers record each device's
      factor.
    - A share of 0 is off. Real fleets' share is nearer a few in a
      thousand, but at that share a 48-place seed, about 380 cores and
      memory devices, would have one flaky part or none.
    - Real anchors: the silent data corruption above, and Google's "Cores
      that don't count" (2021), which saw some get worse with age.
    - Chosen over a spread on every device, which moves every place's
      mutation rate a little, and over a floor following a field, which
      makes a flaky region rather than a few bad parts. This settles what
      was open here: whether to vary the floor per device.
- **The cosmic-ray floor: rare flips in all memory.** Each bit has a chance
  per tick of flipping. It's a classic flip, with no shielding, and a
  sliding scale with 0 meaning off.
  - "Static" means only the random bytes memory and disk start with (10.1).
    The ongoing flips are "the floor".
  - The rate waits on a test.
  - **A falling rate (agreed with Damian 2026-11-18, and built that day on
    his word; off by default).** The young Earth took harsher radiation
    than today, from a more active Sun and with no ozone layer. The rate
    starts at `noise.cosmic_start` and falls to `noise.cosmic`, its excess
    over it halving every `noise.cosmic_half_life` ticks, in eight steps a
    half-life. It is fixed before the run and depends only on the tick, as
    the sources' schedules do. A start of 0 is no fall.
    - Why: a high rate early means fast change, but only short programs
      survive it (the error threshold, below). As it falls, longer
      programs become possible.
    - It falls only to a floor that still keeps full places turning over.
      At the runs' rate a sleeping thread's registers take a wrong bit
      about every 5,200 ticks (7.6); at the default rate, a thousand
      times lower, that would be about every 5 million, and full places
      would come close to freezing as at gate 2's first answer (14.1).
    - A change of rate redraws each device's countdown, as a change of
      clock level does, so the rate stays exact. The rates are a table
      worked once at setup, so the tick code stays in integers.
    - Memory's edge errors stay on the floor: they are the device's, not
      the sky's.
    - Tests: the flips follow the curve tick by tick; a start equal to the
      floor gives the same flips as no fall; a world with the fall hashes
      the same on 1 and 24 threads, and resumed from a snapshot just
      before a step and one between steps.
- **The error threshold.** The floors and the edges together must stay well
  under one error per copy of a typical program (Eigen's error threshold).
- **The floors in detail [details at build, step 8; 2026-11-11]:**
  - **The clock's effect:** the rate per result is the kind's floor times
    2^((k − 10) ÷ d), for a clock level k in tenths of stock. d is a setting
    (`noise.clock_doubling`, placeholder 1 tenth): at 1.5 times stock a core
    makes 32 times the errors, and at half stock a thirty-second.
  - **A change of level redraws the core's countdown.** A countdown at a
    fixed rate has no memory, so the redraw keeps the rate exact.
  - **The draw** is a geometric gap from a fixed-point log2, in integers
    only, from the core's own stream for the tick.
  - **The cosmic-ray floor** counts down bit-ticks per memory device the
    same way. So it's exact per bit and per tick, and costs only per flip.
    It covers memory, not disks. A flip in the reset region counts as a
    write to it, so it can wake a halted core.
  - **Edge errors aren't built at gate 1.** The supply is steady and in
    mid-range there; they come with the varying supply at gate 2.

### 7.6 Failure, and what a cut-out loses

**A failed device stays off until it's replaced (7.8):**

- **a core:** the thread running on it ends, as at a cut-out. Then the
  scheduler has one fewer core, and its other threads run on the rest;
- **a memory device:** its contents are lost and its pages freed. Touching
  them faults, so a program with all its pages there dies, and one with a
  fault handler may survive. The reset region on a failed device is gone
  until the visit (10.2);
- **a disk:** its data is lost, and the boot block too if it held it. DREAD
  and DWRITE on its offsets fail soft;
- **a network device:** the place can't send, receive or forward;
- **a cooling device:** the place runs hotter;
- **a link:** it carries nothing until it's replaced (11.6).

**In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
agreed with Damian]:** a failed device draws nothing and cools nothing,
and SENSE reads 0 for it in every reading but its kind. A thread ended by
its core failing has the cause "failed device" (13.1), the cause already
named for memory, now for every kind. Wear's failures come in step 4 of
the tick (appendix C), devices in their order at each place, then links
in link order. A failed link carries no power, no data and no heat.

**Every kind of hardware has two failure states:** power too low (an
outage) and power too high (a surge). Both are the two ends of each
device's range, under one law (4.4).

**A device that cuts out (below its threshold, or tripped by heat) is off,
but not failed. It loses its work in flight, and keeps only what it
stores:**

| Device | While cut out | Lost when it cuts out |
|---|---|---|
| Core | Runs nothing. Its threads wait, or the scheduler moves them to a powered core | The thread running on it ends, as a fault with no handler would. Threads that are queued, asleep or waiting on a call keep their saved state in their stacks (9.4) and survive as long as memory holds |
| Memory | Fades: errors first, then loss | Its contents, gradually |
| Disk | Keeps its data. DREAD and DWRITE wait for it, up to the call's own limit | A write in flight is torn, leaving the block part new and part old. A read in flight fails |
| Network device | Sends, receives and forwards nothing | The messages queued at it |
| Cooling device | Cools nothing | – |
| Power source | – | Nothing; it doesn't wear |

- **So night is a crash for whatever is running on a core when the core cuts
  out, and a sleep for everything else.** A program that senses its voltage
  falling, or takes the low-voltage interrupt (12.4), and sleeps in time
  survives.
- **This rebuilds Nova 1's crash from physics.** The share lost follows
  crowding: a crowded place loses few, and a sparse one loses everything
  running.
- **Risks:**
  - A fresh single-threaded replicator in an empty place has a core to
    itself, so the first cut-out kills it. That's an Allee effect, where
    small populations fail.
  - Faults with no handler may also be too harsh on the first, sloppy
    replicators.

  Both are tested at gates 1 and 2. The fallback is to pause instead of
  crashing. Gate 2 builds it as a switch (`os.cutout_pause`), off by
  default, so the origin regression can try it without a rebuild
  [2026-11-14, `build-plan-gate2.md` 2; agreed with Damian].

### 7.7 Cooling devices [gate 4]

- **Passive, driven by temperature.** A cooling device works harder as its
  place gets hotter: both its cooling and its draw rise with the place's
  temperature. No program controls it, and no call exists for it.
- **Real anchor:** thermistor-controlled fans. A thermistor is a passive
  part, so this needs no firmware.
- **Its trade-off:** a place with cooling holds more heat-making hardware,
  but pays a base load that grows in the hot hours. Everyone at the place
  pays it.
- **It has the common fields that apply** (7.1). It has its own range, and it
  wears and fails (fans are moving parts). When a hot place's cooling fails,
  the place overheats until the next visit.
- **In detail [details at build; 2026-11-19, `build-plan-gate4.md` 2;
  agreed with Damian]:** its cooling rises evenly from nothing at its
  start temperature to its full rate at its top temperature, and its draw
  rises with it. Below its voltage threshold it cools nothing. It doesn't
  wear or fail until gate 5.

### 7.8 Maintenance visits and respawning places [gate 5]

**Visits restore hardware.**

- **The schedule.** Each place draws its visit times from the seed before
  the run. Nothing in the run moves them, and a place stores only its next
  visit's tick.
- **Between visits, failures wait.** One visit replaces everything that
  failed since the last.
- **What a visit does:**
  - **A failed device** is replaced by a fresh draw from the place's fields,
    with no wear and the same size (R17). New memory and disks hold static,
    a new disk's boot block included.
  - **A worn device past the replacement threshold** is replaced like for
    like: the same catalogue numbers, no wear, and its contents copied
    across first. Bits already corrupted stay corrupted.
    - The threshold is a slider, off above 1. At 1 (the default), only
      failed devices are replaced.
    - Real anchors: server memory sparing copies a failing rank to a spare,
      and a RAID array rebuilds a failed disk from the others.
  - **A failed link** is replaced at the next visit to either of its places.
  - **Nothing pauses** (real parts are hot-swapped), and programs are never
    touched.
- **What visits don't do:**
  - replace working devices below the threshold;
  - upgrade anything, since there's no technology trend;
  - touch sources.
- **Three sliders:**
  - the mean interval: short makes a failure a blip; long leaves lasting
    scars; "never" lets the world wear out;
  - regularity: 0 means random gaps (the default), 1 exactly periodic;
  - a tie to the density field, so towns are visited more often: 0 (the
    default) means the same interval everywhere.
- **Scars follow stress.** The share of time a device spends dead is about
  wait ÷ (lifetime + wait), so the same interval leaves cool places whole
  and hot hubs patchy.
- **Real anchors:** staffed data centres swap parts within days, while "fail
  in place" designs leave failures alone. Microsoft's sealed undersea data
  centre (Project Natick, 2018–2020) ran two years with its failures left in
  place.
- **Not a reaction to the world.** The timing is fixed before the run, and
  what a visit does depends only on the hardware's state.
- **Replacing the reset region's device is a hot swap, not a cold start
  [2026-11-20, `build-plan-gate5.md` 2; agreed with Damian].** The region
  comes back as static, idle runs start again, and port 0's queue, kept
  while the region was gone (R6), runs. The boot block doesn't run, and
  programs on other devices carry on. The failure has already ended what
  the device held (7.6). The cold start is power's rule (10.5); the
  visit is a hot swap. Chosen over a cold start, which would be the
  reboot visit below in a narrow form.
- **Visits in detail [details at build; 2026-11-20, `build-plan-gate5.md`
  2; agreed with Damian]:** each place's next visit is drawn from the
  seed, its id and its visit count. A gap is the mean × (the regularity
  + (1 − the regularity) × an exponential draw of mean 1), and the mean
  is scaled by the density tie: mean × (the place's density ÷ the
  average)^(−tie). "A large enough share of a place's devices" (below)
  counts devices, every kind alike; links don't count.
- **Visits as built [details at build, step 4; 2026-11-20,
  `build-plan-gate5.md` 11]:**
  - a visit happens in the place's own tick, after the tick's voltage
    is known and before the device states. A new device whose place is
    under its threshold then starts off, and comes on when the power
    returns; for the reset region's device that later return is a cold
    start, which is power's rule and not the visit's;
  - a place's density, for the tie, is the map's density to the power
    of the place's rank by the density field (1 at the sparsest, the
    map's density at the densest). With no map or no fields every place
    has the world's mean;
  - like for like needs wear by time on, and looks at device wear, not
    at a disk's block writes. A disk replaced like for like gets new
    blocks and keeps its bytes and its boot block's writer; memory gets
    new leak ranks, so its weak cells will be other cells;
  - a new device keeps its slot's temperature;
  - a place also stores its visit count and its last visit's tick.
- **A possible lever, not built:** a visit that reboots a place (a
  maintenance window), using the existing cold start. It's held in reserve
  in case gate 2 shows full places don't turn over (14). It would break
  "programs are never touched", so it needs Damian's decision.

**Respawning places.**

- **When a visit finds a large enough share of a place's devices failed,**
  it tears the place down instead of repairing it. A new place is built at a
  random point on land, drawn by the density field, respecting the minimum
  spacing, with fresh hardware.
  - The share is a slider, off above 1. At 1 (the default), only wholly
    dead places move, so nothing living is wiped.
- **The transition:**
  - **Links:** the Gabriel rule links the new place, and the old place's
    neighbours relink among themselves. New links start without wear.
  - **The id:** the new place takes the dead place's id, so the number of
    places stays the same. Messages addressed to that id go to the new
    place, as a reused real address does. Messages queued at the dead place
    were already lost with its network device.
  - **Sources and bridges are anchored to points on land, not to places.**
    Each attaches to the nearest place, and that is recomputed when a place
    respawns. Sources don't follow the id, so the supply stays a boundary
    condition (principle 3).
  - **Catchments** (for sources by area) are recomputed.
  - **The new place keeps the old id's visit schedule,** which is seeded by
    id.
  - **Observers** see an incarnation number on each place id (13.1).
- **In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
  agreed with Damian]:** the new point is drawn from the seed, the id and
  the incarnation, by the density field at the spacing the map settled on
  (6.1); a point that doesn't fit is drawn again, up to a cap, after which
  the place stays where it was with fresh hardware. Both link sets are
  worked out again where the Gabriel rule's answer changed; a landmass
  left with no bridge gets its shortest crossing, as the map's setup joins
  landmasses (6.1); new links' bandwidths and resistances come from their
  fields. Messages on the wires of links that are gone are lost, and
  counted. The new place starts at the outside temperature, with static in
  memory and on disk and an empty port 0 queue, and takes its values as
  7.3 says. At a respawn the place's colonies and holds end for the
  observers, and the new place starts with none.
- **Respawning as built [details at build, step 6; 2026-11-21,
  `build-plan-gate5.md` 11]:**
  - **When:** a teardown is the first of its tick's inputs, before the
    links' mends and cuts and before the solve (appendix C, 1), in
    place order. The devices counted for the share are the place's
    cores, its memory devices, its disk and its cooling device if it
    has them, and its network device.
  - **Teardown:** every program and thread at the place ends, with the
    cause "failed device" (13.1), and those are counted apart. At a
    share of 1 nothing living is left to end. The id keeps its
    programs' numbering and its counters.
  - **The new point:** up to 1,000 draws, then the place is built again
    where it was. On a test map (a grid, a line, none) it stays where
    it is.
  - **Links:** a link between two places that haven't moved is the link
    it was: its values, its wear, its failed state, its cut schedule
    and its queues. A new link reads its field at its middle, with a
    scatter of its own, and takes the value setup dealt to the nearest
    field value, as a place's values do (7.3), so the world's mix of
    link values stays setup's. Its cut schedule starts at the respawn's
    tick. The mean link length stays setup's, so the cut chances and
    delays of surviving links don't shift.
  - **Bridges** keep the two shore points setup gave them. Each end
    joins the nearest place on its own landmass. A landmass left with
    no place has no bridge end. One that gains its first place, or is
    left with no way to the rest, gets the shortest crossing to another
    peopled landmass, kept from then on. Landmasses setup left alone
    stay alone.
  - **Sources** stay setup's in time and on the map. Each joins the
    place whose catchment its cell is in now.
  - **Visits:** the id's schedule doesn't depend on whether it
    respawned; its pace follows the density at the new point.
  - **Messages:** one queued or waiting at a surviving place for a link
    that is gone waits on there and takes the new route (11.6); at a
    lifetime of 0 it is lost.
  - **The incarnation** is kept by the world and saved, with the tick
    of each place's first respawn. The observers read it from the log
    of respawns; no record holds it (13.1; Damian: "Yes, log only").
- Over long runs the most punished ground is abandoned, and the network
  rebuilds itself elsewhere. The density field keeps towns and countryside,
  on average, where they were.
- **The cost:** routes and the power graph already handle a changing
  topology (11.6). Relinking touches only the neighbourhoods involved.
- **Frequent visits keep places alive; rare ones let them die and move.**

## 8. Programs: the instruction set

A small set where every byte means something. In such a set random code
runs longer, and the shortest replicator is short (18 bytes, traced in
appendix A). Tierra used 32 instructions, Avida 26, and BFF 10. The exact
semantics of every instruction are in appendix A.

### 8.1 Encoding and registers

- **One byte per instruction, plus one byte for #.**
  - The high 5 bits pick the instruction (32 slots), and the low 3 bits the
    operand (8). Every byte is a valid instruction.
  - The operand # takes the next byte as a number. So instructions are one
    or two bytes, and a flipped bit that turns an operand into # (or back)
    changes how the next byte decodes. Every other flip changes one
    instruction only.
- **Operands (8):** A, B, C, D, IP, [B] and [D] (the byte at the address in B
  or D), and # (the next byte, −128 to 127).
- **An accumulator machine.** Every instruction works on A and one operand,
  and the result lands in A, as on the 6502. D = B + C is `MOV B; ADD C;
  STO D`.
- **Registers per thread:** A, B, C, D and IP, all 32-bit. Reading IP gives
  the address of the current instruction. Each thread has a stack (9.4),
  whose pointer can't be named.
- **No flags.** Conditional jumps test A as a signed number.

### 8.2 The 22 instructions

| Group | Instructions |
|---|---|
| Data (5) | MOV s (A ← s), STO s (s ← A; storing to IP jumps there; storing to # does nothing), SWP s, PUSH s, POP s (POP IP is a return) |
| Arithmetic (4) | ADD, SUB, MUL, DIV (A ← A op s, 32-bit, wrapping on overflow; DIV is signed and truncates; **dividing by 0 gives 0**, as on ARM, R18) |
| Logic (4) | AND, OR, XOR (A ← A op s), NOT s (A ← s with every bit flipped) |
| Shifts (2) | SHL s, SHR s (shift A by s mod 32 bits, filling with zeros) |
| Jumps (5) | JMP s (to the address of this instruction + s; with a register, a computed jump), JEZ, JNZ, JGZ, JLZ (if A is 0, not 0, above 0, below 0) |
| Block (1) | CPY s (copy s bytes from the address in B to the address in D, at a cost per byte; registers unchanged; R29) |
| OS (1) | SYS s (call number s; arguments in A–D, result in A) |

- **The 10 spare slots do nothing** (80 neutral byte values), as BFF's no-ops
  do. With # they still take the next byte. There's no setting to make them
  crash. They leave room to add instructions later without changing the
  encoding.
- **Not included,** but could go in a spare slot if a need shows: NEG, MOD,
  rotates, a sign-keeping shift, and one-byte forms with small constants.

### 8.3 Idioms and faults

- **Jumps are relative and IP is readable,** so code works wherever it's
  copied. `JMP # 0` jumps to itself: the two-byte endless loop that static
  makes often. The idle run's cap ends it (10.2).
- **Call and return are idioms:** compute the return address, push it, and
  jump; POP IP returns. For example, `MOV IP; ADD # 6; PUSH A; JMP # target`.
  Return addresses sit on the stack, so stack overflows work as real ones do
  (9.8).
- **Memory operands are one byte wide,** read as signed numbers. Code is
  bytes, so reading code as data stays simple.
- **Some bytes are neutral:** MOV A, a store to #, and the spare slots. Like
  some DNA changes, they do nothing.
- **Faults come from ordinary instructions only,** and are handled as in
  9.5:
  - touching memory the thread's program can't access (9.3);
  - the stack running out of its allocation;
  - PUSH or POP in an idle run, which has no stack (10.2).

  Calls never fault; they fail soft (9.2).
- **The shortest replicator is 18 bytes** on a lax place, and 24 on a strict
  one with 64-byte pages. Both are traced register by register in appendix
  A, child included. The 14-byte sketch from the history didn't set ALLOC's
  permission, so it worked only by luck.

### 8.4 Costs

- **The unit is cycles,** the same on every device. A core's speed is cycles
  per unit of simulated time (7.5), and a tick is a fixed span of it (R9). So
  a faster core runs more cycles in the same tick.
- **An instruction's cost has four parts:**
  - its own work (below);
  - **fetching it:** the latency of the memory device the instruction lies
    in (R11);
  - a memory add-on if an operand reaches memory;
  - for SYS, the OS crossing plus the service's own work (9.7).

| Cycles | Instruction work (register or # operands) |
|---:|---|
| 1 | MOV, STO, SWP, ADD, SUB, AND, OR, XOR, NOT, SHL, SHR, all five jumps |
| 3 | MUL |
| 20 | DIV |
| 1 + stack access | PUSH, POP |
| about 2 per byte, plus a small setup and both devices' latency | CPY |
| crossing + service | SYS |

- **Memory latency** is drawn per device, from about +4 cycles for the
  fastest to about +12 for the slowest.
  - Every instruction pays it once, for its fetch.
  - [B], [D] and the stack pay it again, for the device the operand lies in.
  - Registers and # add nothing.
  - Direct operands reach only the local place; anything further goes
    through SYS.
  - Real anchor: code run from slow memory runs slowly, as code run in place
    from flash does.
- **Costs are kept in sixteenths of a cycle,** so the E core's table (7.2)
  can say "15% slower" on a 1-cycle instruction.
- **Reads and writes cost the same** until a run shows the asymmetry
  matters (principle 7).
- **The SYS crossing is about 150 cycles.** Expensive calls are expensive
  because of their work, not the crossing. That keeps the 4-to-12 memory gap
  meaningful.
- Real reference points, in cycles:

  | Operation | Cycles |
  |---|---|
  | ALU operation | about 1 |
  | Branch miss | about 15 |
  | L1, L2 and L3 cache | about 4, 12 and 40 |
  | Main memory | 200–300 |
  | System-call crossing | 150–500 |
  | Page fault or context switch | 1,000–3,000 |
  | Network round trip | millions |

- **[open]** The exact crossing figure.

## 9. The OS

### 9.1 A neutral OS on every place

- **The OS is neutral infrastructure,** maintained from outside like the
  power grid and the network. It acts only on requests, shares time by a
  fixed policy, and never acts for or against a particular program.
- **Two questions for any OS behaviour:**
  1. Was it asked for, or is it a fixed rule applied the same way to
     everyone?
  2. Does it mention only the asker and the world (cells, pages, cores,
     places), never another program?
- **Fine:**
  - "give me N cells";
  - "free these cells";
  - "start a thread at my address";
  - "send these bytes to place Y";
  - "set my fault handler";
  - "what is the voltage here?"
- **Not fine:**
  - "kill P", "read P's memory" or "give me P's core";
  - the OS restarting, moving or rescuing programs unasked.

  Linux's out-of-memory killer was the real example to avoid: in Nova 2,
  when memory is full, requests fail and nobody is killed. That still
  holds for a place that is full for a while. Since 2026-11-20 a place
  kept crowded loses a program, by the narrow amendment below.
- **Amended [2026-11-20, `build-plan-gate5.md` 2, gap 14; Damian's
  decision, gate 5]:** a fixed rule may act on a program by what it
  holds, or on a thread by how it runs, the same way for all, never by
  its id, lineage or kind. Two rules use it, each with an off:
  - **The out-of-memory killer.** A place is crowded when its free memory
    is under a tenth (13.3). When it has been crowded for a set time (a
    day), the OS ends one program there, at the barrier, and the place's
    clock starts again. The program is drawn by bytes held, stacks
    included, from the place's stream: each byte equally likely, so a big
    holder is likeliest but never certain. Every thread ends, its memory
    is freed with its bytes left, and the cause is "out of memory"
    (13.1). Real anchors: Linux's out-of-memory killer, which scores
    processes by the memory they hold; Kubernetes evicting pods when a
    node runs short; and in ecology, viruses that keep the fastest-growing
    bacteria from taking over (Thingstad's kill-the-winner).
  - **The watchdog.** Each thread counts the cycles it has run since its
    last call, any call. Past a set count (a million cycles, ten times
    the idle-run cap, so a thread that copies never trips it) it takes a
    new fault, "watchdog" (9.5, A.3), which a handler can catch; with no
    handler the thread ends with that cause.
  - **Why:** late worlds thinned to one program a place holding hundreds
    of threads (gate 3's seed 15) or over a thousand (`runs/open-2`), so
    no core was idle and nothing else was born there. The killer checks
    that; the watchdog alone wouldn't, since those threads call SEND and
    SPAWN. Drawing by bytes, rather than always ending the biggest
    holder, keeps the second biggest from being the safe size to be.
  - **What stays ruled out:** "kill P" for a P the OS picks by who it is,
    and restarting, moving or rescuing programs.
  - **The risk 9.1 guarded against stays:** a rule that ends big holders
    is a selection pressure designed in. Gate 5's comparison run, with
    the four mechanics off, tells its effect from life's (14.1).
  - Chosen over the watchdog alone with 9.1 kept, which leaves fan-out
    untouched, and over a fair share of cores by program, which would
    reverse 9.5's "the main lever is the number of threads".
- **Every OS service passes the no-bootstrap test** (principle 12).

### 9.2 The call table

- **Every SYS is a real call.** The operand modulo the slot count picks a
  slot, and the numbering table maps every slot to a call, so there is no
  "invalid call" (approved 2026-11-11).
- **Every argument value is valid.** Appendix A says what any 32-bit value
  means in every argument (modulo, clamp or fail soft).
- **Calls fail soft.** A bad size or an address the program can't use
  returns the call's failure value (appendix A), and the thread runs on.
- **Arguments go in A–D.** The result comes back in A, and RECV also puts
  the sender in B.
- **No call names another program.** Programs touch each other only
  through physics (9.8).
- **Sixteen active calls.** GLAUNCH and NMATMUL are parked with the GPU and
  NPU (R20), so they aren't in the table, and their numbers aren't taken.
  Fewer calls also make ALLOC and SPAWN easier for random code to find.

| # | Call | Arguments → result | Effect |
|---|---|---|---|
| 0 | ALLOC | size, permission, device → address or 0, in A and D | Reserve memory. Permission is data, executable or both (data is both on a lax place). Device picks by speed (6.3). Placed by size class (9.3) |
| 1 | FREE | address → 1 or 0 | Give back an allocation your program holds |
| 2 | PROTECT | address, permission → 1 or 0 | Change a page between data and executable. Works only if your program holds every allocation in the page (9.3) |
| 3 | SPAWN | address, starting value → thread id or 0 | Start a thread on an executable page your program holds. Inside the allocation holding your own IP, it's your thread. In any other allocation you hold, that allocation passes to a new program (9.4) |
| 4 | EXIT | — | End the calling thread |
| 5 | SLEEP | ticks → — | Positive: sleep that many ticks. 0: sleep until an interrupt. Negative: end this turn (a yield, R13) |
| 6 | SETCLOCK | level → the level applied | Scale the core's stock speed, in steps, during this thread's turns (7.5) |
| 7 | SETCORE | kind (any, P only, E only) → 1 or 0 | Pin the calling thread to one kind of core (9.5) |
| 8 | SETHANDLER | kind, address, parameter → 1 or 0 | Register this thread's handler for faults, arriving data on a port, a timer with a period, or low voltage (9.5) |
| 9 | HRET | — | Return from a handler, restoring the interrupted registers |
| 10 | SENSE | which, index → value | Read the world (below) |
| 11 | SEND | place, port, address, length → 1 or 0 | Send bytes. The thread sleeps while its network device transmits, then returns. Fire and forget (11.2) |
| 12 | RECV | port, address, max length, timeout → count, sender in B | Take one message from a comms port into your buffer, at most max length bytes (11.2) |
| 13 | DREAD | disk offset, address, length → count | Read the place's raw disk. Completes at the tick's barrier (9.3) |
| 14 | DWRITE | disk offset, address, length → count | Write it. Slow, and it wears the disk. Completes at the barrier |
| 15 | PING | place, timeout → round-trip ticks or −1 | The destination's OS answers if its network device is on (11.2) |

**Parked with the GPU and NPU [parked, R20]:**

- GLAUNCH (address, lanes, pages) runs lanes in lockstep on the GPU.
- NMATMUL (operand addresses and shapes) has the NPU multiply matrices.

Their designs are in 9.5.

**Idle runs.** Code running in the reset region with no program (10.2) can
use only:

- SENSE, which needs no owner;
- ALLOC, FREE and PROTECT, on what the run itself holds;
- SPAWN into what it holds, which creates a program (9.4);
- DREAD (a booted second stage reads the disk this way, 10.5).

Every other call fails soft, DWRITE included, so junk can't overwrite the
disk.

**Not SEND [2026-11-17, `build-plan-gate3.md` 11 step 8; Damian].** An
idle run's SEND fails soft too, so code leaves a place only in a program's
SEND. Gate 3's flood test found bytes that SENT themselves from port 0 to
port 0 without becoming a program, filling the queues and taking the cores
static needed (10.4). A setting lets idle runs SEND, as before; then a SEND
from an idle run has no sender program, so a birth from it starts a new
lineage (13.2).

**SENSE reads only what's local:**

- **its own place:**
  - temperature, voltage and the time;
  - the place's id;
  - the thread's remaining time in its turn;
  - bytes waiting at a port;
  - idle cores and free memory;
  - the page size and whether the place is strict or lax (R25);
  - how many devices of each kind it has, and how many neighbours;
- **a device here, by index:** its kind, its speed (cores included), its
  nominal threshold (not its worn one, R25), and its free bytes;
- **a neighbour, by index** (its data links, in a fixed order): its id,
  voltage, temperature, idle cores and free memory. This costs one hop of
  network time: the thread sleeps for it.

A program's own remaining time and bytes waiting stay private to that
place.

**Numbering.**

- The slot count and the numbering table (slot → call) are settings, fixed
  for the run.
- **The default is 18 slots.** Slots 0–15 are the calls in the table's
  order above, slot 16 is a second ALLOC and slot 17 a second SPAWN, so
  random code finds them sooner. The origin test's first lever (10.6) adds
  more.
- The other numbers are set by testing origin and mutation rates.
- Neighbouring numbers matter: a one-bit flip turns one call into another.

### 9.3 Memory and disk

- **Memory is allocated by request,** and allocations have boundaries.
- **The allocator uses size classes,** as modern allocators do (slab
  allocators, jemalloc). Allocations of similar size share pages, first-fit
  within a class.
  - Kin are the same length, so kin pack together: local structure for
    cooperation.
  - A parasite shares its host's page only by matching the host's size, as
    real heap exploits "groom" the heap.
- **Pages are grouped by size class and permission.**
- **Access is by page; ownership is by allocation.**
  - A program owns allocations. A page may hold allocations from several
    programs.
  - A thread may touch any byte of a page where its program holds at least
    one allocation, within the page's permission:
    - data pages are read and written;
    - executable pages are read and run;
    - "both" pages are read, written and run.
  - So within a shared page nothing is protected: an overrun reaches the
    neighbour.
  - When a program frees its last allocation in a page, it loses access to
    that page.
  - Touching a page where its program holds nothing faults.
  - Asking for whole pages is safe, but the program pays for the unused
    rest. That's packed and exposed against padded and wasteful (principle
    6).
- **Operations that span pages:**
  - an instruction (or CPY, all or nothing) faults if any byte it touches is
    out of bounds;
  - a call stops at the first page it can't use, and returns the count done.
- **PROTECT changes a whole page,** so it works only if the caller's program
  holds every allocation in the page. Otherwise it fails soft.
  - On a strict place, a copier must take a whole page to make its copy
    executable, so every program there holds at least one page. Strict
    places force padding.
  - Real anchor: `mprotect` works on whole pages, which is why JIT
    compilers map whole pages for their code.
- **Strict and lax places.** Whether a page may be writable and executable
  at once (W^X) is an OS value per place (9.6).
  - **A lax place has no no-execute bit** [2026-11-13, the design review
    after the origin test's levers; agreed with Damian]. Any page a program
    can read, it can run, as on x86 before NX (2004). So ALLOC's and
    PROTECT's "data" give "both" there. "Executable" still gives read-only
    code, so a program can still protect its code from being written.
    - Why: a copier's ALLOC takes its permission from B, and B often holds
      its own address, so before this a copier worked only where that
      address was 2 mod 3 ("both"): a third of pages for the copiers found.
  - On a lax place, a copier allocates "both" (or "data") and skips PROTECT.
    Copying is cheap, and injection is easy.
  - On a strict place, ALLOC with "both" fails, and the reverse holds. Data
    pages there don't run.
- **Nothing is zeroed.** A dead program's pages go back to the free pool
  with their bytes intact. ALLOC hands out dirty memory: a corpse's code, old
  data, or static. A program that wants clean memory zeroes it itself.
  - Real OSes zero memory for security. Nova 2 skips that on purpose: the
    leak is how the dead feed new life.
  - ALLOC can't be aimed at a particular corpse. Matching a size class is
    the nearest thing.
  - *Prediction:* ALLOC with permission "executable" can return a dead
    program's code, and a SPAWN there is a birth with no copy at all. On a
    strict place it may be the cheapest birth.
- **The allocator in detail [details at build, step 3; 2026-11-11].**
  - **Size classes:** 8 bytes, then two per doubling (16, 24, 32, 48, 64,
    96, ...) up to the page size. A setting (`memory.class_steps`) gives
    powers of two only. An 18-byte program takes a 24-byte slot.
  - **Larger allocations take whole pages** (rounded up), never shared, up to
    the largest allocation (`memory.max_alloc`, placeholder 4,096 bytes, at
    least a page). That's ALLOC's "largest size class" in appendix A.
  - **First fit by address:** the lowest page on the device with a free slot
    of that class and permission, else the lowest free page; the lowest free
    slot in it. A whole-page allocation takes the lowest run of free pages.
  - **A slot's padding and a page's tail** belong to no allocation. Access is
    by page, so whoever holds any slot in the page can touch them.
  - **PROTECT on a multi-page allocation** changes only the page named; a
    strict program bigger than a page protects each page.
  - **The reset region** takes whole pages at the start of its device (after
    page 0 if that's where it is). Only idle runs may touch it: programs hold
    no allocation there, so by access-by-page they can't.
  - **"Free memory"** (SENSE) counts free pages and free slots, not the
    tails.
- **The disk is raw and unowned (R14).**
  - DREAD and DWRITE take any offset in the place's disk space (the disks
    laid end to end, 6.3). There is no disk allocation, and no disk
    permission.
  - Data on disk outlives its writer. Anyone can read it, and anyone can
    overwrite it. So backups are possible, and so is vandalism: a trade-off
    programs can take either side of.
  - DWRITE needs a thread. Idle runs may DREAD but not DWRITE (9.2).
  - **Disk I/O completes at the tick's barrier,** in a fixed order (by core
    index, then request order). The thread sleeps until then.
  - **In detail [details at build, step 7; 2026-11-11]:** the count done
    lands in A at the barrier. The disk's latency (placeholder 20,000
    cycles) plus a cycle per byte is then paid from the caller's next
    quantum. A length of 0, or a place with no disk, answers 0 at once. An
    idle run's DREAD waits for the barrier with its core doing nothing; if a
    thread takes the core first, the DREAD is forgotten.
  - The boot block is the first block (10.5).
  - Real anchor: a disk with no file system, which is where boot-sector
    viruses lived.

### 9.4 Programs, ownership and individuals

- **A program is an owner record** kept by the OS. Its record begins at
  SPAWN (R5), in two ways:
  - **from an idle run:** a run in the reset region may ALLOC, and holds
    what it gets. Its SPAWN into one of those allocations creates a program
    that owns it, with the run's cause (static, an arrival, or a boot);
  - **from a thread:** SPAWN into an allocation the caller's program holds,
    other than the one holding the caller's IP, gives that allocation to a
    new program.

  The new thread is the new program's first.
- **SPAWN's "own code" is the allocation holding the caller's IP at that
  moment (R15).**
  - A SPAWN there starts another thread of the same program.
  - A SPAWN into any allocation where one of the caller's program's threads
    is running also starts a thread, never a gift. So an allocation in use
    is never given away.
  - **Page sharing counts as running there** [2026-11-12, after the third
    origin run; agreed with Damian]. On a lax place a thread can run past
    the end of its allocation, in the rest of a page it may use by holding
    that allocation. A thread whose IP is in no allocation of its own runs
    on every allocation its program holds in that page. Otherwise a program
    could give its own running code away: the hand-overs of
    build-plan-gate1.md 11, which looked like copies and weren't.
  - Real threads run inside their process's image, and running a different
    image makes a new process.
- **So copies are separate programs.** ALLOC, CPY and SPAWN produce offspring
  that live and die on their own, as Tierra's daughters do.
- **When a run ends,** whatever it still holds (anything it didn't give at
  SPAWN) is freed. A failed ALLOC creates nothing.
- **A program ends when its last thread ends.** Its pages are freed, with
  their bytes left in place.
- **Each thread has a stack:** an allocation made at SPAWN, owned by the
  thread's program, on the same memory device as its start. Its size is a
  setting (appendix A).
  - A thread's saved registers live there between turns. So they fade and
    corrupt with their memory, and an overrun into a stack can rewrite a
    sleeping thread's state.
  - **A thread ends when a bit of its saved state fails** [2026-11-14,
    `build-plan-gate2.md` 2, for fading; widened 2026-11-16 to every way
    a bit fails, `build-plan-gate2.md` 11 step 10; agreed with Damian].
    - Its saved state is its saved registers while it is off its core,
      and the frame of a handler it is in, on its core or off (A.4).
    - A bit fails when it fades, takes a cosmic-ray flip or has an edge
      error, whatever it then reads back. That is appendix B's "failing
      past use", and the cause is memory.
    - Without it, a thread asleep until an interrupt that never comes
      would hold its memory until a cold start. Fading alone was too rare
      to stop that: gate 2 failed with it and passed with the wider rule
      (14.1). At the runs' cosmic floor, a sleeping thread's 192 bits of
      registers take a wrong bit about every 5,200 ticks.
    - An overwrite only corrupts the state, which the thread reads when it
      next runs. A thread on its core holds its registers in the core, so
      a failed bit in their old copy in its stack doesn't end it.
    - The switch `os.saved_state_fails` (on by default) narrows the rule
      back to fading, so gate 2's first run can still be run as it was.
  - **A thread that ends while its program lives on gives its stack back**
    [details at build, step 5; 2026-11-11], as real threads' stacks are
    freed when they exit. Otherwise every ended thread would leave its stack
    held for the program's life.
  - **A live thread's stack is in use** [2026-11-19, after the bug scan
    of gate 3's worlds; agreed with Damian]. It follows R15: an allocation
    in use is never given away.
    - A SPAWN into the stack of a live thread of the caller's program, the
      caller's own included, starts another thread of that program, never
      a gift.
    - A FREE of it fails.
    - So a thread's saved registers always lie in memory its program
      holds, and its end frees its own stack and nothing else.
    - Why: the OS writes the saved registers at the stack's address each
      time the thread leaves its core. Before, a program could give a
      sibling thread's stack to a new program, or free it, and those 24
      bytes then went into memory another program held, on a strict place
      too; and if the same program's next allocation landed there, the
      thread's end freed it. It was rare: 48 of about a million threads in
      gate 3's saves (`nova2 check` counts them).
    - The switch `os.stack_held` (on by default) brings the old rule back,
      so gate 3's runs can still be run as they were. A world saved under
      the old rule keeps the threads it had in that state until they end.
- **OS records are protected infrastructure (R16).** These are ownership
  and allocation tables, scheduler queues, routing tables, and queue and
  inbox bookkeeping. They live outside simulated memory, and never fade.
  Message bytes waiting in an inbox live on the reset region's device, and
  do fade (11.5).
- **Ownership changes hands in only two ways:** by freeing and being claimed
  again, or by a gift to a new program at SPAWN.
  - Overwriting cells you don't own uses them without owning them.
  - A hijack runs code on the victim's threads, with the victim's
    ownership.
- **Resources:**
  - memory is held as allocations;
  - the disk is shared raw (9.3);
  - cores are shared in time, not held;
  - power is drawn, not held (4.5).
- **What eating becomes:**
  - hijacking: overwriting a neighbour so its threads run your code;
  - sabotage: corrupting it until it fails;
  - scavenging: claiming and running what it leaves.

  Whether that's enough for predation is unproven.
- **Turnover: nothing kills a program that doesn't fault (R30).**
  - Programs end by faulting, by running on a core when it cuts out, by
    their memory failing or fading, or by EXIT.
  - Holding memory costs a program nothing. One program could take a
    place's free memory and fill it with its own threads, out of reach of
    every attack in 9.8.
  - Whether a full place keeps turning over is gate 2's question (14). If
    it doesn't, the lever in reserve is a visit that reboots a place
    (7.8).
  - **Gate 2's first answer: no, not by the physics as written**
    [2026-11-15, `build-plan-gate2.md` 11 step 9;
    `design-review-gate2-2026-11-15.md`, agreed with Damian].
    - Gate 1's frozen places came back, and only cold starts ended them.
    - The saved-state rule rarely fires. Memory fades only in blackouts and
      the deepest drop-outs, and those cold-start the place anyway.
    - The review's answer is to widen the rule from "fades" to "fails":
      a thread ends when any bit of its saved state goes wrong by the
      physics, whether it fades, takes a cosmic-ray flip or has an edge
      error. An overwrite still only corrupts it.
    - The widened rule is built behind a switch (`os.saved_state_fails`,
      off by default) and tried in a pilot. It becomes the rule only if
      the pilot bears it out. The reserve lever stays in reserve.
  - **Gate 2's second answer: yes, with the widened rule** [2026-11-16,
    `build-plan-gate2.md` 11 step 10; agreed with Damian].
    - Gate 2's first run again, with the widened rule on, passed every
      part of the mark: every counted window turned over, every crowded
      one too, and stage 4 came in 13 of 20 seeds (14.1).
    - Every program alive at a window's start died within it: none lasted
      200,000 ticks. What lasts is lineages, by copying, with holds of up
      to 3.6 million ticks.
    - The widened rule is now the rule, on by default. The reserve lever
      stays in reserve.

### 9.5 Threads, scheduling and interrupts

- **Time is simulated time (R9).** A tick is a fixed span of it. A turn and
  a quantum are slices of it. A core's cycles in a slice are its speed ×
  the slice.
- **A fair scheduler.** Each thread gets a fair share of its core's time,
  in plain round robin.
  - The OS moves threads to idle cores to balance load.
  - Linux's extra boost for threads that have slept a lot is left out until
    a run needs it [gate 2 or later].
- **No priority.**
  - If programs set it, everyone sets the maximum.
  - If the OS set it by a rule, the rule would pick winners.
- **The main lever is the number of threads.** More threads get a bigger
  share, but each costs a SPAWN and memory for its stack.
  - Up to the place's thread limit, where it has one (9.6) [2026-11-25].
- **A thread can be pinned to one kind of core** with SETCORE: any (the
  default), P only, or E only.
  - **The pin is strict.** A pinned thread waits in its kind's queue rather
    than run elsewhere. That trades speed against waiting: everyone
    pinning to P crowds the P cores while E cores idle.
  - **Real anchor:** Linux's CPU affinity (`sched_setaffinity`).
  - **SETCORE fails soft** if the place has no working core of that kind.
  - **If every core of a thread's kind fails later,** the pin falls back to
    any, as Linux resets affinity when every allowed CPU goes offline.
  - **New threads start unpinned.** A migrant's threads start unpinned at
    their new place.
  - **Cost:** the crossing plus a few hundred cycles.
- **Order within a place: rotating quanta.**
  - Each tick is split into rounds. In each round, every core runs its
    current thread, or its idle run, for one quantum of time. The cores are
    taken in index order, rotated by one each round, so no core is always
    first.
  - The quantum is a slice of time, so a faster core does more cycles in
    it, and all cores finish the tick together (R13).
  - A scheduler turn spans several quanta.
  - A thread's writes are seen at once by whichever core runs next, so
    races between cores resolve the same way every time.
  - Messages, arrivals and disk I/O complete at the tick's barrier.
  - Smaller quanta interleave more finely but run slower.
  - **Real anchor:** deterministic multithreading systems (Kendo and DMP,
    both 2009) serialise threads in fixed quanta the same way.
- **A long instruction** (a big CPY) runs whole. Its time comes out of the
  thread's turn, and any overrun is repaid in its next turn (R29).
- **The scheduler in detail [details at build, step 6; 2026-11-11]:**
  - **Time** is kept at each core's stock speed. A thread at clock level k
    (tenths of stock) gets k/10 of the cycles in the same time. A new
    SETCLOCK level takes effect from the thread's next quantum.
  - **A thread runs while its quantum has time left.** The last instruction
    may overrun; the overrun is the thread's debt, taken first from its next
    quantum. A call that costs more than a quantum (SPAWN) works the same
    way, so a SPAWN keeps its thread off the core for about a tick.
  - **A thread that leaves mid-quantum** (by sleeping, waiting, yielding,
    ending or faulting) leaves the rest of the quantum to the next ready
    thread, which pays a switch (placeholder 1,000 cycles, plus 500 if the
    clock level changes).
  - **At a turn's end** the thread goes to the back of the queue only if
    another thread is waiting for that core. Alone, it carries on without a
    switch.
  - **The ready queues:** one for unpinned threads and one per pinned kind,
    ordered together by when each thread became ready. A pin to a kind with
    no working core falls back to any.
  - **Interrupts** are delivered one at a time, when the thread starts a
    quantum: low voltage first, then arriving data, then timers. Timers fire
    at the start of a tick. An interrupt wakes a thread waiting in SLEEP 0;
    a thread in SLEEP n or a RECV wait takes it when it next runs.
  - **A ready thread takes the core from an idle run** at the start of a
    quantum.
- **Every call, thread switch and fault costs time.** Asking the OS is
  never free.
- **Clocks are per turn.** A thread's SETCLOCK applies only during its own
  turns (7.5).
  - What's shared is the heat: an overclocker leaves the core hot for the
    next thread, and can trip it.
- **Yielding.** SLEEP with a negative value ends the current turn.
- **Interrupts.** SETHANDLER registers a handler for the calling thread (the
  details are in appendix A):
  - **faults:** a fault with no handler ends the faulting thread, as a real
    segmentation fault does. With a handler, the thread jumps to it and pays
    a crossing. The saved IP points past the faulting instruction, so HRET
    goes on after it. A fault inside a handler ends the thread. Only that
    thread is affected, so a program with several threads survives one
    fault. From gate 5 the watchdog is a fault too (9.1);
  - **arriving data** on a given port [2026-11-16, `build-plan-gate3.md`
    2; agreed with Damian]:
    - a message goes first to a thread waiting in RECV on that port (the
      one that has waited longest, 11.2), and then no interrupt is raised;
    - otherwise it joins the inbox, and every thread with a data handler
      for that port gets the interrupt pending. The first of them to RECV
      gets the message; the others find what's left;
    - a pending interrupt is raised once, however many messages join
      before the thread takes it;
  - **a timer,** with a period in ticks;
  - **low voltage** [gate 4] (12.4): a thread takes it when it starts a
    quantum on a core whose voltage is within the margin of that core's
    cut-off. The margin is a setting.
    - It fires once for a thread, and again only after the voltage has
      been back above the margin [2026-11-19, `build-plan-gate4.md` 2;
      agreed with Damian]. Before, it went to "the thread running on the
      core" as the voltage neared the cut-off. But the voltage changes
      once a tick and a turn is a quarter of one, so in a crowded place
      that thread was rarely the one on the core when it cut out.
    - It wakes nothing. A thread asleep or waiting is safe already (7.6).

  HRET returns from a handler.
- **Waiting is the program's own choice:**
  - wait (RECV with a timeout, or SLEEP);
  - poll (ask again later, paying each time);
  - a handler.
- **The GPU and NPU [parked, R20].** Their designs are kept for when a gate
  names a condition in which they win.
  - **GPU:**
    - GLAUNCH runs N lanes in lockstep groups of 32, over a set of pages.
      Each lane has its own registers and starts with its lane number in
      one. A branch that splits a group runs both sides in turn.
    - Lanes read and write only the given pages, and a fault ends the
      launch. The launching thread sleeps until every lane has exited.
    - If unparked, two gaps must be closed first:
      - EXIT must be the one call a lane may make, since SYS otherwise
        faults on a GPU;
      - a launch needs a cap on its run, and launches from different
        programs need a rule for sharing the device.
  - **NPU:** NMATMUL multiplies matrices in memory into a result, ready
    later. Its calling convention and number format are unset.

### 9.6 OS variety

- **Each place's OS has its own values,** drawn from fields over the map
  with a little scatter and steep drop-offs (6.2; how, 7.3). They're set
  when the place boots and never change.
- **What varies:**
  - the page size, from a short list;
  - the writable-and-executable rule, strict or lax (a lax place has no
    no-execute bit, 9.3);
  - the thread limit, from a short list (below) [2026-11-25].

  Programs can read the first two (SENSE, R25). Everything else is global,
  including the turn length (one world setting) and call costs.
- **Neighbours stay similar,** so a program can adapt to the OS around it.
  Nearby moves are safe, and far moves are a gamble.
- **Large pages make sharing, and so parasitism, easy; small pages make it
  hard.**
- **The thread limit [2026-11-25, Damian: "Yes, as leaned", on
  `design-thread-limit-2026-11-25.md`; off by default, on trial].** Each
  place's OS has a limit on the threads one program may have, from a short
  list (`os.thread_limits`). A 0 in the list is no limit, and no limit is
  under one.
  - **The rule.** A SPAWN that would start a thread fails when the
    caller's program already has as many live threads as its place's
    limit. It fails as any SPAWN fails: nothing is started and nobody is
    ended. A SPAWN that makes a new program is untouched.
  - **A program over its place's limit,** however it came to be (a world
    saved before the limit, say), keeps its threads until they end. It
    can't start another until it is under.
  - **Dealt as the page size is,** by a field of its own (7.3) and from
    draws of its own, so adding it changes nothing else about a world.
  - **Programs can't read it.** A program learns the limit when its SPAWN
    fails, as a real one does.
  - **The default is no limit at every place,** which is the world as it
    was. It is first tried in `runs/open-6`: open-5's world with limits of
    1, 4, 32 and none, about a quarter of the places each, and a second
    run beside it at a tenth of the cosmic-ray rate. A report at 5 million
    ticks decides whether the limits and the rate stay.
  - **Why:** a core is shared by thread (9.5), and a thread costs 64 bytes
    and a SPAWN, so a few programs start thousands. `runs/open-5` at tick
    22.5 million had 3.3 million threads on about 3,600 cores, and in one
    place 6% of the programs held 75% of the threads. Any one rule
    everywhere crowns one way of living: starting threads with no limit,
    copying fast with a limit of one. Places that differ leave room for
    both (Damian: "there has to be a mechanic we can use that should
    balance things to ensure that there is no one king").
  - **It passes 9.1 as first written:** a fixed rule, the same for every
    program in the place, about the asker only. The amendment of
    2026-11-20 isn't used, and its choice isn't reversed.
  - **Real anchor:** `ulimit -u` for a user and `pids.max` for a group of
    processes, set by whoever runs the machine and different from machine
    to machine. Linux's own default scales at 1,024 thread ids a CPU and
    never under 8.
  - Chosen over one limit everywhere, over a share of cores by program
    (the same king as a limit of one, with the waiting lines rewritten),
    and over a limit on a place's threads (a full place would refuse
    births too, and the killer wouldn't see it).

### 9.7 Service costs (agreed; numbers tunable)

**Calls that use the core's time:**

| Call | Cost |
|---|---|
| SENSE (own place or device) | The crossing only, about 150 cycles |
| FREE, EXIT, SLEEP, SETHANDLER, HRET, SETCORE | A few hundred |
| ALLOC | About 1,000–3,000 (page setup) |
| PROTECT | Thousands |
| SPAWN | About 10,000–50,000, so reproduction's cost is mostly making the thread |
| SETCLOCK | About 10,000–100,000, plus a settling delay |
| A fault's trap | The crossing |

**Calls where the thread sleeps and the core runs others:**

- **SEND:** the crossing and a per-byte copy, then a sleep while the network
  device transmits: the device's latency + size ÷ the first link's
  bandwidth (R10);
- **SENSE of a neighbour:** one hop of network time;
- **RECV** while waiting;
- **PING,** for its round trip;
- **DREAD and DWRITE:** until the tick's barrier, plus the disk's latency,
  tens of thousands of cycles and up.

**The shape:**

- sensing and computing are cheapest;
- asking for memory is modest;
- making threads, changing clocks and all I/O, the network included, are
  dear.

**[open]** The exact numbers.

### 9.8 How programs touch each other

Real isolation would be Nova 1's private code, which forced ENGULF and
INJECT. Nova 2 breaks isolation only in the three ways real machines do:

1. **Shared pages.** Allocations are packed, so programs share pages, and
   nothing within a page is protected (9.3). A parasite can read and run its
   host's code within a shared page.
2. **Injection through input.** RECV copies into ordinary memory, at most
   the max length the program passes.
   - A program that passes a max length larger than its buffer is
     overrun: the real bug behind most stack smashing.
   - The overrun runs into its own code or a neighbour's, and return
     addresses on the stack can be overwritten.
   - It needs victims that read input, so reading input must first pay for
     its own sake.
3. **Physical faults:**
   - errors near a device's edges cross allocation boundaries;
   - **rowhammer [gate 5]:** repeated row activations flip bits in
     neighbouring pages. Pages stand in for DRAM rows.
     - **How it counts:**
       - an access counts as a row activation when it touches a different
         page from that thread's previous memory access;
       - CPY counts once for each page it touches;
       - repeated accesses to one page don't re-activate it, as a real open
         row doesn't.
     - Past the memory device's threshold, the pages immediately before and
       after it in the same device get bit flips. The flip chance grows with
       the excess. The flips are drawn from the seeded RNG at the tick's
       end. The counts are kept over a refresh window, as appendices C and
       E have it, and reset at its end [2026-11-20, `build-plan-gate5.md`
       2; agreed with Damian. Before, this said the counts reset each
       tick, which would make the threshold about one activation a tick].
     - **In detail [details at build; 2026-11-20, `build-plan-gate5.md`
       2; agreed with Damian]:** an activation is what the engine's
       access counters count: a data access to a page other than the one
       the thread last touched on its core this tick, and a CPY once for
       each page. Each page's window count is kept beside the per-tick
       count memory's draw uses; the window is aligned to the tick and the
       same for the whole world. A memory device's threshold follows its
       latency on its curve, fast memory lowest (newer, denser chips flip
       sooner). At a tick's end, each activation past the threshold in
       the window flips one bit in the page before and one in the page
       after, in the same device, each with the strength as its chance:
       the count drawn, then the bits.
     - The threshold is set per unit of simulated time and scaled by the
       tick (R9), so changing the tick's length doesn't change the physics.
     - A slider sets the strength, with 0 meaning off.
     - **Real anchors:** Kim et al. (2014) found DDR3 rows flipping after
       about 139,000 activations within a 64 ms refresh window. Newer chips
       flip at around 10,000. Real attacks alternate between two rows
       (double-sided hammering).
     - **What it gives:** an attack that crosses page boundaries. Padding
       with whole pages doesn't protect against it. Hammering costs the
       attacker cycles, and its flips can hit kin as easily as rivals.
     - **Cost:** one page comparison and counter per memory access. That's
       measured with the first benchmark.

**An access outside the pages a program can use faults** (9.5). Probing
other memory is possible with a handler, but costly. Early random code has
no handlers, so it dies quickly when it scribbles.

## 10. Where life starts

### 10.1 From static, and nowhere else

- **In runs, life starts as random static in the reset region.** No ancestor
  is ever seeded in a run. Hand-placed programs are allowed only in unit
  tests and test worlds, which check mechanics and never count as evidence
  (2, R7).
- **Memory and disks start random ("static").** Memory changes afterwards
  through four things:
  - lost bits reading back random (4.4);
  - the cosmic-ray floor (7.5);
  - errors at device edges;
  - code writing over code.
- **The cost, accepted:** in runs, nothing downstream can be tested until a
  replicator arises. Tierra and Avida seed a hand-written ancestor for that
  reason. Here the origin test comes first (10.6), and test worlds check the
  mechanics so that a failed origin can't be a broken SPAWN.

### 10.2 The reset region

- **One shared region per place, owned by nobody and executable.** Code
  running there may write anywhere in it, so it's a small soup. An idle run
  may touch only the reset region and what it has allocated; anything else
  faults.
- **Idle cores run it,** P or E. A GPU never would, since lanes can't call the
  OS.
- **The idle run is not a thread.**
  - It holds no place in the scheduler, and any ready thread takes the core
    from it.
  - It has no stack, so PUSH and POP fault in it.
  - Its calls are limited (9.2).
- **Start spots step through the region (R2).**
  - Each core starts at its own spot, and each restart moves that spot on by
    a fixed step (a setting), so that over time every byte becomes an entry
    point. Each core's sequence is offset, so cores don't start together.
  - The registers point at the start spot on each start, as a real core
    starts with set values.
  - Why: a core restarted at the same spot, with the same registers, over
    the same bytes, replays the same path. Without stepping, only noise
    would keep the region changing.
- **What ends a run:**
  - a fault with no handler (a real triple fault resets a PC);
  - **a cap on its length** (a setting, longer than the dearest birth). Real
    anchor: a watchdog timer resets a hung machine (R3);
  - a ready thread taking the core;
  - a cut-out.
- **Halting (R1).** When a run ends by a fault or the cap, the core halts at
  idle draw. It restarts at its next spot when one of three things happens:
  - anything writes to the region (another core's run, an arrival, a boot,
    or a floor flip);
  - an arrival waits for it (10.4);
  - a slow retry comes round (a setting). A retry lets the core's own error
    floor produce something new.

  So an empty place draws little, and it isn't all replays.
- **How the region changes:**
  - junk code writing over it;
  - arrivals;
  - fading where the place is hot or its voltage sags;
  - edge errors;
  - the floor.

  Origins come faster where memory is noisy, so life tends to start in rough
  places and spread to calm ones.
- **Its size is a setting.** It lives on the place's memory device with the
  lowest threshold, the one that holds on longest.
  - If that device fails, the region is gone until the visit replaces it.
    The new device starts as static.
  - Meanwhile idle cores halt and arrivals wait in port 0's queue (R6).
- **Observers measure the regions themselves (R4):** how compressible each
  region is, and how many SENDs come from idle runs per sample (13.1).
- **Idle runs in detail [details at build, step 7; 2026-11-11]:**
  - **The step must share no factor with the region's size,** so every byte
    becomes a start spot; setup refuses a step that doesn't. Each core's
    first spot is an equal share of the region further on.
  - **An idle run runs at stock speed.** Its cap counts all its cycles: its
    instructions, its calls, and its DREAD's latency.
  - **A halted core looks for a reason to start** at the start of each
    quantum: the region's write count has moved since it halted, or the
    retry is due. Its own writes before halting don't count.
  - **Halted is a core's state, not a run's.** A core that a ready thread
    took while halted is still halted when the thread leaves. A core whose
    run was ended by a thread starts a fresh run at its next spot when it's
    next idle.

### 10.3 A birth, step by step

1. The reset region holds static.
2. An idle core runs it from its spot. Most bytes crash within a few
   instructions. The core halts, and restarts at its next spot when
   something changes (10.2).
3. Bytes that copy themselves within the region spread. They can't SEND
   themselves on: an idle run may not SEND (9.2).
4. **An idle run that ALLOCs holds what it gets.** If memory is short, the
   call just fails, and nothing is created.
5. **It copies itself into its new memory and SPAWNs there.** That creates a
   new program, which owns the allocation and has a thread under the
   scheduler. Its record begins here, with the cause "static" (9.4, R5). The
   original stays in the region until it's written over.
6. **When the run ends,** whatever else it holds is freed. A birth must
   finish within one run, so the cap must be longer than the dearest birth.

### 10.4 Arrivals at port 0

- **SEND to a place's port 0** queues the payload for its reset region.
  Migration is non-random code arriving in the soup, so travel and origin
  are one mechanism.
- **Port 0's queue is bounded (R3),** in bytes (a setting). When it's full,
  the newcomer is dropped, as at every other queue.
- **An arrival waits for a core,** either a halted one or one whose run
  ends.
  - The core writes the payload into the region at its current start spot,
    cut short if it would run past the region's end.
  - It then runs the payload from its first byte, as a fresh idle run (a
    "prompt run"), with the registers pointing at the landing spot.
  - An arrival never pre-empts a live thread or an idle run in progress.
- **Arrivals share the cores with static [2026-11-17,
  `build-plan-gate3.md` 11 step 8; Damian].** Arrivals from other places
  hold at most a set share of a place's cores at once (a setting, half),
  rounded down but at least one. While they hold it, a core that comes
  free runs static instead, and a halted core stays halted. A boot block is
  never held back. Why: in gate 3's flood test, arrivals took every core
  that came free, and static, and the origin with it, starved.
- **So migration needs spare core time, not just free memory.** A place
  whose cores are all busy with threads runs no arrivals, however well
  adapted they are. That's an absolute barrier to settling, and gate 3 tests
  it as a named hypothesis (14).
- **What travels:**
  - **complete replicators,** which must ALLOC, copy themselves out and
    SPAWN. Compact genomes travel best;
  - **code that never becomes a program,** which only sends itself on, if
    idle runs may SEND (a setting, off since 2026-11-17, 9.2). It pays no
    SPAWN, appears in no lineage, and may spread before any program does.
    Observers count it (13.1). With it off, bytes move between reset regions
    only in what programs send.

### 10.5 The boot block

- **The problem it solves.** A deep outage wipes memory and ends every
  thread, and the disk keeps its contents. Without a boot block, nothing
  would read the disk back.
- **The boot block is the first block of the place's disk space,** at offset
  0, on its most enduring disk. The disk is raw and unowned (9.3), so any
  thread may DWRITE it.
- **A cold start:** power returns after the voltage fell below the threshold
  of the memory device holding the reset region. That's a discrete event,
  not a judgement on gradual fading. On a cold start, the place puts its
  boot block at the head of port 0's queue, to run like an arrival (10.4).
- **A heat trip ends in one too** [2026-11-19, `build-plan-gate4.md` 2;
  agreed with Damian]. When the memory device holding the reset region
  trips on heat and then cools to its cool-down point (5), the place
  cold-starts, as it does after an outage. The device went off and came
  back, whatever turned it off.
- **A replacement doesn't** [2026-11-20, `build-plan-gate5.md` 2; agreed
  with Damian]. When that device fails and a visit replaces it, the visit
  hot-swaps it (7.8): the region comes back as static, and the boot block
  doesn't run.
- **A cold start reboots the place's OS** [2026-11-14,
  `build-plan-gate2.md` 2; agreed with Damian]. Every program and thread
  ends, with the cause "cold start", every allocation is freed with its
  bytes as fading left them, and idle runs start afresh. Then the boot
  block runs. R16 holds while the power is on.
  - So even a short dip below the reset region's threshold reboots the
    place, though most of memory still holds. Real anchor: a PC that loses
    power for a moment reboots, though its DRAM keeps most of its contents
    for seconds (the cold-boot attack, Halderman and others, 2008).
  - Port 0's queue and prompt runs are built at gate 2 for the boot block;
    arrivals from other places join them at gate 3.
- **So booting is migration from the place's own past.**
  - Any program that can migrate can boot. The only new behaviour is one
    DWRITE of itself to offset 0.
  - **Programs bigger than the block use a second stage.** The booted code
    (an idle run) DREADs the rest from fixed offsets. That works because the
    disk is raw: data on disk outlives its writer. A real master boot record
    (446 bytes of code) loads the rest the same way.
  - The last writer before the outage wins. Writing costs DWRITE's time and
    wears the disk, so keeping your copy current has a price, and a rival
    can overwrite it.
- **A worn boot block** makes its disk count as failed, and the next visit
  replaces it (7.4).
- **Disk starts as static,** so a boot block nobody wrote boots static, and
  origins after an outage are unchanged.
- **The outage regime sets the pressure, not the boot block.**
  - When outages are local and short, migrants from neighbours refill a
    place, and dispersal wins.
  - When they're wide and long, the boot block pays, and dormancy wins.
  - Real anchors: plants recover after a fire from seeds in the soil or
    seeds blown in. Dormancy is bet-hedging (Cohen, 1966), and dispersal
    and dormancy are substitutes (Venable and Brown, 1988).
- **The first to boot has an empty place to itself:** a founder's head start
  (priority effects).
- **Real anchors:** a PC's firmware loads the disk's first sector and runs
  it, and boot-sector viruses lived there (Brain, 1986; Stoned;
  Michelangelo).

### 10.6 The origin test (gate 1)

This is the first build target, and it gates everything else (R8).

- **The question:** does a program lineage arise from static, under the 22
  instructions and 16 calls, with nothing seeded? Code that only sends
  itself on doesn't count.
- **The target length.** The traced shortest replicator is 14 bytes on a lax
  place (11 where its address isn't 1 mod 3), and 20 on a strict one with
  64-byte pages (appendix A.5, under the conventions of 2026-11-13).
- **Pass stages,** each reported separately:
  1. a copying sequence appears: a program has a descendant with its
     genome;
  2. the offspring reproduce: viable offspring;
  3. a lineage reaches several generations (a setting, for example 10);
  4. the lineage establishes: it persists despite faults and competition,
     for a set span. It holds its place for the whole span without a break:
     a set number of living members (20) or more, most of them born as
     copies of their parent (faithful, or at least half alike, 13.2), and a
     set share (10%) of the place's programs [2026-11-12, after the third
     origin run, agreed with Damian];
  5. it keeps adapting once the place is crowded: its genome groups change
     (13.2). Only groups it makes after it establishes count, and a seed
     runs on after stage 5 [2026-11-12, after the fourth origin run, agreed
     with Damian].
- **The budget and the pass mark** (placeholders, to fix before the first
  run):
  - a compute budget per seed, about 10^12 instructions, which is Nova 1's
    12-hour trial;
  - at least 10 seeds;
  - a pass means stage 4 in at least half the seeds, within the budget.
- **What it reports besides:**
  - distinct paths tried per second, not only instructions per second, so
    replays can't pass for search (R2);
  - the region measures (10.2);
  - deaths by cause.
- **If it fails,** the levers are tried in this order:
  1. the call numbering's extra slots (9.2);
  2. the reset region's size and the start-spot step;
  3. the noise floors;
  4. the calls' argument rules (appendix A);
  5. the instruction set.

  Then the design is revisited with Damian. A higher noise rate inside the
  reset region alone is possible, but nothing in real hardware justifies
  it.
- **The review after all five levers** [2026-11-13, agreed with Damian;
  `design-review-origin-2026-11-13.md`]. Runs 3 to 6 each reached stage 4
  in 1 seed of 10. Decided:
  - **Lever 5's experiments are dropped.** `os.spawn_top` (a SPAWN that
    makes a new program sent its caller back to its start) broke 9.1 (the
    OS moving a program unasked), principle 12 (it handed copiers their
    loop) and principle 9 (no real anchor), and didn't change the count.
    TOP (a spare code made a jump to its allocation's start) did no better
    than without it.
  - **Two experimental conventions become the rules:** a new thread starts
    with B, C and D at its start (A.3), and ALLOC's result, the address or
    0, lands in D as well as A (A.4). Nothing reached stage 4 without them.
    D is set on a failure too, so A.5's turnover holds.
  - **A lax place has no no-execute bit** (9.3).
  - **Turnover stays at gate 2.** At gate 1 nothing ends a program that
    doesn't fault (R30), so a full place can freeze, and stage 5 may not
    come. Stage 4 counts a frozen hold. If these changes still leave stage
    4 at 1 seed in 10, bringing gate 2's turnover forward is the next
    option; it would also make IP wrapping at an allocation's end safe.
  - **Strict places are barren at gate 1,** a finding: none reached stage 1
    in six runs. Life starts on lax places, and strict ones are for
    migrants to settle at gate 3.
  - **Snapshots of seeds** are built, so a lineage can be traced in the
    place it lived in.
  - **The pass mark stays** until these are tested. It is about seven times
    out of reach (about 0.1 established lineages a seed against the 0.7 it
    needs). Beyond that lie a bigger budget, or reopening 10.1's rule that
    no ancestor is seeded in a run.
- **The result: a pass** [2026-11-14, accepted by Damian; build-plan-gate1.md
  11, runs 7 and 8]. Under the review's rules, with the first three levers
  on, stage 4 came in 11 of 20 seeds within the budget (5 of seeds 1 to 10,
  6 of seeds 11 to 20), against a mark of 10. Every seed reached stage 1,
  19 stage 3 and 6 stage 5. All of it was on lax places.
  - **Half the holds sleep.** In 5 of the 11 seeds lineages were still
    copying and taking turns at the end. In the other 6 copying had
    stopped and the holding lineage stayed, most of its threads asleep
    waiting for an interrupt, in full places and not. Nothing at gate 1
    ends a sleeping program, so turnover in a full place is gate 2's
    question (R30).
  - **The side measurements (14.1):** faults weren't too harsh for the
    first copiers to establish, though nearly every child died by a fault
    (16.8 million fault deaths against 17.0 million births in seeds 1 to
    10). The reset regions kept changing: their compressibility drifted
    from about 0.84 at 2 million ticks to about 0.77 at the end, rising at
    times, against 0.97 for static.
- **Why it might fail:** a replicator here needs ALLOC, a copy, perhaps
  PROTECT, and SPAWN, and each call costs a crossing. BFF got replicators
  from random code because a replicator there needs no calls at all.
  Recent BFF work also separates replicators appearing from their spreading
  (Astra's citation; Claude can't check it).

## 11. The network [gate 3]

### 11.1 Knowledge is local

- **Programs sense their own place and their neighbours** (9.2, SENSE).
  Neighbours are data neighbours: the places a data link reaches (A.4).
  - Anything about distant places must come from other programs: something
    an information-age civilisation would invent. Expect that to be rare.
  - The world must work without it, and neighbour-to-neighbour spreading is
    diffusion.
- **Place ids carry no geography.** They run from 0 to N−1, shuffled by the
  seed. Any value names some place (the value modulo N, appendix A), so
  probing ids learns only reachability and distance.
- **RECV returns the sending place's id with the data.** Any message is a
  weak report that its sender's place was powered and inhabited.
- **PING gives reachability, power and distance,** at the price of a round
  trip (11.2).
- **Routing is the network's,** not something programs evolve.
- *Prediction:* most blind SENDs will go to a few arbitrary places. After
  most calls, A holds 0, −1 or a small count, and a one-byte constant
  reaches only 0–127. Distance then isolates nothing for programs that don't
  sense first. To be watched at gate 3.

### 11.2 SEND, RECV, PING and ports

- **Ports.** Each place has numbered ports (16 by default, a setting).
  - **Port 0 is the reset region:** migration (10.4).
  - The others are comms inboxes.
  - A SEND names a place and a port, never a program; whoever listens there
    gets it. Lineages can agree on ports, as services use well-known ones.
- **SEND is fire and forget, and it costs the sender time (R10).**
  - It pays the crossing and a per-byte copy into its network device.
  - Then the thread sleeps while the device transmits: the device's latency
    + size ÷ the first link's bandwidth, in whole ticks (11.3). Real
    anchor: a blocking send returns once the data has left the sender's
    buffer.
  - It pays a little power per byte at the sending and receiving places.
  - **It fails only for local reasons:**
    - no route in the place's own routing table;
    - its own exit queue is full;
    - its own network device is off.
  - **Nothing comes back from downstream.** A program that wants
    confirmation needs a receiver that replies. That's the end-to-end
    argument (Saltzer, Reed and Clark, 1984): corruption is silent, so only
    the receiver could confirm anyway.
  - **Why it costs time:** with SEND at about 180 cycles, mailing a copy
    would cost about a hundredth of a SPAWN. Every idle thread would then
    flood the queues, leaving PING, neighbour SENSE and real messages
    little room.
  - **A message's size** is limited by a setting, never more than an inbox.
- **RECV** takes one whole message from a port's inbox.
  - It copies at most the max length the program passes, and the rest of
    that message is discarded.
  - It returns the bytes copied in A and the sending place's id in B.
  - Its timeout chooses the waiting style: 0 polls, positive waits that many
    ticks, negative waits until data comes.
  - When several threads wait on one port, the one that has waited longest
    gets the next message.
  - The buffer is ordinary memory: a program that passes too large a max
    length is overrun (9.8).
- **PING place, timeout** returns the round-trip time in ticks, or −1 if no
  reply comes in time.
  - The destination's OS answers by itself, if its network device is on.
    No program and no core is needed. Real OSes answer ICMP echo the same
    way.
  - The request and reply use the same queues as everything else. So
    congestion shows as a longer time, and a full queue as a timeout.
  - It reveals reachability, power, distance and congestion, never
    resources or residents.
  - It costs the crossing and the thread's time for the round trip.
- **SEND, RECV and PING in detail [details at build, step 4; 2026-11-16]:**
  - A SEND copies its bytes when it's called, into its place's outbox, and
    pays the crossing plus `os.send_byte_cycles` (2) a byte. Every place's
    outbox joins the network at the barrier, in place order, ahead of that
    barrier's arrivals (11.3). The room a SEND needs in its exit queue
    counts what the place's earlier SENDs that tick keep there.
  - A message delivered to a comms port goes straight to the thread that
    has waited longest in RECV on it, if any. Otherwise it joins the port's
    inbox, or is dropped if the inbox is full, and the data interrupt is
    pending for each thread with a handler on that port (A.3). RECV takes
    the first message for its port in the inbox.
  - A PING is a request of `net.ping_size` bytes. At its destination the
    network turns it round as an echo of the same size, at the barrier it
    arrives. The echo ends the wait with the ticks since the PING. A PING
    that times out gets −1, and its late echo is ignored. A PING fails at
    once (−1) for SEND's local reasons.
  - **To its own place [2026-11-16, `build-plan-gate3.md` 11 step 4;
    agreed with Damian],** a SEND or PING crosses no link. It's delivered
    after the device's latency. So a PING to its own place takes twice the
    latency, and the SEND's caller waits the latency alone.
  - **An idle run waits on its core [2026-11-16, `build-plan-gate3.md` 11
    step 4; agreed with Damian]** for its SEND's transmission (where idle
    runs may SEND, 9.2) or its neighbour SENSE's round trip, holding the
    core. A ready thread can
    still take the core, which ends the run, as it ends any idle run
    (10.2). An idle run's RECV and PING fail (A.4).
  - **A PING's request and echo are 8 bytes each [2026-11-16,
    `build-plan-gate3.md` 11 step 4; agreed with Damian; a placeholder].**
  - SENSE of the bytes waiting at port 0 reads 0. Port 0's queue is the
    reset region's (10.4), not an inbox RECV can take from. At gate 2 the
    boot block waits there, and reading it would change gate 2's world.
  - With no map there is no network: SEND fails and PING returns −1, as
    before gate 3.

### 11.3 Links: bandwidth, forwarding, corruption, routing

- **Bandwidth is per link, in bytes per tick,** from its own field.
  Power-rich and data-rich routes can differ.
- **Bridges** carry data only, with high bandwidth (real undersea cables
  carry most intercontinental traffic). There are few of them, so they're
  bottlenecks.
- **Forwarding is hop by hop: store and forward (R28).** Each place
  receives a whole message before sending it on. Each hop takes:
  - the per-hop cost;
  - the link's length × the per-length cost;
  - the size ÷ that link's own bandwidth;
  - the wait in that link's queue.

  The delay is the sum over the hops, and it emerges as the message moves.
  - A per-length cost of 0 gives a hop-only network, for experiments.
  - Real anchors: each hop costs about a microsecond of switching. Light in
    fibre covers about 1 km in 5 µs, so crossing an ocean takes about
    30 ms.
- **Time on the network is in whole ticks [2026-11-16,
  `build-plan-gate3.md` 2; agreed with Damian].** Messages move at the
  barrier (appendix C), so a message can't cross part of a hop.
  - A hop takes at least a tick: the per-hop cost plus length × the
    per-length cost, rounded up, plus the size ÷ the bandwidth, rounded up,
    plus the wait in the queue.
  - A message crosses at most one hop at a barrier.
  - **Where messages meet, the order is fixed.** Messages joining one queue
    at one barrier join in order of the place they came from (by id), then
    their order there. A place's own SENDs join in the order they ran. So
    delivery doesn't depend on the host's thread count.
- **Corruption grows with route length,** at a rate that's a setting. It is
  silent. Corrupted data arrives as it is, as with UDP's checksum switched
  off (which IPv4 allows), so it can become mutation. Programs can evolve
  their own checks.
- **Routing takes the path with the least static cost:** the per-hop cost
  plus the per-length cost along the way. Ties go by id, so routing is
  deterministic.
  - Link costs are fixed. Routes don't follow load, as real routing
    protocols mostly don't.
  - Routes are recomputed, batched once a tick, when the topology changes:
    - a link fails or comes back;
    - a network device goes off or comes on;
    - a place respawns.

    If ordinary nights take network devices below their threshold, that's
    the commonest change. Its cost at 10,000 places is measured at gate 3
    (14).
  - Next-hop tables are worked out at each place: about 2 MB for 1,000
    places. At 10,000 places, routes are found on demand and cached.
- **The network in detail [details at build, step 3; 2026-11-16]:**
  - A destination's next hops are worked out when first asked for (a
    shortest-path search from it), and kept until the topology changes, at
    any number of places. The routes are the same as tables at each place
    would give.
  - **Working them out [details at build, step 5; 2026-11-16]:** at the
    barrier, the destinations the next barrier's arrivals will ask for are
    worked out ahead, spread across the host threads. Places SENDing in
    parallel each work out a stale destination on their own, and the
    first to finish keeps it. Neither changes a route, since a
    destination's next hops follow from the dark places alone.
  - The barrier's network step, after every place has run its tick:
    1. every dark network device drops what's queued at it;
    2. the messages reaching their next place, by the place they came from
       (by id), then their order on the link. At a dark place they're
       lost; at their destination, delivered. Any other joins the queue
       toward its next hop, or is dropped if there's no room, or stranded
       if there's no route;
    3. each free link end starts sending its queue's head, or drops it as
       stranded if the next hop is dark;
    4. if any network device went off or came on, the routes are worked
       out again before they're next used.
  - A message sent in a tick is in its queue before that barrier's
    arrivals join it.
  - Each hop flips each bit with a chance of `net.corruption` × the link's
    length ÷ the average link length (at most a half), drawn from a stream
    of its own for each tick.
  - The draw per byte is counted at the sender when the message is sent,
    and at the receiver when it's delivered.

### 11.4 The network device

- **Each place has one,** a router and interface in one, from the catalogue
  (7.2). It has a range, heat, wear, failure and visits like any device.
- **It must be on for the place to send, receive or forward.** So a blackout
  is also a hole in the network, and routes go around it. When it cuts out
  it drops everything queued at it: its link-end queues and the message it
  was sending (7.6) [2026-11-16, `build-plan-gate3.md` 2; agreed with
  Damian]. Real anchors: in
  Puerto Rico after Hurricane Maria (2017), and in Texas in February 2021,
  phone and internet service failed as power failed.
- **Power:**
  - a constant draw while on, about a tenth of a core's; real routers draw
    nearly the same busy or idle;
  - a small extra draw per byte at the sending and receiving places;
  - forwarding is free, because the network is infrastructure.
- **It has no speed of its own.** Links set the bandwidth, and its latency
  is one catalogue number.

### 11.5 Queues and inboxes

- **Each link end has a queue of fixed size,** in ticks of that link's
  bandwidth.
  - Real routers size buffers at about one round trip's worth of bandwidth
    (Villamizar and Song, 1994).
  - **It never holds less than the message size limit (11.2)
    [2026-11-16, `build-plan-gate3.md` 11 step 3; agreed with Damian],**
    so a message of any allowed size can cross any link. Otherwise the
    narrowest links' queues (8 ticks of 16 bytes) would be closed to
    messages over 128 bytes, a wall on each link rather than the pressure
    on big messages below.
  - Messages wait their turn.
- **When a queue is full, the newcomer is dropped,** silently. Messages
  already waiting keep their place (tail drop).
  - Big messages are dropped first, because they need more room. Real UDP
    loses big datagrams first too: losing any fragment loses the whole.
    Compact genomes travel best.
  - A flood from one program fills queues everyone uses: a commons.
- **A message whose next hop is a dark place** (its network device off) is
  dropped when it reaches the head of its queue, and counted as stranded
  [2026-11-16, `build-plan-gate3.md` 2; agreed with Damian]. That's 11.6's
  lifetime of 0, until gate 5 brings rerouting and lifetimes. Routes are
  recomputed at the barrier after a network device goes off or comes on
  (11.3), so later messages go around it. From gate 5 a dark next hop is
  treated as a failed link (11.6).
- **Each comms port has a fixed inbox,** held by the OS.
  - When it's full, the newcomer is dropped, silently. What's waiting
    stays.
  - SENSE reads the bytes waiting.
  - **Inboxes are lost at a cold start,** with port 0's queue, and kept
    whole until then [2026-11-16, `build-plan-gate3.md` 2; agreed with
    Damian. Before, they faded in a brownout]. Inboxes aren't in the
    address space, so fade has no cells to reach. And any dip below the
    reset region's memory's threshold ends in a cold start when power comes
    back (10.5), so inboxes that could have faded are emptied then anyway.
- **Port 0 has a bounded queue too** (10.4).

### 11.6 Outages and rerouting [gate 5]

- **A link fails in three ways,** and is repaired at the next visit to either
  of its places (7.8):
  - **wear** from the current it carries (7.4), for links that carry
    current. Real grids fail this way: in 2003, overloaded lines sagged into
    trees and tripped, which pushed load onto others;
  - **cuts from outside,** at a seeded chance per tick that grows with
    length, for any link. Real anchors: most fibre cuts on land come from
    digging, and roughly 150–200 undersea cable faults a year come mostly
    from fishing and anchors;
  - **network-outage patches** (12.3).
- **A failed link carries nothing.** A link carrying both power and data
  fails as one thing.
- **Messages reroute.**
  - A message crossing a failed link was still held at the near end (store
    and forward, 11.3), so it takes the new route.
  - Messages waiting for a failed link move to the new route's queue, ahead
    of new arrivals, as room frees. Until then they stay in the failed
    link's queue, which bounds their storage.
  - When no route remains, a message waits in that queue up to a lifetime
    (a setting; 0 drops it at once, as IP does).
  - **Dark places too, and when the lifetime starts [2026-11-20,
    `build-plan-gate5.md` 2; agreed with Damian].** A dark next hop (its
    network device off, every night somewhere) is treated as a failed
    link: the message moves to a new route's queue if there is one, and
    otherwise waits, up to the lifetime, counted from when it found no
    route. A lifetime of 0 gives gate 3's drops exactly (11.5). So
    outages become slowdowns for nights as well as cuts. Chosen over
    rerouting around failed links only and keeping gate 3's drop for
    dark places.
  - **The order [details at build; 2026-11-20]:** at the barrier, a
    place's waiting messages join their new queues in link order, then
    in their order in the old queue, ahead of new arrivals, as room frees
    (11.3's fixed order where things meet).
  - **Rerouting in detail [details at build, step 5; 2026-11-20,
    `build-plan-gate5.md` 11]:**
    - **A lifetime of 0 reroutes nothing.** Routes are worked out once
      a tick, at the barrier's end (11.3), so a message found blocked
      at one barrier can take a new route at the next at the soonest. A
      lifetime of 0 leaves it no next barrier: it is dropped where gate
      3 dropped it (11.5), even if another route exists. This is the
      reading under which a lifetime of 0 gives gate 3's drops exactly,
      and the equivalence test with gate 4's world holds to the byte
      with it. With a lifetime of 1 or more a blocked message waits and
      reroutes. Kept by Damian on 2026-11-20: "Yes, keep it".
    - **"Ahead of new arrivals"** is ahead of the barrier's arrivals. A
      place's own SENDs of that tick are already in the queue: the OS
      kept their room during the tick.
    - **A message that reaches a place with no route on** (the way
      changed while it crossed) waits there, in the room of the link
      end it came in by, and is dropped if that end has no room. Before
      gate 5 it was stranded at once.
    - **The lifetime counts time with no route,** from the barrier the
      message found none. A message that has a route and waits only for
      room has no limit, and its count starts again if the route goes.
    - **Messages that come back from a failed link** can put that link
      end over its queue's size, by what the link held; nothing new
      joins it until it has room. So the bound on a queue's storage is
      its size and what its link held.
- **Where data is still lost:**
  - newcomers at full queues and inboxes;
  - stranded messages whose lifetime runs out;
  - everything queued at a place whose network device goes dark (7.6).

  So outages become slowdowns, and partitions become waits.
- **Cuts in detail [details at build; 2026-11-20, `build-plan-gate5.md`
  2; agreed with Damian]:** a link's cut chance a tick is the rate × its
  length ÷ the average link length, times any outage patch over it, drawn
  as gaps from the seed and the link, as the error countdowns are, so
  nothing is drawn per link per tick. Its repair at a visit is a new link
  with no wear.
- **Failed links in detail [details at build, step 5; 2026-11-20,
  `build-plan-gate5.md` 11]:**
  - **A tick's order:** a visit due this tick mends its place's failed
    links, then the cuts due land, both before the solve (appendix C,
    1). Links at their endurance fail after the solve, with the
    devices' failures (C, 4; 7.6); the rest then take the tick's wear.
    So a cut that lands in a visit's tick waits for the next visit.
  - **The cut schedule runs whatever the link's state.** A cut that
    finds its link already failed does nothing, and is counted. So the
    schedule is the same whatever the programs do (principle 3).
  - **What a failed link leaves:** a place cut off from every source
    sits at 0 V. No heat passes between its two places (7.6). A
    neighbour SENSE over it reads 0 in every reading but the id, as
    over a dark device.
- **Straits that part and rejoin [gate 5; 2026-11-20,
  `build-plan-gate5.md` 2; agreed with Damian; one of the four mechanics
  he chose that day].** A strait is all the bridges between one pair of
  neighbouring landmasses (6.1).
  - At setup each strait is drawn to part or not, with a set share
    (half). One that parts is joined and parted by turns, each spell
    drawn evenly on a log scale between two settings (2 to 20 million
    ticks), from the seed and the strait, fixed before the run as the
    sources' schedules are: a boundary condition (principle 3). It starts
    joined, so the origin and the first spread happen as before.
  - While parted, its bridges carry nothing, power or data, and visits
    don't mend them. A parted bridge is a failed link for routing:
    messages reroute if another strait still joins the two sides, and
    otherwise wait out the lifetime and are lost. Messages on its wire
    when it parts are lost, and counted. It carries no current either
    (12.6).
  - So populations evolve apart, then meet. Observers report at each
    rejoining the leading groups on each side (14.1), and over the next
    era the colonies founded across it and the first leads either side.
  - A share of 0 is off. `map.unbridged` stays as it was, for a landmass
    apart for the whole run.
  - Real anchors: Beringia, joined and parted by the sea through the ice
    ages; the Isthmus of Panama, whose joining mixed two continents' life
    (the Great American Interchange).
  - Chosen over `map.unbridged` alone (life apart, never meeting), and
    over slow-mending cuts per bridge, which seldom part a strait whole.
- **The cost:** about one shortest-path search per place for each topology
  change, plus a change in the graph under the voltage solve.

### 11.7 Settings and risks

**Settings:**

- bandwidth: range and smoothness;
- queue size;
- inbox size and message size limit;
- port 0's queue size;
- message lifetime;
- the number of ports;
- per-hop and per-length delay, and the corruption rate;
- link wear and cut rate (12);
- the network device's numbers, in the catalogue.

**Risks:**

- **Computing cost.** Queues scale with traffic, so a flood costs computing
  time. A per-place cap on messages in flight may be needed as an
  engineering limit.
- **Base load.** The network device's constant draw is a floor at every
  place, so keep it small.

## 12. Environment and disasters

Disasters come from the laws, and from boundary conditions fixed before the
run. Nothing is scheduled by a disaster rule. Sag, cut-outs and the day come
at gates 2 and 4. Strikes, regional events and ramps come at gate 5 (2).

### 12.1 Where Nova 1's disasters went

Nova 1 had five scheduled kinds: surges, crashes, burnouts, disk failures
and storms. It had scripted warnings and sounds, and 64 settings. Its
brownout was the surge's warning, not a kind of its own.

| Nova 1 | Nova 2 |
|---|---|
| Brownout (a surge's warning) | Sag under load (4.1), and sources dipping or dropping out (4.3) |
| OS crash and reboot | A deep outage, then a cold start from the boot block (10.5) |
| Burnout and repair | Wear, failure and maintenance visits (7.4, 7.8) |
| Disk failure | Disk wear from writes (7.4) |
| Surge | Load rejection and strikes (12.2), and swells (4.3) |
| Lightning storm | A moving patch where strikes appear (12.3) |
| Warnings and false alarms | Ramps, and what programs already sense (12.4) |
| Sounds | Dropped, along with signals |

### 12.2 Surges (load rejection with gate 4; strikes at gate 5)

- **Load rejection needs no rule; it emerges.** When many devices cut out at
  once, the draw falls, the sag eases, and the voltage rises at every place
  sharing the source.
  - Devices near their maximum are pushed past it. They heat, cut out, and
    raise the voltage again.
  - The result is a cascade: stress built up and released suddenly.
  - Real anchors: a power plant that loses its load sees its voltage jump,
    and the 2003 North American blackout spread as lines and generators
    tripped in turn.
  - Stiff sources rarely cascade; soft ones let crowds move in, but cascade.
- **A strike is a source type** (4.2): a very high voltage, a one-tick life,
  and a moderate internal resistance, because it carries limited charge.
  - It enters the voltage solve like any source. Its spike falls off along
    the links and spreads further through dense towns, as real surges travel
    down lines.
  - Its own loss heats the struck place, and over water the shore drains
    it.
  - Its effects are the decided ones: errors (memory loses bits), heat and
    wear. A big enough spike makes devices fail outright.
  - **In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
    agreed with Damian]:** a storm gives each place inside it a chance a
    tick of a strike, scaled by the storm's strength there. A strike joins
    that tick's solve at the place, as a source of the strike type, and
    is counted per sample, not logged one by one.

### 12.3 Regional events [gate 5]

**What a patch is.** A seeded region of the map:

- it forms at a seeded time and place;
- it has a radius in links;
- it drifts along a heading that turns a little, or stays put;
- it lives for a drawn time;
- it ramps up and down (12.4).

While it's active, it scales one quantity inside it. Everything about it is
fixed before the run, as the day is.

**Patch types are rows in an environment catalogue:**

| Type | What it scales | Real anchor |
|---|---|---|
| Storm | The chance that strikes appear (0 outside storms) | Thunderstorms, and lightning surges on lines |
| Swell | Sources' voltage, upward: the area runs hot | Regional overvoltage, as on lightly loaded long lines (the Ferranti effect) |
| Brownout | Sources' capacity, downward, as night does | Rolling blackouts (California 2000–01; South Africa since 2007) |
| Heat wave | The outside temperature, upward, so cooling weakens | London's July 2022 heat wave took Google's and Oracle's data centres there down |
| Network outage | The chance that links are cut | Hurricanes cutting a region's lines (Puerto Rico, 2017) |

- **Individual events are the same things at one place:** a source's own
  dips and swells, a single strike, a single cut. A patch is many of them at
  once, correlated in space and time.
- **Storms affect power only.** They don't raise data corruption, which
  follows route length (11.3) at its own setting.
- **Gusts** are a sixth type from gate 5 [2026-11-20]: they multiply the
  replication current's strength inside them (12.6).
- **In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
  agreed with Damian]:** a patch's strength is full at its centre and
  falls in a straight line to nothing at its radius. A source is inside
  by its landing point, a place by its position, a link by its middle. A
  patch moves at a drawn speed in links a day, its heading turning by a
  drawn amount each day, and a drawn share stay put. Patches that overlap
  multiply what they scale. A heat wave raises the outside temperature
  its places cool toward; leakage and wear stay counted from the world's
  outside temperature, as gate 4 counts leakage.
- **Settings, about 18:**
  - movement, shared by all types: speed, turn and lifetime;
  - per type: how often, how big and how strong.

### 12.4 Symptoms (ramps at gate 5; the low-voltage interrupt at gate 4)

**What programs can sense:**

- their own place's voltage, temperature and time;
- each device's speed;
- their neighbours' voltage, temperature, idle cores and free memory;
- PING's round-trip time.

A trend is something a program has to notice itself, by keeping earlier
readings.

A device's own temperature isn't a reading [2026-11-19,
`build-plan-gate4.md` 2; agreed with Damian]. Heat trips follow it (5),
but a new reading would renumber SENSE's (A.4) for every evolved program.
The place's temperature is the warning.

**Every outside event ramps up.** Source drop-outs, swells, patches and
storms all rise from nothing to their peak over a lead time drawn per
event, then hold, then ease.

- **The ramp is the event itself, starting gently.** It's not a separate
  scripted symptom. The first part of a drop-out or brownout is a sag, and
  the first part of a swell or heat wave is a rise in temperature.
  Neighbours on the side an event comes from feel it first.
- **The lead must stand out from a source's normal swings,** or nothing
  could tell it from noise.
- **Some ramps are false alarms:** a drawn share peak low and fade. So
  reacting to every dip has a cost. Nova 1 used 25%.
- **Settings:** the lead time's shortest and longest, and the false-alarm
  share.
- **In detail [details at build; 2026-11-20, `build-plan-gate5.md` 2;
  agreed with Damian]:** an event's strength rises in a straight line
  over its lead, holds, and eases over as long again. A false alarm rises
  over its lead to a drawn fraction of its strength, then eases; the
  event never comes. Drop-outs, swells and patches ramp. A strike and a
  cut don't.
- **Real anchors:**
  - in Tokyo's voltage collapse of July 1987, on a hot day, voltages sank
    for about twenty minutes first;
  - storms are seen coming;
  - brownouts precede blackouts;
  - a tree on a line gives no warning, so the shortest lead can be short.

**Warnings that come from the laws themselves:**

- heat rises before a burnout;
- voltage sags before a cut-out;
- a worn processor gets slower;
- a storm's strikes hit the neighbours first;
- pings slow as routes detour;
- a cascade shows voltage rising and neighbours tripping, briefly.

A single cut link gives no warning, as real digging gives none.

**The low-voltage interrupt** (9.5) fires once for a thread, as it starts a
quantum on a core whose voltage is near its cut-off. The handler can SLEEP,
so the thread survives the cut (7.6).

- Real anchors: the PDP-11's power-fail trap, ACPI's low-battery event, and
  a UPS signalling "on battery".
- **Trade-off:** polling costs a SENSE every check and can miss a fast drop.
  The interrupt costs nothing until it fires, but fires late. Reading trends
  sees further ahead.

### 12.5 Settings and risks

**Settings, about 33 for this section alone, against Nova 1's 64 (the whole design's count is in appendix D):**

- wear: 2 slopes, plus catalogue endurance;
- visits: 4;
- respawning: 1;
- strikes: a source row;
- swells: 2;
- regional events: about 18;
- links: 2;
- error floors in the catalogue;
- the wear-levelling toggle;
- symptoms: 3, plus the interrupt's margin;
- gate 5's four chosen mechanics, about 15 more: flaky parts 3 (7.5),
  straits 3 (11.6), the killer and the watchdog 2 (9.1), the current
  about 7 (12.6).

**Risks:**

- **Cascades may happen too often, or never.** That depends on source
  stiffness, device ranges and margins.
- **Long-crowded places carry worn, slow, error-prone devices** until those
  fail and are replaced. That's intended succession, but long-lived lineages
  may pay for their own success.

### 12.6 Replication currents [gate 5]

[2026-11-20, `build-plan-gate5.md` 2, gap 15; agreed with Damian; one of
the four mechanics he chose that day, in place of migration out of full
places: "agreed on the weakness of the current and yes this should
replace migration."]

- **What it is:** background copying that no program asks for, as real
  networks have (backups, mirror syncs, cache fills, follow-the-sun
  replication). It carries whatever lies in its path, living code, dead
  code and junk alike, as wind carries pollen and the sea plankton. It
  never reacts to crowding.
- **It keeps 9.1 and principle 12.** It copies cells by address, never by
  owner, so it acts for or against no program, and its effect can be
  stated in the world's terms alone. Its schedule is drawn before the run
  and never reacts to the world: a boundary condition, as the supply's
  schedule is (principle 3). What it carries depends on what memory
  holds, as what a visit does depends on the hardware. Migration would
  have moved programs unasked, against 9.1.
- **What is copied:** each place now and then copies a block of a set
  size from a random address in its memory, held, free or the reset
  region alike, drawn from the seed, the place and the tick. The original
  stays put.
- **Where it goes:** along the place's data links that point downstream,
  each link's share in proportion to how closely it points along the
  current's heading; a link across or upstream carries none, and a place
  with none downstream sends nothing. Bridges carry it as any data link
  does, so a rare block crosses a strait: rafting. A parted strait
  carries none (11.6).
- **On the wire:** a block is a message of its size. It takes the link's
  bandwidth and queue room as programs' SENDs do, so it competes with
  them. It never waits: a full queue or a dark next hop drops it,
  counted. The lifetime (11.6) is for programs' messages.
- **Where it lands:** in the neighbour's reset region, at a spot drawn
  from the seed, as raw bytes, cut short at the region's end. Not at port
  0: no queue and no prompt run. It counts as a write to the region, so
  it can wake a halted core, as a cosmic-ray flip does, and idle runs meet
  it as their start spots step through, as they meet static (10.2). An
  idle run may touch only the region and what it allocated, so a block
  landed anywhere else could never run. Gate 3 showed bytes landed in a
  region come alive this way: static runs in arrivals' landed bytes made
  seven times the programs the arrivals' own prompt runs did.
- **Its direction and strength:** one heading and one strength for the
  world, eased each season to new draws, as the sources' capacities ease
  (4.3): the heading turning up to an eighth of a turn, the strength
  between half and twice its mean, so a heading lasts about an era and
  turns many times a run. Gusts are a patch type (12.3).
- **With the day:** at each place the strength follows its day, as the
  supply does, strongest at its noon. The strongest band sweeps across
  the map with the sun, and the current mostly leaves places whose
  network devices are lit.
- **Its record (observers only):** a static run that starts in bytes the
  current landed, still as they landed, is counted as runs in arrivals'
  landed bytes are (13.1). A program it makes joins the lineage of the
  program that held those bytes at the source, if one did, marked as
  carried by the current. The spread measure (14.1) doesn't count
  colonies founded that way; they are counted apart. The OS's cause stays
  "static".
- **What it should do:** downstream places get gene flow, upstream places
  stay isolated, and a turn of the current exposes new places to it.
- **Weak by default:** blocks of 256 bytes, about one a place a day at
  full strength, seasons of 500,000 ticks. A strength of 0 is off.
- **Risks:**
  - strong, it is gate 3's flood of travellers again (10.4): bytes landing
    faster than static can make anything of them;
  - it lands where the origin happens, and before life it carries other
    places' static as much as life;
  - like any free carriage it makes SEND worth a little less: it carries
    programs that never paid to send, though it carries junk as readily.
- **Real anchors:** the background replication above; and rafting: in
  1995 green iguanas rode storm debris from Guadeloupe to Anguilla and
  settled there.
- Chosen over landing in free memory outside the region, where nothing
  could run it, and over landing at port 0, a prompt run for every block,
  which is a SEND with no sender.

## 13. Observation

The reused viewer and stats, from Nova 1. They had a map with 22 colourings,
a memory strip, charts, a phylogeny, a genome browser, a "map of life", a
novelty measure, and milestone events. Views are built at the gate that
creates what they show (2).

### 13.1 Facts and labels

- **Watching changes nothing.** The world's hash is the same with or without
  observers, and a test checks it.
- **The engine records only facts:** what a law did, with a tick and a
  place. Labels such as parasite, death, disaster and species are observers'
  patterns. If a law acted, it's a fact; if it's a pattern, it's a label.
- **Facts logged one by one:**
  - a device failed or was replaced;
  - a link failed, was cut, or came back;
  - a place respawned, with its new incarnation number;
  - a source appeared or retired;
  - a patch formed or ended;
  - a cold start booted the boot block;
  - **a program was created** (at SPAWN, R5), with:
    - its parent;
    - its cause (a thread's SPAWN, or an idle run from static, an arrival,
      or a boot);
    - the extent its parent wrote and its hash (13.2);
    - the hash of its birth image.

    An arrival's record names its sender, and a boot's names the boot
    block's last writer;
  - **a program ended,** with the cause: exit, a fault, a crash at a cut-out,
    a failed memory device, memory that lost its contents, or a cold start
    (10.5). From gate 5 [2026-11-20, `build-plan-gate5.md` 2]: "failed
    device" for a failure of any kind (7.6), "out of memory" for the
    killer, and "watchdog" for its fault with no handler (9.1). A place's
    records carry its incarnation (7.8).
    - **Read from the log of respawns, not held in each record
      [2026-11-21, `build-plan-gate5.md` 11, step 6; Damian: "Yes, log
      only"].** No record holds the incarnation, so no record changes
      size. Each respawn is logged as a fact, above, with its tick, its
      place, its new incarnation and its new point, and a record's
      incarnation is the count of its place's respawns up to its tick.
      The world keeps and saves the incarnation itself. Chosen over a
      field in every record.
    - A teardown's deaths have the cause "failed device", counted
      apart, and no cause of their own [details at build, step 6;
      2026-11-21].
- **Frequent facts are counted per sample:**
  - threads started;
  - threads crashed at cut-outs;
  - messages sent, dropped and stranded;
  - SENDs from idle runs (R4): the calls they make, which fail unless idle
    runs may SEND (9.2);
  - from the combined run of gates 4 and 5 [Damian, 2026-11-20: "Yes,
    add it"]: the calls programs' threads make, by kind, all 16; SENDs
    to a port other than 0; and the calls that succeed, by kind, if that
    is cheap. World totals in each written sample, reported and not
    judged (`build-plan-gate5.md` 1).
- **Observers also read sampled, read-only views of the state,** because some
  labels are states, not events. The sample interval is a setting, 100
  ticks in every gate's runs. From the combined run of gates 4 and 5,
  every second sample is written to disk, a row each 200 ticks [Damian,
  2026-11-20: "we can probably get away with halving the sampling to
  every 200 ticks", then "Agreed" to sampling at 100 inside and writing
  every second]. The stage 4 and settling clocks (10.6, 13.3) are
  checked at each sample and start again at one failed check, so the
  sampling itself stays at 100 and no measure changes
  (`build-plan-gate5.md` 7). The views include:
  - each thread's IP and the code around it;
  - each reset region's bytes, with their compressibility (R4);
  - page ownership.
- **Observer memory is saved beside each snapshot,** outside the hash, so the
  family tree survives a resume. [Built 2026-11-18, `build-plan-viewer.md`
  step 9: a seed of the origin test saves a second file beside its world's,
  with the observers' memory, the counters kept for reports and the test's
  own record, and `origin.resume` carries the test on from it.]
- **What can be re-run later, and what can't.** A label can be redefined
  and re-run over an archive only if its evidence was kept. Each detector
  lists its evidence (13.3), so anything costly to keep shows up before
  it's built.
- **Volume and retention:**
  - about 32 bytes per birth and per death, in a compressed, append-only
    log;
  - at 10,000 births and 10,000 deaths a second, that's about 55 GB a day
    before compression.
  - **Retention [starting point]:** full facts for a rolling window (a
    setting), then births and deaths only for lineages that are still
    alive or were abundant. The genebank keeps each distinct genome once.
  - **Records written as they finish [2026-11-18,
    `build-plan-observe.md`; Damian's direction, the 30 seconds his;
    built for samples, voltages, windows, kept genomes and the lineages
    let go].** Gate 3's run held about 6 GB of observers' memory per
    seed, all history, none let go. On Damian's direction, records that
    are finished go to files per seed as the run goes: samples and
    voltages, turnover windows, kept genomes (13.2), and lineages that
    are extinct and can't come back. They are buffered and written every
    30 seconds and at each save. Memory keeps only what the observers
    still act on, and a save holds that plus how far each file had got.
- **Why:**
  - it's principle 2's test;
  - labels can be redefined without rerunning the world;
  - an observer bug can't corrupt the world;
  - disasters can only be found by watching.

  Real anchors: monitoring and security tools apply detection rules to raw
  logs, and Avida's analysis mode works out what saved genomes do after a
  run.

### 13.2 Lineages and genomes

- **A program's genome is the extent its parent wrote** into the allocation:
  the bytes CPY (or stores) put there before SPAWN. Its id is a hash of
  those bytes.
  - The whole allocation at birth, leftovers included, is kept as the birth
    image, a fact.
  - Using the written extent stops leftover bytes making every birth look
    new. An 18-byte program in a 1,024-byte page would otherwise be 1,006
    bytes of leftovers.
  - Programs born from a boot block or an arrival take the bytes their run
    copied.
- **The genebank** saves each new genome once, with when, where and its
  parent.
  - **Only genomes whose lines last [2026-11-18, `build-plan-observe.md`;
    agreed with Damian; built].** On Damian's direction ("we only really need to
    save programs that last at least 3 generations"): a genome is kept
    once a program carrying it has a grandchild, whatever genomes the
    child and grandchild have (`observe.keep_generations`; 1 keeps every
    genome, as before). Each kept genome records the genome it came from
    and its generation, so a line that lasts is kept whole, mutations
    included, as a tree of genomes (Damian: "a record of each generation
    [...] so we can look at how it mutated and branched out over time").
    A genome is forgotten once it can no longer be kept: no carrier, and
    no carrier's child, alive. Only kept genomes join or found groups,
    so stage 5's new groups are new kinds whose lines lasted. Nothing is
    held for 13.3's scavenger label: a birth into leftover code of a
    genome no longer held counts as a birth into unknown leftovers
    (Damian: "I don't think we need to keep this memory of genome
    corpses around"). Gate 3's seed 8 made 15.7 million genomes from
    117.8 million births; most were born once.
    - **The rule stands for the combined run of gates 4 and 5
      [2026-11-20, Damian].** He said: "Remembering extinct lineages is
      kind of the point, but I would want it limited to genomes that
      actually had at least 3 generations of descendants. There's no
      point is saving what is basically just random noise." Asked
      whether that meant one generation deeper than what is built, he
      said "Keep existing rule": a grandchild, `observe.keep_generations`
      = 3, with `observe.forget` off in the gates' runs.
    - **Kept genomes are stored as differences from their parent
      [2026-11-20, Damian; built with the storage changes],** with a
      whole copy at least every 16 steps along a line ("I like the idea
      of storing a key frame every 16 changes, I think it is worthwhile
      to keep in"). It changes the files, not what is kept.
- **Code after birth is tracked by sampling.** Each sample hashes the code
  around each thread's IP. That shows overwrites, colonies running many
  threads, and code that changes after birth.
- **Lineage is tracked by descent,** from the facts:
  - a SPAWN gives the spawner;
  - an arrival gives its sender. There's no parent if the sender was an
    idle run (9.2);
  - a boot gives the boot block's last writer;
  - static gives no parent: a new origin. But a static run that starts in
    the bytes an arrival last landed in the reset region, while all of
    them are as they landed, gives that arrival's sender, as the
    arrival's own run does. A later landing over any of them, or any
    other change to them, ends that [gate 3; 2026-11-17,
    `build-plan-gate3.md` step 7; agreed with Damian].

  No similarity matching is used in the tree. Corruption in transit doesn't
  break a link, because the sender is known.
- **Where the maker and the code differ,** descent still records the maker:
  scavenging, hybrids from the reset region, and parasite code carried
  inside a host. Detectors label these (13.3).
- **Genomes are grouped by similarity** into species-like groups, alongside
  the tree.
  - The groups give population counts, diversity, territories, and colours
    on the map of life.
  - A child in a different group from its parent flags a big jump.
  - **Method [starting point]:** greedy clustering. Each group's first
    genome is its reference, and closeness is the share of 3-byte runs in
    common, with the threshold as a setting.
  - Real anchors: about 97% similarity for microbiology's working units, and
    about 95% across whole genomes for a species. CD-HIT and UCLUST cluster
    greedily.
- **Genomes are real code,** so the browser shows them disassembled in the 22
  instructions.

- **A run with no end forgets the extinct** [2026-11-19; built overnight on
  Damian's approval, and confirmed by him]. With `observe.forget` on, a kept
  genome that no living or remembered program holds leaves memory, and a
  group with its last genome; both stay in the genebank's file. A gate's
  run leaves it off, so its counts are of everything ever kept. The rules
  and what changes in the counts are in `build-plan-observe.md` 3.5.

### 13.3 Detectors

Each detector says what it finds, its evidence, and what kind of claim it
makes:

- an **observation**: a fact read directly;
- a **heuristic**: a pattern that usually means the label;
- a **causal claim**: that a behaviour helped, which needs a comparison.

**Cross-owner execution (decided).**

- **What it finds:** a thread running code in pages where its program holds
  no allocation, found in the sampled views.
- **What it means:** a thread jumping into a neighbour's code in a shared
  page, or running a corpse's leftovers.
- **What it misses:** 9.4's in-place hijack, where an attacker overwrites a
  victim's code where it sits. There the victim's thread runs in the
  victim's own pages, so the owners match.

**The rest are a starting set,** redefined freely:

| Label | Found when | Evidence | Kind |
|---|---|---|---|
| Hijack | A thread's sampled code changed since its last sample, and the new bytes match another lineage's genome | IP code hashes per sample, plus the genebank | Heuristic |
| Replicator | A program has a descendant whose genome matches its own | Birth facts | Observation |
| Colonist | The first program of a lineage at a place, born from an arrival | Birth facts | Observation |
| Scavenger | A program's birth image or running code matches a dead program's genome, in memory it allocated | Birth images, IP samples, genebank | Heuristic |
| Booter | A program born from a boot block | Birth facts | Observation |
| Traveller | Code from idle runs that SENDs itself on without becoming a program (10.4), where idle runs may SEND (9.2) | Idle-run SEND counts, region samples | Heuristic |
| Sleeper | A program that sleeps, or uses the low-voltage interrupt, before cut-outs, and survives | Call counts per lineage, death facts | Heuristic |
| Communicator | A program whose behaviour follows data it RECVs from another lineage | Needs RECV traces, not yet kept: costly | Causal claim |
| Partnership | Two lineages that regularly send to each other's ports | Per-lineage message counts | Heuristic |
| Disk user, pinger | It calls DWRITE or PING, and outlives similar programs that don't | Call counts, death facts | Causal claim |
| Cascade | Cut-outs that spread from place to place within a few ticks | Cut-out counts per place per sample | Heuristic |
| Blackout, outage | A region dark, or cut off, for a time | Per-place samples | Observation |

**Deaths are counted by cause,** from the facts.

**As built [starting point, step 10; 2026-11-11].** All of these are
observers' choices, so they can change without touching the world.

- **Lineages:** every birth with no parent founds a lineage: one from
  static, or by hand in a test. So each copy an idle run makes from the
  region starts a lineage of its own.
- **Faithful births:** a birth is faithful when its genome is its parent's.
  - **The genome must be at least 8 bytes** (`observe.min_genome`)
    [2026-11-12, after the lever trials; Claude's change, confirmed by
    Damian]. On a lax place, a gift of a few bytes inside the parent's page
    gives the child the whole page, so it runs the parent's code in place.
    A byte or two written there matches the parent's genome, yet nothing
    was copied.
  - A lineage's first faithful birth is the origin test's stage 1.
  - Its first faithful birth from a parent that was itself born faithfully
    is stage 2.
- **Hand-overs** [2026-11-12, agreed with Damian]: a birth into the
  allocation its parent was born in. The parent gave its own body away and
  copied nothing, though the written extent the allocation carries matches
  the parent's genome. A hand-over is never faithful or a copy, and is
  counted on its own, off the copying ladder. R15 (9.4) now stops a program
  handing over code it runs on. A program running in another allocation of
  its own can still give its first one away, and that is a hand-over too.
- **Copies:** a birth is a copy when it is faithful, or a near copy: its
  parent wrote 8 bytes or more into it, and its genome and the parent's
  have at least half their 3-byte runs in common (out of the larger set,
  as the groups measure closeness). Each
  lineage counts its living members born as copies, for stage 4 (10.6).
- **Scavenger:** a birth with nothing written, whose whole allocation hashes
  to a known genome. Birth images are hashed in the same way as genomes, so
  the two compare.
- **Hijack:** sampled. A thread's code allocation, over the extent its
  parent wrote, now hashes to another lineage's genome rather than its own.
- **Genome groups:** two genomes' closeness is the share of their distinct
  3-byte runs in common, out of the larger set. A genome joins a group at
  0.8 or more, so one changed byte in an 18-byte genome (13 runs of 16 kept)
  stays in its group. Genomes under 3 bytes group by equality.
- **"Crowded"** (stage 5) means the place's free memory is below a tenth.
- **Distinct paths:** each idle run's executed bytes are hashed, and counted
  once in a table of 65,536 that starts afresh when three quarters full.
- **At gate 1** the facts are kept in memory. Saving the observers' memory
  beside a snapshot (13.1) waited until a run needed it, and was built on
  2026-11-18 (`build-plan-viewer.md` step 9).
- **Across places [gate 3; 2026-11-16, `build-plan-gate3.md` 2; agreed with
  Damian]:**
  - An arrival carries, for the observers only, the program that sent it,
    as a boot carries its block's last writer. A program made by an
    arrival's prompt run has the cause "arrival" and that sender as its
    parent, so its lineage carries on at the new place. So does a program
    made by a static run in the arrival's landed bytes (13.2): it's born
    from the arrival too [2026-11-17]. A lineage's first member at a
    place is a colonist.
  - **A lineage settles a place it arrived at** when it holds it as stage 4
    holds (10.6), with the same numbers, and its first member there was a
    colonist. Copies count only if they were born at that place: members
    that arrived don't.
  - **Life spreads** when a lineage settles a place beyond the one it began
    in (14.1). A colonist that dies at once has spread nothing.
  - Also reported, not judged: colonists by place and lineage; arrivals
    that ran, made a program, or did neither; travellers; and how far each
    settlement is from where its lineage began, in hops.
  - **In detail [details at build, step 4; 2026-11-16]:**
    - A program that SENT to a port 0 keeps its record after it dies,
      until nothing it sent is left on the network, waiting in a port 0
      queue, or running as a prompt run (checked at each sample). So its
      colonist still finds its parent.
    - A prompt run of an arrival is counted when it ends: it made a program
      (a SPAWN), else it sent (a traveller), else neither.
    - An idle run's SEND (where idle runs may SEND, 9.2) carries no
      sender. So what its arrival makes at the far end founds a lineage of
      its own.

### 13.4 The viewer

The viewer is drawn with Direct2D and DirectWrite (2). Its views follow the
gates.

- **The map (gate 3 on):**
  - places at their positions, with water and bridges;
  - links drawn by what they carry;
  - overlays for sources by type, moving patches, and grid borders;
  - colourings: Nova 1's population, births, lineage territories and biome
    fields, plus:
    - voltage, temperature and draw;
    - wear and failed devices;
    - network device down;
    - queue fill and drops;
    - power and data flow;
    - cooling;
    - age since the last visit.
- **A place inspector (gate 1 on):**
  - each device with its numbers (speed, threshold, wear, temperature, on
    or off);
  - threads on cores;
  - the reset region and its measures;
  - queues and inboxes;
  - the OS values.
- **Strips for each memory device (gate 1 on):**
  - pages by owner, lineage, permission, size class and errors, with the
    reset region marked;
  - a wear strip for each disk.
- **Charts:**
  - populations of programs and threads;
  - births and deaths by cause;
  - distinct paths tried (10.6);
  - power and heat;
  - wear, failures and visits;
  - traffic, drops and stranded messages;
  - labelled disasters;
  - diversity and novelty.
- **Genomes:** the phylogeny, the genome browser with disassembly, and the
  map of life.
- **An events timeline:** facts and labels together, filterable, and linked
  to the map.
- **As built at gate 1 [step 12; 2026-11-11]:**
  - **Places** as tiles, coloured by how many programs each holds (the map
    comes at gate 3).
  - **The place inspector.**
  - **Its memory page by page**, coloured by owner, lineage, permission or
    size class.
  - **Three charts:** programs and threads, births and deaths, distinct
    paths tried.
  - **The frame is new and small** (immediate-mode, as Nova 1's was), not a
    port of Nova 1's 5,000 lines. Nova 1's setup page, scrolling and zooming
    widgets, genome browser and phylogeny come when they're needed.
  - **A headless render** matches a stored image pixel for pixel. A world
    opened from a snapshot draws exactly as the one it was saved from.
- **As built at gate 3 [details at build, step 6; 2026-11-16]:**
  - **The map** takes the memory strips' place in the middle, the tiles
    staying on the left; a key (V) switches the two (Damian's choice). It
    draws the landscape's land and water, the links with bridges lighter,
    and the places as squares at their positions, the inspected place
    framed with its links marked; a click picks the nearest place.
  - **Four colourings (C):** population (dimmer as the voltage falls);
    lineage territories (each place's largest lineage, dimmer as its share
    falls); network device on or off; and queue fill and drops (each link by
    its fuller end's queue, of its room; each place by the messages dropped
    there since the world was built or opened, on a log scale: report-only
    counters, never saved; Damian's choice).
  - **The inspector** adds the place's queues, inboxes and drops.
  - **Charts:** with a map, the second row shows gate 3's: messages sent,
    delivered and PINGs answered; messages dropped (no room, stranded, lost,
    cut); and colonists, with the colonies alive and settled (by the origin
    test's criteria). A key (G) switches back to gate 2's power row
    (Damian's choice).
  - **With no map** the views are gate 2's, pixel for pixel.
  - The other colourings and overlays above come with the gates that make
    them.

### 13.5 Stats [starting point]

- **One row per sample:**
  - world totals: programs, threads, births and deaths by cause, power,
    heat, failures, visits, traffic and drops;
  - performance: instructions, bytes processed, simulated time, and births
    per second (2);
  - slower per-place samples, for the archive the viewer reads.
- **The intervals are settings.**
- **Cost:** facts are append-only, and detectors and charts run on samples.
  The IP sampling costs per thread per sample, so it misses brief events.

## 14. What's still open

### 14.1 What the gates must show

Each gate answers its question by runs, with a pass mark set before the
first run (2).

- **Gate 1: the origin test (10.6),** and with it the discoverability risk.
  - Built from primitives, eating and parasitism are multi-instruction
    programs, so random code finds them later, or within a run's budget not
    at all.
  - Tierra's parasites came from a seeded ancestor, not from random code.
    BFF and Core War-style systems get replicators from random code with
    free overwriting.
  - Claude knows of no result showing predation and parasitism emerging
    from random code in a world with several resources. Argument can't
    settle this; only a test can.
  - Also measured at gate 1:
    - whether faults are too harsh on the first, sloppy replicators (7.6);
    - whether the reset region keeps changing (10.2);
    - the cost of the per-access counters (2).
- **Gate 2: does a full place keep turning over? (R30)**
  - Measured as deaths per program per day, by cause and by place.
  - The checks:
    - whether the crash at a cut-out lets early life survive (7.6);
    - whether one program taking all memory freezes a place (9.4).
  - The lever in reserve is a visit that reboots a place (7.8).
  - **The first answer: a fail** [2026-11-15, `build-plan-gate2.md` 11
    step 9].
    - 33% of counted windows turned over, and 47% of crowded ones, against
      the 90% agreed in advance.
    - The origin regression reached stage 4 in 8 of 20 seeds, against the
      10 it needed.
    - Frozen places ended only at cold starts.
    - Next: the widened saved-state rule on trial (9.4), against the same
      pass mark (`design-review-gate2-2026-11-15.md`).
  - **The second answer: a pass** [2026-11-16, `build-plan-gate2.md` 11
    step 10; agreed with Damian]. The same 20 seeds, with the widened
    saved-state rule (9.4):
    - 586 of 586 counted windows turned over, and 448 of 448 crowded ones,
      from 13 seeds.
    - The origin regression reached stage 4 in 13 of 20 seeds.
    - The 9.4 test world's takers all ended by memory failing, within
      26,000 to 50,000 ticks, against 1.7 million before.
    - With the rule on, the measure can't fall short: every program alive
      at a window's start died within it. It stays as it is, since gate
      2's question was whether full places freeze.
- **Gate 3:**
  - does life spread? A lineage spreads when it settles a place it arrived
    at (13.3) [2026-11-16, `build-plan-gate3.md` 2; agreed with Damian].
    The pass mark is set in `build-plan-gate3.md` 7 before the first run;
  - whether CPU saturation shuts arrivals out completely (10.4);
  - whether blind SENDs pile onto a few places (11.1);
  - the cost of recomputing routes at nightfall (11.3);
  - whether the map itself works (6.1).
  - **The runs are at 48 places a seed,** at gate 2's budget of 10¹²
    instructions [2026-11-16, `build-plan-gate3.md` 2; agreed with Damian].
    - At gate 2's rate that budget buys a 1,000-place world under a million
      ticks, and stage 4 took 5 to 15 million at 48 places. So at full size
      nothing would arise to spread.
    - Test worlds with hand-placed migrants, at 1,000 places and 10,000 for
      the routing cost, test the map and the network at the size the
      design is for (R7: evidence about the mechanism, not about life).
    - Whether life spreads on a full-size map is the next question, not
      this gate's.
  - **The answer: a pass** [2026-11-19, `build-plan-gate3.md` 11 step 8;
    confirmed by Damian]. The 20 seeds at their second run
    (`runs/gate3-2`), with idle runs not SENDing and arrivals holding at
    most half a place's cores (10.4):
    - a lineage settled a place beyond the one it began in, in 17 of the
      20 seeds, against the 5 agreed in advance: 9,187 settlements of
      1.57 million colonies founded;
    - the origin regression reached stage 4 in 18 of 20 seeds, against
      the 10 it needed, and stage 5 in 17;
    - 57,803 of 57,808 counted windows turned over, and 50,251 of 50,255
      crowded ones, from 17 seeds;
    - strict places never passed stage 3: all the life at stages 4 and 5
      is in lax places.
- **Gate 4:**
  - whether crowds creep in on soft sources (4.2);
  - whether heat settles or oscillates (5).
  - **Does the physics shape it?** [2026-11-19, `build-plan-gate4.md` 2;
    agreed with Damian. The naming of ground and the direction changed
    2026-11-20, before any judged run, `build-plan-gate4.md` 7 and 11;
    Damian: "Either direction"] Each place, in each window of 200,000
    ticks, is named by what a full place gets there, with its cores kept
    busy and nothing else living: hot (over a quarter of its core time
    lost to heat trips), dim (over a quarter lost to cut-outs, or its
    reset region's memory off for over a quarter of the time) or fair.
    The name is worked from the seed and its sources, so a crowd can't
    change the name of its own ground. The physics shapes life if fair
    ground and hot ground differ by at least a factor of two in the
    programs they hold, whichever way, and fair and dim ground likewise;
    the direction is reported. The first reading is reported beside it,
    not judged: ground named by what happened to the place in the
    window, with fair ground holding clearly more than hot and than dim.
    The pass mark is set in `build-plan-gate4.md` 7 before the first
    run, with the regressions: the origin test's stages, gate 2's
    turnover and gate 3's spread.
  - The two checks above are reported with fixed definitions, not judged,
    since either outcome is an answer.
  - **The runs are at 48 places a seed** (2), with test worlds at 1,000
    places for the geometry of bands.
  - The regressions are also the first 20 seeds under 9.4's stack rule. A
    pilot of gate 3's world under that rule runs first, to tell its
    effect from the physics'.
  - **Judged in the combined run** [2026-11-20, Damian: one 20-seed run
    for gates 4 and 5 (2)]. Gate 4 has no 20-seed run of its own. Its
    mark is read from the combined run's ground record, where gate 5's
    wear, disasters and four mechanics act on the same ground, so it
    says how the physics shapes life in gate 5's world, not what gate
    4's physics does alone. It is judged on the whole combined run. The
    same measure on the place-windows gate 5 left alone (fresh ground,
    no patch over the place, no kill there) is reported beside it, and
    both go to Damian if the two disagree. A run of gate 4's world
    alone, every gate 5 strength off, is made only if the mark fails
    [Damian, 2026-11-20: "Agreed"].
  - In the combined run the judged naming is the paragraph above's, as
    changed on 2026-11-20, and the reading on the ground gate 5 left
    alone uses it too. Under gate 5 a place changes over a run, and
    its name is worked on the place as built [Damian, 2026-11-20:
    "Yes, as built"]: on the first map, each device as dealt and
    unworn, with only what is fixed before the run at that tick, the
    sources' schedules and the straits' partings (11.6). Wear,
    failures and patches are left out, since wear and failures come
    from load and a crowd would rename its own ground again. A
    respawned place's later windows are left out of the mark and
    counted (`build-plan-gate5.md` 5, step 15).
- **Gate 5:**
  - how often cascades happen (12.2);
  - whether reading input pays enough for injection to arise (9.8);
  - whether old, worn places punish long-lived lineages (12.5).
  - **Does it stay interesting?** [2026-11-20, `build-plan-gate5.md` 2;
    agreed with Damian] The gate judges whether new kinds keep taking
    ground late in the run:
    - a genome group (13.2) **leads** a place at a window's start when it
      has more of the place's programs than any other group, and at least
      stage 4's numbers of them (10.6): 20 or more, a tenth of the
      place's programs, half of them born as copies;
    - a **first lead** is a group leading a place for the first time in
      the run, at any place;
    - each seed's run from its stage 4 to its end is cut into **eras** of
      2 million ticks; its **late eras** are the whole eras in the second
      half of that span, and an era **has change** if a first lead comes
      in it. [Details at build, 2026-11-20, `build-plan-gate5.md` 11:
      the late eras are the later half of the seed's whole eras, the
      odd one counted early, so 7 eras are 4 early and 3 late; a
      stretch shorter than an era at the seed's end is no era; a first
      lead before stage 4 belongs to no era.]

    A world thinned to one program a place, as gate 3's seed 15 was, has
    no group of 20 anywhere, and a world where one group leads everywhere
    and nothing new grows has no first leads; both fail. Groups split by
    drift as well as by adaptation, so it measures change, not progress,
    and the report says so. The pass mark is set in `build-plan-gate5.md`
    7 before the first run, with the regressions: the origin test's
    stages, gate 2's turnover, gate 3's spread and gate 4's ground.
    Chosen over judging the regressions alone, which never tests the
    question; over beating gate 4's run by the same measure, which could
    fail a world that does stay interesting (it is reported, not
    judged); and over 12.5's succession, which is one mechanism.
  - **The three checks above are reported with fixed definitions,** not
    judged, since either outcome is an answer:
    - **cascades:** a place goes off in a tick when devices there cut
      out or trip, and passes it on to a proximity neighbour that goes
      off within a few ticks after, at the edge its voltage moved toward:
      rising before a trip, falling before a cut-out. Places that go off
      in the same tick pass nothing to each other. A cascade is a chain
      of two places or more, named by what began it (a strike, a swell or
      load), and reported per 1,000 place-days with its size and the
      stiffness of the source it began at;
    - **reading input and injection:** a lineage reads input if its
      RECVs that return bytes come to at least one a member a day; its
      share of led places and births is set against its share of
      programs, a correlation. An overrun is a RECV whose message runs
      past the end of its buffer's allocation, counted by what it reached
      (own code, kin, another lineage, a stack, free memory); an
      injection is a thread found running bytes an overrun wrote from
      another lineage's message, still as written;
    - **worn ground:** a place-window is worn if wear has slowed its
      cores by a tenth or more at its voltage, on average over the
      window, or a device there has failed and waits for its visit. Holds
      (stage 4's hold, 13.3) ending on worn and on fresh ground are
      reported by age. Long-lived lineages pay for their success if old
      holds sit on worn ground more than young ones, and holds there end
      sooner.
  - **Gate 4's ground measure** stays as a regression: a core's time lost
    to failure is neither a cut-out nor a heat trip, and a failed
    reset-region device counts toward dim.
  - **The runs are at 48 places a seed** (2): one 20-seed run for gates 4
    and 5 together, and a comparison run with the four chosen mechanics
    off (2).
- **Numbers set by runs:**
  - page and memory-device sizes (6.3);
  - service costs and the exact crossing (8.4, 9.7);
  - error floors and the cosmic-ray rate (7.5);
  - wear slopes and endurances (7.4);
  - the visit interval (7.8);
  - disaster rates (12);
  - device curves and ranges (7.2).
- **The engine:** the benchmarks that would decide on a GPU backend (2).

### 14.2 Decisions still open

- A trade-off curve for the network device (7.2).
- The GPU and NPU: a condition in which each wins, before either is unparked
  (R20). If unparked: EXIT for lanes, a launch cap and device sharing, and
  NMATMUL's calling convention (9.5).
- Large point sets in the viewer, which would need D3D11On12 (2).
- Which other parts of Nova 1 to reuse (2).

- **Places far larger than 4 MB** [2026-11-19]. The OS's and memory's
  tables are sized for the worst case when a place is built, 11 bytes for
  each of the place's own, and a tick's cost follows the threads alive
  (`build-plan-observe.md` 9, step 6). A 250 MB place needs tables that
  follow use and a tick that doesn't pay for waiting threads.

### 14.3 Settled on 2026-11-11

**The gaps found while writing the clean version, all decided by Damian:**

1. Ownerless code is limited to a few calls. Refined by R5: a program's
   record begins at SPAWN (9.2, 9.4).
2. The core-kind preference is SETCORE, a strict pin (9.5).
3. Rowhammer is specified. Refined by R9: it counts row activations per unit
   of time (9.8).
4. Threads within a place run in rotating quanta (9.5).
5. Turn length is one world setting (9.6).
6. "C with classes" is confirmed (2).

**The two external reviews (Astra and Fable).** All 30 of Claude's
recommendations were accepted (R1–R30, in
`design-review-triage-2026-11-11.md`). Seven reversed earlier decisions:

- port 0 can refuse (R3);
- a program's record begins at SPAWN (R5);
- the reset region stays put when its device fails (R6);
- hand-placed programs are allowed in tests (R7);
- the disk is raw and unowned (R14);
- DIV by zero returns 0 (R18);
- the GPU and NPU are parked (R20).

### 14.4 Where the history is

`design-history.md` holds the original working document, word for word:

- every option considered;
- the questions and answers;
- the rejected ideas;
- the reasons;
- the dated log.

Search it for any decision's background. New log entries are added at the
end of its log. The original text above them stays unchanged.

## Appendix A. The instruction and call contract

The exact behaviour of every instruction and call. Numbers marked
"placeholder" are settings or structural constants fixed before the first
run.

### A.1 Conventions

- **Numbers are 32-bit two's complement.** Arithmetic wraps on overflow.
- **Addresses are unsigned 32-bit.** Address 0 is never valid (6.3).
- **"x mod n" reads x as unsigned 32-bit,** so every value gives a
  remainder from 0 to n − 1. (So −1 mod 18 is 3.)
- **Byte reads are sign-extended,** so a byte read from memory is −128 to
  127, like #. Byte stores write the low 8 bits of A.
- **Reading IP gives the address of the current instruction.** Relative
  jumps are measured from it too.
- **An instruction is 2 bytes if its operand is #, otherwise 1.** After any
  instruction that doesn't jump, IP moves on by its length.

**The encoding.** A byte is the instruction code × 8 + the operand code.

- **Operand codes:**

  | Code | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | Operand | A | B | C | D | IP | [B] | [D] | # |

- **Instruction codes (placeholder order, a structural constant):**
  - 0–4: MOV, STO, SWP, PUSH, POP;
  - 5–8: ADD, SUB, MUL, DIV;
  - 9–12: AND, OR, XOR, NOT;
  - 13–14: SHL, SHR;
  - 15–19: JMP, JEZ, JNZ, JGZ, JLZ;
  - 20: CPY;
  - 21: SYS;
  - 22–31: spare.
- **The order matters** as the call numbering does (9.2): a one-bit flip
  turns an instruction into its neighbour.

### A.2 Instructions

**How operands behave:**

| Operand | Read as | Written by STO, POP or SWP |
|---|---|---|
| A, B, C, D | The register | The register |
| IP | The address of this instruction | A jump to the value |
| [B], [D] | The sign-extended byte at that address (faults if the page can't be read, 9.3) | The low byte stored there (faults if the page can't be written) |
| # | The next byte, −128 to 127 | Nothing |

**What each instruction does:**

| Instruction | Effect |
|---|---|
| MOV s | A ← s |
| STO s | s ← A |
| SWP s | Exchange A and s. With #: A ← #, nothing else |
| PUSH s | SP ← SP − 4; the 4 bytes at SP ← s. Faults if outside the stack, or in an idle run (no stack) |
| POP s | s ← the 4 bytes at SP; SP ← SP + 4. POP IP jumps; POP # discards. Same faults as PUSH |
| ADD, SUB, MUL s | A ← A op s, wrapping. MUL keeps the low 32 bits |
| DIV s | A ← A ÷ s, signed, truncating toward 0. If s is 0, A ← 0 (R18). The smallest number ÷ −1 gives itself (wrapping) |
| AND, OR, XOR s | Bitwise, into A |
| NOT s | A ← s with every bit flipped |
| SHL, SHR s | Shift A left or right by (s mod 32) bits, filling with zeros |
| JMP s | IP ← this instruction's address + s |
| JEZ, JNZ, JGZ, JLZ s | The same jump if A (signed) is = 0, ≠ 0, > 0 or < 0; otherwise go on |
| CPY s | n ← s mod 65,536. Copy n bytes from the address in B to the address in D, as if through a buffer (overlap is safe). See below |
| SYS s | The call in slot (s mod the slot count) of the numbering table (9.2). Arguments in A–D, the result in A (RECV also sets B). |
| Spare (codes 22–31) | Nothing. With #, still two bytes |

**CPY in full:**

- Every byte is checked before any is copied. If any source byte can't be
  read or any destination byte can't be written, CPY faults and copies
  nothing.
- The registers are unchanged.
- Each byte is a result for the error floor (7.5).
- For rowhammer, CPY counts once per page it touches (9.8).
- Its cost is about 2 cycles per byte, plus a setup and both devices'
  latency. It runs whole: any overrun of the quantum is repaid in the next
  turn (9.5).

### A.3 Threads, stacks and handlers

- **SPAWN makes the new thread's stack:** an allocation of the stack size
  (a setting, placeholder 64 bytes), on the same memory device as the start
  address.
  - The stack is owned by the new thread's program: the new program for a
    gift, the caller's program for a thread.
  - If the stack can't be allocated, SPAWN fails (returns 0), and nothing is
    given.
- **Saved state.** While a thread is off its core, its registers (A–D, IP
  and SP, 24 bytes) sit at the base of its stack allocation. So the usable
  stack is the stack size minus 24 bytes, and saved state fades and corrupts
  with its memory. A bit of it failing ends the thread (9.4).
- **A new thread's registers:** A = SPAWN's starting value; B, C, D and IP =
  the start address, as an idle run's are; SP = the top of its stack
  [2026-11-13, the design review after the origin test's levers; agreed
  with Damian. Before, B, C and D were 0]. A real thread gets its start
  argument in a register; and code that copied itself as an idle run finds
  the same registers when it runs as a program.
- **An idle run's registers:** A, B, C and D = its start spot (or landing
  spot) address, IP = that address. It has no stack.
- **Handlers are per thread, one per kind.**
  - **Entry:** A, B, C, D and IP are pushed onto the thread's stack, IP goes
    to the handler, and A gets a value for the kind:
    - a fault: its code (memory, stack, and from gate 5 the watchdog,
      9.1);
    - arriving data: the port;
    - a timer: 0;
    - low voltage: the core's voltage reading.
  - **After a fault,** the saved IP is the instruction after the faulting
    one.
  - **HRET** pops them and goes on. HRET outside a handler does nothing.
  - **The thread ends** if the push overflows the stack, or if a fault
    happens inside a handler.

### A.4 What every call's arguments mean

Every 32-bit value means something, either by modulo, by clamping, or by
failing soft.

| Call | Arguments | Result |
|---|---|---|
| ALLOC | A size: unsigned. 0, or more than the largest size class, fails. B permission: B mod 3 (0 data, 1 executable, 2 both); "both" fails on a strict place. C device: signed. 0 is any, k > 0 is the k-th fastest, k < 0 the k-th slowest, each clamped to the devices that exist | Address, or 0 |
| FREE | A: the start of an allocation held by the caller's program (or run), and not a live thread's stack (9.4) | 1, or 0 |
| PROTECT | A: any address in the page. B: B mod 3 as for ALLOC. Fails unless the caller's program holds every allocation in the page, and on a strict place for "both" | 1, or 0 |
| SPAWN | A: an address in an executable or "both" page, in an allocation the caller's program (or run) holds. B: the starting value. The gift rule is in 9.4, and a live thread's stack is never given | The thread id (> 0), or 0 |
| EXIT | — | — |
| SLEEP | A > 0 sleeps that many ticks; A = 0 sleeps until an interrupt; A < 0 ends this turn | — |
| SETCLOCK | A: the level, clamped to the step range (a setting) | The level applied |
| SETCORE | A mod 3 (0 any, 1 P only, 2 E only). Fails if the place has no working core of that kind | 1, or 0 |
| SETHANDLER | A mod 4 (0 fault, 1 data, 2 timer, 3 low voltage). B: the handler's address, in an executable page the program holds (0 clears it). C: for data, the port (mod the port count; port 0 fails); for a timer, the period in ticks (0 clears it) | 1, or 0 |
| HRET | — | — |
| SENSE | A: which reading (A mod the number of readings). B: the index, mod the count it refers to (devices, neighbours or ports). An index into an empty list reads 0 (approved 2026-11-11) | The reading, in fixed-point units set per reading |
| SEND | A: the place (A mod N). B: the port (B mod the port count). C, D: the address and length; the length is clamped to the message limit, and 0 fails. The bytes must be readable | 1, or 0 |
| RECV | A: the port (port 0 fails). B: the buffer. C: the max length (0 fails). D: the timeout (0 polls, > 0 ticks, < 0 forever). Copying stops at the first page the program can't write | Count in A (0 if none). The sending place's id in B, or −1 |
| DREAD, DWRITE | A: the disk offset, mod the disk space's size. B: the memory address. C: the length, clamped at the end of the disk space. Completes at the tick's barrier, rechecking access then. DWRITE fails in an idle run | Count done (0 if the disk has failed) |
| PING | A: the place (A mod N). B: the timeout in ticks (≤ 0 uses the default setting) | Round-trip ticks, or −1 |

**The SENSE readings,** numbered in this order:

- **own place:**
  - temperature, voltage, the tick;
  - the place's id;
  - remaining turn time;
  - bytes waiting at port B;
  - idle cores, free memory;
  - page size, strict (1) or lax (0);
  - device count, neighbour count;
- **device B:** kind, speed, nominal threshold, free bytes;
- **neighbour B:** id, voltage, temperature, idle cores, free memory. This
  one costs a round trip [2026-11-16, `build-plan-gate3.md` 2; agreed with
  Damian]:
  - the thread waits twice the link's delay, outside the queues, and the
    neighbour's OS answers, as it answers a PING (11.2);
  - it reads the neighbour as it was at the barrier when the request
    landed, so the answer doesn't depend on timing between places;
  - if either place's network device is off, every reading but the id is
    0, as an empty list's index reads 0;
  - neighbours are data neighbours: the places a data link reaches.

**In detail [details at build, step 5; 2026-11-11]:**

- **SENSE's units:**
  - temperatures in tenths of a degree C, voltages in millivolts;
  - the tick's low 32 bits;
  - the turn's time left in cycles;
  - idle cores are working cores with no thread on them (a core running an
    idle run counts as idle).
- **SENSE's devices** are listed cores first, then memory devices, then
  disks. A core's speed is its stock cycles per tick; a memory device's or
  disk's is its latency in cycles. A disk's free bytes are its size, since
  the disk is raw.
- **SETCLOCK's level** is in tenths of stock speed: 10 is stock, and the
  range is 5–15 (settings).
- **A handler's frame** is 20 bytes: IP on top, then D, C, B and A (A was
  pushed first). A fault handler finds 1 in A for a memory fault, 2 for a
  stack fault.
- **SETHANDLER for a timer** with a period of 0 clears the timer's handler
  as well.
- **Calls an idle run may not make** fail with 0 in A (RECV also sets B to
  −1); EXIT, SLEEP and HRET just do nothing.
- **A call's cost** in 9.7 includes the crossing. An E core pays about 1.6
  times as much (26 sixteenths per cycle against 16).
- **A program's id in the facts** is the place's number plus one, times
  2⁴⁰, plus a count kept per place. So ids are unique in the world, and
  independent of how many host threads run it.

### A.5 The shortest replicator, traced

[Rewritten 2026-11-13 for the conventions agreed in the design review after
the origin test's levers: a new thread starts with B, C and D at its start
(A.3), and ALLOC's result lands in D as well as A (A.4). The earlier 18-byte
replicator's `SWP D` no longer brings the parent's address back. Both below
are traced in tests/test_os.cpp.]

**On a lax place: 14 bytes.** The parent sits at address S, with B, C and D
= S.

| Offset | Instruction | Bytes | After it |
|---:|---|---:|---|
| 0 | MOV # 14 | 2 | A = 14 |
| 2 | STO B | 1 | B = 14, which is 2 mod 3 ("both") |
| 3 | SYS # ALLOC | 2 | Size 14, permission both, device C = S. A = D = N, the new address, or 0 |
| 5 | MOV C | 1 | A = S |
| 6 | STO B | 1 | B = S |
| 7 | CPY # 14 | 2 | 14 bytes copied from S to N |
| 9 | MOV D | 1 | A = N |
| 10 | SYS # SPAWN | 2 | N lies outside the allocation holding IP, so N's allocation passes to a new program. Its thread starts at N with A = S (B, the starting value) and B, C and D = N. A = the thread id |
| 12 | JMP # −12 | 2 | Back to offset 0 |

- **The child** runs the same sequence at its own address: its C is its own
  start.
- **The size doubles as the permission.** 14 is 2 mod 3, so storing it in B
  asks for "both". On a lax place "data" does too (9.3), so any size that
  isn't 1 mod 3 works.
- **Shorter, but not everywhere: 11 bytes.** `MOV # 11; SYS # ALLOC; CPY #
  11; MOV D; SYS # SPAWN; JMP # −9` leaves B at its own start, so its
  ALLOC's permission is its address mod 3. It works where that is 0 or 2,
  two addresses in three. At 1 its new block is executable only, and its
  CPY faults.
- **When memory runs out,** ALLOC returns 0 in A and D, and CPY faults
  writing to address 0. The parent's only thread ends, and so does the
  parent. That gives early turnover. A two-byte check would avoid it, and
  selection will likely find one.
- **The device is C,** its own address, which clamps to the slowest memory
  device. A copier that wants another sets C.
- **An origin from an idle run** starts with the same registers (A.3), so
  the same bytes work as a run and as a program.

**On a strict place with 64-byte pages: 20 bytes.** The copy must be made as
data on a page of its own, then made executable with PROTECT.

| Offset | Instruction | Bytes | After it |
|---:|---|---:|---|
| 0 | MOV # 63 | 2 | A = 63 |
| 2 | STO B | 1 | B = 63, which is 0 mod 3 (data) |
| 3 | SYS # ALLOC | 2 | 63 bytes take a 64-byte slot, the only one in its page. A = D = N |
| 5 | MOV C | 1 | A = S |
| 6 | STO B | 1 | B = S |
| 7 | CPY # 20 | 2 | Copied |
| 9 | MOV # 1 | 2 | A = 1 |
| 11 | STO B | 1 | B = 1 (executable) |
| 12 | MOV D | 1 | A = N |
| 13 | SYS # PROTECT | 2 | The page becomes executable. This works because the program holds the whole page |
| 15 | MOV D | 1 | A = N |
| 16 | SYS # SPAWN | 2 | A new program at N |
| 18 | JMP # −18 | 2 | Back to offset 0 |

**Pages of 256 or 1,024 bytes** need more bytes to form the size, because #
reaches only 127 (for example, `MOV # 64; SHL # 2`).

## Appendix B. Lifecycles

**An idle run** (a core's use of the reset region):

| From | Event | To |
|---|---|---|
| Halted | Something writes to the region, an arrival waits, or a retry comes round | Running, at the next start spot (or the landing spot) |
| Running | ALLOC succeeds | Running, holding that allocation |
| Running | SPAWN into a holding | Running. The holding now belongs to a new program |
| Running | A fault, or the cap | Halted (idle draw). Remaining holdings freed |
| Running | A ready thread arrives, or the core cuts out | Ended. Holdings freed. A ready thread then runs |

**A program:**

| From | Event | To |
|---|---|---|
| — | SPAWN gives an allocation | Alive, with one thread. Its birth is logged |
| Alive | SPAWN inside its own code | Alive, with one more thread |
| Alive | Its last thread ends | Ended. Its allocations are freed with their bytes kept, and its death is logged with the cause |

**A thread:**

| From | Event | To |
|---|---|---|
| Ready | A quantum on a core | Running |
| Running | The quantum or turn ends, or it yields | Ready |
| Running | SLEEP, RECV waiting, PING, SEND's transmission, DREAD or DWRITE, SENSE of a neighbour | Waiting, until its condition. Disk I/O waits for the barrier |
| Running | An interrupt for a registered kind | In its handler, until HRET |
| Any | EXIT; a fault with no handler; a fault in a handler; running on a core when it cuts out or fails; its stack's memory device failing; a bit of its saved state failing, by fading, a cosmic-ray flip or an edge error (9.4) | Ended |

**An allocation:**

| From | Event | To |
|---|---|---|
| Free (dirty) | ALLOC | Held by a run, or owned by a program |
| Held by a run | SPAWN into it | Owned by the new program |
| Held by a run | The run ends | Free, with its bytes kept |
| Owned | FREE, or its program ends | Free, with its bytes kept |
| Any | Its memory device fails | Lost. Its pages are freed with the device, and touching them faults |

**A device:**

| From | Event | To |
|---|---|---|
| On | Voltage below its threshold | Cut out, for at least the minimum off-time |
| Cut out | Voltage at threshold + gap, after the off-time | On |
| On | Temperature past its heat limit | Tripped |
| Tripped | Cooled below its cool-down point | On |
| Any | Wear reaches endurance (or a disk's first block wears out) | Failed |
| Failed, or worn past the threshold | A visit | Replaced: fresh (or like for like), same size |

**Pending operations, and what they see:**

- **SEND** copies its bytes when called, so later changes to the buffer
  don't matter.
- **RECV** copies at delivery, rechecking access then.
- **DREAD and DWRITE** act at the barrier, on the buffer as it is then,
  rechecking access. They stop at the first page the program can't use.
- **A place that respawns** gets an incarnation number one higher (7.8).

## Appendix C. One tick: order and equations

**The order of a tick:**

1. **Boundary inputs:** the day's phase; the sources' states (swings,
   drop-outs, swells, ramps); patches and strikes; visits due; cuts;
   straits parting or joining; the current's heading and strength [gate
   5].
2. **Conductances:** each device's draw is worked out from the last tick's
   voltage, temperature and activity, and turned into a conductance,
   G = P ÷ V².
3. **The voltage solve:** nodal analysis.
   - Sources are Norton equivalents: their voltage over their internal
     resistance.
   - Water links and shore drains are resistances (6.1).
   - The sparse system is solved in fixed point by an iterative solver,
     warm-started from the last tick. It stops at a tolerance or an
     iteration cap (both settings).
   - A component with no source is at 0 V.
4. **Device states:** cut-outs, restarts, heat trips and cool-downs, then
   failures from wear (4.4, B).
5. **Execution,** in parallel by place: rounds of rotating quanta (9.5). Each
   core runs its quantum × speed × clock factor cycles. Error countdowns and
   access counters run here.
6. **The barrier:**
   - disk I/O completes, in order, with worn blocks' flips as they are
     written [gate 5];
   - messages advance through their hops, at most one hop each, joining
     queues in a fixed order (11.3); waiting messages reroute (11.6); the
     current's blocks are copied and queued after programs' messages, and
     written into the reset region as they land (12.6) [gate 5];
   - the out-of-memory killer, at places crowded long enough (9.1) [gate
     5];
   - arrivals join port 0 queues;
   - RECVs are delivered, and data interrupts set pending (9.5);
   - neighbour SENSE readings are taken (A.4);
   - routes are recomputed if the topology changed.
7. **Heat:** devices exchange with their place, places with the outside and
   their land neighbours. Every step is clamped so it can't overshoot the
   balance it's moving toward.
8. **Memory and disk:**
   - fading, by each device's leak ranks;
   - the cosmic-ray floor, each device's at its own floor (7.5);
   - weak cells' flips (7.4);
   - rowhammer flips;
   - the threads with a bit of saved state failed in this step, by any of
     these or an edge error, end (9.4);
   - wear increments.
9. **Facts and samples.**

**The equations** (all coefficients are settings or catalogue numbers):

- **Speed:** s = slope × min(max(V − Vth, 0), Vmax − Vth), where Vth is the
  worn threshold. Cycles in a slice = s × the clock factor × the slice.
- **Core draw:**
  - running: P = k × size × V² × s × (1 + leakage × max(T − T₀, 0));
  - halted, or with all threads asleep: the idle draw.
- **Memory draw:** holding draw + per-access draw × the last tick's accesses.
- **Network device draw:** constant, plus per byte. **Cooling device draw:**
  rising with its place's temperature.
- **Above a device's maximum,** its constant draws are multiplied by
  (V ÷ Vmax)² (4.4).
- **Device temperature:** T ← T + (P − h × (T − T_place)) × dt ÷ C, clamped.
- **Place temperature:**
  T_p ← T_p + (Σ heat in − c × (T_p − T_out) − Σ k × (T_p − T_neighbour)) ×
  dt ÷ C_p, clamped.
- **Wear per tick:**
  dW = dt × w₀ × 2^(d × (T − T_ref) ÷ 10) × 2^(v × max(V − Vmax, 0) ÷
  100 mV), where d is doublings per 10 °C and v doublings per 100 mV over
  the maximum [2026-11-20, `build-plan-gate5.md` 2; agreed with Damian.
  Before, the voltage factor was (1 + v × max(V − Vmax, 0)), which can't
  break a device with a spike without ruining it with a swell (7.4)].
  Disks also add their writes, per block. Wear raises Vth in proportion.
  A link's wear is (I ÷ I_ref)² × (R ÷ R₀) a tick (7.4).
- **The error floor and edges:** each result has a chance of being wrong,
  ε = floor + edge(V) + clock(over stock).
  - The engine draws "results until the next wrong one" from a geometric
    distribution at ε, and counts down.
  - At zero, a random bit of that result flips.
- **Memory fading:** each device keeps a seeded permutation from leak rank
  to cell.
  - The cells failing this tick are the lowest-ranked ones for its voltage
    margin and temperature.
  - The engine visits only those cells, which read back random.
- **The cosmic-ray floor:** each device draws a count of flips per tick, and
  then their positions.
- **Rowhammer:** activation counts accumulate over a refresh window (a
  setting, in simulated time) and reset at its end. Past the device's
  threshold, flips are drawn for the adjacent pages at the tick's end.

**Fixed point [details at build].**

- Conductances span many orders of magnitude, because water links fall off
  exponentially. So the solve uses per-unit scaling and 64-bit fixed point.
- The formats and the tolerance are fixed before the first run.
- **The solve [2026-11-19, `build-plan-gate4.md` 2; agreed with Damian]:**
  voltages in microvolts and conductances in 2⁻²⁰ siemens, in 64-bit
  integers with 128-bit products. Gauss–Seidel sweeps in place order on
  one host thread, started from the last tick's voltages, until no place
  moves by more than the tolerance or the sweep cap is reached. A capped
  solve leaves the voltages its last sweep gave, and is counted. Places
  read their voltage rounded to the millivolt.

## Appendix D. The laws on one page, and the settings count

**The laws** (principle 2's test: they fit on one page):

1. **Circuit:** one sparse voltage solve a tick, with a one-tick lag, over
   proximity links. Sources are a voltage behind an internal resistance, and
   water leaks to ground.
2. **Device:** a voltage range. Speed ∝ (V − Vth); draw ∝ V² × speed, with
   leakage and idle draw. Cut-outs (with a gap and a minimum off-time) and
   heat trips.
3. **Error:** wrong results near the edges and above stock clock, plus a
   floor everywhere, far higher on a few flaky parts. Memory fails to hold
   bits by leak rank, and its weak cells as it wears.
4. **Heat:** two temperatures, Newton's cooling, spread over land links;
   water is a sink.
5. **Wear:** grows with heat, overvoltage and writes, and raises the
   threshold. A device fails at its endurance.
6. **Instructions:** the 22 (appendix A).
7. **Calls:** the 16, failing soft, with every value valid.
8. **Memory:** pages, size classes, access by page, ownership by
   allocation. Nothing is zeroed.
9. **Programs:** a SPAWN outside your own code gives an allocation to a new
   program. A program ends with its last thread. A thread ends when a bit
   of its saved state fails. A place crowded for a day loses a program,
   drawn by the bytes it holds (9.1).
10. **Scheduler:** round robin in rotating quanta of time; SETCORE pins;
    per-turn clocks.
11. **Interrupts:** faults (the watchdog's among them), data, timers and
    low voltage, through handlers.
12. **Reset region:** idle runs, stepping start spots, a cap, halting, and
    prompt runs for arrivals.
13. **Disk:** raw; I/O at the barrier; the boot block; the cold start.
14. **Network:** store and forward, queues with tail drop, inboxes, static
    routing, corruption by length, and PING's echo.
15. **The cosmic-ray floor.**
16. **Rowhammer.**

**The boundary conditions** (fixed functions, not laws):

- the map and its fields;
- the day;
- sources (appearance by area, lifetime, swings, drop-outs, swells, ramps);
- patches and strikes;
- link cuts;
- maintenance visits and respawning;
- straits parting and rejoining (11.6);
- replication currents (12.6).

So there are sixteen laws and eight boundary inputs. It fits, with no room
to spare.

**Settings, a rough total (Claude's estimate, to be counted when the
registry exists):**

| Group | About |
|---|---:|
| Hardware catalogue (about 8 kinds × 10 fields) | 80 |
| Drawn fields (about 10, each with range, scale, edge strength, sharpness) | 40 |
| Map, links, water and bridges | 20 |
| Sources and their catalogue | 15 |
| Regional events | 18 |
| Network | 12 |
| OS, scheduler and reset region | 16 |
| Instruction costs (two tables) and service costs | 60 |
| Wear, visits, respawning and symptoms | 15 |
| Gate 5's four chosen mechanics (12.5) | 15 |
| Observers | 10 |
| **Total** | **about 305** |

Nova 1 had 385. The curves keep most of the catalogue to a few numbers per
kind (7.3). Tuning this by hand is the risk Nova 1 met, and it may need a
settings-search tool again.

## Appendix E. The default world's numbers (placeholders)

| Item | Placeholder |
|---|---|
| Places | 1,000 (the runs of gates 1 to 3 use 48, 14.1) |
| Links per place | about 4 |
| Map width | about 32 links |
| Cores per place | 2–8, typically 4 |
| Memory per place | about 160 KB, in 2–4 devices |
| Disk per place | about 1 MB |
| Reset region | 1 KB |
| Page sizes | 64, 256 and 1,024 bytes |
| Stack per thread | 64 bytes |
| Call slots | 18: the 16 calls, plus a second ALLOC and a second SPAWN (9.2) |
| Tick | the time in which a stock P core runs about 25,000 cycles |
| Turn | a quarter of a tick |
| Quantum | a sixteenth of a tick |
| Idle-run cap | 100,000 cycles, longer than the dearest birth (about 11,000–53,000) |
| Day | about 2,000 ticks |
| Habitable band | about 10 links wide |
| Grid borders | 20–30 links apart |
| Typical program | unknown. The traced minimum is 18 bytes |

**Ratios worth watching:**

- **A SPAWN** (10,000–50,000 cycles) takes 0.4–2 ticks of a core. A birth
  takes about a tick.
- **The map is only about 32 links across.** That's room for one to three
  bands, grids or barriers. Bigger worlds give more.
- **Rowhammer.**
  - If a tick stands for about 8 µs of a real 3 GHz core, a 64 ms refresh
    window is about 8,000 ticks.
  - A program alternating between two pages can make several hundred
    activations a tick. So it reaches a real 10,000 threshold within a few
    dozen ticks.
  - The window and the threshold set how cheap the attack is.
- **The visit interval against device lifetime** sets how scarred places
  get (7.8).
