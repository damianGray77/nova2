# Nova 2 — Design review (Fable)

> **Reviewed:** `design.md` as it stood on 2026-11-11 (2,002 lines). Section numbers (§) and line numbers refer to that file.
>
> **Checked against:** `design-history.md` and Nova 1's code and docs (`E:\Projects\nova`). Section 10 says how.
>
> **Changed:** nothing. This file holds findings only.

**How to read this**

- Findings are numbered 1–52. Numbers 1–40 are the ones given in chat on 2026-11-11, under the same numbers. Numbers 41–52 are smaller points left out there.
- **Prediction** marks a claim about how the world will behave. Those are my reasoning and are untested. Everything else can be checked against the text.
- Each finding ends with what it needs:
  - **Decision:** yours to make;
  - **Spec:** something to write down, with no real choice in it;
  - **Text fix:** a correction that needs no decision;
  - **Test:** something only a run can show.
- Quotes from the history carry its line numbers, as "history 1468".
- Section 9 lists every decision in one table, with the option I'd pick.
- Finding 6 corrects one thing I said in chat.

## 1. Summary

**Verdict.** As a document, `design.md` is in good shape. It reads cleanly, its cross-references resolve bar one, and it is honest about what is unproven. The problems are in the design, in two places:

- **The program-facing rules are still sketches.** Sections 4–7 (power, heat, hardware) go down to shore drains and fan thermistors. Sections 8–10 (instructions, calls, the reset region) are what the origin test depends on, and there the rules don't always produce what the text says they produce.
- **Principle 7 has been applied to almost nothing.** The design carries about seventy mechanisms by my count. Few have a failure or a stated test behind them.

**What matters most**

1. **The reset region (findings 1–4).** Idle cores never rest, the region stops changing, nothing limits a run, and code with no owner can travel. The origin test and the power story both rest on this.
2. **Death (finding 5).** Nothing ends a program that doesn't fault. Once a place is full, evolution runs at the death rate.
3. **Three numbers (finding 6).** SEND's cost, the unit of a turn, and whether fetching an instruction pays memory latency. Each decides whether a whole group of choices is real.
4. **Disk (finding 7).** A booted program can't read what its predecessor saved. Making the disk raw and unowned fixes that and removes machinery.
5. **The cut list (findings 9–12).** The GPU and NPU have no work that pays, and ten more mechanisms can wait for a run to ask for them.

### All findings at a glance

| # | Finding | Kind | Needs |
|---|---|---|---|
| 1 | Idle cores never rest, so life doesn't change what a place draws | Logic | Decision |
| 2 | The reset region stops changing soon after the start (prediction) | Logic | Decision, test |
| 3 | Nothing limits an idle run or says how arrivals queue | Logic | Spec, decision |
| 4 | Code that never becomes a program can travel; first-ALLOC records have no genome | Logic | Decision, text fix |
| 5 | Nothing ends a program that doesn't fault | Logic | Test, then decision |
| 6 | SEND costs a hundredth of a birth; the turn's unit; memory speed never shows | Logic | Decision |
| 7 | A booted program can't read its predecessor's disk; a worn boot block is never repaired | Logic | Decision |
| 8 | The 14-byte replicator hasn't been checked against the rules it runs under | Logic | Spec, decision |
| 9 | The GPU and NPU have no work that pays | Cut | Decision |
| 10 | Ten mechanisms to park until a run asks for them | Cut | Decision |
| 11 | "The rules compile as HLSL" is paid now for a backend that may never exist | Cut | Decision |
| 12 | Nobody has totalled the settings, and the laws aren't listed | Cut | Spec |
| 13 | A flipped bit can change how the next byte decodes | Text | Text fix |
| 14 | RECV returns a count, or a count and a sender | Text | Spec |
| 15 | Neighbour SENSE costs a hop, or the crossing only | Text | Text fix |
| 16 | SENSE reads a threshold that wear raises | Text | Decision |
| 17 | Heat causes errors in §5 but not in the catalogue | Text | Decision |
| 18 | "10% more speed costs 33% more power" predates the speed law | Text | Text fix |
| 19 | "0 means off" is inverted for two visit sliders | Text | Text fix |
| 20 | SETCORE is the pin that NUMA was dropped to avoid | Text | Decision |
| 21 | The parasite detector doesn't detect §9.4's hijack | Text | Spec |
| 22 | [starting point] tags a method that changes the world | Text | Text fix |
| 23 | Data noise "would need a law"; one exists, with no setting | Text | Text fix |
| 24 | "Scans" and "block size (6.3)" point at nothing | Text | Text fix |
| 25 | "Five scheduled kinds" above a table of six | Text | Text fix |
| 26 | Four terms used two ways | Text | Text fix |
| 27 | Two open items missing from §14 | Text | Text fix |
| 28 | No build order, pass mark or fallback for the origin test | Missing | Decision |
| 29 | "Not even as a test hook" has no scope | Missing | Decision |
| 30 | No table of the default world's numbers | Missing | Spec |
| 31 | How loads enter the voltage solve | Missing | Spec |
| 32 | Genomes include leftovers; nothing tracks code after birth | Missing | Spec |
| 33 | Details a builder would have to invent | Missing | Spec |
| 34 | The canvas claim holds; the viewer is not a 350-line port | Check | Text fix |
| 35 | Four Nova 1 figures to correct | Check | Text fix |
| 36 | Details dropped on the way from the history | Check | Text fix |
| 37 | Direct2D can't share a device with Direct3D 12 directly | Engine | Decision |
| 38 | Route recomputation at nightfall hasn't been costed | Engine | Spec |
| 39 | A few arbitrary places will take most blind sends (prediction) | Engine | Decision, later |
| 40 | Two per-tick costs with cheaper forms | Engine | Spec |
| 41 | Network delay: one formula, two models | Smaller | Spec |
| 42 | SENSE can't read page size or strict/lax | Smaller | Decision |
| 43 | Strict places cost a page per program; "both" there is undefined | Smaller | Spec |
| 44 | An idle run that has ALLOCed is owned but still not a thread | Smaller | Spec |
| 45 | "Cores cut out first, memory last" is an assumption | Smaller | Spec |
| 46 | The common row doesn't fit every kind | Smaller | Text fix |
| 47 | Links come in three kinds; the doc speaks of one | Smaller | Text fix |
| 48 | No call yields | Smaller | Decision |
| 49 | Heat stepped once a tick can overshoot | Smaller | Spec |
| 50 | Fast cores run alone at the end of each tick | Smaller | Decision |
| 51 | The fact log and genebank have no retention rule | Smaller | Spec |
| 52 | One real anchor to correct | Smaller | Text fix |

## 2. Rules that don't do what the text says

### 1. Idle cores never rest, so life doesn't change what a place draws

**The text**

- §10.2 (line 1373): "Every idle CPU core runs it, P or E." Line 1382: "Cost: idle cores draw power unless their code sleeps."
- §9.2: SLEEP is on the list of calls that fail for code with no owner.

**The problem**

- Junk in the reset region can't sleep. So every powered core draws working power from tick 0, in empty places as much as full ones. A core's idle draw (§7.1) is never reached.
- The line about sleeping comes from the history (1468). It was written when any first call made a program. Gap 1 (first ALLOC) removed the only way to sleep, and the line was never revisited.

**What it undoes**

- §5 (line 437): "The world starts cold ... and warms as life grows." It warms from tick 0, with or without life.
- §4.5 (line 405): "more threads (SPAWN)" as a way to spend power, and the commons that follows. A place's cores are all busy whether it holds no threads or a thousand.
- §4.3 (line 347): night "punishes crowding, not presence". Crowding becomes a matter of how much hardware sits near a source.
- §12.5 (line 1795): "long-lived lineages may pay for their own success." Wear follows heat, and heat no longer follows the lineage.
- Programs can't shed load. If every thread sleeps, the cores run the reset region at full draw. Only underclocking lowers a place's draw.
- The history recorded the underlying risk and a fix (762–764): "power is the driving input, yet no program holds or spends it ... the fix would be a cost tied to the voltage drawn." `design.md` kept neither.

**A second cost.** The engine executes every core in the world every tick, however empty the world is.

**Options**

- (a) An idle core that faults halts at idle draw until something writes to the reset region, then restarts. Real anchor: a machine with nothing bootable halts.
- (b) Idle cores run a set share of the time.
- (c) Keep it, and rewrite §4.5, §5 and §12.5 to say that draw is set by hardware and supply, not by life.

I'd take (a) with a slow retry. Replaying unchanged bytes gives the same result (finding 2), so (a) loses only the replays. The retry keeps the one thing a replay can add: a wrong result from the core's own error floor.

**Needs:** decision.

### 2. The reset region stops changing soon after the start

*Prediction.*

**The text**

- §10.2: each core "starts from its own fixed spot". "On each start the registers point into the region." "A fault with no handler restarts the core at its spot."
- §10.2 lists four sources of change and puts "junk code writing over it (the main churn, as in BFF)" first.

**The reasoning**

- A core restarted at the same spot with the same registers runs the same path through the same bytes. Its own writes can change the path for a while, but nothing new enters, so it settles into a repeat.
- The history saw this for instant crashes (1505–1507): "code that crashes at once would crash the same way forever (the world is deterministic)". Its answer was the error floor.
- So the floor is what keeps the region moving, not the junk. The search for a replicator then runs at the error rate times the computing. Nearly all idle computing is replays.
- The floor has two opposing jobs. It must be high enough to unstick the region, and "low enough that a program usually lives out its life unhit" (history 1586). §7.5 allows 0.
- BFF's churn came from pairing tapes at random every round. The history notes (1484–1486) that BFF worked with and without background mutation, "so noise wasn't the key ingredient". The reset region has no counterpart to the pairing. A place has a handful of cores at fixed entry points.
- History 1534: "A node has few cores, so the soup is small: test this first." That is not in §10.6.

**What I'd do**

- Decide what keeps the region changing before building the origin test. Two candidates:
  - step the start spot through the region at each restart, so every byte becomes an entry point;
  - accept that origins happen only where memory or cores are noisy, and lead §10.6's levers with noise.
- Have the origin test report distinct paths tried per second. Instructions per second will look healthy while nothing new is being tried.

**Needs:** decision; test.

### 3. Nothing limits an idle run, and nothing says what happens when arrivals queue

**What ends a run**

- The history (1566–1568) says a run ends when "the core restarts, or a ready thread takes it". `design.md` uses "ends" in §9.4, §10.3 and §13.1 without defining it.
- Code that loops without faulting never ends. In an empty place no thread comes to take the core, so the loop holds it until noise breaks it.
- History 1524 notes that static makes `jmp $`, a two-byte endless loop, often.

**Arrivals**

- §10.4 (line 1418): "An arrival schedules the next spare core to run it." That has two readings, and both fail:
  - If a new arrival interrupts the run in progress, a stream of arrivals stops any of them finishing a birth. A birth takes about 11,000–53,000 cycles (ALLOC, CPY, SPAWN). Finding 6 explains why streams are the normal state.
  - If it waits for the run to finish, one looping payload per core seals an empty place against every later arrival.
- "Port 0 never refuses" (line 1429) can't hold once arrivals can queue.

**Needed**

- A cap on a run's length, longer than the dearest birth. A watchdog reset is the real anchor.
- A bounded queue for port 0, dropping the newcomer when full, like every other queue.
- Where in the region a payload is written, and what one longer than the region does.
- The starting registers for a prompt run.
- Whether an idle run has a stack. If it has none, PUSH and POP (16 of the 256 byte values) restart the core.

**Needs:** spec; one decision (port 0 can refuse).

### 4. Code that never becomes a program can travel, and the first-ALLOC rule gives observers records with no genome

**Travel**

- §10.4 (line 1423): "Only complete replicators travel. An arrival must ALLOC, copy itself out and SPAWN."
- §9.2: "SENSE and SEND work, because they need no owner."
- So an arrival can SEND itself onward and never ALLOC. It pays no SPAWN, runs only on spare cores, and appears in no fact, lineage or genebank.
- *Prediction:* this spreads before any program does, because it needs one call where a program needs two or three.
- It is also the only thing that carries bytes from one reset region to another, which the origin needs (finding 2). So I wouldn't remove it.
- What to do:
  - fix §10.4;
  - say this kind of code exists;
  - give observers a measure of the regions themselves, such as how compressible each region is and how many SENDs come from ownerless code per sample.
- §10.6 doesn't say whether "a replicator" means this or a program. The pass mark depends on it.

**The record**

- §9.4: a thread with no owner becomes a program at its first ALLOC.
- That record has no thread (the idle run "is not a thread") and no allocation it started in. §13.2 defines a genome as "the bytes of the allocation it started in", so it has none.
- Every origin and every migration then logs two programs: the idle run, then the child it spawns. The first always ends with cause "no thread after its idle run", so deaths are inflated by one for every birth from the region.
- Simpler: the run holds its allocations, and the program record begins at SPAWN, taking the run's cause (static, arrival or boot) directly. §13.1's wording, "its cause (SPAWN, an arrival, a boot, or static)", already describes that model.

**Needs:** decision (does ownerless SEND stay; where the record begins); text fix (§10.4).

### 5. Nothing ends a program that doesn't fault

**The text**

- Causes of death: the thread running on a core when it cuts out (§7.6), a fault with no handler, hardware failure, and EXIT.
- §9.1: "when memory is full, requests fail and nobody is killed."

**Why it matters**

- Tierra has a reaper, and Avida's births overwrite a neighbour. Those are what keep their populations turning over.
- The history took this up when it chose separate programs over colonies (4538–4545: "No turnover ... Selection blurs"). It concluded (4579) that "memory turns over as in Tierra".
- Separate programs make death per copy possible. They add no cause of it, and Tierra's turnover is the reaper.

**Prediction**

- Once a place is full, births happen at the death rate. A place with T threads on C cores loses C threads per cut-out, so about C in T each night. In the band of a steady source it loses none.
- The 14-byte sketch does die on its first failed ALLOC: ALLOC returns 0 and it copies to address 0. That gives early turnover. A two-byte check removes it, and selection will find that quickly.
- Engine speed then buys generations only while places are filling. At capacity the measure that matters is deaths per program per day.

**Two things that follow from "nobody is killed"**

- **Threads that never end.** SLEEP 0 or RECV −1 with no handler holds memory forever. History 3248: a waiting thread "hangs forever only if it set no timeout".
- **Taking everything always wins.** Holding memory costs nothing, so principle 6's test fails for "how much to take". One program can ALLOC a place's free memory and spawn copies as its own threads inside it.
  - No attack in §9.8 can then reach it. Shared pages, input and rowhammer all need the attacker to hold memory at that place.
  - Observers see one program and no births.
  - This is the colony form the history weighed, and kept as "the evolved choice".

**What I'd do**

- Add to §14.1: "does a full place keep turning over?", measured as deaths per program per day, by cause and by place.
- Say in §9.4 where turnover is expected to come from.
- If runs show too little, one lever has a real anchor: a visit that reboots the place, as maintenance windows do. It uses the cold start that already exists. It contradicts §7.8's "programs are never touched", so it is a decision.

**Needs:** test; decision if the test fails.

### 6. Mailing a copy costs about a hundredth of a birth, and the turn's unit decides whether SETCLOCK is a choice

**(a) SEND is cheap**

- §9.7 (line 1304) gives the shape: "making threads, changing clocks and all I/O are dear."
- But SEND is "the crossing, plus about the CPY cost per byte": about 180 cycles for 14 bytes. SPAWN is 10,000–50,000.
- The receiving place's spare core does the ALLOC, CPY and SPAWN on idle time that nobody is charged for.
- A program can also mail itself to its own place's port 0. The history rejected that as the required route to offspring (4546–4550), but it stays available, and it skips the sender's SPAWN whenever a core is spare.
- The history's two brakes on floods (2466) were the full exit queue and "the network device's power per byte". Power is a commons (§4.5), so only the queue is a private cost.
- *Prediction:* every thread with nothing better to do mails copies. Queues stay full. PING, neighbour SENSE and the comms ports work on what is left, which is little.
- A fix that fits the stated shape: make SEND dear like disk I/O. The thread sleeps while the network device transmits, for the device's latency plus size ÷ bandwidth.

**(b) The turn**

- §8.4 (line 972): "A turn gives a thread a budget of cycles." The history had "a core" (3469). Neither says whether turn length is cycles or time.
- With a fixed budget of cycles:
  - overclocking only ends your turn sooner. Everyone on the core shares the gain, and the extra errors are yours alone;
  - underclocking stretches your turn at everyone's expense and cuts your errors.
- So each thread's best move is to underclock, and overclocking never pays in company.
- SETCLOCK becomes a trade-off if a turn is a slice of time, and the cycles in it are slice × speed.
- §2 defines a tick as "instructions per core per tick". That doesn't fit speed following voltage. A tick has to be a span of time too.
- **Correction:** in chat I said this also applied to SETCORE. It doesn't. A faster core gives each of its threads more cycles per tick under either definition.

**(c) Memory speed never shows**

- CPY costs a flat 2 per byte, and the sketch has no `[B]` or `[D]` operand. So memory's 4–12 cycle latency adds nothing to a generation that is mostly SPAWN.
- Slow memory has the lower threshold and holds longer (§6.3). So it wins everywhere at no cost, and principle 6's test fails for the memory catalogue.
- The open item "the cost of fetching instructions" (§8.4) decides this. If each instruction pays its memory's latency, fast memory is worth its fragility. That makes it a design decision, not a number to tune later.
- For the same reason, a 14-byte and a 140-byte program breed at nearly the same rate.

**(d) Clock switching**

- §9.5 says switching clocks "has a cost and a settling delay". §9.7 prices SETCLOCK at 10,000–100,000.
- Unsaid: whether that cost recurs at every turn boundary between threads at different levels. If it does, it swamps a turn. If it doesn't, per-turn clocks contradict "real DVFS is slow".

**Needs:** decision on (a), (b) and (c); spec for (d).

### 7. A booted program can't read what its predecessor saved, and a worn boot block is never repaired

**The second stage**

- §9.4: when a program ends, "its pages and disk blocks are freed".
- §9.3: DREAD and DWRITE "work on blocks the caller holds an allocation in, plus the boot block". And "ALLOC can't be aimed at a particular corpse."
- §10.5 (line 1446): "Programs bigger than the block need a second stage that DREADs the rest."
- A deep outage ends every thread, so every disk allocation at the place is freed. The booted code owns nothing on disk. It can reach its old blocks only if a fresh ALLOC happens to return them.
- The history states the problem in the passage that introduces the boot block (1330–1331): "Nothing can read back what a program saved, and its owner's death frees the range anyway." The boot block answers the first half. The second half is still there when the second stage is described (1357).
- So a private disk allocation helps only a program that is still alive, and that program could have used memory.

**The boot block itself**

- Disk wear is counted per block (§7.4). No rule turns worn blocks into a failed disk, in either document.
- At the defaults (wear levelling off, replacement threshold 1), a boot block worn out by programs racing to be the last writer stays bad until the whole disk fails from heat.

**One change fixes both**

- Make the disk raw and unowned, as the boot block already is. DREAD and DWRITE address any offset. A cold start loads the first block.
- That removes disk ALLOC and FREE, the permission argument on FREE, disk size classes and per-block protection.
- A second stage works, because it reads fixed offsets.
- Backups become something programs can overwrite, which is a trade-off they can take either side of.
- Real anchor: a disk with no file system, which is what boot-sector viruses lived on.
- Also define when a disk has failed, for example when its boot block passes its endurance, so that a visit restores it.

**Also unstated**

- The block size. §7.2 cites §6.3, which has none. History 1309 says only "a setting".
- Whether several disks form one offset space.
- The boot block's offset.

**Needs:** decision.

### 8. The 14-byte replicator hasn't been checked against the rules it runs under

**The sketch** (§8.3): `MOV IP; STO B; MOV # 14; SYS # ALLOC; STO D; CPY # 14; MOV D; SYS # SPAWN; JMP # −12`

The history wrote it when "the call numbers are placeholders until the OS call table is designed" (3408). It works only if three undefined things fall the right way:

| The sketch needs | What the doc says |
|---|---|
| `MOV IP` gives the address of that instruction, so the copy starts at byte 0 | Nothing. And whichever way it reads, "push the return point and jump" can't be a bare `PUSH IP`: the return lands on the push or the jump, not past them |
| CPY leaves B and D unchanged, so `MOV D` is the copy's start | It cites `rep movsb`, which advances both. A copy longer than a turn also needs somewhere to keep its place |
| ALLOC's permission decodes as "both" | The permission is B, which holds the replicator's own address. No rule says how an arbitrary value decodes |

- Children land on size-class boundaries, so their addresses share their low bits. Under any decoding by low bits, either every child works or none does.
- On a strict place, §8.3 says "two more bytes". That holds only if PROTECT ignores B and the page is 64 bytes. B would otherwise have to mean one thing to ALLOC and another to PROTECT. §9.3 says a copier "must take a whole page", and 256 or 1,024 doesn't fit a one-byte constant.
- Page size and strict or lax can't be read by SENSE (finding 42).

**Why it matters.** §10.6 uses the sketch to set the target length for the origin test. How a garbage argument decodes is as much an origin lever as the call numbering, and it has had no design.

**What to write**

- One table. For each of the 18 calls: what any 32-bit value in each argument means (modulo, clamp or fail), and what comes back.
  - Principle 13 ("every call number and argument value is valid") is true today only for the call number and ALLOC's device.
  - The doc says failure is "0 or −1" without saying which call returns which.
- The instruction semantics the doc leaves open:
  - the base of a relative jump. `JMP # 0` is a no-op under one reading and an endless loop under the other;
  - whether a byte read into A is sign- or zero-extended. It decides whether `JLZ` can ever fire after `MOV [B]`;
  - shift counts of 32 or more;
  - signed or unsigned DIV;
  - what `SWP #`, `POP #` and a spare slot with `#` do with the operand byte;
  - CPY with a negative or huge count, overlapping ranges, and a fault part-way.
- I'd make DIV by zero return a value, as ARM and RISC-V do. It removes one way for random code to stop, which is principle 13's aim.

**Needs:** spec; one small decision (DIV by zero).

## 3. Machinery the principles would cut

Principle 7 says a mechanic is added only to fix a failure seen in a run, or to meet a stated test. No runs exist yet, and few of the mechanisms below meet a stated test.

The pattern behind this section: principle 9 (follow real design) wins every argument against principles 7 and 8. A real anchor is treated as a reason to add. An anchor shows that a mechanism is plausible. It doesn't show that the world needs it.

### 9. The GPU and NPU have no work that pays

**The test.** Principle 6: "for every choice a program can make and every hardware model, name a condition in which the other side wins."

**The GPU.** I can't name a condition where a program gains from GLAUNCH:

- lanes can't ALLOC, SPAWN or SEND, so they can't reproduce or travel;
- the only use the history names is "copying or scanning many places at once" inside the pages handed over (3212);
- copying 100 bytes costs about 200 cycles on a core. A launch costs thousands in overhead, and the SPAWN that follows costs 10,000–50,000.

**The NPU.** §9.5 says it "earns its place only if the world has matrix-shaped work". The world has none. The history calls it a "device nothing uses" until some task needs it (3330).

**Both are also the least specified parts of the OS**

- A lane's only way out is EXIT, which is SYS 4. §8.3 and §10.2 say SYS on a GPU ends the launch.
- Nothing shares lanes between two launches, and nothing bounds a launch whose lanes loop.
- GLAUNCH's "pages" and NMATMUL's two operands, result and shapes aren't fitted to four registers. No element width is given.
- "Neighbouring addresses read together are fast; scattered reads are slow" is a cost model of its own.

**What cutting them removes:** two calls (16 left), the lockstep machinery, two catalogue rows and two detector rows. Fewer calls also raises the odds that random code finds ALLOC and SPAWN.

**Needs:** decision. If they stay, name the winning condition first.

### 10. Ten mechanisms to park until a run asks for them

Each is designed. None has a failure behind it.

| Mechanism | Why park it |
|---|---|
| Cooling devices (§7.7) | To a program, a fan is a place with a different cooling rate, and the cooling field already gives that. Most of the common row means nothing for it |
| Respawning places (§7.8) | The history says it "may almost never happen" (3840). It moves bridge ends that are "fixed for the run", a source's place, and what an id points at. None of that is specified |
| Rerouting without loss (§11.6) | Hop-by-hop custody in a network whose principle is end to end (principle 15) and whose everyday loss is silent tail drop. It reads as a leftover of "nothing gets dropped" |
| Wear levelling "on", like-for-like replacement, visits tied to density | Each is off or neutral by default, so the default world never tests it. That runs against "new features are on by default" (§2) |
| PING and neighbour SENSE | Neither pays while a blind SEND is cheaper than asking (finding 6) |
| The low-voltage interrupt | History 4152–4154: "barely easier to evolve than polling ... So it isn't needed at first" |
| The scheduler's sleeper bonus | Plain round robin is fair and deterministic. No user is waiting on a wake-up |
| The E core's own cost table | A 15% penalty on a 1-cycle instruction needs units smaller than a cycle. One table and a slower clock give the same trade-off |
| Five patch types and false alarms (§12.3, §12.4) | Not needed before the origin and turnover tests. Build them when a run shows the world is too steady |
| Rowhammer (§9.8) | Its threshold is counted per tick, so it scales with tick length. At 25,000 cycles a tick (Nova 1's figure) one core manages 2,000–5,000 memory accesses, under the 10,000–139,000 anchors. It costs a counter on every memory access |

A note on rowhammer: it is the only attack that reaches a program holding whole pages, so it has a job. It can't do that job until its threshold has a unit tied to the tick.

**Needs:** decision, row by row.

### 11. "The rules compile as HLSL" is paid now for a backend that may never exist

**The text.** §2 (line 140): "The rules are written once, in a restricted C++ that also compiles as HLSL." The GPU backend is added only if two benchmarks call for it.

**The problems**

- The history caps the backend's best case at "a few times the CPU", as an unmeasured estimate (5483). `design.md` dropped that.
- Code that isn't compiled as HLSL in every build stops compiling as HLSL. The rule as written has no check behind it.
- It collides with the C-with-classes list. References, constructors and destructors, templates and namespaces aren't HLSL. The history's claim that the two subsets match (4771) was written before the allowed list was confirmed.
- The approved compiler (`d3dcompiler_47`) has no 64-bit integers, which a fixed-point voltage solve wants.
- The doc never says which code the rule binds. The history went from "the interpreter core" (5487) to the whole "engine, resident on the GPU" (5515).

**Either**

- build the HLSL compile and a same-hash test from day one, and say exactly which files are bound; or
- relax the rule to a style (flat arrays, fixed records, integers, explicit order), and port if the backend is ever approved.

**Two rules the GPU plan implies for the CPU now**

- "DREAD and DWRITE batched once a tick" must be the CPU's rule too, or the hashes differ. §9 doesn't say disk I/O completes at the barrier.
- History 5500: "a run can resume on the other backend." That constrains the snapshot format, and it isn't in `design.md`.

**Needs:** decision.

### 12. Nobody has totalled the settings, and the laws aren't listed

- The comparisons with Nova 1 are per topic (44 against about 20; 59 against about 33) and leave out the catalogue.
- The history counts the catalogue at "about ten fields for each of nine kinds, plus the source types" (4429), and flags the risk (4436): "Too many numbers to tune by hand. Nova 1 needed a settings-search tool for the same reason." `design.md` has neither the count nor the risk.
- Add the drawn fields (each with its own range, scale, edge strength and sharpness), two cost tables, 18 service costs and the call map. Nova 2 is likely near Nova 1's 385.
- The history's settled test for principle 2 was "the laws fit on one page" (418). It was dropped, and `design.md` never lists the laws.
- Write that page. If it doesn't fit, that is the finding. My count is about seventy mechanisms.

**Needs:** spec.

## 4. Contradictions in the text

| # | Where | What | Needs |
|---|---|---|---|
| 13 | §8.1 (line 917) | "A flipped bit changes one instruction, never how the rest decode." A flip into or out of operand `#` turns the next byte from an instruction into a constant, or back | Text fix |
| 14 | §9.2 vs §11.2 | The call table says RECV returns a count. §11.1 and §11.2 say it returns a count and the sender's id. Where the id goes is unsaid, and so is whether one RECV takes one message | Spec |
| 15 | §9.2 vs §9.7 | Neighbour SENSE "costs one hop of network time". §9.7 lists SENSE as "the crossing only" | Text fix |
| 16 | §9.2 vs §7.4 | SENSE reads a device's threshold. Wear raises the threshold. "SENSE does not read wear." The history (1752) defined per-device SENSE for memory devices only, so nothing reads a core's speed, though §7.4 relies on it | Decision |
| 17 | §5 vs §7.1, §7.5 | §5 lists "errors near the top of a device's range" as an effect of heat. The catalogue defines errors by voltage edges and clock only | Decision |
| 18 | §4.4 vs §7.5 | "10% more speed costs about 33% more power" assumes speed follows voltage. With speed following (voltage − threshold), it is about 17% at a threshold of 0.7 and a voltage of 1.0 | Text fix |
| 19 | §2 vs §7.8 | "0 means off" for sliders. The respawn share and the replacement threshold are off at 1, and 0 would tear down every place at every visit. History 3835: "Above 1: never." Heat wear's unit, degrees per doubling, can't be 0 either | Text fix |
| 20 | §6.3 vs §9.5 | The history dropped NUMA partly because pinning "is priority by another name" (1709). SETCORE is a strict pin | Decision |
| 21 | §13.3 vs §9.4 | The parasite test (the thread's owner differs from the owner at its IP) is called "a hijack". §9.4's hijack runs the victim's thread in the victim's memory, so the owners match and nothing detects it. Code that runs off its end into a neighbour's allocation will trip the test by accident | Spec |
| 22 | header vs §6.2 | [starting point] is defined as an observer-side method. It tags the field-generation method, which changes the world | Text fix |
| 23 | §12.3 vs §11.3 | "Noise on data links would need a law of its own." Corruption by route length is that law. §11.7 has no setting for its rate | Text fix |
| 24 | §7.2 | "Scans" is a Nova 1 cost category with no Nova 2 instruction. "Block size (6.3)" points at a section that has none | Text fix |
| 25 | §12.1 | "Five scheduled kinds" above a table of six. In Nova 1, brownout was the surge's warning, not a kind | Text fix |
| 26 | terms | "Node" for place (§8.3, §9.3). "Static" for both the initial random bytes and an ongoing flip rate (§7.5, §10.6, §14.1). Pages "owned" (principle 14, §13.3) where §9.3 says allocations are. "Most enduring" memory device, where the lowest threshold is what matters | Text fix |
| 27 | §2, §7.5 | The [open] list of reuse candidates in §2 isn't in §14. The per-device floor "option" in §7.5 is undecided and untagged | Text fix |

## 5. Missing

### 28. No build order, pass mark or fallback for the origin test

- §10.6 gates everything, but states no pass mark, no budget and no fallback.
- Principle 11 says tests decide. Decide now what a fail triggers, while nothing is built and nothing is sunk.
- A staged plan would apply principle 7 for you: each mechanism is built at the gate that needs it. One possible order:

| Gate | Question | Built for it |
|---|---|---|
| 1 | Does a replicator arise from static? | §8, §9.1–9.4, §10.1–10.3, one place, a noise source, the region measure of finding 4 |
| 2 | Does a full place keep turning over? | The scheduler, memory fade, cut-outs, one place on a varying supply |
| 3 | Does life spread? | The map, links, SEND, port 0, the boot block |
| 4 | Does the physics shape it? | The voltage solve, heat, the day, sources |
| 5 | Does it stay interesting? | Wear, visits, regional events |

- The viewer's views can follow the same gates. Most of §13.4 shows things that won't exist before gate 3.
- "The first benchmark" is cited in §9.8 but defined only in the history.

**Needs:** decision.

### 29. "Not even as a test hook" has no scope

- §10.1 (line 1357): "No ancestor is ever seeded, not even as a test hook."
- The history shows this was deliberate (1415–1416, Log 5131). The hook you rejected was one "that writes a hand-made replicator into the reset region". I'm not reopening that for runs.
- Two things follow from the rule as written:
  - Engine unit tests have to load chosen bytes to test MOV at all. So the rule needs a boundary.
  - Every item in §14.1 after the origin (sizes, cascades, the boot block, input injection, the detectors) queues behind a test that may take weeks or fail.
- A hand-placed program in a test world is an initial condition under principle 2. It isn't a law.
- I'd allow it for tests and experiments, and never for runs. This is your call.

**Needs:** decision.

### 30. No table of the default world's numbers

- Most findings in section 2 come from ratios the doc never states.
- The table should give: places, cores and memory per place, tick length, turn, quantum, day length, band width in links, and a typical program's size. Placeholders are fine.
- Numbers that exist but aren't in `design.md`:
  - the history's reference world is "1,000 places with four cores each" at 25,000 instructions a tick (115, 146);
  - the history's memory figure works out at about 160 KB per place (5506: 16 GB for 100,000 places).
- Ratios worth a line each:
  - SPAWN against a tick;
  - the rowhammer threshold against a tick;
  - a 10-link band and borders 20–30 links apart, against a 1,000-place map about 32 links across. That is room for one to three of anything.

**Needs:** spec.

### 31. How loads enter the voltage solve

- §4.1 calls it "a sparse linear solve". Draw is voltage² × (voltage − threshold), with cut-outs, leakage and temperature. That isn't linear.
- Unsaid in both documents:
  - whether a device is a conductance or a current sink;
  - which tick's draw sets which tick's voltage;
  - the solver and its stopping rule;
  - how fixed point copes with conductances spread over many orders of magnitude. Water links fall off exponentially.
- A one-tick lag matters because of the restart gap (§4.4). The gap is one global voltage, while the sag a place causes itself varies by place.
  - Where self-sag exceeds the gap, cores flicker on and off.
  - Each cut-out ends the thread running on the core.
  - A minimum off-time would stop that. Real undervoltage lockouts have one.

**Needs:** spec.

### 32. Genomes include leftovers, and nothing tracks code after birth

- §13.2: a genome is "the bytes of the allocation it started in". That includes whatever was in the slot's unused tail.
  - On a strict place, a 14-byte program in a 1,024-byte page is 1,010 bytes of leftovers. Every birth hashes as a new genome.
  - Fix: record the extent the parent wrote, and use that. The Scavenger detector needs the same record.
- Nothing tracks what code a thread runs after birth. Overwrites, the colonies of finding 5 and the reset-region code of finding 4 are all invisible.
  - Fix: sample each thread's IP and hash the code around it.
- §13.1's causes of death lack "memory lost its contents". A deep outage ends every thread with no cause to log.

**Needs:** spec.

### 33. Details a builder would have to invent

- **The stack.** Size, device, owner, and whether it is packed with others. A stack page dwarfs a 14-byte program, so it sets what a thread costs in memory. Its position decides whether an overrun can reach a return address.
- **OS data.** Where thread state lives, and whether it fades.
- **Cold start.** The history's test (1381) is crisp: power returns "after the voltage fell below the minimum of the memory device that holds the reset region". §10.5 says only "lost its contents", which gradual fade leaves undefined. Restore the history's wording.
- **The address map.**
  - Whether address 0 is valid. ALLOC returns 0 for failure.
  - What a replacement memory device of a different size does to the addresses after it.
  - Where the reset region lands when it "moves to the next" device, whose pages may be owned.
- **SETHANDLER.** No period for the timer. No port for arriving data. Per thread or per program. Where a fault returns to. A fault inside a handler.
- **The low-voltage interrupt.** Which thread gets it. "Fires once" reaches only the thread on the core at that moment, and a different thread may be running when the core cuts out.
- **SPAWN.** Its "starting value", the new thread's registers, and what happens when it is aimed at another program's allocation in a shared page.
- **Messages.** The size limit, and what happens when two programs wait on one port.
- **Sources.** What happens to a source when its place is torn down. Growth and fade times. Drop-out rate, depth and length. Ramp depth; history 4039–4042 sets it "relative to the swing size".
- **Errors.** What counts as a "result" for the floor. History 4182: "each byte CPY moves, each store".
- **Cut-outs.** What each device does when it is cut out but not failed. The history has the table (4092–4100); `design.md` states fail-soft only for failed devices. History 3884 also says drop-outs act on capacity, which §4.3 doesn't.

**Needs:** spec.

## 6. Checked against Nova 1 and the history

### 34. The canvas claim holds, but the viewer is not a 350-line port

- `app/canvas.h` and `app/canvas.cpp` total 348 lines and hold all GDI drawing. The setup page uses no native controls. So §2's claim about the drawing layer is right.
- The viewer is a different matter:
  - roughly 70% of `app/viewer.cpp` (about 1,950 of 2,783 lines) names Nova 1 concepts: bodies, codons, lineages, substrates;
  - the view layer in `core` that feeds it (`view.h` and `view.cpp`, about 2,800 lines) is Nova 1-shaped throughout.
- What carries over is the frame, the widgets, the charts and the setup page. The setup page is driven by the settings registry, bar about 250 lines.
- Headless renders and ten test sites read the canvas's pixels. The new canvas needs a way to read pixels back.
- §2 reads as if the UI work is 350 lines. It is nearer 5,000.

**Needs:** text fix.

### 35. Four Nova 1 figures to correct

| Claim in `design.md` | What Nova 1's records show |
|---|---|
| "73 million ... 519 million on 24, and was still 98% parallel at 10,000 nodes" (§2) | Two benchmarks joined. The 73 and 519 are a 100-node world. The 98% is a separate landscape profile. Nova 1's best was 898 million on 24 threads (a 1,000-node landscape), so 2–5 billion is 2 to 6 times Nova 1, not 4 to 10 |
| "Nova 1's real GPU ran even its lockstep warps slower than the CPU" (§2) | True for mature worlds. On a young landscape the GPU halved the time (15.2 s against 31.4 s) |
| "Nova 1 had 44 settings for the same job" (§6.1) | 44 rows, of which about 24 are map generation. The rest are the seed, node lifecycle and path hazards |
| "59 settings" for disasters (§12.1, §12.5) | The registry and the survey in this folder both show 64 |

Confirmed as written: six land settings, 22 colourings, 25% false alarms, tests on old defaults, and the E core figures (real measurements of a Core Ultra 9 285K).

**Needs:** text fix.

### 36. Details dropped on the way from the history

The three passes found no place where `design.md` departs from a final decision in the history. The faults in sections 2–5 are in both documents. What was lost in the rewrite:

- "a run can resume on the other backend" (5500), which constrains the snapshot format;
- "The falloff must be wide, or the sources many and small" (584), so that ecosystems sprawl instead of forming dots;
- "A grid region must be large enough to catch sources of its own" (2242);
- "A slope of 0 turns off wear from that cause" (3671);
- the risk that faults "may be too harsh on the first, sloppy replicators" (2878), which isn't in §14.1;
- the last clause of the abstraction rule: a quantity stays only if a program would feel the world differently because of it "in a way no other quantity already gives" (553). §4.1's rejection of AC relies on it;
- page sizes are powers of two;
- what "the first benchmark" is.

**Needs:** text fix.

## 7. Engineering notes

### 37. Direct2D can't share a device with Direct3D 12 directly

- §2: Direct2D and DirectWrite "sharing a device with Direct3D 12".
- A Direct3D 12 device doesn't offer the interface Direct2D needs. Microsoft's own sample goes through the D3D11On12 layer.
- D3D11On12 and Direct3D 11 aren't on §2's approved list, so by §2's own rule this needs asking.
- For the UI alone there is a simpler route: Direct2D with a window target on screen and a WIC bitmap target off screen. It needs no Direct3D 12, it is software-rendered off screen, and it gives the pixel read-back of finding 34.
- Sources: [D2D using D3D11on12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/d2d-using-d3d11on12), [Microsoft Q&A on ID3D12Device and IDXGIDevice](https://learn.microsoft.com/en-us/answers/questions/699369/d3d12-can-use-idxgioutputduplication-acquirenextfr).

**Needs:** decision.

### 38. Route recomputation at nightfall hasn't been costed

- §11.6 prices each topology change at "about one shortest-path search per place".
- History 2397: "Routes recompute around a dark place, as around a failed link." §11.3 lists only links and respawns as triggers.
- Network devices cutting out and restarting with the nightly sag would be the commonest change of all.
- State whether an ordinary night takes the network device below its threshold. If it does, batch the recomputation per tick and cost it at 10,000 places.

**Needs:** spec.

### 39. A few arbitrary places will take most blind sends

*Prediction.*

- Shuffling ids removes "my id + 1" as a free map (history 2297). That works.
- But SEND's place is A, which holds 0, −1 or a small count after most calls. A one-byte constant reaches only 0–127. So a few arbitrary places will take most blind sends.
- One SEND reaches any place at the same cost to the sender. So distance isolates nothing, and §9.6's "nearby moves are safe, and far moves are a gamble" holds only for programs that choose to sense first.
- Worth a look once SEND's cost is settled (finding 6).

**Needs:** decision, later.

### 40. Two per-tick costs with cheaper forms

- **Memory fade.** §7.4 ranks cells by "a hash of its address and the seed". Finding the leakiest cells that way means testing every cell. A seeded permutation from rank to address costs per failed cell. The history had a method too (3636: draw the failing cells per page each tick), which `design.md` dropped.
- **The error floor.** Draw the count of instructions until the next wrong result, and count down. A draw per instruction sits in the hottest loop.

**Needs:** spec.

## 8. Smaller points not raised in chat

### 41. Network delay: one formula, two models

- §11.3 gives delay as one end-to-end sum: hops, route length, size ÷ the narrowest bandwidth, and queue waits.
- §11.5 and §11.6 describe a queue at each link end, messages held hop by hop, and links failing "mid-crossing".
- In a hop-by-hop network the delay emerges at each hop, and each hop pays size ÷ its own bandwidth, not the route's narrowest.
- Pick one model and restate §11.3 in its terms.

**Needs:** spec.

### 42. SENSE can't read page size or strict/lax

- Page size and strict or lax are the two OS values that vary by place (§9.6). Neither is in SENSE's list.
- §9.6 says "a program can adapt to the OS around it". Without a reading it can adapt only by evolving per region.
- Also missing: how many devices and neighbours there are, so an index can't be bounded; and a device's kind.
- Real anchors: `sysconf` and `getpagesize`.

**Needs:** decision.

### 43. Strict places cost a page per program, and "both" there is undefined

- PROTECT needs the whole page. So on a strict place every program holds at least a page: 64 to 1,024 bytes for a 14-byte program.
- §6.3's "a memory device holds hundreds to thousands of programs' worth" assumes packing.
- What ALLOC does when asked for "both" on a strict place is unsaid: fail, or give data.
- *Prediction,* worth knowing: ALLOC with permission "executable" returns a dead program's code, because nothing is zeroed, and SPAWN runs it. That is a birth with no copy and no PROTECT. On a strict place it may be the cheapest one.

**Needs:** spec.

### 44. An idle run that has ALLOCed is owned but still not a thread

- §9.2's list says which calls fail "until then", meaning until the first ALLOC.
- After it, nothing says what SLEEP, EXIT, a waiting RECV, PING, SETHANDLER, SETCLOCK or SETCORE do for something that "holds no place in the scheduler".
- §10.2's "unless their code sleeps" can only mean this case.
- Under finding 4's simpler model this goes away: a run can only ALLOC, PROTECT, FREE, SPAWN, SENSE and SEND.

**Needs:** spec.

### 45. "Cores cut out first, memory last" is an assumption

- §4.4 states it, and §7.6's account of night depends on it.
- Wear raises thresholds, and each device has scatter. A worn fast memory can sit above a fresh E core.
- Make it one of the checks on the drawn devices. §7.3 already says "check the drawn devices before a run".

**Needs:** spec.

### 46. The common row doesn't fit every kind

- "Speed against voltage" means nothing for memory, the network device or a fan.
- "Errors" and "lost at a cut-out" mean nothing for a fan.
- Links have no range, though §7.6 says every kind has "the two ends of each device's range".
- The network device has no trade-off curve. The history's table says "None yet" (4306). That runs against principle 8.
- A grid of kinds against fields, with the empty cells marked, would settle it.

**Needs:** text fix.

### 47. Links come in three kinds, and the doc speaks of one

- The kinds: power only (proximity links over water), data only (bridges), and both.
- §11.6: "A failed link carries neither power nor data."
- Say which kinds wear (only those carrying current), which can be cut, and whether a link in both graphs fails as one thing.

**Needs:** text fix.

### 48. No call yields

- The history's decided scheduler list (2708) has "a thread can step aside or sleep".
- SLEEP counts ticks, so the shortest wait is a whole tick.
- If stepping aside within a tick matters, one value of SLEEP could mean "end my turn".

**Needs:** small decision.

### 49. Heat stepped once a tick can overshoot

- With every number a setting, a heatsink or cooling rate that is large against a heat capacity makes a one-step update swing past its target and oscillate.
- Clamp each step to the gap it is closing, or reject such settings at setup.
- The same applies to leakage, which feeds back on draw.

**Needs:** spec.

### 50. Fast cores run alone at the end of each tick

- §9.5 gives every core the same quantum per round. Faster cores have bigger budgets, so they run more rounds.
- At the end of each tick only the fastest cores are still running, and their writes always land last.
- Scaling each core's quantum by its speed makes all cores finish together.

**Needs:** small decision.

### 51. The fact log and genebank have no retention rule

- The history expects "megabytes per second" from a busy world (4523). `design.md` dropped the figure.
- With the leftovers of finding 32, every birth is a new genome.

**Needs:** spec.

### 52. One real anchor to correct

- §6.2 (line 576): "North America has three main grids." It has four: Eastern, Western, Texas and Québec. The lower 48 states have three.

**Needs:** text fix.

## 9. The decisions, in one list

| # | Decision | Options | I'd pick | Finding |
|---|---|---|---|---|
| D1 | What an idle core does with nothing new to run | Halt until the region changes, with a slow retry. Run a set share of the time. Keep it and fix the text | Halt, with a slow retry | 1 |
| D2 | What keeps the reset region changing | Step the start spot. Rely on noise | Step the start spot | 2 |
| D3 | A cap on an idle run, and a bounded queue for port 0 | Yes. No | Yes | 3 |
| D4 | Does code with no owner keep SEND | Keep it and observe it. Remove it | Keep and observe | 4 |
| D5 | Where a program record begins | First ALLOC. SPAWN | SPAWN | 4 |
| D6 | Turnover in a full place | Wait for a run. Add reboot visits now | Wait, but add the test to §14.1 | 5 |
| D7 | SEND's cost | Cheap, as now. Dear, like disk I/O | Dear | 6 |
| D8 | The turn | A budget of cycles. A slice of time | A slice of time | 6 |
| D9 | Does fetching an instruction pay memory latency | Yes. No, and drop memory's speed axis | Yes | 6 |
| D10 | Disk | Owned allocations, with the second stage fixed some other way. Raw and unowned | Raw and unowned | 7 |
| D11 | DIV by zero | Fault. Return a value | Return a value | 8 |
| D12 | GPU and NPU | Keep, and name the winning condition. Cut | Cut | 9 |
| D13 | The park list | Row by row | Park all ten | 10 |
| D14 | The HLSL rule | Enforce with a build and a hash test. Relax to a style | Relax | 11 |
| D15 | SENSE: nominal or worn threshold; add page size and strict/lax | — | Nominal. Add both | 16, 42 |
| D16 | Heat and errors | Add temperature to the error law. Drop the line in §5 | Drop the line | 17 |
| D17 | SETCORE | Keep. Drop | Keep, and reword why NUMA went | 20 |
| D18 | The origin test's pass mark, budget and fallback | — | Set all three now | 28 |
| D19 | Hand-placed programs in tests | None. Unit tests only. Test worlds too | Test worlds too | 29 |
| D20 | The UI's device stack | Approve D3D11On12 and Direct3D 11. Direct2D on its own | Direct2D on its own | 37 |
| D21 | Yield, and quantum scaled by core speed | Yes. No | Yes to both | 48, 50 |

Everything marked "text fix" or "spec" in section 1 can be done without a decision.

## 10. How this was checked, and what wasn't

**What I did**

- Read `design.md` in full.
- Had three passes each read a third of `design-history.md` in full and report where the two documents differ. I then confirmed by search every history quote used in this file.
- Had Nova 1's code and docs searched for each claim `design.md` makes about Nova 1. I re-checked the line counts, the 898 million figure and the GPU timing myself.
- Checked the Direct2D point against Microsoft's documentation.

**What I didn't do**

- I didn't re-read the whole history myself. A dropped detail that the passes missed is still missing.
- I didn't audit the real-world anchors. Finding 52 is one I happened to know.
- Nothing was run. Every item marked "prediction" is reasoning about a world that doesn't exist yet, and a run could show it wrong.
- Line numbers are for the files as they stood on 2026-11-11.
