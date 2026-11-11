# Nova 2 — Triage of the two design reviews

> **Inputs:**
> - `design-review-codex-2026-11-11.md` (Astra: 25 findings and 9 smaller
>   corrections);
> - `design-review-fable.md` (Fable: 52 findings and a table of 21
>   decisions).
>
> Both reviewed the same 2,002-line `design.md`.
>
> **Prepared by:** Claude, 2026-11-11. Nothing in `design.md` has been
> changed yet. This file sorts every finding into decisions for Damian,
> specs Claude writes, and text fixes Claude makes.

## 1. How the reviews compare

- **They agree wherever they overlap,** and their specific claims check out.
  Claude verified a sample of each against `design.md`, `design-history.md`
  and Nova 1's records. All held, including Fable's Nova 1 figures: 898
  million instructions a second on 24 threads, and 64 disaster settings, not
  59.
- **The status line was wrong.** "Complete apart from numbers" overstated
  it. The program-facing rules (instructions, calls, the reset region) are
  still sketches, and both reviews say so.
- **The biggest single problem, found by both:** life doesn't change what a
  place draws. Idle cores run the reset soup at full draw from tick 0, and
  memory draws whoever owns it. So "crowds cause sag", "the world warms as
  life grows" and "programs spend power by spawning" don't follow from the
  rules as written.
- **Errors Claude introduced in the clean rewrite:**
  - the cold-start trigger lost its discrete wording;
  - "[starting point]" was put on the world's noise method;
  - the Nova 1 speed comparison joined two benchmarks;
  - the disaster settings count (59, not 64);
  - "sharing a device with Direct3D 12";
  - the overstated status line.
- **Faults inherited from the history,** present in both documents:
  - the encoding claim;
  - GPU EXIT against "SYS faults on a GPU";
  - the boot loader's second stage, which can't read freed blocks;
  - the untraced replicator;
  - the custody rule against the pipelined delay formula;
  - the parasite test missing the in-place hijack;
  - "only blackouts lose data".
- **One citation can't be checked:** Astra's 2026 BFF follow-up postdates
  Claude's knowledge. Its point stands without it.

## 2. Decisions for Damian

"Reverses" marks a recommendation that undoes something Damian decided
earlier. Those need his explicit yes. The sources are Astra's findings (A)
and Fable's (F, with D for its decision table).

### The reset region and life's start

| # | Decision | Options | Claude recommends | Source | Reverses? |
|---|---|---|---|---|---|
| R1 | What an idle core does with nothing new to run | (a) Halt at idle draw after a fault until something writes to the reset region, with a slow retry. (b) Run a set share of the time. (c) Keep it, and rewrite the power story | **(a).** Plus memory drawing more when accessed than when only holding (real DRAM: active against self-refresh power), so that life raises draw | A2, F1, D1 | No |
| R2 | What keeps the reset region changing | Step each core's start spot through the region at each restart. Rely on noise | **Step the start spot,** and have the origin test report distinct paths tried | F2, D2 | Partly: it changes the decided fixed start spots |
| R3 | Cap an idle run, and bound port 0 | A run cap longer than the dearest birth (a watchdog). A bounded port-0 queue that drops the newcomer | **Yes to both** | A15, F3, D3 | Yes: "port 0 never refuses" |
| R4 | May ownerless code SEND? | Keep it and observe it. Remove it | **Keep it,** and give observers a measure of each region (compressibility, ownerless SENDs) | F4, D4 | No |
| R5 | Where a program's record begins | At the first ALLOC (decided today). At SPAWN: the idle run holds its allocations, and the record starts at SPAWN with the run's cause | **At SPAWN.** It fixes the genome-less records, and the deaths counted twice | A7, F4, F44, D5 | Yes: today's gap-1 answer, in refined form |
| R6 | The reset region when its memory device fails | Move to the next device (decided). Stay put, with no idle runs at the place until the visit | **Stay put.** Moving needs free pages and can overwrite allocations | A12, F33 | Yes |
| R7 | Hand-placed programs in tests | None (decided). Unit tests only. Test worlds too, never runs | **Unit tests and test worlds, never runs.** Otherwise an origin failure can't tell a hostile world from a broken SPAWN | A22, F29, D19 | Yes: "not even as a test hook" |
| R8 | The origin test's pass mark, budget and fallback | Set them now. Later | **Now.** Staged: copying appears; viable offspring; several generations; establishment despite faults; adaptation under crowding. Plus a compute budget, a seed count, and what a fail triggers | A21, F28, D18 | No |

### Time, cost and power

| # | Decision | Options | Claude recommends | Source | Reverses? |
|---|---|---|---|---|---|
| R9 | What a tick and a turn are | Budgets of cycles. Slices of simulated time | **Time.** A tick is a fixed simulated duration. A core's cycles in it are speed × duration. Turns and quanta are slices of time, and fairness counts time | A3, F6b, D8 | Partly: §2 defines the tick by instructions |
| R10 | SEND's cost | Cheap, as now (about 180 cycles). Dear like disk I/O: the thread sleeps while its device transmits (latency + size ÷ bandwidth) | **Dear.** Still fire and forget, and still no delivery news | F6a, D7 | No |
| R11 | Does fetching an instruction pay memory latency? | Yes. No, and drop memory's speed axis | **Yes.** Otherwise slow memory wins everywhere at no cost | F6c, D9 | No |
| R12 | The voltage solve with nonlinear loads | Devices as conductances from last tick's voltage, one linear solve a tick. Iterate to consistency within the tick | **A one-tick lag,** plus a minimum off-time after a cut-out so self-sag can't make cores flicker | A1, F31 | No |
| R13 | Quanta, and yielding | Scale each core's quantum by its speed; one SLEEP value means "end my turn". Neither | **Yes to both** | F48, F50, D21 | No |

### Disk, memory and ownership

| # | Decision | Options | Claude recommends | Source | Reverses? |
|---|---|---|---|---|---|
| R14 | The disk | Owned allocations (decided today), with persistence fixed some other way. Raw and unowned: DREAD and DWRITE take any offset, and a cold start loads the first block | **Raw and unowned.** It makes a second stage work, removes disk ALLOC and FREE, disk size classes and per-block protection, and makes backups overwritable (a trade-off). A disk fails when its boot block passes its endurance, and a visit restores it | A10, F7, D10 | Yes: "disk allocated like memory" |
| R15 | Which allocation counts as SPAWN's "own code" | The one holding the caller's IP. A program's first allocation | **The one holding the caller's IP.** SPAWN into an allocation where any of the caller's threads is running makes a thread, never a gift | A8 | No |
| R16 | Where OS state lives | Protected infrastructure. In simulated memory | **Ownership, allocation and scheduler records are protected infrastructure.** A thread's saved registers and stack live in its program's memory, and fade and corrupt with it | A11, F33 | No |
| R17 | A replacement device of a different size | A fresh size, shifting addresses. Keep the predecessor's size | **Keep its size;** the other numbers are drawn fresh | A12, F33 | No |
| R18 | DIV by zero | Fault (decided). Return a value | **Return 0,** as ARM does. One less way for random code to stop | F8, D11 | Yes |

### What's built, and when

| # | Decision | Options | Claude recommends | Source | Reverses? |
|---|---|---|---|---|---|
| R19 | A staged build | Build everything. Build in gates, each mechanism at the gate that needs it | **Gates** (Fable's five): (1) does a replicator arise; (2) does a full place keep turning over; (3) does life spread; (4) does physics shape it; (5) does it stay interesting. Everything stays in the design; it's built when its gate comes | F28, A24 | No |
| R20 | The GPU and NPU | Keep, naming the condition where each wins. Park them, designed but unbuilt until a gate names that condition. Cut them | **Park.** Neither has a winning condition yet; the NPU has no matrix work in the world | A24, F9, D12 | Yes: they were kept |
| R21 | Fable's park list | Row by row | **Assign each to a later gate rather than drop it.** Two exceptions: the E core keeps its own cost table (Damian decided measured tables; use fractional cycles), and PING and neighbour SENSE stay once R10 makes SEND dear | F10, A24, D13 | Partly |
| R22 | The HLSL rule | Enforce it with an HLSL build and a hash test from day one. Relax it to a style | **Relax it to a style:** flat arrays, fixed records, integer maths, explicit order. The confirmed C++ features (references, constructors, namespaces, templates) aren't HLSL anyway | F11, D14 | Partly |
| R23 | The UI's device stack | D3D11On12 with Direct3D 11. Direct2D alone: a window target on screen, a WIC bitmap target off screen | **Direct2D alone.** It needs WIC, a Windows platform component, so it needs your approval | A, F37, D20 | Corrects Claude's wording |
| R24 | "Every number is a setting" | As is. Draw a boundary | **Draw the boundary.** Structural constants (instruction width, opcode table, register count, call-table shape) are part of the design. Everything else stays a setting, and setup checks the constraints between settings | A25, F12 | Partly |

### Smaller choices

| # | Decision | Claude recommends | Source |
|---|---|---|---|
| R25 | What SENSE reads | The nominal threshold (wear stays hidden), plus page size, strict or lax, device and neighbour counts, each device's kind, and each core's speed | F16, F42, D15 |
| R26 | Heat and errors | Drop §5's separate "heat causes errors" line. The high-voltage edge already covers it | F17, D16 |
| R27 | SETCORE against the reason NUMA was dropped | Keep SETCORE. Reword the NUMA reason: a cost by luck was the problem, and a pin is a chosen, costed act | F20, D17 |
| R28 | The network model | Hop by hop. Each hop pays per-hop + length + size ÷ its own bandwidth + its queue. The route metric is static (per-hop and length) | A16, F41 |
| R29 | The CPY instruction | Runs whole within its turn, leaves its registers unchanged, and any overrun of the quantum is repaid next turn. The length is capped at 2^16. The replicator trace follows from this | A13, A14, F8 |
| R30 | Turnover in a full place | Wait for gate 2. Add "does a full place keep turning over?" to §14.1 now. If not, the lever is a visit that reboots a place, using the existing cold start | F5, D6 |

## 3. Specs Claude writes (no real choice in them)

- **An operational appendix** (A13, F8, F14, F33, F43, F44):
  - for every call: what any 32-bit argument means, what comes back, and
    which value means failure;
  - overflow, signed division, and shifts of 32 or more;
  - IP's value, and the base of relative jumps;
  - sign extension;
  - `SWP #` and `POP #`;
  - the stack: size, device, owner, and word width;
  - initial registers for threads, prompt runs and lanes;
  - RECV's sender id (in B) and one message per RECV;
  - SETHANDLER's timer period and port arguments;
  - HRET after a fault (it resumes past the faulting instruction);
  - a fault inside a handler;
  - the low-voltage interrupt's recipient;
  - SPAWN's starting value;
  - the message size limit, and several waiters on one port;
  - "both" on a strict place;
  - address 0 being invalid.
- **The 14-byte replicator,** traced register by register, including its
  child at another address (A6, F8).
- **Lifecycle tables** for owners, threads, devices, allocations and pending
  operations (A7, A14).
- **Page access against allocation ownership.** A thread may touch any byte
  of a page where its program holds an allocation; executable implies
  readable; multi-page operations stop at the first page it can't touch
  (A9).
- **One tick-order page:** the equations for power, heat, wear and errors,
  and the update order (A3, A1, F31, F49 heat-step clamping, F40 cheaper
  per-tick forms).
- **The laws page and a count of settings,** to test principle 2's "laws fit
  on one page" (F12).
- **A table of the default world's numbers,** with placeholders and the key
  ratios (F30).
- **Observers** (A18, A19, A20, F21, F32, F51):
  - the parasite test renamed "cross-owner execution";
  - a hijack detector from sampled code hashes;
  - the genome as the extent the parent wrote, with the birth image kept as
    a fact;
  - each detector's evidence and storage cost;
  - a retention rule for the fact log and the genebank.
- **The respawn transition** (A17), when respawning's gate comes. Sources
  stay at their land positions and reattach. A bridge to a respawned place
  is rebuilt or removed. In-flight messages to the old place are dropped.
  Observers see an incarnation counter.
- **Long operations** (A14), and route recomputation at nightfall (F38).
- **Performance reporting** (A23): instructions, bytes processed, simulated
  time and reproductive throughput; a storage budget (about 55 GB a day at
  10,000 births and deaths a second).

## 4. Text fixes Claude makes

- **The status line:** remove "complete apart from numbers".
- **The encoding claim** (A4, F13): `#` makes instructions one or two bytes,
  so a flip can merge an instruction with the next byte.
- **GPU EXIT** (A5, F9): moot if R20 parks the GPU. Otherwise EXIT becomes the
  one call a lane may make.
- **Neighbour SENSE's cost** against §9.7 (F15).
- **"10% more speed costs about 33% more power"** becomes about 17% under the
  speed law (F18).
- **"0 means off"** is inverted for the respawn share and the replacement
  threshold, and doesn't fit degrees per doubling (F19).
- **The "[starting point]" label** is removed from the world's noise method
  (A, F22).
- **Data noise** already has a law (corruption by length), and gets a
  setting (F23).
- **"Scans"**, the block-size reference, and "five kinds" above six rows (F24,
  F25).
- **Terms** (F26): node and place; static (the start state) and the floor
  (the ongoing flips); pages and allocations; lowest threshold rather than
  most enduring.
- **Missing open items** (F27, F36). These are the reuse candidates, the
  per-device floor option, faults being too harsh on early replicators, and
  "what the first benchmark is". The history's other dropped clauses come
  back too:
  - "the falloff must be wide";
  - "a grid region must be large enough";
  - "a slope of 0 turns off wear";
  - "in a way no other quantity already gives";
  - page sizes are powers of two;
  - resuming on the other backend.
- **The UI's real size** (F34): about 5,000 lines of viewer and view layer to
  rework, not 350. The canvas also needs pixel read-back.
- **Nova 1's figures** (F35):
  - 898 million instructions a second, so the estimate is 2–6 times Nova 1;
  - the GPU won on young worlds;
  - 44 rows, of which about 24 are map generation;
  - 64 disaster settings.
- **The common row as a grid** of kinds against fields (F46), and the three
  kinds of link (F47).
- **North America has four interconnections** (F52).
- **Astra's smaller corrections:**
  - a failed core's running thread crashes first;
  - RECV overruns only if the program passes a length larger than its
    buffer;
  - how rowhammer counts CPY;
  - "soft sources shrink the hot zone" is a hypothesis;
  - bounded power doesn't prove equilibrium;
  - "only blackouts lose data" is wrong;
  - restore the cold-start trigger: the voltage fell below the reset memory
    device's threshold.

## 5. Order of work, once the decisions are in

1. Apply the decisions (section 2) to `design.md`.
2. Write the specs (section 3), the operational appendix and the replicator
   trace first, because the origin test depends on them.
3. Make the text fixes (section 4).
4. Record everything in `design-history.md`'s log.
