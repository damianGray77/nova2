# Nova 2 — Gate 4 build plan

> **Status (2026-11-20):** agreed, and all 11 steps are done (11); its
> marks wait for the combined run. Step 11: its numbers (7) were worked
> again from measurements (11, "Step 11, first part") and Damian agreed
> the four rows that follow from 7's own rules ("rules only stays closer
> to my original vision"). The reference gate 5 asked for is made again
> at them (`runs/gate4-reference-d`), and the pilot ran to its ticks
> without errors (`runs/gate4-pilot`; 11, "Step 11 done": copying began
> in 6 of 10 seeds, as in gate 3's third pilot; no stage 4, and no hot
> place-window as the record names ground). Gate 5's build session is
> the code's one writer now (11, "After the proposal"). It has made the
> agreed numbers the build's defaults and built the classing tool, the
> mark's report and the measures Damian asked for that day (11, "The
> defaults and the classing tool are built" and "The measures are
> built"). The pass mark's naming of ground and its
> direction were changed by Damian the same day, before any judged run
> ("Either direction"; 7, and 11, "The pass mark as changed"). Gate 4
> has no 20-seed run of its own: by
> Damian's word of 2026-11-20, "we only need a single 20 seed test that
> is a combined gate 4+5. I don't think we need 2", so its marks are
> judged in the one combined run after gate 5 is built (5, step 11).
> The pilot may run beside `runs/open-2` ("Yes, beside open-2").
> Damian agreed the ten design gaps (2), the pass mark (7)
> and the stack rule's pilot (2) as Claude leaned ("Agree as leaned"),
> and said "Start step 1". The gaps are in `design.md` and logged in
> `design-history.md`. The other numbers in 7 are proposals still, to
> agree before the first gate 4 run. The plan starts from the stack rule
> Damian chose on 2026-11-19 (9.4: a live thread's stack is in use),
> which no 20-seed run has been made under yet (2). He said "Start step
> 2" the same day, and on 2026-11-20 gave the word for steps 3 to 10
> ("Lets go ahead and get those going"), with `runs/open-2` (9) left
> running beside them.
>
> Gate 4 asks one question: does the physics shape it? (design 2, 14.1).
> This plan says what is built, in what order, how each step is checked,
> and what must be fixed before the first gate 4 run.
> Numbers in brackets, like (4.2), are sections of `design.md`, which
> stays the authority. When the two disagree, the design wins and the plan
> is fixed.
>
> **Where gate 3 left it:** life spreads (`build-plan-gate3.md` 11, step
> 8; `runs/gate3-2`). A lineage settled a place beyond the one it began
> in, in 17 of 20 seeds; stage 4 came in 18; and 57,803 of 57,808 counted
> windows turned over. Each seed is 48 places on a map of 5 to 14
> landmasses, with 65 to 80 data links, about 3 a place. The supply is
> still gate 2's: every place has a source of its own, 1,000 mV behind a
> resistance that sags it 50 mV at the place's full draw, on a day of its
> own. Under it a place sat at 928 mV on average and drew 14.8 W of a
> full draw of 23.1 W. The P cores were cut out for 21% of their ticks,
> the E cores for 0.05%. Temperature is a constant 25 °C, and nothing
> heats.
>
> **Three things that run showed bear on gate 4:**
> - strict places never passed stage 3, so all the life is in lax places:
>   where life lives is already uneven, before any physics;
> - late worlds can thin to one program a place with hundreds of threads
>   (seed 15: 59 programs, 33,559 threads), which keeps every core busy
>   and a place at its full draw;
> - most copies sent are lost at port 0's door, so a place emptied by an
>   outage is refilled slowly.

## 1. What gate 4 delivers

- **The answer to gate 4's question** (2, 14.1): whether the physics
  shapes life, by a measure and a pass mark set before the first run (gap
  1, and 7).
- **Its named checks** (14.1), each answered with numbers, whichever way
  it falls:
  - whether crowds creep in on soft sources (4.2);
  - whether heat settles or oscillates (5).
- **Watched, not judged:**
  - load rejection (12.2): whether cut-outs in one place push its
    neighbours past their maximum. How often cascades happen is gate 5's
    question (14.1);
  - whether anything uses the low-voltage interrupt, and whether sleeping
    through a cut pays (12.4, 13.3's Sleeper);
  - whether late worlds still thin to one program a place, now that a
    place at full draw sags and heats its neighbours.
- **The regressions,** rerun under gate 4's world, as every gate promises:
  the origin test's stages, gate 2's turnover measure and gate 3's spread
  measure.
- **The mechanisms the gate table names** (2): the voltage solve (4), heat
  (5), the day's sweep across the map, sources coming and going, swells,
  the low-voltage interrupt and cooling devices. And the parts of the map
  gate 3 left for it (`build-plan-gate3.md` 2, gap 1): proximity links
  and what they carry, water as a conductor, the resistance field and
  grid borders, and the cooling field.
- **The engine still deterministic** across thread counts and through
  snapshot and resume, with every voltage and temperature in the world
  hash.

## 2. Design gaps found while planning

Agreed by Damian on 2026-11-19 ("Agree as leaned"): in each gap below,
the choice named under "Claude's lean" is the one agreed, and gap 10's
forms are agreed as a set. They are applied to `design.md` and logged in
`design-history.md`. The other choices are kept here as what was
considered.

1. **What "the physics shapes it" means, and what the gate judges.**
   - The problem: gates 1 to 3 each asked something a run could answer
     yes or no. Gate 4's question has no measure yet, and its two named
     checks (14.1) are "which of these happens": a crowd that creeps in
     and one that doesn't are both answers, and so are heat that settles
     and heat that oscillates. The design itself says the net effect of a
     soft source "isn't known" (4.2). So they can be reported, but not
     passed or failed.
   - Choices:
     - a. **Judge the regressions only.** Gate 4 passes if life still
       arises, turns over and spreads under the real physics. The two
       named checks and everything else are reported. Cost: nothing new
       to build for the mark, but "shapes" is never tested: a physics so
       mild that it changes nothing would pass.
     - b. **Judge where life lives, by the ground it lives on.** Each
       place, in each window of 200,000 ticks (gate 2's window), is
       labelled by what the physics did to it:
       - **hot:** it lost more than a quarter of its core time to heat
         trips;
       - **dim:** it lost more than a quarter of its core time to
         cut-outs below the threshold, or its reset region's memory was
         off for more than a quarter of the window (dark);
       - **fair:** neither.

       The measure is the programs alive at a window's start, by kind of
       ground. The physics shapes life if fair ground holds clearly more
       of it than hot ground and than dim ground (the mark is in 7).
       This isn't true by construction: a cut-out or a heat trip ends
       only the thread running on the core (7.6), so a place whose cores
       are off a quarter of the time can stay as full as any other. Gate
       3's P cores were off 21% of the time and life did well. Cost:
       three counters a place and a section of the report.
     - c. **Judge whether life follows the sources as they come and
       go** (4.2: "their communities move with them"): a lineage settles
       a place lit by a source that opened after the lineage began, in
       some number of seeds. Cost: it needs sources that die and open
       within a run, so short lives, which also cut the origin's chances
       (gap 3).
   - Either way the two named checks need definitions, so the report
     means the same thing in every seed:
     - **Creep:** for each source, in each window: how hard it is loaded
       (its current over the most it can give), its voltage, and the
       ground and the programs at its own place and at the places one
       link from it. A crowd creeps in if those inner places go from hot
       to fair, and gain programs, in the windows where the load is
       high. Reported for the softest third of sources beside the
       stiffest third.
     - **Settles or oscillates:** for each place that holds life, its
       temperature averaged over each day. It has **settled** in a
       window if those day averages stay within 5 °C of each other; it
       **cycles** if it heat-trips 10 times or more in the window; the
       rest are **drifting**. The swing within a day is the day's own
       forcing, and is reported apart. For the places that cycle, the
       report gives the period.
   - **Claude's lean:** b for the mark, with a's regressions beside it,
     and c reported but not judged. The reason against c as the mark: it
     rewards tuning the sources' lives short, against the origin.
   - **Changed on 2026-11-20, before any judged run:** how b names
     ground for the mark, and the mark's direction (7; 11, "The pass
     mark as changed"). What is above is as agreed on 2026-11-19.

2. **The world's size for the gate 4 runs.**
   - The problem: the design's power geography is sized for 1,000
     places: a habitable band about 10 links wide, and grid borders 20
     to 30 links apart, on a map about 32 links across (appendix E).
     Gate 3's runs are 48 places on a map about 7 links across, broken
     into 5 to 14 landmasses of a few places each. A 10-link band has no
     room there. But the budget hasn't changed: stage 4 took 1.8 to 18.3
     million ticks at 48 places, and 10¹² instructions buys a 1,000-place
     world under a million.
   - Choices:
     - a. **48 places, gate 3's budget.** The regressions compare like
       with like and the run costs what gate 3's did (9 hours). The band
       is made to fit: links resist enough that a band is about 3 links
       wide, so a landmass shows a gradient at all. What the runs can
       then show is ground by place (gap 1b), not the geometry of bands.
     - b. **About 200 places at four times the budget.** A map about 14
       links across with landmasses big enough for a band 5 or 6 links
       wide. About four times the time (a day and a half) and four times
       the observers' memory, which reached 7.9 GB in one seed at gate 3
       on a host with 64 GB. The regressions no longer compare like with
       like. The open-ended run (`runs/open-1`, 200 places of gate 3's
       world) will say whether a world that size gets going at all.
     - c. **a, plus test worlds at full size,** as gate 3 did (R7):
       hand-placed replicators that spread by SEND, on a 1,000-place map
       with the design's 10-link band, to show bands, creep and heat at
       the size the design is for. They are evidence about the
       mechanism, not about life from static. The placed programs could
       be genomes gate 3's runs evolved (`runs/gate3-2/genomes`) instead
       of hand-written ones.
   - **Claude's lean:** c. It keeps the judged runs at a known cost and
     comparable with gates 2 and 3, and puts the geometry where it fits.
     The weakness is real: the 48-place runs use a narrower band than
     the default world, so they speak for the laws, not for the default
     world's numbers. If Damian wants life from static on a map with
     room for bands, that is b, and it is better asked after `open-1`
     has shown what a 200-place world does.

3. **How many sources, and whether dark ground is left to luck.**
   - The problem: sources land by area and are counted per 100 places
     (4.2), and "a region, or the whole world, can go without power for a
     long stretch". At 48 places on 5 to 14 landmasses, with water
     passing only a trickle (6.1), a seed with a few large sources would
     leave most of its landmasses dark for good. Gates 2 and 3 gave the
     origin 48 lit places a seed; stage 4 came in 18 of 20. Every place
     left dark, burnt or dim is a ticket the origin loses, and the
     regression needs stage 4 in 10 seeds.
   - Choices:
     - a. **Many small sources** (4.1 item 5: "the sources many and
       small, so that ecosystems sprawl"): about one for every four
       places, so most landmasses catch one, with the sizes still skewed.
       Lives long against the time to stage 4 (a mean of 20 million
       ticks), so most origins have a source that outlasts them, and a
       seed still sees sources die and open.
     - b. **Few and large,** the design's picture at full size: a strong
       source with a wide band. At 48 places that is 2 or 3 lit
       landmasses a seed.
     - c. **a, and every landmass with places guaranteed a source at the
       start.** It protects the origin regression, but it is a rule
       against "luck is part of the world".
   - **Claude's lean:** a, with luck left alone. If the pilot shows the
     origin starving, the count is the lever, agreed before the 20
     seeds.

4. **How much of a day the map spans.**
   - The problem: the day sweeps east to west and doesn't wrap (4.3,
     6.1), but the design doesn't say how far apart in time the map's two
     edges are. Gate 2 gave each place its own random phase, so at any
     moment about as much of a world was in daylight as in the dark.
   - Choices:
     - a. **A whole day across the map:** it is always noon somewhere,
       so the world's total supply stays about level, as at gates 2 and
       3. Neighbours share their time of day, and night crosses a
       48-place map at about 290 ticks a link, which a SEND can outrun.
     - b. **A fraction of a day** (a quarter, say): the whole map has
       night together, and nothing can migrate out of it.
     - c. **A setting,** with one of those as its default.
   - **Claude's lean:** c with a as the default. A night that takes the
     whole world at once is a bigger change from the world the earlier
     gates passed in than gate 4 needs to make.

5. **What the constant draws do as the voltage moves.**
   - The problem: 4.4 says every kind of device heats above its maximum.
     For a working core that follows from the law (draw ∝ V² × speed,
     with speed capped at the maximum). But memory's holding draw, the
     disk's, the network device's and a halted core's idle draw are
     constant watts (appendix C), so a swell wouldn't heat them at all,
     and they could never trip from overvoltage.
   - Choices:
     - a. **Leave them constant.** Only working cores heat from
       overvoltage; the rest heat through their place. Nothing changes
       from gate 3.
     - b. **Quote them at 1,000 mV and scale them with V².** One law for
       every device, and the solve behaves better, since a constant-watt
       load pulls more current as the voltage falls. But every place's
       draw changes from gate 3's (those 3.5 W fall 14% at 928 mV).
     - c. **Constant inside the range, and rising as (V ÷ maximum)² above
       it.** It gives 4.4's sentence a law and leaves gate 3's draws as
       they are wherever the voltage is in range.
   - **Claude's lean:** c.

6. **Who takes the low-voltage interrupt, and when it fires again.**
   - The problem: 9.5 says it "fires once, for the thread running on the
     core, as the core's voltage nears its cut-off". The voltage changes
     once a tick, and a turn is a quarter of a tick, so in a crowded
     place the thread on the core when the voltage enters the margin is
     rarely the one on it when the core cuts out, a tick or more later.
     Read as written, the warning mostly goes to the wrong thread. And
     "once" needs a rule for when it can fire again.
   - Choices:
     - a. **As written:** the thread on each core at the start of the
       tick the voltage enters the margin. Cheapest, and nearly useless
       where threads share a core.
     - b. **Every thread at the place with a handler gets it pending,**
       as arriving data does (9.5). But an interrupt wakes a thread from
       SLEEP 0, so it would wake sleepers, who were safe, onto cores
       about to cut out.
     - c. **Each thread takes it once, when it starts a quantum on a
       core whose voltage is within the margin of that core's cut-off.**
       It fires again for that thread only after the voltage has been
       back above the margin. Nothing is woken, it costs nothing until
       it fires, and the thread warned is one that is running. One bit
       more of thread state.
   - **Claude's lean:** c.

7. **Whether a heat trip of the reset region's memory ends in a cold
   start.**
   - The problem: 10.5 defines a cold start by voltage: power returning
     after it fell below the threshold of the memory device holding the
     reset region. A memory device tripped by heat is off too (7.6), and
     fades faster for being hot (5). The design doesn't say what happens
     when it cools.
   - Choices:
     - a. **It is a cold start:** the device that holds the reset region
       went off and came back, whatever turned it off. One rule. A place
       too hot for its memory reboots each time it cools.
     - b. **It isn't:** the device comes back with whatever fading left,
       and the saved-state rule (9.4) ends the threads it reached.
   - **Claude's lean:** a. A real machine that trips on heat shuts down
     and boots again.

8. **Swells and drop-outs before ramps exist.**
   - The problem: 4.3 says swells and drop-outs ramp, and 12.4 puts
     ramps at gate 5. Gate 2 built drop-outs with no ramp.
   - Choices: **a.** swells start and end at once at gate 4, as gate 2's
     drop-outs do, and ramps come at gate 5 for both; **b.** bring ramps
     forward to gate 4.
   - **Claude's lean:** a, as the design's own headings have it. A
     device's heat capacity already delays a trip, so a short swell can
     pass. The report counts what swells do, so gate 5 can compare.

9. **Whether a program can sense a device's temperature.**
   - The problem: heat trips follow each device's own temperature (5),
     but SENSE reads only the place's (A.4). A program can't see how
     close its core is to tripping.
   - Choices: **a.** leave it: the place's temperature is the warning
     (12.4); **b.** add a device reading. That
     changes how many readings SENSE has, and SENSE takes its argument
     modulo that number, so every evolved SENSE would mean something
     else: gate 3's regressions would no longer compare like with like.
   - **Claude's lean:** a.

10. **The forms the design leaves "details at build".** Each is fixed
    before the first run, since changing it changes the world. Proposed
    as a set, for one yes or for changes:
    - **The solve** (appendix C): voltages in microvolts and conductances
      in 2⁻²⁰ siemens, in 64-bit integers with 128-bit products, as the
      supply has now. Gauss–Seidel sweeps in place order on one host
      thread, started from the last tick's voltages, until no place
      moves by more than the tolerance or the sweep cap is reached. A
      capped solve leaves the voltages its last sweep gave, and is
      counted. Places then read their voltage rounded to the millivolt.
    - **Sources' schedules** are a function of the seed and the tick
      alone, as the supply's are now: each source's landing point, type,
      size, birth and lifetime, swings, drop-outs and swells. A source
      grows over the first tenth of its life and fades over the last
      tenth, by its capacity. Lifetimes are spread evenly on a log scale
      from a quarter of the mean to four times it.
    - **Landing:** a random land cell of the raster, weighted by the
      source-area power, joined to the nearest place. The point is kept,
      for gate 5's respawning (7.8).
    - **Water:** a proximity link's water width is the part of it that
      lies over water cells. Its resistance is its land resistance ×
      e^(width ÷ reach). A place's shore drain is a conductance to
      ground, the drain setting × the share of water among the raster
      cells bordering its catchment.
    - **Grid borders:** a proximity link whose ends lie on opposite sides
      of a step of the resistance field has its resistance raised by the
      border strength, in links.
    - **Heat between places:** a land link's conductance is the setting ÷
      its length in average links.
    - **Cooling:** a place's rate is the base rate × its cooling field
      value × (1 + the coast factor × its water share).
    - **Leakage** is counted from the outside temperature, so a cold
      world draws what gate 3's did.
    - **Fade when hot:** a cell's retention halves for each 10 °C its
      device is above 45 °C.
    - **A cooling device** adds cooling that rises evenly from nothing
      at its start temperature to its full rate at its top temperature,
      and its draw rises with it. It has a voltage range, and below its
      threshold it cools nothing.
    - **A heat trip** needs no off-time: the device comes back at its
      cool-down point. A thread ended by one has the cause "heat", so
      the report tells trips from cut-outs.

**The stack rule, decided on 2026-11-19, and what it means for gate 4.**
Damian chose it after gate 3's runs ("I'll go with your recommendation";
9.4, A.4): a live thread's stack is in use. A SPAWN into the stack of a
live thread of the caller's program starts another thread of that
program, never a gift, and a FREE of it fails. The switch
`os.stack_held` is on by default; off gives the rule gates 1 to 3 ran
under. Gate 4 starts from it.

- **Gate 4's regressions are the first 20-seed measure of the earlier
  gates under the new rule.** Every run so far was made under the old
  one. The case the rule changes was rare (48 of about a million threads
  in gate 3's saves), so Claude expects little change, but that isn't
  measured, and a world under the new rule parts from the old one tick
  for tick.
- **So a change in the regressions could be the rule's doing or the
  physics'.** One choice here is Damian's, about runs, not the design:
  - a. **a pilot of gate 3's world under the new rule** (`supply.model =
    place`, heat off, the rule on): seeds 1 to 10 to 2 million ticks,
    about half an hour, against gate 3's third pilot and `runs/gate3-2`
    at the same tick. It shows early whether the rule slows the origin;
  - b. **gate 3's 20 seeds again under the new rule,** about 9 hours and
    87 GB: a full baseline, so gate 4's regressions compare with a run
    that differs only in the physics;
  - c. **neither:** read gate 4's regressions as they come.
  - **Claude's lean:** a, and b only if a shows a difference.
  - **Agreed (2026-11-19):** a. If it shows a difference, b is put to
    Damian again with the pilot's numbers.
- **Sleeping through a cut is safer for it.** The gate 4 survival story
  (7.6, 12.4) is a thread asleep with its registers in its stack. Those
  bytes now always lie in memory the thread's program holds.
- **Heat trips and shared cut-outs make threads leave their cores more
  often,** and each time the OS writes the saved registers at the
  stack's address. Under the old rule that would have meant more writes
  into memory given away; under the new one it can't happen.
- **The new interrupt was never part of it.** A handler's frame is
  pushed through the owner check.
- **The equivalence check** (6) runs with the rule off, since
  `runs/gate3-2` was made under the old one.

## 3. The gate 4 world

- **Gate 3's world,** with every rule that passed: 48 places a seed (gap
  2), the map, the network, the 24 call slots, the 4 KB reset region,
  the raised noise floors, fade, cut-outs, the cold start, the widened
  saved-state rule, idle runs not SENDing, and arrivals holding at most
  half a place's cores.
- **And the stack rule decided since** (9.4; 2, above): a live thread's
  stack is in use. No 20-seed run has been made under it yet.
- **Sources on the map** (4.2) in place of a source at each place: a
  voltage behind an internal resistance, landing by area, of a type from
  the catalogue (steady, daylight, gusty), with skewed sizes, growing,
  holding and fading on a schedule drawn before the run.
- **The day sweeping the map** (4.3, gap 4), swings, drop-outs by source,
  and swells.
- **Proximity links** (6.1): the Gabriel graph over all the places, each
  link with a resistance from its length and the resistance field. Links
  over water pass a trickle, places on the shore pay a drain, and grid
  borders thin the ties between regions (6.2).
- **The voltage solve** (4.1, appendix C): each tick, every device as a
  conductance from the last tick, every source behind its resistance,
  one solve for every place's voltage. A part of the map with no source
  sits at 0 V.
- **Heat** (5): a temperature for every device and every place; heat
  from devices, from links' loss and from each source's own loss;
  cooling to the outside by Newton's law, better on coasts and by the
  cooling field; heat spreading along land links; leakage; heat trips and
  cool-downs; memory fading faster when hot. The world starts cold.
- **Cooling devices** (7.7) at some places.
- **The low-voltage interrupt** (9.5, 12.4, gap 6).
- **What follows from those without a rule:** load rejection (12.2),
  the top edge's errors (a place above a device's maximum makes wrong
  results at the edge's top rate; no place reached it before), and
  SENSE's voltage and temperature, of a place and of its neighbours,
  reading something that differs from place to place.
- **What goes:** gate 2's source at each place, its own day and its own
  drop-outs. A place is no longer given a source sized to its hardware:
  a place with 8 P cores draws up to 84 W from the same links as its
  neighbours.
- **Switches** (2: welcome for experiments):
  - `supply.model`: `sources` (the default from gate 4) or `place` (gate
    2's and 3's supply);
  - `heat.on`: on (the default) or off (a constant temperature, as
    before);
  - `os.stack_held`, built already: on (the default) or off (the stack
    rule gates 1 to 3 ran under).
- **With `supply.model = place`, heat off and `os.stack_held` off,** gate
  4's engine gives gate 3's world exactly. That is the regression check
  that runs throughout (6).
- **Left for gate 5:** wear of devices and of links that carry current
  (7.4); visits and respawning places (7.8); strikes and storms (12.2,
  12.3); the other regional events (swell, brownout and heat-wave
  patches, network outages); ramps and false alarms (12.4); link cuts,
  outages and rerouting (11.6); rowhammer (9.8). Until then cooling
  devices never fail, and sources and swells come without warning.
- **Parked:** the GPU and NPU (R20).

## 4. The code: what changes

Gate 4 builds on gate 3's engine. Nothing new from outside. The solver,
the sources and heat are new code: Nova 1 had no circuit.

| Area | Files | What changes |
|---|---|---|
| Settings | `keys.inc` | The source catalogue and counts, the day's span, swells, link resistance and its field, water's reach and drain, grid borders, heat (capacities, heatsink rates, limits, cooling, spread, leakage), cooling devices, the low-voltage margin, the solve's tolerance and cap, the two switches |
| Map | `map.*`, `field.*` | Proximity links with their water widths, each place's catchment and water share, the resistance and cooling fields and one for cooling devices, grid borders. The data links, positions and ids stay as gate 3 built them |
| Sources | new `sources.*` | The catalogue, and each source's schedule as a function of the seed and the tick. `supply.*` stays for `supply.model = place` |
| Solve | new `solve.*` | Appendix C's steps 2 and 3 for the whole map, and what each link, drain and source lost |
| Heat | new `heat.*`, `power.*`, `place.cpp` | Device and place temperatures, leakage, heat trips, cooling devices; the constant draws above the maximum (gap 5) |
| Memory | `noise.cpp` | Fade by temperature as well as voltage |
| OS | `os.cpp`, `sched.cpp` | Raising the low-voltage interrupt (its delivery is built already); the death cause "heat" |
| World | `world.cpp` | The tick's order (appendix C): the sources and the solve before the places run, heat between places after the barrier. Today each place works out its own voltage inside its own tick |
| Observers | `observe.cpp`, `origin.cpp` | Ground by place and window, the creep and heat readings, sources' states, cut-outs spreading, the interrupt's use; the report |
| Snapshots | `snapshot.*` | Format 4: temperatures, trip states, each thread's interrupt bit. Sources need nothing saved |
| Views | `app/ui`, `view_cmd.cpp` | 13.4's power and heat colourings and overlays |

## 5. The order of work

Each step ends with tests that pass before the next starts. "Done when"
names the check.

**Step 1. The measures, on gate 3's world.** Built first, as each gate's
measure was, so it can be checked before the world changes.

- For each place and window: its core time lost to cut-outs and to heat
  trips, the time its reset region's memory was off, its ground (gap 1),
  its programs at the window's start and its births.
- The report's ground section, by seed and by OS kind, since strict
  places hold no life whatever their ground.
- Done when: a test world with places held hot, dim and fair by hand
  labels each as expected; and gate 3's world, rerun with the measure
  on and `os.stack_held` off, reproduces `runs/gate3-2`'s samples for
  two seeds to 2 million ticks, with no hot ground at all.

**Step 2. The power map (6.1, 6.2).**

- Proximity links, water widths, catchments and water shares, the
  resistance, cooling and cooling-device fields, and grid borders, all
  worked out once from the seed.
- The map's report gains: proximity links a place, how many cross water,
  how many differ from the data links on land, and the borders crossed.
- Done when: the map's tests pass (the Gabriel rule over all places, no
  crossings, every data link on land also a proximity link or counted as
  a difference, a link wholly over land with no water width); and for
  the 20 seeds, positions, ids, data links and bridges are gate 3's, bit
  for bit, so gate 3's saves still open.

**Step 3. Sources and their schedules (4.2, 4.3).**

- The catalogue's types, the skewed sizes, landing by area, lives, the
  day's sweep by position, swings, drop-outs and swells.
- Sources placed by hand in test worlds.
- Done when: two worlds with the same seed and different programs have
  the same sources in the same states at every tick (principle 3); the
  long-run count matches the chance × the lifetime; sources land in
  proportion to catchment area; and the day's phase at a place follows
  its east–west position.

**Step 4. The voltage solve (4.1, appendix C).**

- Conductances from the last tick, sources as Norton equivalents, water
  links and shore drains, the solve, and each place's voltage for the
  tick. Each link's, drain's and source's loss, kept for heat.
- The constant draws above the maximum (gap 5).
- Done when:
  - one source at one place gives gate 2's voltages to the millivolt;
  - a chain and a small grid match values worked by hand;
  - random maps match a solve in doubles within the tolerance;
  - the power out of the sources equals the devices' draw plus every
    loss, within the tolerance;
  - a landmass with no source and no crossing sits at 0 V, and one
    across a strait gets a trickle that falls off as 6.1 says;
  - the same world gives the same voltages on 1, 2 and 24 host threads;
  - in a test world, a crowd cut off by hand raises its neighbours'
    voltage (12.2's load rejection).

**Step 5. Heat (5, appendix C).**

- Device and place temperatures, the heat made (devices, links, sources),
  cooling to the outside, the coast and the cooling field, spread along
  land links, leakage, heat trips and cool-downs, fade when hot, the
  cold start after a trip (gap 7).
- Done when:
  - a device's and a place's warming to a steady draw follows the
    equations step by step, and their resting temperatures match values
    worked by hand;
  - at rest, the heat leaving the world equals the power the sources
    give, less what the sea takes;
  - no step overshoots with rates set absurdly high (the clamp);
  - a core past its limit trips, its running thread ends with the cause
    "heat", and it comes back at its cool-down point;
  - a hot memory device loses the share a cold one would in half the
    time for each 10 °C;
  - with heat on and every source far below the maxima, a quiet world
    stays within a degree of the outside.

**Step 6. Cooling devices (7.7).**

- Their field and counts, their cooling and draw by temperature, their
  voltage range.
- Done when: a place with one rests cooler than its twin without, by the
  amount worked by hand, and draws more while it does; and below its
  threshold it cools nothing.

**Step 7. The low-voltage interrupt (9.5, 12.4, gap 6).**

- Raising it, each thread's bit, and the handler's reading of the
  voltage (built already).
- The observers' part: handlers registered, interrupts taken, and the
  threads that slept within a tick of taking one and outlived the cut
  (13.3's Sleeper).
- Done when: in a test world through a slow dip, a thread with a handler
  that SLEEPs survives the cut and a twin without one ends; the
  interrupt fires once for a dip and again for the next; and nothing
  asleep is woken by it.

**Step 8. Determinism, snapshots and cost.**

- Temperatures, trip states and the interrupt bits in snapshots and the
  world hash.
- The fuzz world on a map with sources, swells and heat, the invariant
  checker after every tick in the checked build, the power balance among
  its checks.
- The benchmark's gate 4 part: the solve and heat per tick at 48, 1,000
  and 10,000 places, with the sweeps a tick and the capped solves.
- Done when: the same seed gives the same hash on 1, 2 and 24 host
  threads, through a snapshot taken mid-swell, mid-trip and with a
  source fading, and with observers on or off.

**Step 9. The gate 4 views (13.4).**

- The map's colourings: voltage, temperature, draw and cooling. Overlays:
  sources by type and state, and grid borders. Proximity links drawn by
  what they carry, with the power flowing.
- The inspector: each device's temperature and whether it is tripped;
  the place's cooling.
- Charts: power in and out, heat, trips and cut-outs.
- Done when: a headless render matches a stored image pixel for pixel,
  and the views open on a snapshot taken mid-swell.

**Step 10. The test worlds at full size (R7; gap 2).**

- **The band:** a stiff source and a soft one of the same voltage, each
  on its own 1,000-place map, with replicators placed by hand that SEND
  themselves to their neighbours. Reported: where they last, by links
  from the source, as the load grows; the inner edge's place over time
  (creep).
- **Heat:** a place filled by hand under a steady source and under a
  daylight one. Reported: whether its temperature settles, follows the
  day or cycles through trips, at three cooling rates.
- **Water:** a strait and an island. Reported: the voltage each side.
- **The sleeper:** a replicator that sleeps on the interrupt beside one
  that doesn't, through nights. Reported: who is left.
- **Load rejection:** a crowd at a soft source and at a stiff one, cut
  off at once. Reported: how far the rise spreads and what trips.
- Done when: each runs without errors and reports its numbers.

**Step 11. The gate 4 runs.**

**Changed by Damian on 2026-11-20: gate 4 has no 20-seed run of its
own.** He first said "I want to hold off on doing the 20 seed run until
both gates are fully built", and later that day: "we only need a single
20 seed test that is a combined gate 4+5. I don't think we need 2." So:

- **The numbers and the pilot stay here,** as below. The pilot also
  gives gate 5 its numbers marked "(gate 4)" and the baseline for gate
  5's pilot (`build-plan-gate5.md` 7).
- **The pilot may run beside `runs/open-2`.** Asked whether it may,
  small and at low priority, Damian said: "Yes, beside open-2". The
  conditions he was told would apply: below normal priority; a few
  seeds at a time, not all ten at once; the first seeds' memory
  measured before more are added; free memory kept well above
  `runs/open-2`'s 6 GB safety stop; `runs/open-2` and its exe left
  alone.
- **Seeds 1 to 20 move to the one combined run** of gate 4's and gate
  5's world together, made with gate 5's build after gate 5 is built
  (`build-plan-gate5.md` step 15). Gate 4's pass mark (7), its three
  regressions and its two named checks are judged there, from that
  run's ground record. The record's columns for hot, dim and fair
  ground (step 1) stay as they are for it. From 2026-11-20 the mark's
  judged reading names ground by what a full place gets there, worked
  from the seed after the run; the record's own naming is reported
  beside it (7; 11, "The pass mark as changed").
- **This step is done** when the numbers are agreed and the pilot has
  run to its ticks without errors. The hand check of one seed's report
  moves with the 20 seeds. **Done on 2026-11-20** (11, "Step 11 done").

What follows is the step as first planned.

- **The numbers worked again first:** the resistances and heat rates in
  7 are given by what they should do. Their values are worked from the
  draws and temperatures steps 4 to 6 measure, and put to Damian before
  any run, as gate 2's day was.
- **The stack rule's pilot** (2, if Damian chooses it): gate 3's world
  with the new rule on, seeds 1 to 10 to 2 million ticks. It can run as
  soon as the rule is built, before any of gate 4 is.
- **A pilot:** seeds 1 to 10 to 2 million ticks, against gate 3's third
  pilot (copying began in 6 of 10), `runs/gate3-2` at the same tick, and
  the stack rule's pilot.
- **Then seeds 1 to 20** under gate 4's world, at gate 3's budget,
  reporting the ground measure against 7's pass mark, the three
  regressions, the two named checks, and what 1 lists as watched.
- Done when: they run to their budgets without errors, and the report
  reads correctly against a hand check of one seed.

## 6. Tests that run throughout

- **Unit tests** for each step, as named in its "done when".
- **The gate 3 equivalence test:** with `supply.model = place`, heat off
  and `os.stack_held` off, gate 4's engine gives `runs/gate3-2`'s
  samples for a seed to 1.6 million ticks. It runs on every engine
  change, so gate 3's result can't drift unnoticed.
- **The supply independence test,** gate 2's: the sources are the same
  whatever the programs do (principle 3).
- **The power balance:** what the sources give is what the world draws
  and loses, checked every tick in the checked build.
- **Test worlds** with hand-placed programs and sources (R7).
- **The fuzz world,** on a map, with traffic, sources, swells and heat.
- **Determinism checks** on every engine change: the same hash on 1, 2
  and 24 threads, through a snapshot, and with observers on or off.
- **The checked build and the fast build** run the same tests.

## 7. Numbers to fix before the first gate 4 run

**Agreed (2026-11-19):** the pass mark, with gap 1's kinds of ground, and
what the gaps settle: 48 places a seed at gate 3's budget, sources many
and small, a whole day across the map, swells with no ramp. The values
in the other rows are placeholders still.

**Agreed (2026-11-20):** the four rows that step 11 worked again from
measurements, each by its own rule: the heatsink rates, the cores' heat
capacities with them, the source sizes and the link resistance (11,
"Step 11, first part", its set d). Damian's words, relayed by the
map-design session: "rules only stays closer to my original vision".
The sources' stiffness stays as its row has it. The rows below carry
the agreed values.

**Changed (2026-11-20), before any judged run:** the pass mark's naming
of ground and its direction, by Damian through the map-design session
("Either direction"). The rows for ground and the pass mark carry it,
and 11's "The pass mark as changed" has his words, the reason, and
that the new wording is an easier test than the first.

All are placeholders, proposed here and to be agreed with Damian before
the first run, as 14.1 requires a pass mark set in advance. Changing
them afterwards means rerunning. Where a row gives what a number should
do rather than its value, the value is worked at step 11 from measured
draws and temperatures, and agreed then. The rows follow the leans in
2, and change with Damian's choices there.

| Number | Proposed | Why |
|---|---|---|
| Seeds and budget | Seeds 1 to 20, 10¹² instructions each, 48 places a seed | Gate 3's, so the regressions compare like with like (gap 2) |
| Sources | 12 a seed at the start (25 per 100 places); new ones at the chance that keeps that average; lives of 20 million ticks on average, 5 to 80 million | Many and small (gap 3). Stage 4 took 1.8 to 18.3 million ticks at gate 3, and seeds ran 17 to 100 million, so most origins have a source that outlasts them, and each seed still sees ten or more sources die and open |
| Source types | Steady, a quarter of them: 1,050 mV, stiff, ignoring the day, swings of 5%. Daylight, half: 1,250 mV, night a fifth of noon, swings of 25% over about 10 days. Gusty, a quarter: 1,350 mV, ignoring the day, swings of 50% over about a day, with swells | 4.2's starting catalogue without the strike. Steady but weak, or strong but erratic. The daylight type's day and swings are gate 2's |
| Source sizes | Rated for the full draw of 2 to 4 average places (100 to 200 W) for most, 10 places or more (500 W) for one in ten: 100 to 800 W | Skewed (4.2). Agreed 2026-11-20, from the full draw measured at 1,000 mV with heat on, 45 to 47 W a place (11, step 11). The placeholder was 50 to 400 W, from gate 3's 23 W at its sagged voltage |
| Stiffness | A source's sag at its rated load: 20 to 50 mV for steady ones, 50 to 300 mV for the others, evenly on a log scale | Soft and stiff sources of each size, which the creep check compares (gap 1) |
| Day | 2,000 ticks; one whole day across the map's width | Appendix E's day; gap 4 |
| Drop-outs | One per source per million ticks; lengths and depths as gate 2's (10 to 1,000 ticks, half to nothing) | Gate 2's, now by source: a drop-out takes a neighbourhood, not a place |
| Swells | Gusty sources only: one per source per 2 million ticks, 1.1 to 1.8 times the voltage, 10 to 1,000 ticks, starting and ending at once | 4.3's range; gap 8 |
| Link resistance | 2.5 mΩ for an average land link: it drops about 75 mV carrying 30 W at 1,000 mV, the draw of a place about two thirds busy; the field spreads it from half to twice that | A band about 3 links wide for a 450 mV range, which fits a 48-place landmass (gap 2). The default world's 10-link band is used in step 10's test worlds. Agreed 2026-11-20: the placeholder was 5 mΩ, from a typical draw of 15 W, and the draws measured are about twice those assumed (11, step 11) |
| Grid borders | Strength 10 links; off in the 48-place runs, on in the test worlds | 6.2 spaces borders 20 to 30 links apart, three of these maps wide. Water already divides them |
| Water | Reach: one average link length. Shore drain: 0.3 W at 1,000 mV for a place wholly on the shore | 6.1's defaults; an E core's idle draw is the "small device" |
| Outside temperature | 25 °C | The constant temperature so far |
| Heat limits | Cores 100 °C, memory 85 °C, disks 70 °C, the network device and cooling devices 85 °C; the cool-down point 10 °C below each | 7.2's real parts; the last two are Claude's placeholders |
| Heatsink rates | Set so that a core working all tick, in a place at 40 °C, trips about 100 mV under its maximum (0.98 W a degree for a P core, 0.124 for an E core, with the leakage counted); 0.1 W a degree for the other devices | So the top of each range is a burn zone for a busy core and not for an idle one (4.4: the sweet spot sits below the maximum). Agreed 2026-11-20: the placeholders, 0.55 and 0.07, left the leakage out, and a busy core tripped about 210 mV under its maximum (11, step 11) |
| Place cooling | Set so that the largest place, at full draw at 1,000 mV with average cooling, rests just under the disks' limit (about 2.5 W a degree); the cooling field spreads it from half to twice that; a place wholly on the coast cools twice as well again | 5's tuning rule: only places close to strong sources, or poorly cooled, pass the limits. A typical place (15 W) then rests about 6 °C over the outside |
| Heat capacities | A device follows its draw within about 20 ticks, a place within about 500 (a quarter of a day). With the heatsinks agreed, a P core's is 19.6 J a degree and an E core's 2.48 | Placeholders. They decide much of "settles or oscillates", so the report states them |
| Leakage | 1% more draw for each degree above the outside | Enough to tip a crowded, poorly cooled place, not a typical one (5) |
| Heat between places | 0.25 W a degree for an average land link | A tenth of a place's own cooling: a hot neighbour costs something (5) |
| Fade when hot | Retention halves for each 10 °C above 45 °C | Real DRAM's refresh doubles above 85 °C (7.2) |
| Cooling devices | At a third of the places, by their own field; cooling rising from nothing at 40 °C to the place's own rate again at 80 °C, drawing up to 3 W; range 600 to 1,300 mV | 7.7: more hardware held, for a load that grows in the hot hours |
| Low-voltage margin | 30 mV above a core's cut-off | Inside the 50 mV restart gap; a tick or more of warning on a slow sag |
| The solve | Tolerance 0.1 mV; at most 64 sweeps a tick | Finer than the millivolt the places read; the benchmark (step 8) says what the cap costs |
| Ground | Hot, dim or fair, by a quarter of a window's core time (gap 1b); windows of 200,000 ticks. For the mark, from 2026-11-20, a place is named by what a full place gets there: its cores kept busy with nothing else living, worked from the seed and its sources, and worked again as sources open and end. The ground record keeps the first naming, by what happened to the place in the window, and it is reported beside | Gate 2's window. A crowd heats its own place and sags its own source, so the first naming follows the load (11, "The pass mark and ground that life makes") |
| **Pass mark** | As changed by Damian on 2026-11-20 ("Either direction"). Over the seeds that reach stage 4, in lax places, with ground named by what a full place gets there (the row above): fair place-windows and hot ones differ by at least a factor of two in the programs they start with, whichever way, and fair and dim ones likewise (both must hold), with at least 100 place-windows of each of the three kinds, from at least 5 seeds. The direction of each is reported. Reported beside it, not judged: the mark as first worded and named (ground by what happened to the place in the window; fair with at least twice the programs of hot and of dim), and the reading on untouched ground. And the regressions hold: stage 4 in at least 10 of the 20 seeds; gate 2's turnover mark (9 in 10 counted windows and 9 in 10 crowded ones, on 100 crowded windows from 5 seeds); and gate 3's spread mark (a lineage settles a place beyond the one it began in, in at least 5 seeds). The two named checks are reported, not judged | Twice is a difference no reader would call noise, across at least 5 seeds. The floor stops a world with no hot or no dim ground passing: short of it, the answer is that the physics as numbered made no such ground. Lax places only, because strict places hold no life on any ground (gate 3). The regressions are the earlier gates' marks |

## 8. Risks for this gate

- **Gate 4 may lose the origin (Claude's prediction: the likeliest way to
  fail).** Gates 2 and 3 gave every place a source sized to it. Now some
  places are dark, some burnt, and a drop-out takes a neighbourhood at
  once. Fewer fair, lax places means fewer chances for copying to begin
  inside the budget. The pilot shows it early; the levers are the
  sources' count and strength, agreed before the 20 seeds.
- **Tuning could choose the answer.** Gate 4 adds about 40 settings, and
  the sources' and cooling's numbers decide how much ground is hot or
  dim. They and the pass mark are fixed before the first run, and the
  report states the values a run used.
- **48 places has no room for bands** (gap 2). The judged runs show
  ground by place; the geometry is in the test worlds, which aren't life
  from static.
- **Where life lives is already uneven.** Strict places hold none of it,
  and fields put like places together. The ground measure is taken in
  lax places only, and the report gives it by OS kind, but a field edge
  that happens to follow a source's reach could still be read as the
  physics.
- **The one-tick lag can flicker across places.** At gate 2 a busy place
  under a deep drop-out flipped between 821 and 926 mV until the
  off-time caught it (`build-plan-gate2.md` 11, step 3). With shared
  sources one place's cut-out lifts its neighbours, so the flicker can
  travel. That is load rejection (12.2), and the off-time and restart
  gap are the design's answer (4.1), but it may be constant, not rare.
- **Heat may only follow the day.** If a place warms and cools much
  faster than a day, its temperature just tracks the supply, and
  "settles or oscillates" is answered by the heat capacities chosen in
  7. The report separates the day's swing from the rest for this reason.
- **Outages are now regional.** A source's drop-out cold-starts every
  place that depends on it, and gate 3 found most copies sent are lost
  at the door. A wiped neighbourhood may stay empty for a long time.
  Boot blocks (10.5) are the design's answer; the report counts births
  from them.
- **Thread-heavy places are a commons problem across places.** A place
  thinned to one program with hundreds of threads sits at full draw, and
  now sags and heats its neighbours too. The physics may break such
  holds or spread their cost; either is a finding.
- **The solve's numbers span many orders of magnitude** (appendix C):
  water links fall off exponentially. A link too weak to matter is
  dropped from the solve at setup, below a set conductance, so the
  integers keep their precision. The check against doubles (step 4)
  covers it.
- **A capped solve is physics.** If the cap is hit often, the voltages
  depend on the cap. The benchmark and every report count capped solves.
- **Dark places stretch the runs.** A dark place runs nothing, so a seed
  makes fewer instructions a tick and takes more ticks to spend its
  budget. More seeds may end at the 10⁸-tick cap, as 3 did at gate 3.
- **The stack rule is new to the regressions** (2). Gate 4's run is the
  first of 20 seeds under it, so a regression that falls short could be
  the rule's doing and be read as the physics'. The stack rule's pilot
  is there to tell them apart early.

## 9. Size, and what the runs would cost

**Size.** Claude's estimate, not a measure: about 500 lines for the power
map, 500 for the sources, 500 for the solve, 700 for heat and cooling
devices, 150 for the interrupt, 800 for the observers and report, 600
for the views, and 2,500 lines of tests.

**The runs,** from gate 3's measured figures (`runs/gate3-2`: 20 seeds,
10 side by side on 2 host threads each, on the 24-thread host with 64 GB
of memory):

- **Time.**
  - Gate 3's 20 seeds took 8 hours 47 minutes. Its slowest seeds were
    the three that ran all 10⁸ ticks, at 4.6 to 6.3 hours each.
  - The solve and heat should add little (Claude's estimate, not
    measured: a few microseconds a tick at 48 places, against ticks of
    125 to 2,900 µs in gate 3's pilots). Step 8's benchmark measures it.
  - What may add more is dark ground: seeds that reach the tick cap
    instead of the budget. If every seed did, the 20 would take about 13
    hours. So: **9 to 13 hours for the 20 seeds.**
  - The pilot (10 seeds to 2 million ticks) took about half an hour at
    gate 3. The stack rule's pilot (2) is another half hour.
  - Step 10's test worlds: gate 3's 1,000-place world ran 367 ticks a
    second on 24 host threads. A world of 200,000 ticks (100 days) is
    about 10 minutes, and the set about an hour or two.
  - Option b of gap 2 (200 places, four times the budget) would be about
    a day and a half, more if the tick cap is raised with it.
- **Memory.** A 48-place world is small (about 56 MB of places). The
  observers are the weight: up to 7.9 GB in one seed late in gate 3's
  run, and ten seeds ran side by side within the host's 64 GB. Gate 4's
  measures add three counters a place and a few numbers a source.
- **Disk.** `runs/gate3-2` is 87 GB, 64 of it the saves (2 kept a seed).
  Gate 4's run would be about the same, under `runs/` on drive E, which
  has about 275 GB free. Nothing large goes to drive C (8 GB free).
- **Not beside the open-ended run.** `runs/open-1` uses most of the
  host's threads and about 10 GB. The pilot, the test worlds and the 20
  seeds each need the host, so each waits for Damian's word on
  `open-1`. Building and the unit tests don't. (Since 2026-11-19, 13:47,
  the open-ended run is `runs/open-2`: Damian had it begun again from
  tick 0 on the newest build, and `open-1` is stopped and kept. What is
  said of `open-1` here now holds for `open-2`.)

## 10. After gate 4

- **A pass:** gate 5 (does it stay interesting?): wear and visits,
  respawning places, regional events and strikes, ramps, outages and
  rerouting, rowhammer. And the question gate 3 left open, whether life
  arises and spreads on a full-size map, which gate 4's bands make
  sharper.
- **A fail on the regressions:** the physics as numbered is too harsh
  for the origin or for spread. The levers are Damian's: the sources'
  count and strength, the cooling, or a design review.
- **A fail on the ground measure with the regressions holding:** life
  lives with the physics and isn't shaped by it, at these numbers. Then
  the question is whether stronger numbers shape it, or whether sleeping
  threads make ground matter less than the design expects.
- Either way, the result, the numbers used and the benchmark go into
  `design-history.md`'s log.

## 11. Results so far

This section fills in step by step, as the earlier plans' did.

**Step 1: the measures, on gate 3's world. Built and checked
(2026-11-19).**

- **What was built:**
  - Each place keeps two more counters beside its core-ticks cut out:
    the core-ticks its cores spent off by a heat trip (nothing feeds it
    until step 5), and the ticks the memory holding its reset region was
    off.
  - The observer reads them as each of a place's turnover windows ends,
    and records every place-window by its ground, with the programs it
    started with and its births. The records go to `seed-N.ground` with
    the others, and to `seed-N-ground.csv` beside the turnover windows.
  - The report has a ground section: a row a seed for its lax places,
    then lax and strict places over all seeds, and the lax places of the
    seeds that reached stage 4, which the mark is judged on.
  - Gate 4's mark is read out after gate 3's, from three new settings
    with the agreed values (`origin.ground_ratio` 2, `origin.ground_min`
    100, `origin.ground_seeds` 5).
- **Details fixed at build** (none changes what was agreed):
  - A place's core time is its cores times the window's ticks, P and E
    cores alike.
  - "Over a quarter" means more than a quarter, not a quarter exactly.
  - A window that is hot and dim at once takes the name of the larger
    loss of core time (hot, if they are the same), and the report counts
    how many were both.
  - Every place-window is recorded, with programs or none, but only if
    the observer watched it from its first tick.
- **Saves:** the observers' memory and the places' counters each gained a
  format. The formats before them are still read, so a run saved by an
  earlier build carries on: its ground starts at the first whole window
  after the save. Checked with a small world saved by the build
  `runs/open-1` uses and carried on by this one (`runs/gate4-step1/old`).
- **The checks:**
  - All 31 test suites pass. New in the observers' suite: the ground
    names worked by hand; a test world with one place left alone, one
    held hot, one held dim by cut-outs and one by its memory off, each
    named as expected in every window, with the programs it had; a world
    under a source with deep nights, where each window agrees with the
    place's devices read tick by tick (17 dim, 47 fair); and the record
    read back from a save, and begun afresh from a save made before it.
  - Gate 3's world, rerun with the measure on and `os.stack_held` off,
    seeds 19 and 20 to 2 million ticks (`runs/gate4-step1`; the two seeds
    that reached stage 4 by then in gate 3's run). All 20,000 sample
    rows of each seed are `runs/gate3-2`'s, every column; so are the
    turnover windows that had ended, and the records files for voltages,
    samples and windows, byte for byte. The stages came at the same
    ticks. It took 8 minutes 39 seconds on 4 host threads, at low
    priority beside `runs/open-1`.
  - **No hot ground, as expected, and no dim ground either:** all 960
    place-windows were fair. The cores were cut out for 2.3% and 2.5% of
    their time, against 21% of the P cores' ticks over gate 3's whole
    run: the early world draws little. The hardest-hit window lost 22%
    of its core time, just under dim's quarter: seed 20's place 39, in
    the window its lineage reached stage 4.
- **For step 5 to settle:** a heat trip of the memory holding the reset
  region. The counter counts that memory off for any reason, and the
  agreed wording puts "memory off" under dim. Once heat can trip it,
  whether that time makes a window hot or dim is Damian's to choose.

**Step 2: the power map. Built and checked (2026-11-19).**

- **What was built**, all worked out once from the seed, beside the data
  map and changing none of it:
  - **Proximity links:** the Gabriel graph over all the places, from
    their triangulation, as each landmass's data links are made. A test
    map's (`grid`, `ring`) are its data links.
  - **Each link's water width,** walked exactly through the raster; the
    data link on land beside it, if there is one; and whether it crosses
    a grid border.
  - **Each link's resistance,** in average links of the usual
    resistance: its length × a value a length from the resistance field
    × e^(water width ÷ reach), and the border strength more if it
    crosses a border. What a usual link is in ohms is step 4's setting.
  - **Catchments:** every land cell belongs to the place nearest it.
    Each place has its catchment's size, and its water share: the share
    of water among the cells bordering the catchment.
  - **Each place's cooling,** over the usual, from the cooling field.
    The field for cooling devices is drawn, and step 6 deals them by it.
  - Three new fields (`resistance`, `cooling`, `coolers`), each with its
    edge and sharpness, and six new settings: `map.resistance.min` and
    `.max` (0.5 and 2), `map.water.reach` (1 link),
    `map.border.strength` (10 links; 0 is no borders), and
    `map.cooling.min` and `.max` (0.5 and 2).
  - The report has a section, power maps, after maps: a line a seed
    with its proximity links a place, those over water, those that are
    power only, the data links on land with no proximity link, the
    borders crossed, the range of resistance, and the water shares.
- **Details fixed at build** (none changes what was agreed):
  - **A catchment is the land nearest a place, whatever the landmass,**
    as 6.1 words it (a source lands on land and joins the nearest
    place). So land with no place on it, an islet, belongs to the
    nearest place across the water. Past the map's edge counts as
    water.
  - **A place can have no catchment,** when it sits so close to another
    that no cell's middle is nearest it: none at 48 or 1,000 places, 4
    of 10,000. No source lands at it, and its water share is read round
    its own cell.
  - **A bridge and a proximity link between the same two places are two
    things** (6.1: a bridge carries no power). The proximity link there
    counts as power only.
  - **A link on one landmass can pass over a bay.** It is a data link
    and a proximity link both, and it has a water width, so power pays
    for the water. About half the links with water under them are of
    this kind.
  - **The resistance a length** is dealt by the field's rank among the
    proximity links, evenly on a log scale from the least to the most,
    as bandwidth is. Cooling is dealt the same way among the places.
  - **A border** is where the resistance field's second layer changes
    sign: a link crosses one when its two ends are on different sides.
    A field with no edge strength has no borders.
- **Saves:** nothing in a save changed. The data map's hash covers what
  it did, and the power map has a hash of its own.
- **The checks:**
  - All 31 test suites pass. New in the map's suite, on maps of 1 to
    10,000 places: the Gabriel rule over all places against every pair,
    by brute force (to 1,000 places); no two proximity links cross; the
    links are in one piece; every data link on land is a proximity link
    or is counted as
    a difference, and each difference has a place of another landmass
    in its circle; each water width against a second way of walking the
    raster and against 2,000 samples along the link, with a link wholly
    over land at exactly none and a link between landmasses never at
    none; each resistance and border worked again from 6.1 and 6.2;
    every land cell against the nearest place by brute force; each
    water share counted cell by cell; the settings (a longer reach, no
    borders, a field with no step, one resistance everywhere, a spread
    the wrong way round refused); and the test maps.
  - **Gate 3's 20 maps, bit for bit.** Before the map's code was
    touched, each seed's map hash was taken (positions, ids, landmasses,
    and every data link and bridge with its length, delay and
    bandwidth), and each seed's map line was checked against
    `runs/gate3-2`'s report. A test now holds the 20 hashes, and they
    are unchanged.
  - **The program itself** (`runs/gate4-step2`, about a minute at low
    priority): gate 3's world, all 20 seeds to tick 20,000. Each seed's
    map line is `runs/gate3-2`'s, and all 200 sample rows of each seed
    are too, every column.
  - **A save still opens:** the small world of step 1's check, saved by
    the build `runs/open-1` uses, was carried on by this build. Its
    records, its windows and the worlds it saved are those the build
    before the power map made, but for the folder's name in the saved
    settings.
  - The settings window's stored picture was renewed: twelve more
    settings shorten its scroll bar by a few pixels, and nothing else
    in the picture changed.
- **How gate 3's 20 maps came out** (48 places each):
  - 78 to 90 proximity links, 3.5 a place on average, against 3.0 for
    the data links.
  - 31 to 47 of them pass over water, 45% of all, and 12 to 32 are
    power only. 4 data links on land, in 2 seeds, have no proximity
    link.
  - Water is 47 to 59% of what borders a catchment. At this size nearly
    every place is on a coast, so the shore drain and the coast's
    cooling reach nearly all of them.
  - A link's resistance runs from about 0.3 of a usual link to between
    30 and 380 (with borders on, at the default strength, which the
    48-place runs won't have).
- **For Damian, before borders are switched on (step 10):** with the
  fields' one scale (`map.field.scale`, 3), the resistance field's steps
  come far closer than 6.2's 20 to 30 links. At 1,000 places 11% of the
  proximity links cross one (251 of 2,213), which is a border about
  every 6 links; at 48 places 38% do. The 48-place runs have borders
  off, as agreed, so nothing here waits on it. Wider grids would need
  the resistance field's step, or all of that field, on a scale of its
  own. The choices and a lean come with step 10.
- **For step 5 to settle:** whether a link over a bay carries heat.
  6.1 says heat goes over land and not across water; the link's water
  width is there to decide by.

**Steps 3 to 10 were built on 2026-11-20,** after Damian's word that day
("go ahead and give me game plans for building out gates 4 and 5 and
anything else that still needs finishing. Lets go ahead and get those
going"). Nothing below was run as a pilot or as the 20 seeds: those wait
for step 11's numbers. `runs/open-2` ran beside all of it, so every run
here was kept small, below normal priority, on 8 host threads at most.
The points for Damian from all eight steps are gathered at the end.

**Step 3: sources. Built and checked.**

- **What was built** (`core/src/sources.cpp`, the suite `sources`):
  - A source is a function of the seed, its number in the seed's
    sequence, and the tick. Nothing of it is saved: a snapshot works
    the sources out again.
  - The sequence: those placed by hand first (`sources.fixed`, a list
    of "place type watts sag"), then `sources.start` for each 100
    places, each as old at tick 0 as a draw puts it within its life,
    then births, each tick with the chance `sources.chance` for each
    100 places.
  - Three types, a catalogue row each (`source.steady.`, `.daylight.`,
    `.gusty.`): its share of the sources drawn, its voltage, the range
    its sag is drawn from, its night, its swings, its drop-outs and its
    swells.
  - A source grows over the first tenth of its life and fades over the
    last. It lands on a land cell, chosen by `sources.landing`, and
    joins that cell's place.
  - The day sweeps the map from east to west: a source's day starts
    earlier the further east it is (`sources.day_span`).
  - The supply's laws (day, swings, drop-outs) moved to one place that
    gate 2's supply and the sources both use. Gate 2's supply behaves
    as before (its suite passes).
- **Details fixed at build:**
  - **A lifetime is drawn evenly on a log scale from a quarter to four
    times `sources.life`,** as agreed, so the average life is 1.35
    times the setting, not the setting. `sources.chance`, left unset,
    is worked from that so the count holds steady.
  - **Sizes follow one law,** least × (most ÷ least)^(u^skew): 50 to
    400 W with the skew at 3 puts the middle source at 64 W and one in
    ten over 226 W.
  - **Swells** are the gusty type's alone by default (0.5 a source in a
    million ticks), each raising the voltage 1.1 to 1.8 times.
  - Sources placed by hand are settings, not calls, so snapshots,
    resuming and the viewer need nothing more for them.
- **The checks** (1,168,113 in the suite): the count alive holds at the
  chance × the lifetime (50.16 against 50.00 over 2 million ticks);
  60,000 landings put every place within 3 standard deviations of its
  share; types and sizes came out as their shares and law say; a gusty
  source's swells and drop-outs came as often and lasted as long as
  their settings say; growing and fading at each end of a life.

**Steps 4 to 7: the solve, heat, cooling devices and the low-voltage
interrupt. Built and checked.**

- **What was built:**
  - **The solve** (`core/src/solve.cpp`; 4.1, appendix C): each tick,
    before the places run, the sources' states at that tick, each
    place's load from its last tick's draw, and Gauss–Seidel sweeps
    over the proximity links, starting from the last tick's voltages.
    It stops when no place moves more than `grid.tolerance` (0.1 mV) or
    at `grid.sweeps` (64), and counts the solves that stop at the cap.
    Whole numbers throughout (microvolts and 2^-20 siemens), on one
    host thread, so the answer is the same on any host.
  - Links too weak to matter are left out when the world is built. A
    part of the map with no source that tick sits at 0 V. Water drains
    what crosses it, and the sea's share of the loss is counted.
  - **Heat** (`core/src/heat.cpp`; 5): every device has a temperature,
    a heatsink to its place and a limit; a place has one too, cools to
    the outside at its own rate (more on a coast) and passes heat to
    its neighbours over land. A device at its limit trips, and comes
    back when it has cooled by `heat.cooldown`, with no off-time. A
    thread on a core that trips ends, with the new cause "heat".
    Memory fades faster as it warms. A warm device draws more
    (`heat.leakage`).
  - **Cooling devices** (7.7): the share `cooler.share` of places has
    one, dealt by the coolers' field. It works harder the hotter its
    place is, draws for it, and can cut out or trip like any device.
  - **The low-voltage interrupt** (9.5): a thread with a handler for it
    is interrupted once when its place's voltage comes within
    `power.low_voltage_margin` (30 mV) of its core's threshold. The
    counters tell how many slept on it and how many of those woke
    after a cut-out.
  - A constant draw rises above its device's maximum, as the square of
    the voltage (gap 5).
- **Details fixed at build:**
  - **A sweep rounds down to the microvolt,** so one source at one
    place gives gate 2's voltage exactly (20,000 of 20,000 draws);
    rounding to nearest missed 13.
  - **Heat between places uses the temperatures the tick began with,**
    so the order places are worked in can't matter.
  - **Only links with no water under them carry heat** (step 2 left
    that to settle).
  - **A tripped memory device fades as one at 0 V does.**
  - **A place-window hot and dim at once** keeps step 1's rule: the
    larger loss of core time names it. The reset region's memory off
    by heat counts with "memory off", under dim.
  - **`os.cutout_pause` pauses threads on a heat trip too.**
  - **The cooling device's heatsink is 1.0 W a degree,** not the 0.1 of
    the other small devices: at 0.1 it would sit 30 degrees above its
    place at full draw and trip itself.
  - Heat is one tick behind in two places: fading uses the temperature
    the last tick left, and the cooling device's work is set from its
    place's temperature as the tick began.
  - The tests of gates 1 to 3 run under `supply.model = place` and
    `heat.on = false`, set once for the test runner, so they test what
    they did.
- **Saves:** a place's gate 4 state is a block of its own, written only
  when the world has sources or heat, so a gate 3 world's save is
  byte for byte what it was. The solve's voltages are saved and hashed
  when the sources are on.
- **The checks** (the suites `solve`, `heat` and `lowv`):
  - The solve against the same equations in doubles on 48, 300 and
    1,000 places: within 31 microvolts when run to rest, and power
    given equal to power taken within 0.01%.
  - A world with one source placed by hand ran 600 ticks at gate 2's
    voltages, tick for tick.
  - Water: with no crossing kept, the places across the water sit at
    0 V; with the reach at 1 link they get a trickle, each wet link's
    loss as e^(water ÷ reach) says.
  - Heat against the equations in doubles for 12,000 ticks, never more
    than 0.0005 degrees apart; 48 quiet places at rest, each within
    0.0006 degrees of its solved temperature; rates set 10,000 times
    too high stay stable.
  - A core's trip, its thread's end and its return; the reset region's
    memory tripping and the cold start after; fading at 45, 55 and 65
    degrees, twice as fast for each 10.
  - A cooling device against its place's rest temperature by hand, cut
    out below its threshold, and dealt to the 100 of 300 places the
    field puts highest.
  - The interrupt: raised at the threshold plus the margin, the
    handler slept, the cores cut out, the twin with no handler ended,
    the sleeper woke and went on.
  - The same world hash on 1, 2 and 24 host threads in each suite.

**Step 8: determinism, snapshots, cost, and the routes. Built and
checked.**

- **What was built:**
  - **The measures:** the observer's samples gained the sources
    (alive, swelling, dropped out), the power given, drawn and lost,
    the heat leaving, the places' mean and highest temperature, trips
    by kind of device, cut-outs, and the interrupt's counts. Deaths by
    heat are counted as a cause. The report has a section, power and
    heat, and the sample files 19 more columns, both only for a world
    with sources or heat: a gate 3 world's files are byte for byte
    what they were.
  - **Snapshots are version 4.** Version 3 is still read: a version 3
    world is opened with a supply at each place, no heat and no
    low-voltage interrupt, which is what it was saved under.
  - **The solve's counters and the interrupt's watch list are saved
    with the counters,** so a run carried on from a save writes the
    samples of one never stopped.
  - **`nova2 bench` has a gate 4 part** (`bench.part` picks a part):
    the solve and the heat between places against the tick, at 48,
    1,000 and 10,000 places, as the settings are and with sources that
    come and go fast, and the routes' cost there.
- **Gate 3's two open points about routes, put here:**
  - **Routes tying forever with both map delays at 0:** it cannot
    happen. The map gives every link a delay of at least a tick, so
    with both settings at 0 a route goes by the fewest hops and each
    hop is nearer. Checked on 34,834 routes on 3 landscapes, a tenth
    of their places dark: each followed to its end, none back to a
    place. The route search also treats a delay of 0 as 1, as a guard.
  - **Routes at 10,000 places:** measured under gate 4, where the
    sources change which places are lit. In 200 ticks there were 44
    changes and 11,072 columns worked out, taking 26.6 ms a tick of
    the threads' time, with 18.4 MB of columns kept after 220 ticks.
    Nothing bounds them still (Damian chose on 2026-11-16 not to), and
    the cost a tick still grows with the places, since each change
    drops every column. Bounding them, or keeping the columns a change
    doesn't touch, is his to ask for.
- **Saves, and what an older build's files do:** the observers' memory
  has a new format and the earlier ones are not read, since a sample's
  row grew. So a run saved by an earlier build can't be carried on by
  this one, and its record files can't be read by it. Each run folder
  keeps the `nova2.exe` that made it, which still reads its own. World
  snapshots of version 3 open in this build.
- **The checks:**
  - **Determinism** (the suite `gate4`, 47,010 checks): a 16-place
    world under sources that come and go, with heat trips, cut-outs
    and the interrupt at work, hashed every 400 ticks to tick 2,000:
    the same on 1, 2 and 24 host threads, watched or not.
  - **Resumed from five moments:** mid-swell, mid-trip, with a source
    fading, with a thread's low-voltage bit set, and with a cooling
    device at work. Each ended at the same hash with the same counts,
    on 2 and on 24 threads.
  - **A version 3 snapshot** loaded and ran on to the hash of the world
    it was taken from; a version 2 was refused.
  - **The fuzz world:** 2,500 ticks, every place's invariants and the
    network's checked each tick; the power the sources gave equal to
    what the loads, links and drains took within 0.7% in the solves
    that settled and 1.75% in the 22 that stopped at the cap.
  - **Gate 3's world is gate 3's still** (`runs/gate4-step8`): all 20
    seeds to tick 20,000 under `supply.model = place`, `heat.on =
    false` and `power.low_voltage_margin = 0`. Every seed's map line
    and all 200 sample rows are `runs/gate3-2`'s, every column.
  - **The benchmark** (`runs/gate4-step8/bench-gate4.txt`; figures are
    low, beside `runs/open-2`):

    | Places | Ticks a second | The solve | Sweeps a solve | Solves at the cap | Heat between places |
    |---|---|---|---|---|---|
    | 48 (1 thread) | 6,802 | 5% of a tick | 36 | 0.5% | 0.6% |
    | 1,000 | 373 | 13% | 60 | 62.5% | 3.8% |
    | 10,000 | 24 | 18.5% | 62.5 | 78.5% | 7% |

    With sources that come and go fast, 39% of solves stop at the cap
    at 48 places, 92% at 1,000 and 96% at 10,000.
  - All 36 test suites pass.

**Step 9: gate 4's views. Built and checked.**

- **What was built** (`app/ui/power_view.cpp`, with small hooks in the
  viewer), all of it shown only for a world with sources or heat:
  - **The map's places coloured by voltage, temperature, draw or
    cooling:** four buttons in the map's top left corner. A press on
    the one in force gives the map's own colouring back. Under
    cooling, a place with a cooling device has a white corner.
  - **The proximity links over the map** (the button "grid"): gold over
    land, blue across water, violet where one crosses a border of the
    resistance field; each as wide as the power flowing in it, with a
    mark towards the end the power flows to.
  - **The sources** (the button "sources"): a diamond round its place
    in its type's colour (white steady, yellow daylight, light blue
    gusty), larger for a larger source, doubled in the swell's colour
    while it swells, thin and dim while dropped out or giving nothing.
  - **The inspector:** in place of the lines about a place's own
    supply, its voltage, what it draws by device, and the sources at
    it with their states; then its temperature and cooling rate, its
    cooling device, each device's temperature with a tripped one
    marked "off", and its trips by kind.
  - **A third row of charts, "heat":** power given, drawn, lost and
    leaving as heat; the places' mean and highest temperature; and
    heat trips, cut-outs and sources swelling a sample.
  - **A snapshot shows its sources at once:** a world loaded from a
    snapshot works its sources out as they stood at the last tick run
    (before, they appeared only after the next tick).
- **Gate 3's two open points about the viewer, put here:**
  - **Colony records from a snapshot:** `nova2 view` opened on a run's
    save of a world (`seed-S-tick-T.world.snap`) now reads the test's
    save beside it, so the views have the run's counters, samples,
    lineages and colonies with their clocks from the first frame.
    It works for a run that keeps its records in memory. A run that
    writes them to files (`origin.records`, as the long runs do) saves
    only counts, so there the views still start afresh, and say so.
    A world saved alone has nothing beside it, as before.
  - **Map drawing at 10,000 places, timed** (`nova2 view --time`, off
    screen, `runs/gate4-step9`): the map takes 62 to 85 ms a frame,
    against 6 ms at 1,000 places. With gate 4's links and sources over
    it, 160 to 235 ms at 10,000 and 18 ms at 1,000. So at 10,000 the
    window would draw about 12 frames a second, and 4 to 6 with gate
    4's overlays on. How large point sets are drawn is 14.2's open
    decision, so nothing was changed for it.
- **The checks:**
  - **A headless render matches a stored image pixel for pixel**
    (`tests/golden/view_gate4.png`): a 30-place world mid-swell at
    tick 316, the map by voltage with sources and links, the inspector
    on the place whose source is swelling, and the heat row.
  - **A snapshot taken mid-swell, with a core off by its heat, draws
    as its world did:** 18 renders, each of gate 4's colourings and
    the memory view, on three places.
  - Each colouring and each overlay changes the picture, and every new
    button does what it says.
  - From the command line: the default world drawn after 600 ticks; a
    short run's save opened with its test's save read (100 samples and
    its lineage known at the first frame).
  - The settings window's stored picture was renewed for the one new
    setting (`bench.part`). The other stored pictures are unchanged,
    since a world without sources or heat shows none of this.
- **Not checked:** the window itself was not opened (the run beside it
  has the host, and a person should judge the look); no colony was
  carried over a save in the command-line check, since the short run
  made none; and the stored picture is from one world at one size.

**Step 10: the test worlds at full size. Built and run.**

- **What was built** (`app/power_worlds.cpp`): five worlds made by hand,
  `nova2 worlds` with `worlds.test` set to `band`, `heat`, `water`,
  `sleeper` or `rejection`. Each builds its world from the config given
  and sets its own sources over it. Gate 3's five test worlds and
  `nova2 taker` now set the three settings that make a world gate 3's
  (`supply.model = place`, `heat.on = false`, the interrupt's margin 0)
  when the config doesn't, so they test what they did.
- **How they were run** (`runs/gate4-step10`): seed 1, 1,000 places,
  20,000 ticks (the water world 200, load rejection 1,000), 8 host
  threads below normal priority. Nine commands, each ended with exit
  code 0 and nothing on its error stream. They are single runs of one
  seed under 7's placeholder numbers: they show how the mechanisms
  behave, and are for working 7's numbers again. They judge nothing.
- **Where they fall short of the plan's words:**
  - **The water world** uses the landscape's own landmasses, not a
    strait and an island made by hand.
  - **The sleeper's threads do not copy themselves.** No replicator
    with a low-voltage handler is written, so the world compares threads
    placed by hand, not lineages.
  - **Load rejection is read against a twin world.** A world this size
    is never at rest (cores at the edge of the lit places cut in and
    out every tick), so before against after measured the world's own
    movement. Two worlds are run the same to the cut, the crowd kept
    full in both, and cut off in one: the difference is the cut's.
- **The band:** one steady source of 1,500 mV, rated 500 W, at the map's
  middle place (27 links from the farthest), sagging 20 mV (stiff) or
  400 mV (soft) at that load. Gate 3's replicators that SEND themselves
  were placed in each of the 307 places whose cores ran at the start.
  - **Only the places within 4 to 6 links of the source kept threads.**
    Stiff: from 0 to 6 links out at tick 1,000 and 0 to 5 at tick
    20,000 (1,718 threads in 14 places). Soft: 0 to 6, then 0 to 4 (684
    threads in 12 places).
  - **The voltage falls fastest at the source.** Stiff, at the end:
    1,479 mV at the source's place, 957 a link out, 825 at two, 691 at
    three, 646 at five. Three links carry all the source gives.
  - **No inner edge formed.** The source's own place, at 1,479 mV and
    46 degrees, kept threads to the end. A core above its maximum does
    not stop: it draws more and is noisier, so only heat or noise could
    clear the places nearest a source, and at these numbers they don't.
  - **The soft source held fewer:** it gave 322 to 376 W at 1,231 to
    1,278 mV where the stiff one gave 502 to 545 W at 1,478 to 1,480.
  - **With no noise** the stiff source's threads lasted out to 3 or 4
    links and the soft one's to 2 or 3. **With no grid borders,** to 4
    links for both at the end (3 to 6 over the run): the borders, one
    every 6 links at this size, are not what limits the reach.
- **Heat:** one place of 4 cores kept full by hand, its source rated 60
  W, with no noise, swings or drop-outs.
  - **Every case cycles through trips.** Steady source: at a cooling
    rate of 0.625 W a degree the place sits at 73.5 degrees with 471
    trips in a day of 2,000 ticks; at 2.5, 45.3 degrees and 236 trips;
    at 10, 29.8 degrees and 76 trips.
  - **An E core flat out reaches its limit wherever it is.** At 1,021
    to 1,049 mV its own heatsink (0.07 W a degree, with 1% more draw a
    degree) takes it to 100 degrees even in a place at 30. The place's
    cooling changes how often it trips, not whether.
  - **Under a daylight source** (965 to 1,249 mV over the day) P and E
    cores both trip: 566, 800 and 706 trips a day at the three rates,
    and the place's temperature follows the day by 0.6 to 1.5 degrees.
- **Water:** one stiff source on a landmass of 266 places, nothing
  placed. Its own landmass sits at 539 to 1,049 mV. Across water, by the
  least water on a link straight across: 0.77 of a link, 560 mV on
  average; 1.3 links, 215 mV; 6.3 links, 1 mV (a landmass of 317
  places). 272 links over water carry power, and none was too weak to
  keep. 14 landmasses more (188 places) are reached only through
  others, the highest of them at 299 mV.
- **The sleeper:** daylight sources only, the night at 2% of the day,
  4,000 threads with a handler that sleeps half a day on the interrupt
  and 4,000 without.
  - **With the handler 122 were left at tick 20,000, without 76.** At
    tick 2,000 it was 269 against 136.
  - **Most of both ended in the first day.** The interrupt was raised
    2,126 times and taken each time, and 1,719 sleepers woke after a
    cut-out; but there were 455,439 core cut-outs, most with no warning
    inside the margin.
  - **With the margin at 100 mV:** 89 left against 28.
- **Load rejection:** a crowd of 36 or 37 counting threads in the 13
  places within two links of a source rated 300 W, cut off at once.
  - **The cut sheds a third to a half of the crowd's draw:** 43.7 W
    became 28.7 W at the soft source of 1,050 mV. The rest is the
    places' devices idling, which a thread's end does not stop.
  - **The rise is 10 to 30 mV near the crowd.** Soft source, 1,050 mV:
    10 mV in the crowd's places, 9 mV four links out, a millivolt or
    more on average out to 14 links. Stiff source at 1,300 mV: 30 mV
    in the crowd, 15 at four links, a millivolt out to 10 links.
  - **In the other two cases there was no clear rise.** Stiff at 1,050
    mV and soft at 1,300 mV: the source's place rose 1 and 20 mV, and
    the places around it stood 3 to 6 mV below their twins. A likely
    cause, not checked: the freed power lit cores further out, and
    their draw took it back.
  - **Nothing tripped and no place passed its cores' maximum** that
    stayed under it in the twin, in any of the four.

**Where gate 3's four open points were put.**

- **Routes tying forever with both map delays at 0:** step 8. It can't
  happen (a link's delay is at least a tick); checked on 34,834 routes.
- **Route columns not bounded at 10,000 places, and their cost:** step
  8. Measured under gate 4's sources; not bounded and not made cheaper,
  which is Damian's to ask for.
- **The viewer losing colony records from a snapshot:** step 9. The
  viewer reads a run's test save beside the world's. Not for runs that
  keep their records in files.
- **Map drawing at 10,000 places not timed:** step 9. Timed; slow.

**For Damian, from steps 3 to 10.** None of these was decided for him,
and nothing was built for any of them. The first group is step 11's
work on 7's numbers.

- **7's numbers, as the test worlds show them:**
  - **One source lights 4 to 6 links around it** and no more, with or
    without borders. If a source is to feed a region, the links'
    resistance or the devices' idle draw has to come down.
  - **No inner edge forms at a source.** A band with two edges needs
    something that clears the places nearest a source, and nothing
    does at these numbers.
  - **An E core flat out trips at about 1,030 mV wherever it is.** So
    a full place cycles through trips at every cooling rate tried, and
    cooling changes only how often.
  - **A quiet world is not within a degree of the outside:** 48 quiet
    places sit 1.79 degrees above it on average, the warmest 4.29.
  - **The sweep cap bites at size:** 62.5% of solves stop at the cap
    at 1,000 places and 78.5% at 10,000. In the fuzz world the power
    balance was within 1.75% in the solves that stopped there.
  - **A cooling device's heatsink is 1.0 W a degree,** ten times the
    other small devices', or it trips itself.
  - **A source's average life is 1.35 times `sources.life`,** from the
    range agreed. With sources on and no map, most places are dark.
  - **Borders come every 6 links at 1,000 places,** not every 20 to 30
    (step 2). The band was the same with and without them. The choices
    are as step 2 gave them: leave borders as they are, give the
    resistance field a scale of its own, or switch borders off. The
    lean is to leave them until 7's resistances are worked again,
    since those decide far more.
- **The low-voltage interrupt:**
  - **It is on by default and changes gate 3's worlds** (seed 13 from
    tick 9,300), so every gate 3 check here sets its margin to 0.
  - **It helps, but most cut-outs come without warning.** A margin of
    30 mV left 122 sleepers against 76; 100 mV left 89 against 28.
- **Load rejection is small:** a cut sheds a third to a half of the
  crowd's draw and raises the voltage by 10 to 30 mV at most, and
  nothing tripped. If 12.2's overshoot is to matter, an idle place
  has to draw less.
- **Behaviour fixed at build, for a word if any is wrong:** only links
  with no water carry heat; heat is one tick behind for fading and the
  cooling device; `os.cutout_pause` pauses on a heat trip too.
- **Cost and tools:**
  - **Route columns are not bounded,** and each change of what is lit
    drops them all (18.4 MB kept, 26.6 ms a tick at 10,000 places).
  - **The viewer at 10,000 places draws about 12 frames a second,** and
    4 to 6 with gate 4's links and sources (14.2's open decision).
  - **The viewer can't carry a long run's records over a save,** since
    those runs keep them in files.
  - **This build doesn't read earlier builds' run saves or record
    files.** `runs/open-2` can only be carried on by its own
    `nova2.exe`, which its folder keeps.
  - **No replicator with a low-voltage handler is written,** so the
    sleeper world can't yet show a lineage that sleeps.

**After step 10: who led each place, and gate 5's reference
(2026-11-20).** Two things gate 5's plan asks of gate 4's code before
gate 5's step 1 starts (`build-plan-gate5.md` 2, the first choice about
runs, which Damian agreed as a, and 6, the gate 4 equivalence test).
Damian also set the runs' order that day: "I want to hold off on doing
the 20 seed run until both gates are fully built." So gate 4's 20 seeds
no longer start when `runs/open-2` ends.

**Overtaken later the same day** by Damian's "we only need a single 20
seed test that is a combined gate 4+5. I don't think we need 2" (5,
step 11). The columns below were built before that word came. With no
gate 4 run of its own to read, nothing needs them now, and
`build-plan-gate5.md` (2) has gate 5's step 1 record the leads in the
combined run itself. They are left in: they cost a sort of a place's
programs each 200,000 ticks, the columns gate 4's mark reads are as
they were, and gate 5's step 1 can use them or take them out. The
reference stands either way.

- **Who led each place as a window began, in the ground record.** Each
  row of `seed-N-ground.csv` has ten more columns, at the end:
  - **the leading group:** the genome group with the most of the
    place's programs as the window began (`lead_group`, its number,
    and `lead_group_ref`, its reference genome's hash, which names it
    for the whole run), its programs, and how many of those were born
    there as copies;
  - **the most programs any other group had,** since a group leads only
    with more than any other, and how many groups had a program there;
  - **the leading lineage,** by the same rule: its origin, its programs,
    the copies among them, and the most any other lineage had.
- **Details fixed at build:**
  - **The row holds the counts, not the verdict.** Whether a lead
    counts (20 programs or more, a tenth of the place's, half of them
    copies) is for gate 5's measure, which reads the row.
  - **A program whose genome isn't kept has no group,** so it counts in
    the place's programs and for its lineage, and for no group.
  - **Copies are stage 4's:** born at the place as a copy. An arrival's
    births aren't counted.
  - **Of two groups with the same programs, the lower number is named,**
    and the other shows as the most any other had. A window with no
    kept genome there has a group of -1.
  - **The lead is worked out once every place's facts for the tick are
    in,** since a birth at one place can keep a genome that programs at
    another carry.
  - Observers only: the world and its hash don't change.
- **Saves:** the observers' memory is format 6, since a ground row
  grew. Saves and record files of the build before it aren't read.
  Nothing had been run to keep under that build.
- **The checks:**
  - In a four-place world where the replicator and its mutants crowd
    the places under noise, every one of 240 place-windows has the lead
    counted here from the records of its living programs, one against
    every other: 236 led by a group (137 programs at most), 52 with two
    groups or more there.
  - A row's lead by hand: no one for a place's first window, then what
    the observer set.
  - All 36 suites pass. Gate 3's 20 seeds to tick 20,000 are gate 3's
    still (`runs/gate4-step8`, run again with this build).
- **Gate 5's reference** (`runs/gate4-reference`, its `README.txt`):
  seed 1 of gate 4's world to tick 1,600,000 at the settings as they
  stand, by this build, on 4 host threads beside `runs/open-2` in about
  4 minutes.
  - **Its settings** are gate 3's world with gate 4 as the build's
    defaults have it (sources, heat, the interrupt, the stack rule on),
    and no grid borders, as agreed for 48 places. Every other gate 4
    number is 7's placeholder still, so the reference is made again
    when step 11 changes them.
  - **It is the same on 2 host threads,** and made twice by this
    build: the samples, the windows and the ground record, byte for
    byte.
  - **It has no life in it.** Seed 1 reached no stage by tick 1.6
    million (gate 3's stage 4 came at 1.8 million ticks at the
    soonest). So gate 5's test against it checks the physics and static
    under gate 4, not replicators, and no group leads any of its 384
    place-windows.
  - **What it shows of gate 4's world, for step 11:** 147 of its 384
    place-windows are dim and 237 fair, none hot. Cores lost 30.8% of
    their time to cut-outs and 0.018% to heat trips. 0.75% of solves
    stopped at the cap.
- **A fault found in the report and fixed:** the report's line for the
  places' mean temperature above the outside was a thousand times too
  small before the outside was taken off (it read -24.97 degrees). It
  now reads 3.54 for the reference, which is the samples' own mean.
  The samples were right and are unchanged.

**Step 11, first part: 7's numbers worked again (2026-11-20). A
proposal for Damian. Nothing is changed until he agrees: 7's table, the
build's defaults and `design.md` are as they were.**

- **How they were measured.** `nova2 worlds` has a measuring tool,
  `worlds.test = numbers` (`app/power_worlds.cpp`). It reports:
  - a P core and an E core working all tick, by the engine's own laws:
    the draw, the temperature it rests at, the voltage it trips from,
    and the heatsink 7's rule needs;
  - the world's places at a steady 1,000 mV, idle and with every core
    kept busy by hand: their draw and the temperature they rest at;
  - the world as it stands, without noise, for 20 days: the places hot,
    dim and fair by the ground measure's rule, by links from the
    nearest source. Three ways: with nothing living, with every core
    kept busy, and with each place in turn busy alone (what a first
    settler finds). A living world lies between the first two.
- **The runs** (`runs/gate4-step11`, its `README.txt` and
  `summary.txt`): the tool on seeds 1 to 3 under eight sets of values;
  step 10's heat world under two; and the reference's run (seed 1 to
  tick 1,600,000 with gate 3's noise) made again under three. All ended
  with exit code 0 and nothing in their error streams. With the tool
  in, all 36 suites pass.

**What the placeholders do:**

- **The heatsinks let a busy core trip about 210 mV under its maximum,
  not 100.** In a place at 40 °C a core working all tick trips from
  1,089 mV (P, maximum 1,300) and 994 mV (E, maximum 1,200). The
  placeholders were worked without the leakage: 0.56 and 0.071 W a
  degree meet the rule without it, and 0.98 and 0.124 with it. As they
  stand, a busy E core trips at the usual 1,000 mV once its place
  passes 38 °C, so the top half of each core's range is the burn zone,
  not its top 100 mV.
- **A place draws about twice what 7 assumed.** 7 took gate 3's 15 W
  for a typical place and 23 W for a full one. Those were at gate 3's
  sagged voltage (928 mV on average) with no heat. At a steady 1,000 mV
  with heat on:
  - idle, a place draws 7.3 to 7.4 W;
  - with every core busy, 49 to 51 W on average, and up to 105 W for a
    place of 8 cores (45 to 47 W and 97 W with the heatsinks by the
    rule, since cooler cores leak less);
  - a P core working all tick draws 10 W at 1,000 mV, 20 W at 1,100 and
    34 W at 1,200, before the leakage.
- **So the sources are half the size their row means.** A seed's 12
  sources are rated 826 to 1,080 W in all; its 48 places busy at
  1,000 mV would draw 2,360 to 2,460 W. The row's rule (the full draw
  of 2 to 4 average places) gives 100 to 200 W a source, not 50 to 100.
- **The world is dim, and almost never hot.** Of a seed's 48 places:
  - with nothing living, 3 to 8 are dim and none hot;
  - with every core busy, 24 to 41 are dim and 0 or 1 hot: about half
    the places one link from a source are dim (12 of 24, 12 of 24, 18
    of 21) and nearly all those further out;
  - busy alone, 7 to 14 are dim and 1 to 5 hot.
  The reference (static only) agrees: 147 of 384 place-windows dim,
  none hot, a place at 919 mV on average and under the P cores'
  threshold for 28% of its ticks. Gate 3's world sat at 928 mV with its
  P cores cut out 21% of the time, so gate 4's world as numbered is
  harsher than gate 3's before anything lives in it.
- **Place cooling does what its row says,** and stays. With every core
  busy at 1,000 mV the hottest place rests at 58 to 64 °C, and none
  reaches the disks' 70 °C. One thing the row leaves out, by the
  arithmetic and not measured apart: a disk sits about 15 °C above its
  place at its limit, so a disk trips once its place passes about
  55 °C.
- **The heat capacities do what their row says,** being set from it: a
  core's capacity is 20 ticks of its heatsink, a place's 500 of its
  cooling.

**The eight sets tried.** Each cell is the count for seeds 1, 2 and 3,
of 48 places a seed. Hot and dim are by the ground measure's rule over
the 20 days; the rest are fair.

| Set | What is changed | Dim, nothing living | Hot, every core busy | Dim, every core busy | Hot, busy alone | Dim, busy alone |
|---|---|---|---|---|---|---|
| a | Nothing: 7's placeholders | 8, 3, 6 | 1, 0, 1 | 25, 24, 41 | 1, 3, 5 | 14, 7, 9 |
| b | The heatsinks by 7's rule (0.98 and 0.124 W a degree) | 8, 3, 6 | 0, 0, 1 | 23, 23, 41 | 0, 0, 1 | 14, 7, 8 |
| c | b, and sources twice the size (100 to 800 W) | 6, 0, 5 | 1, 1, 1 | 20, 18, 29 | 2, 2, 4 | 13, 4, 8 |
| d | c, and links of half the resistance (2.5 mΩ) | 1, 0, 0 | 1, 1, 1 | 16, 9, 25 | 2, 2, 5 | 7, 1, 3 |
| e | b, and the links, not the sources | 2, 0, 0 | 0, 0, 1 | 20, 18, 43 | 0, 0, 1 | 8, 1, 4 |
| f | Sources twice the size, heatsinks as they stand | 6, 0, 5 | 4, 6, 7 | 20, 18, 30 | 5, 6, 12 | 13, 4, 9 |
| g | f, and the links | 1, 0, 0 | 3, 3, 5 | 16, 9, 25 | 6, 7, 15 | 7, 1, 3 |
| h | d, and daylight and gusty sources sagging 100 mV at most | 0, 0, 0 | 4, 3, 4 | 13, 8, 19 | 4, 3, 11 | 8, 1, 4 |

No set has a hot place with nothing living: an idle core doesn't trip.

**The proposal: set h.**

| Number | Now | Proposed | Why |
|---|---|---|---|
| Heatsink rates | P 0.55, E 0.07 W a degree | P 0.98, E 0.124 | 7's rule with the leakage counted. Measured: a busy core in a place at 40 °C then trips from 1,201 mV (P) and 1,101 mV (E) |
| Cores' heat capacities | P 11, E 1.4 J a degree | P 19.6, E 2.48 | 7's rule: a device follows its draw within about 20 ticks |
| Source sizes | 50 to 400 W (most 50 to 100, one in ten 250 or more) | 100 to 800 W (most 100 to 200, one in ten 500 or more) | 7's rule, from the full draw measured: 45 to 47 W a place, not 23 |
| Link resistance | 5 mΩ | 2.5 mΩ | 7's rule (about 75 mV a link under a typical draw, a band about 3 links wide), from draws twice those assumed. Measured between sets c and d, which differ in this alone, with every core busy: the places two links out are at 891 to 930 mV with 31 to 67% of their core time cut out and 22 of 32 dim, where at 5 mΩ they were at 834 to 893 mV with 45 to 77% cut out and 28 of 32 dim |
| Stiffness of daylight and gusty sources | Sag of 50 to 300 mV at the rated load | 50 to 100 mV | Not a rule's working: a choice, to make hot ground (below). Steady sources stay at 20 to 50 mV |
| Place cooling, and every other row of 7 | | Unchanged | They do what their rows say, or don't turn on the draws |

- **What set h does, with every core busy:** the picture the design
  draws (4.4), a burn zone at the sources and the dark far from them.
  - At a source's place: 1,056 to 1,104 mV, 15 to 29% of core time off
    by heat trips, 3 or 4 of the 9 to 12 such places hot, none dim.
  - One link out: 964 to 992 mV, 4 to 23% cut out, most places fair.
  - Two links out and further: 834 to 926 mV, a quarter to four fifths
    cut out, most places dim.
  - In all 3 to 4 places hot, 8 to 19 dim and 25 to 37 fair, where the
    placeholders gave 0 to 1, 24 to 41 and 6 to 24.
- **With nothing living, no place is hot or dim** over the 20 days:
  the ground is made by what lives on it (see below).
- **The ground record under static** (the reference's run again, 384
  place-windows): 4 hot, 35 dim and 345 fair, a place at 1,033 mV on
  average, where the placeholders gave none hot, 147 dim and 237 fair
  at 919 mV. Set d gave none hot, 58 dim and 326 fair, and set g the
  same counts.
- **Step 10's heat world** (one full place of 4 cores): with the
  heatsinks by the rule it settles under a steady source at the usual
  cooling and at four times it, and cycles through trips at a quarter
  of it and under a daylight source. With the placeholders it cycled
  through trips at every cooling rate, under both sources.

**Three things in this for Damian:**

- **The stiffness change is tuning toward the pass mark's floor,** and
  8's risk "tuning could choose the answer" applies to it. With every
  row worked by its own rule and nothing more (set d), a seed has one
  hot place when wholly busy, which makes the floor of 100 hot
  place-windows from 5 seeds doubtful. There are two levers that make
  more, and they differ in what burns:
  - **stiffer sources (set h, proposed):** a steady source's place
    never burns, and a daylight or gusty one's does when it is busy.
    The catalogue's "steady but weak, or strong but erratic" keeps its
    meaning. The cost: soft and stiff sources differ less, which the
    creep check compares (20 to 100 mV in place of 20 to 300);
  - **the heatsinks as they stand (set g):** 7's heatsink row is then
    restated as what they do (a busy core trips about 210 mV under its
    maximum). A busy E core then burns at any source's place, steady
    ones too, and nearly the top half of each core's range is never
    usable.
  Or neither (set d), and the floor is left to say what it says.
- **Under set h, seed 1 reaches stage 1 at tick 746,122,** stage 4 at
  tick 957,400 and stage 5 at 963,600, where the reference, set d and
  set g reach no stage by tick 1,600,000 and gate 3's soonest stage 4
  was at 1.8 million. That is one seed, and the origin is chancy: it isn't
  evidence that set h brings the origin sooner. The pilot's ten seeds
  say.
- **Life makes its own ground hot, which the pass mark doesn't allow
  for.** In that run the life is in place 19 (lax, 8 cores). The place
  lost no core time to heat in its first three windows, with only
  static in it. From the window in which the
  replicators filled it, it lost 42 to 50% of its core time to heat
  trips, and it is hot in all four windows since: the run's only hot
  ground. So its hot place-windows start with 334 programs on average
  and its fair ones with 3.6, the opposite of what the mark asks
  (fair at least twice hot). Gap 1b reasoned that hot ground need not
  hold less life. It didn't consider that a crowd heats its own place,
  as it must also sag its own source: the ground's name follows the
  load as well as the place. One seed to 1.6 million ticks shows it;
  whether the mark should be read differently is Damian's, before the
  combined run. Nothing is proposed here.

**What agreeing would set off, in order:** the defaults changed in
`core/src/keys.inc` and 7's rows rewritten; every suite run again, with
gate 3's equivalence (which runs with the sources and heat off, so
shouldn't move); gate 5's reference made again (`runs/gate4-reference`)
and the gate 5 sessions told; then the pilot, beside `runs/open-2`
under the conditions in step 11.

**What wasn't checked:**

- Seeds 1 to 3 only, 20 days each, with no noise, drop-outs or sources
  dying. "Busy" is counting threads placed by hand, not life.
- Only these eight sets. Halving and doubling were the steps; nothing
  between was tried, and no other lever (source voltages, source
  counts, cooling).
- Step 10's test worlds (1,000 places) weren't run again under set h,
  beyond the heat world under the heatsinks.
- The ground record under sets h, d and g is one seed each.

**After the proposal: who writes the code, and two settings of the
combined run (2026-11-20, from the parent session).**

- **The code has one writer now, and it isn't this plan's session.**
  Gate 5's build began the same day in its own session, which from now
  on makes every change under `core/`, `app/` and `tests/`. If Damian
  agrees the numbers, the change to the defaults is asked of that
  session. This plan and its run folders stay with gate 4's session.
- **Gate 4's own last build is kept:** `runs/gate4-step11/nova2.exe`,
  built at 11:13, steps 1 to 10 with the measuring tool. Every
  measurement in "Step 11, first part" was made by it.
  - **Checked against the reference:** at the reference's settings it
    gives `runs/gate4-reference`'s samples, windows and ground record
    again, byte for byte (`runs/gate4-step11/origin-a`). So its engine
    is gate 4's, whatever was being edited in the source beside it.
  - **The pilot and a reference made again can both run from it,**
    with the agreed numbers written in their config files. Neither
    waits on a change to the defaults, and neither is touched by gate
    5's code.
- **A correction to "Step 11, first part":** its "all 36 suites pass"
  was a pass of the shared source as it stood at 12:05. By then that
  source also held the gate 5 session's first changes (files changed
  from 11:26 on), so it wasn't a pass of gate 4's code alone. What is
  known of gate 4's code alone:
  - all 36 suites passed at 11:08 with the tool's first parts in,
    before its busy-alone part was added;
  - the busy-alone part is in the 11:13 build, which the check above
    covers for the engine, and it passed the `rules` suite at 12:05;
  - whether the gate 5 session had changed anything before 11:13 can't
    be told from the files. The check above is the answer for the
    engine.
- **Two settings Damian chose for the combined run:** the observers
  sample every 200 ticks ("we can probably get away with halving the
  sampling to every 200 ticks"), and genomes are kept only with at
  least 3 generations of descendants, which the parent session reads as
  `observe.keep_generations` = 4 (the gate 5 plan's session is checking
  what depends on them).
  - **Neither bears on 7's physics rows or on the proposal.** Both are
    the observers' settings, and the world and its hash don't change
    with the observers. So the measurements above stand as they are.
  - **What they do change,** by reading the settings and not by a run:
    a stage's tick is read at a sample, so to the nearest 200 ticks;
    and fewer genomes are kept, so there are fewer groups, which stage
    5 and the ground record's leading-group columns read. The ground
    measure itself counts programs and core time, not kept genomes.
  - **For the pilot (Claude's lean, for Damian to confirm with the
    numbers):** it runs at these two settings, given in its config,
    since it is the baseline for gate 5's pilot and the combined run.
    Its comparison with gate 3's third pilot and `runs/gate3-2`, which
    were made at 100 ticks and 3 generations, then differs in these
    two. What is compared there is the count of seeds in which copying
    began. By the report's own wording, stages up to 4 follow a
    lineage, not a genome group; that wasn't checked in the code.
  - **The reference stays at its own settings** (100 and 3), as the
    parent session said.

**The pass mark and ground that life makes: options for Damian
(2026-11-20). Nothing is decided here, and the mark is as agreed.**

The map-design session asked for these, after "Step 11, first part"
found that a crowd heats its own place. Each option says what it would
have read in the one seed that has life under gate 4's physics
(`runs/gate4-step11/origin-h`: seed 1 to tick 1,600,000 under set h,
which wasn't chosen; no run under the agreed numbers has life yet), and
whether the floor of 100 place-windows of each kind from 5 seeds looks
reachable in the combined run under the agreed numbers (set d).

**What the one seed shows, for all of them:**

- **Its 184 lax place-windows** (23 lax places, 8 windows): 170 fair,
  starting with 3.6 programs each; 4 hot, with 334; 10 dim, with none.
  All four hot windows are place 19's, after the replicators filled it.
- **The same happens to dim ground, short of the line.** Place 24 (lax,
  6 cores) lost 0.1 to 1.3% of its core time to cut-outs in its first
  three windows, with only static in it. As births there grew to
  16,000 to 64,000 a window it lost 3.3%, 7.4%, 7.4%, 9.5% and then
  18.1%, in the window that began with 410 programs. It never passed a
  quarter, so it stayed fair, but a crowd sags its own source as it
  heats its own place.
- **A second place heats with its life:** place 47 (lax, 8 cores) lost
  under 1.2% of its core time to heat trips in its first five windows,
  then 9.4%, 20.6% and 7.3% as its births rose.
- **Untouched ground is never hot.** Four runs of 384 place-windows
  with nothing but static in them (the reference, and sets d, g and h
  before life) have no hot window that life didn't make. They have 35
  to 147 dim ones.
- **Place 19 is very likely at a source,** by inference only: it never
  cut out, and in the measuring tool's run of that seed under set h
  every place that is hot when busy alone is at a source's place. No
  run here prints a place's links from its source.

**1. The mark as it stands, read as it stands.**

- **What it measures:** where the programs are, by what the supply and
  the programs together did to the place. A crowd at a good source
  makes its place hot, so "hot" comes to mark where life does best.
- **This seed:** fair 3.6 programs a window, hot 334, dim 0. Fair
  against hot is not met, by a factor of 90 the wrong way. Fair against
  dim is met.
- **The floor under set d:** dim is reachable (static alone made 25 lax
  dim windows in seed 1's first 8; a seed runs 85 to 500). Hot is
  doubtful: it needs life to fill, for windows on end, one of the few
  places that burn when full (1 a seed with every core busy; 2, 2 and 5
  of 48 busy alone, about half of them lax), in 5 seeds.
- **What a fail would mean:** not that the physics doesn't shape life.
  A place losing half its core time to heat is being shaped. The mark
  would be answering a different question from the one it was set for.

**2. Ground named by what the place is, not by what its load made
it.** Two ways:

- **a. By what a full place gets there:** the measuring tool's
  "busy alone" class, worked for each place from the seed and its
  sources with nothing else living. A crowd can't change it.
  - **This seed:** the tool prints the classes summed by links, not by
    place, so place 19's class isn't known for sure. It is very likely
    hot: the run itself is close to that condition (one full place in
    a nearly empty world), and there it is hot. Then the hot class
    holds the life again (its 8 windows start with 167 programs on
    average, the fair class's with about 4), and the mark is not met,
    the same way round. But it would now mean something: life arose
    and stays on ground that burns a full place.
  - **The floor under set d:** reachable for both. Busy alone, 2, 2
    and 5 of a seed's 48 places are hot and 7, 1 and 3 dim, about half
    of them lax, and every window of such a place counts whether or
    not anything lives there: 85 to 500 a seed for each.
  - **The cost:** sources open and end through a run, so the classes
    are worked again as they change. A classing is 48 small worlds,
    about 100 seconds; one every 2 million ticks is 8 to 50 a seed,
    some hours for the 20 seeds. It is done after the run, from the
    seed alone, so the combined run itself doesn't change. The tool
    would need to print a class for each place and start from a given
    tick: a code change for gate 5's build session.
- **b. By the place's links from its nearest source and that source's
  kind,** from the supply alone. Free to work, and it is the design's
  own picture (4.4: a burn zone, a band, the dark).
  - **This seed:** not known beyond place 19, by inference at a
    source's place.
  - **The floor:** reachable (of 48 places, 9 to 12 are at a source's
    place, 21 to 24 one link out, 12 to 18 further).
  - **Its weakness:** distance isn't heat. A well-cooled place at a
    steady source never burns, and it would be named with those that
    do. Under set d only 2 or 3 of the 9 to 12 places at a source burn
    when full, and in seed 3 two places one link out burn as well.

**3. Ground named by the window before the lineage arrived** (the
reading on untouched ground, already to be reported beside the mark).

- **This seed:** place 19 was fair before the replicators, so all its
  programs count as on fair ground, and nothing is on hot.
- **It can't carry the hot half of the mark.** With nothing living, no
  place is hot (above), so its hot floor is 0 windows under any
  numbers tried. It can carry the dim half: dim ground exists before
  life (25 lax dim windows in set d's seed 1), and what it reads there
  isn't the crowd's doing.

**4. Claude's lean: 2a as the judged reading, with 1 and 3 reported
beside it.**

- **Why 2a:** hot ground can't be named before life arrives (3) and
  can't be told from life's own doing after (1). "Would burn if full"
  and "would starve if full" are names a place can carry whether or
  not anything lives there, and they keep gap 1b's three kinds of
  ground. Dim ground is named by the same rule as hot, in place of a
  different rule for each half.
- **What it leaves open, plainly:** this one seed reads "not met"
  under 2a as well. If the combined run agrees, the finding is that
  life lives on the richest ground and pays for it in heat, which is
  the physics shaping life in a way the mark's wording (fair ground
  holds twice the life) doesn't count as a pass. Whether that is a
  pass, a fail, or a reason to word the mark again is Damian's.
- **What isn't known:** all of this rests on one seed, 1.6 million
  ticks, under numbers that weren't chosen. The pilot under the agreed
  numbers will give the counts of hot, dim and fair place-windows as
  lived, and little more about hot ground: it runs 10 windows a seed,
  and hot ground needs a crowd.

**Step 11: the numbers agreed, and the reference made again
(2026-11-20).**

- **Damian chose set d.** The map-design session put sets d and h to
  him side by side and relayed his answer in full: "rules only stays
  closer to my original vision". So the four rows worked by their own
  rules change, and nothing is tuned:
  - the heatsinks are 0.98 W a degree (P cores) and 0.124 (E cores),
    with the cores' heat capacities 19.6 and 2.48 J a degree;
  - the sources are 100 to 800 W;
  - an average land link is 2.5 mΩ;
  - the sources' stiffness stays as it was (daylight and gusty ones sag
    50 to 300 mV at their rated load). Set g isn't chosen either.
  7's rows carry the values now.
- **The build's defaults** are for gate 5's build session to change,
  as the code's one writer. The seven values were handed to it by
  message. Nothing here waits on that: the runs below give the values
  in their config files.
- **The reference made again** (`runs/gate4-reference-d`, its
  `README.txt`), in a folder of its own, so `runs/gate4-reference` (the
  placeholders') is left as it was for anything being compared with it.
  - Seed 1 to tick 1,600,000 by gate 4's own last build, with the seven
    values written in its `origin.cfg`: a build gives it again whether
    or not its defaults have changed yet.
  - **Made three times, the same byte for byte:** on 4 host threads, on
    2, and earlier that day as `runs/gate4-step11/origin-d`.
  - **It has no life either:** no stage by tick 1.6 million. Its ground
    is 58 dim and 326 fair place-windows, none hot, a place at 999 mV on
    average and under the P cores' threshold for 6.7% of its ticks
    (the placeholders': 147 dim, 919 mV, 28%).
  - Its observers are the first reference's: a sample each 100 ticks,
    genomes kept at three generations.

**The pass mark as changed (2026-11-20), before any judged run.**

- **Damian's decision,** through the map-design session, which put the
  options above to him with Claude's lean and two wordings for the
  direction. His answer in full: "Either direction".
- **Naming.** For the mark, ground is named by what a full place gets
  there: option 2a above, the measuring tool's busy-alone class, worked
  from the seed and its sources with nothing else living, and worked
  again as sources open and end. This is the judged reading.
- **Direction.** The mark is met if fair ground and hot ground differ
  by at least a factor of two in the programs their place-windows start
  with, whichever way, and fair and dim likewise. The direction of each
  is reported. The rest stands as agreed on 2026-11-19: the seeds that
  reach stage 4, lax places, at least 100 place-windows of each kind
  from at least 5 seeds, and the regressions.
- **Reported beside it, not judged:** the mark as first worded and
  named (ground by what happened to the place in the window, fair with
  at least twice the programs of hot and of dim), and the reading on
  untouched ground.
- **Said plainly:**
  - the wording was changed before any judged run, and before any run
    at the agreed numbers had life in it;
  - the evidence is one seed to 1.6 million ticks, under numbers that
    weren't chosen (`runs/gate4-step11/origin-h`);
  - it is an easier test than the first wording. The first asked that
    fair ground hold twice the life. This asks only that the ground's
    kind make a twofold difference, either way.
- **Kept as the first wording had it,** where the decision didn't say:
  both comparisons must hold, fair against hot and fair against dim. A
  kind whose windows start with no programs differs from one whose
  windows start with any.
- **Claude's choices in what is to be built, not Damian's:** a classing
  runs each place full for 10,000 ticks (5 days) without noise, as the
  measuring tool does now; and a seed is classed every 2,000,000 ticks,
  a place-window taking the latest classing at or before its start.
  The step is for cost: a classing is about 80 to 100 seconds, and
  sources live 5 to 80 million ticks.
- **What it needs built:** the tool printing a class for each place,
  from a given tick, and the report counting place-windows under it.
  That is code, so its short spec went to gate 5's build session. The
  ground record itself doesn't change: it keeps the first naming, which
  is reported beside the mark. The combined run doesn't change either,
  since the classes are worked from the seed after it.
- **Where else it is recorded:** `design.md` 14.1's paragraph for gate
  4. `design-history.md`'s log is the gate 5 plan session's file, which
  was asked to enter it. Gap 1b in 2 above stays as the record of what
  was agreed on 2026-11-19.
- **Open for gate 5's plan:** with gate 5's strengths on, a place
  changes over a run. Whether its class is worked on the place as built
  or as it stands at that tick is for that plan to say.
  - **Its lean, not agreed and not yet put to Damian**
    (`build-plan-gate5.md` step 15, as that session reported it): the
    place as built on the first map, with the sources' schedules and
    the straits' partings at the classing tick; wear, failures and
    patches left out; a respawned place's later windows left out of
    the mark and counted.
  - **If he agrees it,** the tool needs one thing the specification
    sent to gate 5's build session doesn't ask for: the straits'
    partings at the classing tick. Nothing has been asked of that
    session for it.
  - **Settled by Damian the same day: "Yes, as built"** (relayed by
    gate 5's plan session; `build-plan-gate5.md` step 15, `design.md`
    14.1 and 11.6).
    - A place's class in the combined run is worked on the place as
      built: the first map, each device as dealt and unworn, with only
      what is fixed before the run at that tick, which is the sources'
      schedules and the straits' partings.
    - Left out: wear, failures and patches. A replaced device keeps
      the values its place was dealt, so it changes nothing.
    - A respawned place's windows from then on are left out of the
      mark and counted. If a seed has more than a few, its classing is
      worked again with the respawns applied.
    - **What it adds to the tool's specification,** sent to gate 5's
      build session: with straits on, each classing world has its
      straits joined or parted as their spells stand at the classing
      tick, and they part and join through the classing's ticks as
      the run's would. Every other gate 5 strength is off in it. With
      straits off it is the tool as first specified, so the check
      against the present tool's counts stands. The report leaves a
      respawned place's later windows out of the mark's counts and
      says how many it left out.

**The pilot begun again (2026-11-20, 12:35).** Its first start, at
12:28, had two observer settings that weren't the combined run's:
Claude had set a sample each 200 ticks and genomes kept at four
generations, on a reading of Damian's words that he had since settled
otherwise ("Keep existing rule" for three generations; and "Agreed" to
sampling each 100 ticks and writing every second sample). It was
stopped after 6 minutes, its files kept in
`runs/gate4-pilot/stopped-200-4`, and started again at 100 ticks and
three generations, which are gate 3's pilots' settings too. Everything
else is as it was.

**Step 11 done: the pilot ran to its ticks (2026-11-20, 12:35 to
13:46).** `runs/gate4-pilot`, its `README.txt` and `report.txt`.

- **How it ran.** Seeds 1 to 10 to tick 2,000,000 at the agreed
  numbers, by gate 4's own last build, three seeds at a time beside
  `runs/open-2`, below normal priority.
  - Exit 0, and no error, failure or stop in `progress.txt`.
  - 71 minutes in all, 15 to 22 minutes a seed.
  - Its memory peaked at 0.54 GB; the host had 29 to 30 GB free
    throughout. Its files are 57 MB.
  - The report says each seed had 1 host thread, though the config
    asked for 2. Not looked into: results don't depend on the count
    (the reference is the same on 2 threads and on 4).
- **Stages, against gate 3's third pilot** (`runs/gate3-pilot-both`:
  the same seeds and ticks, without gate 4):

  | | Gate 4's pilot | Gate 3's third pilot |
  |---|---|---|
  | Copying began (stage 1) | 6 of 10: seeds 3, 4, 5, 7, 9, 10 | 6 of 10: seeds 1, 2, 5, 7, 9, 10 |
  | Stage 2 | seed 7 | seed 1 |
  | Stage 3 | seed 7, at tick 1,156,800 | none |
  | Stages 4 and 5 | none | none |
  | Births, all seeds | 324,691 | 250,101 |

  - So gate 4's physics at the agreed numbers didn't keep copying from
    starting: as many seeds began as without it, though not all the
    same ones. Ten seeds to 2 million ticks is a pilot, not a judged
    regression.
  - Seed 7 came nearest stage 4: a lineage held place 19 for 92,600
    ticks (from 1,594,800), where stage 4 asks 100,000. It then fell
    from 97 programs to 1.
  - Stage 1's ticks by seed: 3 at 1,830,716; 4 at 905,766; 5 at
    1,122,637; 7 at 1,154,970; 9 at 1,688,864; 10 at 144,377.
- **Ground as the record names it** (the first naming: what happened
  to the place in the window), place-windows by seed:

  | Seed | All places: fair, hot, dim | Lax places: fair, hot, dim |
  |---|---|---|
  | 1 | 400, 0, 80 | 195, 0, 35 |
  | 2 | 480, 0, 0 | 210, 0, 0 |
  | 3 | 402, 0, 78 | 226, 0, 34 |
  | 4 | 431, 0, 49 | 266, 0, 44 |
  | 5 | 473, 0, 7 | 230, 0, 0 |
  | 6 | 440, 0, 40 | 220, 0, 10 |
  | 7 | 431, 0, 49 | 201, 0, 19 |
  | 8 | 366, 0, 114 | 142, 0, 58 |
  | 9 | 429, 0, 51 | 129, 0, 21 |
  | 10 | 440, 0, 40 | 170, 0, 0 |
  | All | 4,292, 0, 508 | 1,989, 0, 221 |

  - **No place-window was hot, in any seed.** With next to no life,
    nothing keeps a place's cores busy. Core time lost to heat trips
    was 0.046% over all seeds; to cut-outs 7.65% (by seed from 0.45%
    in seed 2 to 21.4% in seed 8).
  - Lax fair windows began with 0.08 programs on average, dim ones with
    none. No window was hot and dim at once.
  - The one place life filled stayed fair. Seed 7's place 19 (lax, 2
    cores) began a window with 97 programs and lost 28 core-ticks to
    heat trips at most in any window, of 400,000. In
    `runs/gate4-step11/origin-h`, seed 1's place 19 (8 cores) lost 42
    to 50% once filled. So life doesn't make every place it fills hot.
- **Ground by what a full place gets** (the naming the mark judges by,
  from 2026-11-20), places of 48 by seed, for the sources each seed
  starts with (`runs/gate4-step11/d-seed1.txt` to `d-seed10.txt`):

  | Seed | Hot | Dim | Fair |
  |---|---|---|---|
  | 1 | 2 | 7 | 39 |
  | 2 | 2 | 1 | 45 |
  | 3 | 5 | 3 | 40 |
  | 4 | 2 | 3 | 43 |
  | 5 | 5 | 0 | 43 |
  | 6 | 14 | 7 | 27 |
  | 7 | 7 | 1 | 40 |
  | 8 | 5 | 15 | 28 |
  | 9 | 3 | 6 | 39 |
  | 10 | 6 | 5 | 37 |
  | All | 51 | 48 | 381 |

  - **These are counts of places, not of place-windows.** The tool in
    the kept build prints how many places fall in each class, not
    which. So the pilot's place-windows can't be counted under this
    naming, nor its lax places told from its strict ones, until the
    tool gate 5's build session was asked for is built. It also works
    only the sources a seed starts with.
  - What the counts do show: about a tenth of places are hot and a
    tenth dim under this naming (2 to 14 and 0 to 15 a seed), where
    the first naming finds no hot ground at all without life.
  - With every place kept busy at once, the same tool counts fewer hot
    and more dim: by seed, hot 1, 1, 1, 2, 4, 2, 2, 0, 0, 3 and dim
    16, 9, 25, 11, 11, 20, 33, 25, 22, 21. A full neighbourhood pulls
    the voltage down before the cores can heat.
- **What it says for the mark.** Nothing yet: no seed reached stage 4,
  so no place-window counts toward it under either naming.
  - The floor of 100 place-windows of each kind looks reachable under
    the judged naming, if seeds reach stage 4 as gate 3's did (18 of
    20): about 5 hot and 5 dim places a seed, each giving a window
    every 200,000 ticks. That is a guess until the tool tells lax
    places from strict; and seed 5 has no dim place at its start.
  - Under the first naming, reported beside the mark, hot windows will
    come only where life fills a place, as before.
- **The rest of the physics, all seeds:** a place 5.29 degrees above
  the outside on average, the hottest 182.1 (seed 5, place 27); 70,891
  threads ended by heat; a place at 1,027 mV on average, below the P
  cores' threshold for 4.61% of place-ticks and the E cores' for 1.14%
  (gate 3's supply: 928 mV, and the P cores out 21% of their ticks);
  9 to 14 sources a seed at the end, rated 1,258 to 2,606 W together.
- **So step 11 is done** as section 5 has it: the numbers are agreed
  and the pilot ran to its ticks without errors. Gate 4's marks wait
  for the combined run. Still not done, and not Claude's to do: the
  build's seven defaults and the classing tool, both with gate 5's
  build session, neither confirmed yet.

**Four things an outside reviewer asked the combined run's report to
show (2026-11-20): what the records give now.** Relayed by the
map-design session as a suggestion: not a change to the mark, and not a
decision of Damian's. Nothing below is built or asked for. Each is
checked against what the pilot's files hold: the ground record
(`windows/seed-N-ground.csv`: a row for each place and 200,000-tick
window, with its cores, the core-ticks it lost to cut-outs and to heat
trips, the ticks its reset region was off, its class, the programs it
began with, the births in it, and who led it), the turnover windows,
and the samples, which are for the whole world.

1. **The value behind each class, not only its name.**
   - *First naming: the files give it now.* Each row has the core-ticks
     lost to cut-outs and to heat trips beside the class; its core
     time is its cores times 200,000. The report prints only counts by
     class, so a spread by class is worked from the files after the
     run, or is a few lines of report code.
   - *Judged naming: in the tool's specification already.* The
     classing tool works the two losses before it names a place, and
     the specification sent to gate 5's build session asks for both
     beside the class, as core-ticks. (Corrected the same day: this
     note first said the specification asked for the class only. That
     was wrong, and was reported so before it was checked against the
     message as sent.)
2. **Births for each core-tick, nominal and actually available.**
   - *The files give a version now.* Births, cores and both losses are
     in each row: nominal core time is cores times 200,000, and
     available is that less the two losses.
   - *Two limits, each new code to lift.* The record's births are every
     program born in the place, whoever's code ran. Code that noise
     ran makes thousands a window in an empty place (seed 7's place 1:
     4,592 births in a window begun with no program; 45,007 of the
     pilot's 324,691 births were faithful copies). Births that are
     copies need a column of their own, which the observer can count
     as it sees each birth. And "available" here is core time not cut
     out or tripped: it counts cores idle for want of a thread. Core
     time actually worked needs a counter at the place and a column.
3. **Arrival and persistence apart from numbers, by class.**
   - *The files don't give it.* What they hold is who led each place as
     each window began, so a leading lineage's stay can be read at
     200,000 ticks' grain, and nothing of lineages that never led.
   - *The observer holds most of it in memory, unwritten.* For every
     lineage at every place beyond the one it began in it keeps the
     colonist's birth tick, the arrivals that founded it, its births
     there, its living members, the tick its last member there died,
     and whether it settled. The report prints these as sums by seed.
     A file of them by place is report code only, with no new
     bookkeeping. Joined to the classes it gives, by class, the share
     of colonies that settle and how long each lasted.
   - *Not held by place as far as the report shows:* arrivals that
     landed and made no program, which are nearly all of them (seed 3:
     11,291 landed, 8 made a program). The report gives them by seed.
     By place is new code; how much wasn't looked into.
4. **The within-place reading as a standing table.**
   - *The files give a coarse one now.* A place's rows in order are the
     table: the programs each window began with, and the core time
     lost to trips and to cut-outs in it. The reading of places 19, 24
     and 47 above was made that way, by hand.
   - *Three limits.* A window is 200,000 ticks, and a place filled
     within one or two of them, so "filling" is a row or two. The
     record has programs at a window's start, not how busy the cores
     were in it, and it is busy cores that heat: gate 3's seed 15 had
     59 programs with 33,559 threads. That is the same counter as in 2.
     And "after a crash" needs a rule for what a crash is (say, a
     window that begins with under a tenth of the most the place has
     begun with), which is a choice to agree, not a measurement.
   - *As a standing table it is report code;* the first version can be
     worked from the files after the run with none.

Claude's view, for whoever decides: the two columns in 1 are worth
asking for now, while the tool is being written (they were in its
specification already: see the correction in 1). One counter, core
time actually worked in each place-window, serves both 2 and 4 and is
the only change that reaches the engine. The colonies' file in 3 is
report code and can wait for the combined run's report. All of it
would be gate 5's build session's to write.

**The mark's counter counts programs that don't copy (2026-11-20): an
open point, not a change.** Raised by the map-design session from
`runs/open-2`'s records, and told to Damian as open. Nothing is built
or asked for.

- **What it found** (its figures, not checked here): 105 of open-2's
  200 places have never held a kept genome in 25 million ticks, yet
  their windows start with 63 programs on average and have 14,290
  births. Those are programs born of static bytes whose children
  aren't copies and die within about 20,000 ticks. Lax places whose
  replicators have faded often still start windows with 1,000 to 4,000
  programs, none of them a replicator.
- **So "programs a place-window starts with" isn't a count of life
  that copies.** Keeping to lax places, as the mark does, removes most
  of it but not what is left in a lax place after its replicators go.
- **What the records hold on it now:**
  - The observer knows, for every living program, whether it was born
    as a copy (faithful, or at least half like its parent's genome)
    and whether it came of an arrival. It reads that for each program
    in a place as each window begins, to find who leads it.
  - The ground record writes that count only for the leaders:
    `lead_lineage_copies` of `lead_lineage_programs`, and
    `lead_group_copies` of `lead_group_programs`. It has no count of
    copies among all a window's starters.
  - So the files give a lower bound now, the leading lineage's copies
    made there, and how much of the window that lineage is
    (`lead_lineage_programs` against `starters`). Where one lineage is
    nearly all of a place the bound is close; where many small static
    lineages share it, it says little.
  - A column for copies among all starters would be new code: small,
    since the count is at hand in the walk that finds the leader, but
    it changes the ground record's format.
- **In the pilot** the bound is nearly the whole answer, since the
  leading lineage was 179 of its 181 starters. Lax windows began with
  168 programs in all, 80 of them copies made there, and 76 of those
  are one window of seed 7's place 19 (97 programs). Strict windows
  began with 13, none a copy. Seed 1's place 40 began a window with 10
  programs of one lineage and no copy among them: the static kind.

**The reviewer's measures: to be added (Damian, 2026-11-20).**

- **His decision,** through the map-design session, which asked:
  "Should the gate 5 build session add these measures now — the two
  columns, the core-time counter and the copy-births column?" His
  answer in full: "Yes, add them".
- **Covered:**
  1. the classing tool's two values behind each class;
  2. one counter of core time actually worked, for each place and
     window, in the ground record's rows;
  3. a column of births that are copies beside the record's births;
     and starters born as copies, only if the observer already holds
     it. It does (above), so that column is in.
- **Not covered:** the file of arrivals and persistence by place, which
  waits for the combined run's report; the split within and between
  places; the fingerprint.
- **The mark is unchanged.** It still counts the programs a
  place-window starts with. Whether it should count only programs born
  as copies is a question the map-design session has put to Damian. It
  isn't decided, and nothing here assumes an answer.
- **Who builds it:** gate 5's build session, told by the map-design
  session and sent the exact form below. Nothing is built yet.
  (Corrected later the same day: the build session says no message
  from the map-design session on these measures reached it. It has
  the form from this plan and builds from it.)

The exact form, as sent. The names are suggestions; the meanings are
the specification.

- **The classing tool's rows** (one a place and classing tick). The
  first specification already asked for the core-ticks cut out and off
  by heat. Added, so that a row gives the two shares by itself:
  - `ticks`: the ticks the place was kept full for (10,000). Its core
    time is `cores` times `ticks`.
  - `cut_share` and `heat_share`: `cut` and `heat` over that core
    time, to four places. These are the two values behind the class:
    hot is a heat share over 0.25, dim a cut share over 0.25 (or the
    reset region's memory off for over a quarter of the ticks). A
    row says too when a place is hot and dim at once.
- **The ground record,** three columns added at the end of each row,
  so the columns it has keep their places:
  - `worked`: core time the place's cores spent running something in
    the window, in core-ticks. It is the measure the scheduler already
    makes of each core each tick for its draw (the part of the tick it
    ran a thread or an idle run, the rest halted), added up over the
    place's cores and the window's ticks. With `cut` and `heat` it
    shouldn't pass cores times 200,000: a check for the build.
  - `births_copies`: of `births`, the programs born as copies made
    there: faithful, or at least half like the parent's genome, and
    not an arrival's birth. It is the test `lead_lineage_copies` uses.
  - `starters_copies`: of `starters`, the programs born so. It is no
    fewer than `lead_lineage_copies` and no more than `starters`.
- **One limit of `worked`, said plainly.** It counts time a core ran
  code that noise started (an idle run) with time it ran a program's
  thread, because that is what the scheduler measures and what heats.
  In a full place the two are nearly the same; in a sparse one some of
  it is noise's. Thread time apart would be a second counter, which
  Damian wasn't asked about. It isn't asked for.
- **The reference.** `runs/gate4-reference-d`'s ground record has the
  22 columns of today. A build with the new columns is checked against
  it on those 22; the three new ones are extra.

**How the report uses them, beside the mark.** All of it is reported,
none of it judged. It is printed for the mark's own windows (lax
places of the seeds that reach stage 4) under the judged naming, and
again under the first naming.

- **The value behind each class.** For each class, the lowest, middle
  and highest share of core time its places lose to heat trips and to
  cut-outs. Under the judged naming the shares are the classing rows':
  what a full place loses there. Under the first naming they are the
  ground record's: what the place lost in the window. It shows whether
  hot means just over a quarter or nearly all.
- **How full each class's places were.** `worked` over core time, by
  class. The mark compares programs; this says whether a class's
  windows were busy or idle.
- **Programs that copy, beside programs.** For each class, the programs
  a window starts with and, beside it, those born as copies. The two
  ratios the mark asks for (fair against hot, fair against dim) are
  printed again on copies alone, marked as not the mark. This is the
  figure the open question above would turn on.
- **Births for core time.** For each class, births and births that are
  copies for each million core-ticks, three ways: of all core time
  (cores times 200,000 a window), of core time not cut out or tripped,
  and of core time worked. The first says what the ground yields, the
  second what it yields when the physics lets it run, the third what
  running code yields there.
- **Not in the report yet:** arrivals and persistence by class, and the
  standing table of one place sparse, filling, full and after a crash.
  `worked` makes that table readable from the files when it is wanted.

**Threads beside programs: two more columns (Damian, 2026-11-20).**

- **His decision,** through the map-design session, which asked:
  "Should the gate 5 build session also record, per place per window,
  the thread count and the largest single program's thread count, and
  split thread starts from births in the calls-by-kind counter?" His
  answer in full: "Yes, record them".
- **What is added,** by gate 5's build session. The map-design session
  reported telling it; the build session says that message didn't
  reach it, and it works from what is written here:
  - two more columns at the end of each ground-record row, beside
    `worked`, `births_copies` and `starters_copies`: the threads alive
    in the place as the window begins, and the threads of the one
    program holding the most of them;
  - in the calls by kind, SPAWN counted two ways: a thread started in
    the caller's own program, and a new program.
- **Why it bears on gate 4** (the map-design session's figures from a
  copy of `runs/open-2`'s save, not checked here):
  - single programs hold 1,600 to 31,000 threads in nearly every place.
    Of 19 lax places looked at so far, 18 have a program with over
    1,000 threads; of 19 strict ones, 16;
  - the world starts 1,793 threads a tick against 46 births;
  - they start threads at their own first byte and never copy.
- **So a place's cores are busy, and it heats, whether or not anything
  there copies.** What follows, none of it changing the mark:
  - `worked` will be high in places with almost no programs. It says
    how hard the cores ran, not how much life there is.
  - The first naming's hot ground is made by load, copying or not. One
    program with thousands of threads can turn its place hot as a
    crowd of replicators does.
  - The judged naming is unharmed by it, and fits it: a place held by
    one such program is a full place, which is what that class is
    worked for.
  - Gate 3 saw the same thing in small: its seed 15 ended with 59
    programs and 33,559 threads (the top of this plan).
- **How the report would show a place held by one such program.**
  Reported, not judged, and the lines drawn are Claude's choices:
  - **Named so** when, as a window begins, one program holds over
    half the place's threads and at least as many threads as the place
    has cores, so it could keep every core busy by itself.
  - **Its signature in the row:** `worked` a large share of the core
    time not cut out or tripped; few starters, and few or none of them
    born as copies; the largest program's threads most of the place's;
    births that are copies near none.
  - **In the report, by class:** how many of the mark's windows are
    held so, and the programs and copy-born programs a window starts
    with, for held windows and for the rest apart. Beside it, the
    share of core time worked in each. If the mark's twofold
    difference is in the held windows alone, or in the rest alone, the
    report says so.
  - **Births for core time worked** (above) reads the same thing from
    the other side: a held place works its cores and yields next to no
    copies for it.
- **The mark is unchanged.** It counts the programs a place-window
  starts with, and a program holding 31,000 threads counts as one.

**The defaults and the classing tool are built (2026-11-20, evening).**
By gate 5's build session, as it reported and as the files show;
`build-plan-gate5.md` 11, "Gate 4's classing tool", is its record.

- **The seven agreed numbers are the build's defaults** now
  (`core/src/keys.inc`, read here: 0.98, 19.6, 0.124, 2.48, 100, 800
  and 2.5).
- **The tool:** `nova2 worlds` with `worlds.test = classes` writes
  `seed-N-classes.csv`, a row a place for each classing: its tick,
  the place, its links from the nearest source, its class, whether it
  is hot and dim at once, the ticks, the core-ticks cut out and off by
  heat, the two shares, and its mean voltage.
- **The report** reads that file and judges the mark on the classes,
  either way round, saying which way each stands. The mark as first
  worded and named is printed after it, marked not judged.
- **The three checks set for it are met,** by its report
  (`runs/gate5-classes`): tick 0's counts for seeds 1 to 3 are the
  measuring tool's (hot 2, 2, 5; dim 7, 1, 3; fair 39, 45, 40); the
  same rows on 1, 2 and 24 host threads; and a classing from a tick is
  the same whether or not earlier ticks were classed in the same call.
- **It is five times quicker than planned for:** a classing of 48
  places is 17 seconds on 4 host threads, where the measuring tool
  took 80 to 100, because the places' worlds now run side by side.
- **Not built yet:** the straits at the classing tick (waits for gate
  5's step 9); a respawned place's later windows left out (its step
  6); the five new ground-record columns and the report's readings of
  them; and the reading on untouched ground, which had no rule.

**The reading on untouched ground: its rule (Claude's, 2026-11-20).**
Damian decided only that it is reported beside the mark. The build
session asked which window names a place, and from when. Kept to the
first wording, ground named by the window before the lineage arrived,
and worked from the ground record's rows alone, place by place in
order:

- A window that starts with no program is untouched. It keeps the
  record's own name and counts under it, with no programs.
- A window that starts with any program counts under the name of its
  place's latest untouched window, with the programs it starts with.
  The name carries until the place next starts a window empty.
- A window with programs whose place has no earlier untouched window
  is left out and counted. It shouldn't happen: every place starts
  empty.
- The report prints it for the mark's own windows, with the same two
  ratios, marked not judged, and says how stale the names are: the
  mean ticks from the naming window to the window it names.
- **Its limits:** with nothing living no place is hot, so its hot
  count will be near none; and where most places always hold some
  program, few windows are untouched and the names go stale. It is a
  cross-check on the dim half, no more.
- **It isn't gate 5's "ground gate 5 left alone"** (no patch over the
  place and no kill there in the window), which is that plan's to
  set.

**The pilot under the judged naming (2026-11-20, evening).**
`runs/gate4-pilot-classes` (`run.ps1`, `join.pl`, `join.txt`): the
pilot's ten seeds classed by the new tool at the start of each window,
and joined to the pilot's ground record. Exit 0 for every seed.

- **Lax place-windows, all seeds, as fair, hot, dim** (programs they
  start with in brackets):

  | Naming | Fair | Hot | Dim |
  |---|---|---|---|
  | Judged: tick 0's classing (the 2,000,000-tick step) | 1,760 (155) | 230 (12) | 220 (1) |
  | Judged, classed at each window's own start | 1,767 (161) | 223 (6) | 220 (1) |
  | First naming (the record's) | 1,989 (168) | 0 | 221 (0) |
  | Untouched ground, by the rule above | 1,989 (168) | 0 | 221 (0) |

- **By seed, judged naming at the step, lax, as fair, hot, dim:**
  seed 1: 180, 20, 30; 2: 200, 10, 0; 3: 210, 40, 10; 4: 260, 20, 30;
  5: 200, 30, 0; 6: 150, 50, 30; 7: 170, 40, 10; 8: 120, 0, 80;
  9: 120, 0, 30; 10: 150, 20, 0.
- **Lax places hot when full, at tick 0:** 23 in all, in 8 of the 10
  seeds; dim, 22, in 7 of the 10.
- **The floor looks reachable** under the judged naming: two million
  ticks of ten seeds already give 230 hot and 220 dim lax windows. It
  still needs seeds at stage 4, which the pilot has none of, so the
  mark itself isn't read.
- **The 2,000,000-tick step holds up here:** 126 of 4,800
  place-windows (2.6%) have a class at their own start unlike tick
  0's, and the lax totals barely move. A longer run has more sources
  opening and ending, so this is worth reading again there; the tool
  is now quick enough to class at every window if wanted.
- **Seed 7's place 19,** the one place life filled, is fair under the
  judged naming too.
- **The untouched reading is the first naming here,** to the window:
  the pilot has too little life for the two to part.

**The measures are built (2026-11-20, night).** By gate 5's build
session, to the form above, as it reported; `build-plan-gate5.md` 11,
"The reviewer's measures, and threads beside programs", is its record.
Read here in the source: the columns' names and the held-window line.
Its tests and its equivalence check are its report, not run here.

- **The ground record's five new columns,** after the 22 it had:
  `worked`, `births_copies`, `starters_copies`, `threads`,
  `top_threads`.
  - `worked` is whole core-ticks, rounded down. With `cut` and `heat`
    it never passes cores times the window: a core cut out or tripped
    works nothing that tick. Its tests check that on every row, and
    nothing is clamped.
  - `worked` counts code that noise ran with programs' threads, as the
    form said. Thread time apart doesn't come for nothing (the
    scheduler's measure doesn't say which), and isn't built.
  - `starters_copies` is in: one pass over the place's programs a
    window.
- **SPAWN two ways,** in the samples' calls by kind: `spawns_thread`
  and `spawns_program`, each with its count that succeeded. A SPAWN
  asks for a thread when its address is in an allocation one of the
  caller's program's threads is running in, or in one of its stacks
  (the engine's own rule). Any other allocation it holds asks for a
  program. An address it doesn't hold asks for neither.
- **The report now has:** place-windows by class; the closer look by
  class, by the first naming, and on untouched ground by the rule
  above, with how old the names are and the windows left out; held
  windows, by the line above; gate 4's mark judged on the classes
  (not judged if a stage-4 seed has no classes file); then the mark as
  first worded, reported only. The untouched reading is kept apart
  from gate 5's "ground gate 5 left alone".
- **Its checks, by its report:** 37 suites pass, and the equivalence
  test passes against `runs/gate4-reference-d` on its 22 columns.
- **What a finished run needs to get the new report:** carrying on
  from its saves with the new build, which prints the report without
  running anything. The pilot's saves are gate 4's own build's, from
  before the observer's save changed format. Whether the new build
  reads them wasn't tried; the pilot's counts under the judged naming
  are the join above, and its record has the 22 columns only.
- **Still to come, with gate 5's steps:** the straits at the classing
  tick (its step 9) and a respawned place's later windows left out of
  the mark (its step 6).
