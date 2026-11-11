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
| 2 | Does a full place keep turning over? | Memory fade, cut-outs and the crash rule (7.6), a varying supply, the boot block's cold start (10.5) |
| 3 | Does life spread? | The map (6), links, SEND, RECV and PING (11), port 0 (10.4), the network device, handlers for arriving data |
| 4 | Does the physics shape it? | The voltage solve (4), heat (5), the day, sources, the low-voltage interrupt, cooling devices |
| 5 | Does it stay interesting? | Wear and visits (7.4, 7.8), respawning places, regional events and strikes (12), network outages and rerouting (11.6), rowhammer (9.8), the wear-levelling and replacement options |

- **Why gate 1 has every call that works within one place (approved
  2026-11-11).** The origin test needs a SPAWNed thread to run, so the
  scheduler can't wait for gate 2. More widely, a call stubbed at gate 1 but
  live later would make the origin test answer a different question from
  the one the world asks: SETCLOCK, for example, costs 10,000–100,000
  cycles of a run's cap.
- **Calls before their gate.** Until gate 3, SEND and PING fail as they do
  when the place's network device is off, and RECV waits on inboxes that
  never fill. They charge their real costs.
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
- **Sources land by area.** Each new source lands at a random point on land
  and joins the nearest place.
  - A place's chance is its catchment area raised to a power, set by a
    slider: 1 is by area (the default), 0 is by place, and above 1 favours
    the countryside more.
  - Consequence: the countryside is rich in power and poor in network,
    and towns the reverse.
  - Real anchor: sunlight and wind fall by area.
- **Sources don't wear.** The "power companies" maintain them from outside
  the world.
- **Luck is part of the world.** A region, or the whole world, can go
  without power for a long stretch.

### 4.3 The day, swings and swells

- **Day and night sweep across the map from east to west.** The settings
  are the day's length and the night's depth.
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

### 4.4 Devices on the supply

- **Each kind of device has a voltage range** (7.1).
  - Below its threshold it cuts out.
  - Near either edge it makes wrong results.
  - Above its maximum it heats, and past its heat limit it trips until it
    has cooled.
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
  - Consequences:
    - coasts are cool and rich in data, and pay a small power tax;
    - narrow straits pass a trickle of power, and wide seas pass none;
    - islands near a mainland get a trickle;
    - distant landmasses are separate power grids, joined only by data;
    - small islands have long dark spells.
- **Lengths are measured in links.** Every length setting is in units of the
  average link length, so adding places makes the world bigger, not
  denser.
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
- **Barriers are hard to cross, not walls.**
- **Hardware biomes:** smooth fields over the map decide where fast GPUs,
  large memories and so on cluster (7.3). These are Nova 1's substrates,
  carried over.
- **Power is not a drawn field.** A place's power follows from sources and
  resistance.
- **Grid borders, on the links.** The resistance field's step contours are
  read as borders: a proximity link that crosses one has its resistance
  multiplied, making it a thin tie between grids.
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

### 7.4 Wear [gate 5]

- **Every device has a wear count and an endurance.** It fails when its wear
  reaches its endurance.
- **Wear grows with stress:**
  - **With temperature, steeply.** A common rule of thumb is that failure
    rates double for every 10 °C (Arrhenius). The setting is doublings per
    10 °C, so 0 means no heat effect.
  - **With voltage above the device's maximum.** A big enough spike breaks a
    device at once. The slope is a world setting.
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
  - **[open]** Whether to vary the floor per device through the scatter, so
    a few parts are flaky, as in real fleets.
- **The cosmic-ray floor: rare flips in all memory.** Each bit has a chance
  per tick of flipping. It's a classic flip, with no shielding, and a
  sliding scale with 0 meaning off.
  - "Static" means only the random bytes memory and disk start with (10.1).
    The ongoing flips are "the floor".
  - The rate waits on a test.
- **The error threshold.** The floors and the edges together must stay well
  under one error per copy of a typical program (Eigen's error threshold).

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
  crashing.

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

  Linux's out-of-memory killer is the real example to avoid. In Nova 2,
  when memory is full, requests fail and nobody is killed.
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
| 0 | ALLOC | size, permission, device → address or 0 | Reserve memory. Permission is data, executable or both. Device picks by speed (6.3). Placed by size class (9.3) |
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

- SENSE and SEND, which need no owner. A SEND from an idle run has no sender
  program, so a birth from it starts a new lineage (13.2);
- ALLOC, FREE and PROTECT, on what the run itself holds;
- SPAWN into what it holds, which creates a program (9.4);
- DREAD (a booted second stage reads the disk this way, 10.5).

Every other call fails soft, DWRITE included, so junk can't overwrite the
disk.

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
  - On a lax place, a copier allocates "both" and skips PROTECT. Copying is
    cheap, and injection is easy.
  - On a strict place, ALLOC with "both" fails, and the reverse holds.
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
    fault;
  - **arriving data** on a given port;
  - **a timer,** with a period in ticks;
  - **low voltage:** it fires once, for the thread running on the core, as
    the core's voltage nears its cut-off. The margin is a setting
    [gate 4] (12.4).

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
  with a little scatter and steep drop-offs (6.2). They're set when the place
  boots and never change.
- **What varies:**
  - the page size, from a short list;
  - the writable-and-executable rule, strict or lax.

  Programs can read both (SENSE, R25). Everything else is global, including
  the turn length (one world setting) and call costs.
- **Neighbours stay similar,** so a program can adapt to the OS around it.
  Nearby moves are safe, and far moves are a gamble.
- **Large pages make sharing, and so parasitism, easy; small pages make it
  hard.**

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
       end, and the counts reset.
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

### 10.3 A birth, step by step

1. The reset region holds static.
2. An idle core runs it from its spot. Most bytes crash within a few
   instructions. The core halts, and restarts at its next spot when
   something changes (10.2).
3. Bytes that copy themselves within the region, or send themselves on with
   SEND, spread.
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
- **So migration needs spare core time, not just free memory.** A place
  whose cores are all busy with threads runs no arrivals, however well
  adapted they are. That's an absolute barrier to settling, and gate 3 tests
  it as a named hypothesis (14).
- **What travels:**
  - **complete replicators,** which must ALLOC, copy themselves out and
    SPAWN. Compact genomes travel best;
  - **code that never becomes a program,** which only sends itself on, since
    idle runs may SEND (R4). It pays no SPAWN, appears in no lineage, and
    may spread before any program does. It's also what carries bytes between
    reset regions, which origins may need. Observers count it (13.1).

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
- **The target length.** The traced shortest replicator is 18 bytes on a lax
  place, and 24 on a strict one with 64-byte pages (appendix A).
- **Pass stages,** each reported separately:
  1. a copying sequence appears: a program has a descendant with its
     genome;
  2. the offspring reproduce: viable offspring;
  3. a lineage reaches several generations (a setting, for example 10);
  4. the lineage establishes: it persists despite faults and competition,
     for a set span;
  5. it keeps adapting once the place is crowded: its genome groups change
     (13.2).
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
- **Why it might fail:** a replicator here needs ALLOC, a copy, perhaps
  PROTECT, and SPAWN, and each call costs a crossing. BFF got replicators
  from random code because a replicator there needs no calls at all.
  Recent BFF work also separates replicators appearing from their spreading
  (Astra's citation; Claude can't check it).

## 11. The network [gate 3]

### 11.1 Knowledge is local

- **Programs sense their own place and their neighbours** (9.2, SENSE).
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
    + size ÷ the first link's bandwidth. Real anchor: a blocking send
    returns once the data has left the sender's buffer.
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

### 11.4 The network device

- **Each place has one,** a router and interface in one, from the catalogue
  (7.2). It has a range, heat, wear, failure and visits like any device.
- **It must be on for the place to send, receive or forward.** So a blackout
  is also a hole in the network, and routes go around it. Real anchors: in
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
  - Messages wait their turn.
- **When a queue is full, the newcomer is dropped,** silently. Messages
  already waiting keep their place (tail drop).
  - Big messages are dropped first, because they need more room. Real UDP
    loses big datagrams first too: losing any fragment loses the whole.
    Compact genomes travel best.
  - A flood from one program fills queues everyone uses: a commons.
- **Each comms port has a fixed inbox,** held by the OS on the reset region's
  memory device, so it fades in a brownout.
  - When it's full, the newcomer is dropped, silently. What's waiting
    stays.
  - SENSE reads the bytes waiting.
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
- **Where data is still lost:**
  - newcomers at full queues and inboxes;
  - stranded messages whose lifetime runs out;
  - everything queued at a place whose network device goes dark (7.6).

  So outages become slowdowns, and partitions become waits.
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

**The low-voltage interrupt** (9.5) fires once as a core's voltage nears its
cut-off. The handler can SLEEP, so the thread survives the cut (7.6).

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
- symptoms: 3, plus the interrupt's margin.

**Risks:**

- **Cascades may happen too often, or never.** That depends on source
  stiffness, device ranges and margins.
- **Long-crowded places carry worn, slow, error-prone devices** until those
  fail and are replaced. That's intended succession, but long-lived lineages
  may pay for their own success.

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
    a failed memory device, or memory that lost its contents.
- **Frequent facts are counted per sample:**
  - threads started;
  - threads crashed at cut-outs;
  - messages sent, dropped and stranded;
  - SENDs from idle runs (R4).
- **Observers also read sampled, read-only views of the state,** because some
  labels are states, not events. The sample interval is a setting. The
  views include:
  - each thread's IP and the code around it;
  - each reset region's bytes, with their compressibility (R4);
  - page ownership.
- **Observer memory is saved beside each snapshot,** outside the hash, so the
  family tree survives a resume.
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
- **Code after birth is tracked by sampling.** Each sample hashes the code
  around each thread's IP. That shows overwrites, colonies running many
  threads, and code that changes after birth.
- **Lineage is tracked by descent,** from the facts:
  - a SPAWN gives the spawner;
  - an arrival gives its sender. There's no parent if the sender was an
    idle run (9.2);
  - a boot gives the boot block's last writer;
  - static gives no parent: a new origin.

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
| Traveller | Code from idle runs that SENDs itself on without becoming a program (10.4) | Idle-run SEND counts, region samples | Heuristic |
| Sleeper | A program that sleeps, or uses the low-voltage interrupt, before cut-outs, and survives | Call counts per lineage, death facts | Heuristic |
| Communicator | A program whose behaviour follows data it RECVs from another lineage | Needs RECV traces, not yet kept: costly | Causal claim |
| Partnership | Two lineages that regularly send to each other's ports | Per-lineage message counts | Heuristic |
| Disk user, pinger | It calls DWRITE or PING, and outlives similar programs that don't | Call counts, death facts | Causal claim |
| Cascade | Cut-outs that spread from place to place within a few ticks | Cut-out counts per place per sample | Heuristic |
| Blackout, outage | A region dark, or cut off, for a time | Per-place samples | Observation |

**Deaths are counted by cause,** from the facts.

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
- **Gate 3:**
  - does life spread?
  - whether CPU saturation shuts arrivals out completely (10.4);
  - whether blind SENDs pile onto a few places (11.1);
  - the cost of recomputing routes at nightfall (11.3);
  - whether the map itself works (6.1).
- **Gate 4:**
  - whether crowds creep in on soft sources (4.2);
  - whether heat settles or oscillates (5).
- **Gate 5:**
  - how often cascades happen (12.2);
  - whether reading input pays enough for injection to arise (9.8);
  - whether old, worn places punish long-lived lineages (12.5).
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

- Whether the error floor varies per device, so a few parts are flaky
  (7.5).
- A trade-off curve for the network device (7.2).
- The GPU and NPU: a condition in which each wins, before either is unparked
  (R20). If unparked: EXIT for lanes, a launch cap and device sharing, and
  NMATMUL's calling convention (9.5).
- Large point sets in the viewer, which would need D3D11On12 (2).
- Which other parts of Nova 1 to reuse (2).

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
  with its memory.
- **A new thread's registers:** A = SPAWN's starting value; B, C and D = 0;
  IP = the start address; SP = the top of its stack.
- **An idle run's registers:** A, B, C and D = its start spot (or landing
  spot) address, IP = that address. It has no stack.
- **Handlers are per thread, one per kind.**
  - **Entry:** A, B, C, D and IP are pushed onto the thread's stack, IP goes
    to the handler, and A gets a value for the kind:
    - a fault: its code (memory, stack);
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
| FREE | A: the start of an allocation held by the caller's program (or run) | 1, or 0 |
| PROTECT | A: any address in the page. B: B mod 3 as for ALLOC. Fails unless the caller's program holds every allocation in the page, and on a strict place for "both" | 1, or 0 |
| SPAWN | A: an address in an executable or "both" page, in an allocation the caller's program (or run) holds. B: the starting value. The gift rule is in 9.4 | The thread id (> 0), or 0 |
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
  one costs a hop.

### A.5 The shortest replicator, traced

**On a lax place: 18 bytes.** The parent sits at address S. Its C is 0, as
every SPAWNed thread's is.

| Offset | Instruction | Bytes | After it |
|---:|---|---:|---|
| 0 | MOV IP | 1 | A = S |
| 1 | STO D | 1 | D = S |
| 2 | MOV # 2 | 2 | A = 2 |
| 4 | STO B | 1 | B = 2 ("both") |
| 5 | MOV # 18 | 2 | A = 18 |
| 7 | SYS # ALLOC | 2 | Size 18, permission both, device C = 0 (any). A = N, the new address, or 0 |
| 9 | SWP D | 1 | A = S, D = N |
| 10 | STO B | 1 | B = S |
| 11 | CPY # 18 | 2 | 18 bytes copied from S to N |
| 13 | MOV D | 1 | A = N |
| 14 | SYS # SPAWN | 2 | N lies outside the allocation holding IP, so N's allocation passes to a new program, whose thread starts at N with A = S. A = the thread id |
| 16 | JMP # −16 | 2 | Back to offset 0 |

- **The child** starts at N with A = S, B = C = D = 0. Its first two
  instructions set A and D to N, so it runs the same sequence at its own
  address.
- **When memory runs out,** ALLOC returns 0, D becomes 0, and CPY faults
  writing to address 0. The parent's only thread ends, and so does the
  parent. That gives early turnover. A two-byte check would avoid it, and
  selection will likely find one.
- **An origin from an idle run** has C equal to its start spot, not 0. The
  device argument then clamps to a real device, so it still works.

**On a strict place with 64-byte pages: 24 bytes.** The copy must be made as
data on a whole page, then made executable with PROTECT.

| Offset | Instruction | Bytes | After it |
|---:|---|---:|---|
| 0 | MOV IP | 1 | A = S |
| 1 | STO D | 1 | D = S |
| 2 | MOV # 0 | 2 | A = 0 |
| 4 | STO B | 1 | B = 0 (data) |
| 5 | MOV # 64 | 2 | A = 64: a whole page |
| 7 | SYS # ALLOC | 2 | A = N |
| 9 | SWP D | 1 | A = S, D = N |
| 10 | STO B | 1 | B = S |
| 11 | CPY # 24 | 2 | Copied |
| 13 | MOV # 1 | 2 | A = 1 |
| 15 | STO B | 1 | B = 1 (executable) |
| 16 | MOV D | 1 | A = N |
| 17 | SYS # PROTECT | 2 | The page becomes executable. This works because the program holds the whole page |
| 19 | MOV D | 1 | A = N |
| 20 | SYS # SPAWN | 2 | A new program at N |
| 22 | JMP # −22 | 2 | Back to offset 0 |

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
| Any | EXIT; a fault with no handler; a fault in a handler; running on a core when it cuts out or fails; its stack's memory failing or fading past use | Ended |

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
   drop-outs, swells, ramps); patches and strikes; visits due [gate 5].
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
   - disk I/O completes, in order;
   - messages advance through their hops (11.3);
   - arrivals join port 0 queues;
   - RECVs are delivered;
   - routes are recomputed if the topology changed.
7. **Heat:** devices exchange with their place, places with the outside and
   their land neighbours. Every step is clamped so it can't overshoot the
   balance it's moving toward.
8. **Memory and disk:**
   - fading, by each device's leak ranks;
   - the cosmic-ray floor;
   - rowhammer flips;
   - worn-block flips;
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
- **Device temperature:** T ← T + (P − h × (T − T_place)) × dt ÷ C, clamped.
- **Place temperature:**
  T_p ← T_p + (Σ heat in − c × (T_p − T_out) − Σ k × (T_p − T_neighbour)) ×
  dt ÷ C_p, clamped.
- **Wear per tick:**
  dW = dt × w₀ × 2^(d × (T − T_ref) ÷ 10) × (1 + v × max(V − Vmax, 0)),
  where d is doublings per 10 °C. Disks also add their writes, per block.
  Wear raises Vth in proportion.
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

## Appendix D. The laws on one page, and the settings count

**The laws** (principle 2's test: they fit on one page):

1. **Circuit:** one sparse voltage solve a tick, with a one-tick lag, over
   proximity links. Sources are a voltage behind an internal resistance, and
   water leaks to ground.
2. **Device:** a voltage range. Speed ∝ (V − Vth); draw ∝ V² × speed, with
   leakage and idle draw. Cut-outs (with a gap and a minimum off-time) and
   heat trips.
3. **Error:** wrong results near the edges and above stock clock, plus a
   floor everywhere. Memory fails to hold bits by leak rank.
4. **Heat:** two temperatures, Newton's cooling, spread over land links;
   water is a sink.
5. **Wear:** grows with heat, overvoltage and writes, and raises the
   threshold. A device fails at its endurance.
6. **Instructions:** the 22 (appendix A).
7. **Calls:** the 16, failing soft, with every value valid.
8. **Memory:** pages, size classes, access by page, ownership by
   allocation. Nothing is zeroed.
9. **Programs:** a SPAWN outside your own code gives an allocation to a new
   program. A program ends with its last thread.
10. **Scheduler:** round robin in rotating quanta of time; SETCORE pins;
    per-turn clocks.
11. **Interrupts:** faults, data, timers and low voltage, through handlers.
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
- maintenance visits and respawning.

So there are sixteen laws and six boundary inputs. It fits, with no room to
spare.

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
| Observers | 10 |
| **Total** | **about 290** |

Nova 1 had 385. The curves keep most of the catalogue to a few numbers per
kind (7.3). Tuning this by hand is the risk Nova 1 met, and it may need a
settings-search tool again.

## Appendix E. The default world's numbers (placeholders)

| Item | Placeholder |
|---|---|
| Places | 1,000 |
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
