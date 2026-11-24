# Nova 2 — Gate 3 build plan

> **Status (2026-11-19):** approved, run, and passed. Damian agreed the eight
> design gaps (2) as proposed, and they are in `design.md` and logged in
> `design-history.md`. Steps 1 to 7 are built (11), and the numbers in 7
> are agreed (2026-11-17). Damian settled the question step 7's runs
> raised (2026-11-17: a static run in an arrival's landed bytes carries on
> its sender's lineage, 13.2), and it's built (11, step 7); and he kept
> 10.2 as designed. Step 8 is under way: its report and the run's settings
> are built, and pilots ran. The second found the network's traffic
> starving the origin of copying, and a flood test found what it is: code
> that copies itself from port 0 to port 0 without becoming a program
> (10.4's travellers). Damian chose two rules, built and on by default:
> idle runs don't SEND, and arrivals hold at most half a place's cores.
> The third pilots brought the origin of copying back to gate 2's pace
> (11, step 8). The 20 seeds ran as `runs/gate3-1` from 2026-11-17 and
> were stopped on 2026-11-18 with 11 done, the build slowed late in a run
> by a bug in the observers; they ran again as `runs/gate3-2` on the
> build of 2026-11-18 and ended on 2026-11-19, and the report reads
> PASS on every part of the mark (11, step 8): a lineage settled
> another place in 17 of the 20 seeds (the mark is 5), stage 4 in 18
> (the mark is 10), and 57,803 of 57,808 windows turned over.
>
> **Gate 3 passed on 2026-11-19,** at its second run. Damian confirmed
> it ("Yes, passed").
>
> Gate 3 asks one question: does life spread? (design 14.1). This plan says what is built,
> in what order, how each step is checked, and what must be fixed before
> the first gate 3 run.
> Numbers in brackets, like (11.2), are sections of `design.md`, which
> stays the authority. When the two disagree, the design wins and the plan
> is fixed.
>
> **The numbers in 7, the pass mark among them, are agreed before the
> first gate 3 run.**
>
> **Where gate 2 left it:** full places keep turning over, with the widened
> saved-state rule (`build-plan-gate2.md` 11, step 10): 586 of 586 counted
> windows turned over at `runs/gate2-2`, and the origin regression reached
> stage 4 in 13 of 20 seeds. Each seed's 48 places have no links, so every
> lineage lives and dies where it arose. The observer's grouping was made
> fast before gate 3 (11, "Seed 3's slowness, traced"), since spreading
> will make more genomes.

## 1. What gate 3 delivers

- **The spread answer** (14.1): whether a lineage that arises at one place
  goes on to settle others, against a pass mark set before the first run
  (7).
- **Its named checks** (14.1):
  - whether CPU saturation shuts arrivals out completely (10.4);
  - whether blind SENDs pile onto a few places (11.1);
  - the cost of recomputing routes at nightfall (11.3);
  - whether the map itself works (6.1).
- **The regressions:** the origin test's stages and gate 2's turnover
  measure, rerun under gate 3's world, as every gate promises.
- **The mechanisms the gate table names** (2): the map (6), links, SEND,
  RECV and PING (11), arrivals at port 0 from other places (10.4), the
  network device, and handlers for arriving data.
- **The engine still deterministic** across thread counts and through
  snapshot and resume, with messages in flight in the world hash.

## 2. Design gaps found while planning

Agreed by Damian as proposed (2026-11-16), applied to `design.md` and
logged in `design-history.md`.

1. **How much of the map comes at gate 3.**
   - The problem: the gate table says "the map (6)", but much of 6 serves
     gate 4's physics: proximity links carry power and heat, water leaks
     current and drains shores, the resistance field draws grid borders,
     and the day sweeps east to west. None of that does anything before
     the voltage solve.
   - Proposal: **gate 3 builds what data needs, and the fields that make
     places differ:**
     - land and water, from Nova 1's land raster and its six settings
       (6.1);
     - places scattered by a density field with a minimum spacing, their
       ids shuffled by the seed (11.1);
     - data links: the Gabriel graph on each landmass, plus bridges;
     - each link's length, bandwidth (from its own field) and delay;
     - 6.2's fields, for the hardware and OS values (gap 8).
   - **Left for gate 4:** proximity links and what they carry, water as a
     conductor (reach and shore drains), the resistance field and grid
     borders, the cooling field, and the day's sweep. The supply stays
     gate 2's, a source per place with its own day (2).
   - On land the Gabriel graphs are almost the same links (6.1), so gate 4
     adds the water crossings without moving anything gate 3 built.

2. **The world's size for the gate 3 runs.**
   - The problem: the default world is 1,000 places on a map about 32
     links across (appendix E). But a run's budget is in instructions, and
     at gate 2's rate (about 1,200 instructions a place-tick) 10¹² buys a
     1,000-place world under a million ticks. Stage 4 took 5 to 15 million
     ticks at 48 places. So at that size and budget, nothing would arise
     to spread.
   - Options:
     - a. **48 places a seed, gate 2's budget:** the regressions compare
       like with like, and a run costs what gate 2's did (about 11 hours
       for 20 seeds). The map is small, about 7 links across.
     - b. **About 200 places, at four times the budget:** a map about 14
       links across, and roughly four times the time (about 2 days).
     - c. **1,000 places at gate 2's budget per place:** about 20 times the
       time. Not practical.
   - Proposal: **a, plus test worlds at full size.** The origin, spread and
     turnover runs use 48 places, so whatever spreads is life from static
     (R7). Test worlds with hand-placed migrants run at 1,000 places, and
     10,000 for the routing cost, to test the map and the network's
     mechanics at the size the design is for (R7: evidence about the
     mechanism, not about life).
   - **Claude's lean:** a. If gate 3 passes at 48 places, b is the next
     question, not this gate's.

3. **What "life spreads" means.**
   - The problem: 14.1 asks the question but no measure answers it yet.
     13.3 names a Colonist (a lineage's first program at a place, born
     from an arrival), but a colonist that dies at once hasn't spread
     anything.
   - Proposal: **a lineage spreads when it settles a place it arrived at.**
     - It **settles** a place when it holds it as stage 4 defines holding
       (10.6): 20 members or more, at least a tenth of the place's
       programs, half of them born as copies, for 100,000 ticks. Its first
       member there must be a colonist.
     - So spreading reuses the hold the origin test already trusts, at a
       second place.
     - Copying at the new place has to happen there: members that arrive
       don't count as born there.
   - **Also reported, not judged:** colonists by place and lineage;
     arrivals that ran, made a program, or did neither; travellers (code
     that only sends itself on, 10.4); and how far each settlement is
     from where its lineage began, in hops.
   - The pass mark itself is in 7.

4. **What a SENSE of a neighbour returns, and when.**
   - The problem: A.4 says a neighbour reading "costs a hop", and appendix
     B makes the thread wait. But places run in parallel, so a reading
     taken during execution would depend on timing. And a neighbour's
     network device can be off.
   - Proposal: **it's one round trip on that link, read at a barrier.**
     - The thread waits for twice the link's delay, outside the queues,
       and the neighbour's OS answers, as it answers a PING (11.2).
     - The reading is the neighbour's state at the barrier when the
       request lands: its id, voltage, temperature, idle cores and free
       memory.
     - If either network device is off, every field but the id reads 0,
       as an empty list's index reads 0 (A.4).
   - "Neighbours" are data neighbours: the places a data link reaches.

5. **Who takes an arriving-data interrupt.**
   - The problem: SETHANDLER registers a handler for "arriving data on a
     given port" (9.5), but not what happens when several threads have
     one, or a thread is already waiting in RECV.
   - Proposal:
     - **A message goes to a waiting RECV first:** the thread that has
       waited longest takes it (11.2), and no interrupt is raised.
     - **Otherwise it joins the inbox, and every thread with a handler for
       that port gets the interrupt pending.** Each takes it when it next
       starts a quantum (9.5). The first to RECV gets the message; the
       others find what's left.
     - An interrupt pending for a port is raised once, however many
       messages join before the thread takes it.

6. **What power loss does to messages.**
   - The problem: 11.5 says inboxes fade in a brownout, and 7.6 says a
     network device that cuts out loses the messages queued at it. But
     inboxes aren't in the address space, so fade has no cells to reach.
     And rerouting messages around a dark place is gate 5's (11.6).
   - Proposal:
     - **Inboxes are lost at a cold start,** with port 0's queue. Any dip
       below the reset region's memory's threshold ends in a cold start
       when power comes back (10.5), so inboxes that could have faded are
       emptied then anyway. Until then they're kept whole.
     - **A network device that cuts out drops everything queued at it:**
       its link-end queues and the message it was sending.
     - **A message whose next hop is a dark place is dropped** when it
       reaches the head of its queue, and counted as stranded. That's
       11.6's lifetime of 0, until gate 5 brings rerouting and lifetimes.
     - Routes are recomputed at the barrier after any network device goes
       off or comes on (11.3).

7. **Time on the network is in whole ticks.**
   - The problem: the barrier is where messages move (appendix C step 6),
     so a message can't cross part of a hop between barriers. The design's
     delays are sums of costs that needn't be whole.
   - Proposal: **each hop takes at least one tick, in whole ticks:** the
     per-hop cost plus length × the per-length cost, rounded up, plus the
     size over the link's bandwidth, rounded up, plus the queue's wait. A
     message crosses at most one hop at a barrier.
   - **And a fixed order where things meet:** messages joining one queue
     in one barrier join in order of the place they came from (by id),
     then their order there. A place's own SENDs join in the order they
     ran. That makes delivery deterministic whatever the thread count.

8. **How the fields set each place's values.**
   - The problem: 7.3 and 9.6 say hardware and OS values come from fields
     over the map, with "details at build". Gate 1 and 2 drew them evenly
     and independently for each place.
   - Proposal: **each value gate 2 draws evenly is drawn from its own field
     instead, mapped onto the same range, plus a small scatter.** That's
     the core count, the share of E cores, the memory device count and
     latencies, the page size, and strict or lax.
     - Each field is 6.2's two layers of one noise algorithm (Nova 1's
       fractal noise), the second through a steep step, seeded per field.
     - Each value is ranked over the places, so the world's mix of values
       stays gate 2's. Only where they sit changes: neighbours alike, with
       barriers between.
     - Each field gets an edge strength and a sharpness (6.2). An edge
       strength of 0 is smooth fields; a switch turns the fields off, for
       gate 2's independent draws.

## 3. The gate 3 world

- **Gate 2's world,** with every rule that passed: 48 places a seed (gap 2),
  the 24 call slots, the 4 KB reset region, the raised noise floors, the
  supply, fade, cut-outs, the cold start, and the widened saved-state rule.
- **On a map** (gap 1): land and water, places by density, data links
  and bridges, each link's length, bandwidth and delay, and fields for the
  hardware and OS values (gap 8).
- **The network** (11): SEND, RECV and PING with their real costs; 16
  ports a place, port 0 the reset region; store-and-forward hops with
  queues and tail drop; inboxes; routes with the least static cost;
  corruption that grows with a route's length.
- **Arrivals from other places** join port 0's queue and run as prompt
  runs (10.4). Gate 2 built the queue and prompt runs for the boot block.
- **The network device** draws its constant power and a little per byte
  sent or received (11.4). It must be on to send, receive or forward.
- **Handlers for arriving data** (gap 5), and SENSE of neighbours (gap 4).
- **Switches** (2: welcome for experiments):
  - `world.map`: `landscape` (the default), `none` (gate 2's unlinked
    places), and `grid` and `ring` (test worlds only, 6.1).
  - `map.fields`: on (the default) or off (gate 2's independent draws).
- **With `world.map = none` and the fields off,** gate 3's engine gives
  gate 2's world exactly. That is the regression check of step 2.
- **Not built at gate 3:**
  - proximity links, water's power effects, grid borders, the day's
    sweep, heat, sources on the map (gate 4);
  - link wear and cuts, outages, rerouting and message lifetimes (gate 5,
    11.6);
  - the GPU and NPU (parked).

## 4. The code: what changes

Gate 3 builds on gate 2's engine. The map is "Nova 1's landscape, rebuilt"
(6.1): its land raster, place scattering, Delaunay triangulation and
fractal noise are reused from `E:\Projects\nova\core\src\landscape.cpp`
(about 900 lines), with its "paths a node wants" replaced by the Gabriel
graph, and its bridges fixed rather than opening on a schedule. Nothing
new from outside.

| Area | Files | What changes |
|---|---|---|
| Settings | `keys.inc` | Land (6), places and density, bridges, the fields, bandwidth, delays, corruption, ports, inboxes, queues, the message limit, the network device's latency and draw per byte, the two switches |
| Map | new `map.*`, `field.*` | Land, places, ids, data links and bridges, link lengths, the fields; worked out once from the seed; test grids and rings |
| Places | `place.cpp` | Hardware and OS values from the fields (gap 8); the network device's draw per byte |
| Network | new `net.*` | Link-end queues, store and forward, routes and next-hop tables, recomputing them, corruption, drops |
| OS | `os.cpp` | SEND, RECV and PING live; inboxes; data handlers (gap 5); SENSE of neighbours (gap 4) |
| Reset region | `region.cpp` | Arrivals from other places in port 0's queue, with their sender |
| World | `world.cpp` | The barrier's network step, in a fixed order (gap 7) |
| Observers | `observe.cpp`, `origin.cpp` | Lineages across places; colonists, settlements and the spread measure (gap 3); travellers; traffic, drops and stranded messages; SEND destinations |
| Snapshots | `snapshot.*` | Format 3: messages in flight, queues, inboxes, pending interrupts. The map is worked out again from the seed, and checked by its hash |
| Views | `app/ui`, `view_cmd.cpp` | The map (13.4): places at their positions, water, links, colourings |

## 5. The order of work

Each step ends with tests that pass before the next starts. "Done when"
names the check.

**Step 1. The spread measure (gap 3), on hand-made arrivals.** Built
first, as gate 2's turnover measure was, so it can be checked before
anything moves between places.

- The observer keeps each lineage's members by place, and when it was
  first at each one, and how (born there by copying, or a colonist).
- Settlement: stage 4's hold at a place a lineage arrived at, with the
  same clock the origin runner keeps for stage 4.
- The origin runner reports, per seed: colonists, settlements, how far
  each is from where its lineage began, and the first tick a lineage
  settled a second place.
- Done when: a test world that puts a replicator's bytes in another
  place's port 0 queue by hand gives a colonist with the cause "arrival"
  and its sender as parent, and its offspring there settle on the clock's
  terms; and gate 2's world, rerun with the measure on, reproduces
  `runs/gate2-2`'s samples for two seeds.

**Step 2. The map (gaps 1 and 8).**

- Land and water, places, ids, data links and bridges, lengths, the
  bandwidth field, and the hardware and OS fields, all a pure function of
  the seed and the settings, worked out once.
- The Gabriel graph from the Delaunay triangulation: an edge is kept if
  no other place lies in the circle on it as a diameter.
- Setup refuses a map whose places don't fit, and reports landmasses,
  links per place and bridges.
- `world.map = grid` and `ring` for tests.
- Done when: the map's tests pass (no crossing links on a landmass, every
  Delaunay edge that's Gabriel kept, bridges only between landmasses,
  ids a permutation, the fields' value mix equal to gate 2's draws); and
  with `world.map = none` and the fields off, the origin runner
  reproduces `runs/gate2-2`'s samples for seed 5 to 1.6 million ticks.

**Step 3. Links, routes and the network device (11.3, 11.4).**

- Next-hop tables at each place, by the least static cost, ties by id.
- Link-end queues of fixed size, in ticks of the link's bandwidth, with
  tail drop.
- Store and forward at the barrier, a hop at a time in whole ticks (gap
  7); corruption by route length.
- The network device: its draw per byte; cutting out drops what's queued
  at it (gap 6); routes recomputed at the barrier after it goes off or
  comes on.
- Done when: in a grid test world, a message's delay is the sum of its
  hops as 11.3 says; a full queue drops the newcomer and keeps the rest;
  corruption's measured rate matches the setting; a dark place is routed
  around; and the routes match a brute-force search on random maps.

**Step 4. SEND, RECV, PING, arrivals and handlers (10.4, 11.2, 11.5).**

- SEND: the crossing, the per-byte copy into the device, then the thread
  sleeps for the device's latency plus size ÷ the first link's bandwidth.
  It fails only for local reasons.
- RECV: one whole message from a port's inbox, the sending place in B;
  polling, timed and endless waits; the longest-waiting thread first.
- PING: the destination's OS answers if its network device is on; the
  thread waits for the round trip or the timeout.
- Port 0: an arrival joins the queue with its sender, and runs as a
  prompt run when a core is free. A program it makes has the cause
  "arrival".
- Data handlers (gap 5) and SENSE of neighbours (gap 4).
- Idle runs may SEND (R4).
- **The observers' part:** a fact for each SEND, so the observer can keep
  the record of a program that sent to a port 0 and then died, while its
  message may still be on the way. Otherwise its colonist would find no
  parent and found a lineage of its own. It's the boot block's last writer
  again (13.2), at a distance. Also counted: arrivals that ran, made a
  program, or did neither, and travellers (gap 3).
- Done when: test worlds show a replicator SENDing itself to a
  neighbour's port 0 and the copy running there as a prompt run, then
  making programs; a RECV getting a message's bytes and its sender; a
  PING's time matching the route's; an arrival never running at a place
  whose cores are all busy with threads; and a handler firing once for a
  burst of messages.

**Step 5. Determinism, snapshots and cost.**

- Messages in flight, queues, inboxes and pending interrupts in
  snapshots and the world hash.
- The fuzz world on a small map, with traffic, under gate 2's hardest
  supply, the invariant checker after every tick in the checked build.
- The benchmark's gate 3 part: the barrier's network step, and route
  recomputing at 1,000 and 10,000 places (11.3: whole tables at 1,000,
  routes found on demand and cached at 10,000).
- Done when: the same seed gives the same hash on 1, 2 and 24 host
  threads, through a snapshot taken with messages mid-hop, and with
  observers on or off.

**Step 6. The gate 3 views (13.4).**

- The map: places at their positions on land and water, data links and
  bridges, coloured by population, lineage territories, network device
  down, and queue fill and drops.
- Charts: traffic, drops and stranded messages; colonists and
  settlements.
- Done when: a headless render matches a stored image pixel for pixel,
  and the views open on a snapshot taken mid-hop.

**Step 7. The test worlds at full size (R7).**

- **The map at 1,000 places:** a hand-placed replicator that SENDs itself
  to its neighbours' port 0 (found by SENSE), and one that SENDs blind.
  Reported: how far each spreads and how fast, and where the blind one's
  copies land (11.1's prediction).
- **CPU saturation:** places whose cores are kept busy by hand-placed
  threads receive arrivals. Reported: how many ever run (10.4's
  hypothesis).
- **Nightfall:** the same map with nights deep enough to take network
  devices down. Reported: route recomputes a tick and their cost, at
  1,000 and 10,000 places (11.3).
- Done when: each runs without errors and reports its numbers.

**Step 8. The gate 3 runs.**

- Seeds 1 to 20 under gate 3's world, at gate 2's budget, reporting the
  spread measure against 7's pass mark, the origin test's stages and gate
  2's turnover measure, and the named checks (1).
- Each seed's report also gives its arrivals: how many landed, were
  dropped at port 0, ran, made a program, lost their core to a ready
  thread, and the first of their calls that failed (agreed 2026-11-17). So
  if nothing spreads, the report says where it stopped.
- Done when: they run to their budgets without errors, and the report
  reads correctly against a hand check of one seed.

## 6. Tests that run throughout

- **Unit tests** for each step, as named in its "done when".
- **The no-network equivalence test:** with `world.map = none` and the
  fields off, gate 3's engine gives gate 2's samples. It runs on every
  engine change, so gate 2's result can't drift unnoticed.
- **The supply independence test,** gate 2's: the supply is the same
  whatever the programs do (principle 3), now with traffic.
- **Test worlds** with hand-placed programs (R7): a sender, a receiver, a
  pinger, a neighbour-sensing migrant, and a blind one.
- **The fuzz world,** on a map, with traffic.
- **Determinism checks** on every engine change: the same hash on 1, 2 and
  24 threads, through a snapshot, and with observers on or off.
- **The checked build and the fast build** run the same tests.

## 7. Numbers to fix before the first gate 3 run

All are placeholders, proposed here and agreed with Damian before the first
run, as 14.1 requires a pass mark set in advance. Changing them afterwards
means rerunning.

**Agreed (2026-11-17):** Damian agreed them all as proposed, the pass mark
among them, after the test worlds of step 7 ("both", to Claude's
recommendation to accept them unchanged and to count first which call
fails for arrivals). The test worlds had found no reason to change them:
most arrivals were lost at port 0s and for want of core time, not on the
links.

| Number | Proposed | Why |
|---|---|---|
| Seeds and budget | Seeds 1 to 20, 10¹² instructions each, 48 places a seed | Gate 2's, so the regressions compare like with like (gap 2) |
| Land | Nova 1's `landscape.cfg`: aspect 1.6, land 0.3, scale 3, warp 0.3, border 0.3, 256 cells across | Its tested look: a few continents and islands |
| Place spacing | As tight as fits, as Nova 1's scattering does; density field: towns about 3 times as dense as countryside | 6.1's towns and countryside |
| Bridges | 2 per pair of neighbouring landmasses, none longer than a quarter of the map's height, every landmass bridged (a landmass with no crossing that short is joined by its shortest one, step 2) | 6.1. At 48 places a landmass left alone would be a place or two |
| Bandwidth | 16 to 256 bytes a tick, from its field, evenly on a log scale by rank; bridges 256 | A 100-byte genome takes 1 to 7 ticks a hop, about a SPAWN's time (R10). Bridges carry most traffic between landmasses (11.3) |
| Delay | 1 tick a hop, plus 1 tick per average link length | At least a tick a hop (gap 7); the per-length part makes long links slow |
| Queues | 8 ticks of the link's bandwidth at each end, and never less than the message limit (agreed at step 3) | About a round trip's worth (11.5). On the narrowest links, 8 ticks is 128 bytes, which would close them to messages over that |
| Corruption | 10⁻⁵ a bit for each average link length travelled | A 100-byte genome crossing 7 links gets a flipped bit about 1 time in 18: travel mutates, but most copies arrive whole |
| Ports | 16 | 11.2's default |
| Inbox | 1 KB a port | Room for several messages |
| Message limit | 1 KB | Never more than an inbox (11.2) |
| SEND's copy | 2 cycles a byte into the network device, on top of the crossing (added at step 4) | 200 cycles for a 100-byte genome; the wait while the device transmits is the bigger cost (R10) |
| PING | 8 bytes each way, the request and its echo (agreed at step 4) | A request and its echo carry nothing but their tags, so they barely crowd the queues, and still wait in them like anything else (11.2) |
| Port 0's queue | 4 KB | Gate 2's |
| Network device | Threshold 600 mV and draw 1 W (gate 2's); latency 1 tick; 1 µW a byte sent or received | 11.4: a tenth of a P core's working draw; forwarding free |
| Fields | Edge strength 1, sharpness 8, scatter 0.05 of each range; noise scale 3 across the map, as the land's | Placeholders: neighbours alike, with barriers (6.2) |
| **Pass mark** | In at least 5 of the 20 seeds, a lineage settles at least one place beyond the one it began in (gap 3). And the regressions hold: the origin test reaches stage 4 in at least 10 of the 20 seeds, and gate 2's turnover mark still passes | 5 seeds says spreading repeats, not a fluke. Spreading needs a replicator to SEND a working copy of itself to a port 0, which nothing has needed so far, so it can't be expected in every seed that reaches stage 4 (13 at `runs/gate2-2`). The regressions are gate 2's marks |

## 8. Risks for this gate

- **Nothing may spread, because nothing sends (Claude's prediction).** A
  replicator spreads only if its code SENDs a working copy of itself to
  some place's port 0, and that place has a free core and an OS it can
  run on. Nothing has rewarded SEND so far. Some routes to it exist: an
  idle run may SEND (R4). And in A.5's 14-byte replicator, a SPAWN
  mutated into SEND would send from its own start S (C) up to the message
  limit (D, the copy's address, clamped), to place N mod the place count
  (A), at port S mod 16 (B): port 0 whenever its own address is a
  multiple of 16. But whether spreading happens within the budget is what
  the runs measure. If nothing spreads, the step 7 test worlds still say
  whether the mechanism works, and the next lever is Damian's decision.
- **Blind SENDs go to a few places** (11.1). A holds 0, −1 or a small
  count after most calls, so most blind SENDs land on a handful of ids.
  Ids are shuffled, so those are random places, not neighbours: spread
  may jump rather than diffuse. The report shows where SENDs land.
- **Arrivals need spare cores** (10.4). A crowded, busy place takes no
  migrants, so settling may only happen at empty or sleepy places.
- **48 places is a small map** (gap 2): about 7 links across, so distance
  isolates little, and "how far" means a few hops.
- **Fields change every place.** With the fields on, each place's hardware
  and OS values differ from gate 2's, even with the same seed. So the
  regressions compare gate 3's world with gate 2's, not seed by seed.
- **Traffic costs computing time** (11.7). A flood fills queues and the
  barrier's network step runs on one thread. A per-place cap on messages
  in flight is the design's fallback, if the benchmark needs it.
- **Everything asks for the slowest memory device (found after step 7).**
  ALLOC clamps an address used as its device argument to the slowest
  device (A.5), and code started from an address asks with one. In the
  test worlds that device was full at inhabited places while the others
  had room, and that was the commonest reason an arrival made no
  program. A lineage spreads into such places only if its code asks for
  another device, or for any (C = 0). Whether evolved code does is part of
  what the gate 3 runs measure. Asking for any device instead (tried after
  step 7) fills places to their memory: the device caps a place's copies
  and leaves room.
- **A static run of an arrival's bytes begins a new lineage (found after
  step 7).** 13.2 gives a static birth no parent. In the test worlds,
  static runs took up the migrants' landed bytes and began lineages that
  outnumbered the migrant's own and reached every place. So 13.3's
  measure can miss spread: a lineage whose code lands at a place and is
  copied there by a static run, not by its prompt run, hasn't spread by
  the measure, and the new lineage's home is that place, not a colony.
  - **Decided (2026-11-17, Damian: "Yes, change it"):** a static run that
    starts in the bytes an arrival last landed, while all of them are as
    they landed, takes the arrival's sender as its parent, as a boot takes
    its block's last writer (13.2, 13.3). Built after step 7 (11, step 7,
    "Changed afterwards").
- **Tuning the network could choose the answer.** Its numbers and the pass
  mark are fixed before the first run, and the report states which values
  a run used.

## 9. Size

Claude's estimate, not a measure: about 1,200 lines for the map (half of it
adapted from Nova 1), 1,500 for the network and the OS calls, 600 for the
observers and runner, 600 for the views, and 2,000 lines of tests.

## 10. After gate 3

- **A pass:** gate 4 (does the physics shape it?): the voltage solve, heat,
  the day's sweep across the map, sources coming and going, swells, the
  low-voltage interrupt and cooling devices. And the open question from
  gap 2, whether life spreads on a full-size map.
- **A fail:** if the test worlds show the mechanism works but nothing
  spreads from static, the levers are Damian's: a bigger world or budget,
  cheaper SENDs, or a design review.
- Either way, the result, the numbers used and the benchmark go into
  `design-history.md`'s log.

## 11. Results so far

### Step 1: the spread measure (2026-11-16)

Built and checked.

- **What was built:**
  - **In the observer:**
    - each lineage's members at the place it began in, counted apart;
    - a colony record for each place beyond that one where it has had
      members. A colony keeps its colonist, the place the colonist's
      sender was at, its foundings (arrivals that found it with no member
      there), and its members and the copies made there.
    - Stage 4 now holds the home place, so members elsewhere don't count
      for it. A colony settles by the same hold, and only if it was founded
      by a colonist (`colony_settles`).
  - **In the origin runner:**
    - each colony's settling clock is checked at every sample, as stage 4
      is;
    - each seed's spread line: colonies, foundings, colonies alive at the
      end, settlements, births from arrivals, and the first settlement;
    - gate 3's pass mark, after gate 2's. Its share, `origin.spread_share`,
      is 0.25 until 7 is agreed.
- **The test world** (`tests/test_spread.cpp`):
  - A sleeper at place 0 is the sender. A replicator's 14 bytes, put by
    hand in place 1's port 0 queue, ran as a prompt run.
  - The program it made was a colonist: cause "arrival", the sleeper its
    parent, its lineage, one generation down.
  - Its colony settled place 1 at tick 505 on a 500-tick hold. It had 82
    of the place's 82 programs, all copies made there. The colonist, an
    arrival's birth, never counted as one.
  - The lineage's home kept its one member and never held.
  - An arrival with no sender founded a lineage of its own at place 2, not
    a colony.
  - A colony ended by a cold start was founded again by the next arrival.
    It counted two foundings and kept its first colonist.
  - A colony with no copies made there never settled.
- **Gate 2's world, rerun:**
  - The new release reran seeds 12 and 20 with `runs/gate2-2`'s settings.
    Those are its first seeds to reach stage 4.
  - Seed 12 ran to 4.6 million ticks, and seed 20 to 5.1 million.
  - Every sample row and turnover window matched `runs/gate2-2`'s byte for
    byte, through stages 4 and 5. The stage ticks were the same too.
  - So counting stage 4 at the home place changes nothing where nothing
    arrives.
  - On 12 host threads a seed ran at 290 to 305 million instructions a
    second.
- **Left for later steps:**
  - **Distance in hops** comes with the routes (step 3). Until then the
    report gives places.
  - **A sender that died before its arrival ran** isn't found yet, so its
    colonist would found a lineage of its own. Step 4's SEND fact fixes
    that, along with the counts of arrivals that ran, made a program, or
    did neither, and of travellers.
- 27 of 27 test suites pass.

### Step 2: the map (2026-11-16)

Built and checked.

- **What was built** (`map.h`, `field.h`; design.md 6.1 and 7.3 have the
  details, marked "details at build, step 2"):
  - **The fields:** Nova 1's gradient noise, ported. A field is a smooth
    layer plus `edge` × tanh(`sharpness` × a second layer), each noise
    seeded for its own job.
  - **The landscape:** Nova 1's land raster and landmasses. Places are
    darts on land, chosen by the density field, with a spacing that shrinks
    where it's dense, as tight as fits. Their ids are then shuffled.
  - **Data links:** each landmass's Gabriel graph, from its Delaunay
    triangulation.
  - **Bridges:** the shortest crossings, 2 a pair of landmasses, none over
    a quarter of the map's height, no place at the end of two. Then every
    landmass still apart is joined to the rest by its shortest crossing,
    whatever its length (a join).
  - **Each link's numbers:** its length; its delay, 1 tick plus 1 per
    average link length, rounded up; and its bandwidth, 16 to 256 bytes a
    tick from its field. Bridges have 256.
  - **The hardware and OS values,** drawn as at gate 2, then dealt out by
    their fields. The E cores' pattern goes with the core count, and a
    place's set of memory latencies with its device count, so those mixes
    stay gate 2's too.
  - **The switches:** `world.map` (landscape, none, grid, ring) and
    `map.fields`. A world with no map has no fields.
  - **Reports:** `nova2 run` and the origin runner print each map's
    landmasses, links per place, bridges and joins.
- **The map's tests** (`tests/test_map.cpp`):
  - Seeds 1 to 6 at 48 places, and seed 7 at 1,000: every place on land.
    No two links on land cross. The links on land are exactly each
    landmass's Gabriel graph, checked by brute force against every pair.
    Bridges join only different landmasses, and the map is one piece.
    Delays and bandwidths follow the rules.
  - The map is a pure function of the seed. Places that don't fit are
    refused, with the reason. Grids and rings come out as drawn.
  - **The fields,** on 300 places: the mix of every value is gate 2's
    draws exactly, the cores with their kinds and the memory devices with
    their latencies. Only where they sit changes. Linked places' core
    counts differ by 0.99 on average, against 2.28 drawn independently, and
    129 links join strict to lax, against 282. With no map, or the fields
    off, each place has its own draws.
  - **At 48 places** seeds 1 to 6 gave 5 to 9 landmasses with places on
    them, 2.9 to 3.3 links a place, and 5 to 9 bridges, 0 to 3 of them
    joins. Delays run up to 4 ticks.
  - **At 1,000 places,** 4.4 links a place. At 10,000, 4.8, and the map
    takes about 0.15 s to work out.
- **Gate 2's world, rerun** with `world.map = none` and `map.fields = false`:
  seed 5 to 1.6 million ticks with `runs/gate2-2`'s settings. All 16,000
  sample rows and the 5 turnover windows closed by then matched
  `runs/gate2-2`'s byte for byte. It ran at 341 million instructions a
  second on 12 host threads.
- **Choices agreed by Damian (2026-11-16):**
  - **Joins.** Section 7 asks for every landmass bridged and none longer
    than a quarter of the map's height. When a landmass's nearest crossing
    is longer, both can't hold. The build bridges it anyway, by its
    shortest crossing. At 48 places that happened in 5 of the 6 seeds
    tried.
  - **The longest bridge is in the map's height,** as section 7 gives it.
    6.1 says every length setting is in average link lengths.
  - **Two new placeholders** are in section 7's table: the fields' noise
    scale (3 across the map, as the land's), and bandwidth spread evenly on
    a log scale by rank.
- 28 of 28 test suites pass.

### Step 3: links, routes and the network device (2026-11-16)

Built and checked.

- **What was built** (`net.h`; design.md 11.3 has the details, marked
  "details at build, step 3"):
  - **Routes:** each place's next hop toward a destination is the
    neighbour with the least link delay plus that neighbour's distance,
    ties to the lowest id. Dark places are left out of the graph. A
    destination's next hops are worked out when first asked for and kept
    until a network device goes off or comes on, at any size of world.
  - **Link-end queues:** 8 ticks of the link's bandwidth, in bytes, and
    never less than the message limit, with tail drop. Each end sends one
    message at a time.
  - **Store and forward at the barrier,** after every place has run its
    tick, in one fixed order: dark places drop what's queued at them; then
    the arrivals, by the place they came from, then their order on the
    link; then each free link end starts its next message; then the routes
    are marked for working out again if any network device changed.
  - **Corruption:** at each hop, each bit flips at `net.corruption` times
    the link's length in average link lengths. The flips are drawn from a
    stream of their own for each tick.
  - **The network device:** a draw of 1 µW a byte, at the sending place
    when the message is sent and at the receiving place when it's
    delivered. Forwarding is free. A dark device's place is routed around.
  - **Port 0:** a message delivered to port 0 joins the reset region's
    queue with its sender, and runs as a prompt run.
  - **Settings:** `net.queue`, `net.corruption`, `net.message_max` and
    `hw.network.byte_draw`. With no map, the network does nothing.
- **The network's tests** (`tests/test_net.cpp`):
  - **Delay:** on a 4 × 4 grid, a 40-byte message from corner to corner
    took 30 ticks, 6 hops of 3 + 2. A second one, sent with it, waited 3
    ticks at the first link and arrived 3 ticks later. On three
    landscapes, 204 messages, each sent alone, each took exactly its
    route's time.
  - **Tail drop:** a message reaching a full queue was dropped, and the 13
    waiting there all arrived, in order.
  - **Big messages:** on links of 16 bytes a tick, each queue holds 1 KB,
    and a 1 KB message crossed 2 hops in 132 ticks.
  - **Corruption:** 19,441 bits flipped in 6,260 messages, against 19,296
    expected (σ 139).
  - **A dark place:** its queue and the message it was sending were
    dropped. A message waiting to go to it was stranded, and one crossing
    to it was lost. The next message went round the other way, and the
    routes came back when it came back on.
  - **Routes:** 48,824 routes matched a brute-force search, on five
    landscapes with a tenth of their places dark.
  - **In a world:** a replicator sent to place 1's port 0 ran there as a
    prompt run, and the sender's device drew for its 14 bytes.
- **Gate 2's world, rerun** as at step 2 (`world.map = none`, the fields
  off): seed 5 to 1.6 million ticks. All 16,000 sample rows and the 5
  turnover windows matched `runs/gate2-2`'s byte for byte, at 335 million
  instructions a second. A test build was running for part of the time,
  so the speed isn't comparable to step 2's 341.
- **Left for later steps:**
  - SEND, RECV and PING (step 4). Each place's SENDs will need their own
    outbox, joined at the barrier in place order, because places run in
    parallel.
  - The network isn't yet in snapshots or the world hash (step 5).
- **Choice agreed by Damian (2026-11-16): a queue never holds less than
  the message limit.** At 8 ticks of 16 bytes, the narrowest links'
  queues held 128 bytes, against a 1 KB message limit. A message bigger
  than a link's queue can never cross that link: about 16% of land links
  were closed to a 200-byte message, and 75% to a 1 KB one. Now every
  queue holds at least 1 KB. Big messages are still dropped first when
  queues fill (11.5), but no link is closed to them.
- 29 of 29 test suites pass.

### Step 4: SEND, RECV, PING, arrivals and handlers (2026-11-16)

Built and checked.

- **What was built** (`os.h`, `net.h`, `world.cpp`, `region.cpp`,
  `observe.cpp`; design.md 11.2 and 13.3 have the details, marked
  "details at build, step 4"):
  - **SEND** copies its bytes into its place's outbox when it's called, and
    pays the crossing plus 2 cycles a byte. Every place's outbox joins the
    network at the barrier, in place order, ahead of that barrier's
    arrivals (gap 7). The caller sleeps the device's latency plus the size
    over the first link's bandwidth, rounded up. It fails (0) with the
    device off, no route, no bytes, bytes it can't read, or no room in its
    exit queue, counting what the place's earlier SENDs that tick keep
    there.
  - **RECV** takes the first message for its port from the inbox. A
    message delivered while threads wait in RECV on its port goes straight
    to the one that has waited longest. Otherwise it joins the inbox, or is
    dropped if the inbox is full.
  - **PING** sends an 8-byte request. The network turns it round at its
    destination as an 8-byte echo, at the barrier it arrives, and the echo
    ends the thread's wait with the round trip in ticks. Past its timeout
    the thread gets −1, and a late echo is ignored. It fails at once (−1)
    for SEND's local reasons.
  - **To its own place,** a SEND or PING crosses no link and arrives after
    the device's latency (a choice agreed below).
  - **Port 0:** an arrival joins the reset region's queue with its sender.
    What its prompt run SPAWNs has the cause "arrival" and the sender for
    parent.
  - **Data handlers:** a message joining a port's inbox makes the data
    interrupt pending for each thread with a handler on that port. It's
    pending once however many come, so a burst makes one interrupt.
  - **Neighbour SENSE:** the request lands after the link's delay, when the
    world reads the neighbour at the barrier, and the caller waits twice
    the delay.
  - **Idle runs** may SEND (R4) and SENSE a neighbour. They wait on their
    core, holding it, until the transmission or the round trip is done. A
    ready thread can still take the core, which ends the run (a choice
    agreed below). An idle run's RECV and PING fail (0).
  - **The observers:** a fact for each SEND. A program that SENT to a port
    0 keeps its record after it dies, until nothing it sent is on the
    network, waiting in a port 0 queue, or running as a prompt run. So its
    colonists find their parent. Prompt runs of arrivals are counted as
    they end: those that made a program, travellers (they only sent), and
    those that did neither.
  - **Settings:** `os.send_byte_cycles` (2), `hw.network.latency` (1
    tick), `net.inbox` (1 KB a port; the message limit must be no more),
    `net.ping_size` (8 bytes).
  - **SENSE of the bytes at port 0 reads 0.** Port 0's queue is the reset
    region's, which RECV can't take from. At gate 2 the boot block waits
    there, and reading its bytes changed gate 2's world: the viewer's
    stored image no longer matched.
  - **With no map,** SEND fails and PING returns −1, as at gate 2.
- **The tests** (`tests/test_send.cpp`), on a ring of six places with
  hand-placed programs:
  - **A colonist:** a replicator SENT itself (30 bytes) to its neighbour's
    port 0, then EXITed. Its copy ran there as a prompt run. Its SPAWN was
    a colonist of the dead sender's lineage, which made 362 more programs
    there. The dead sender's record went once nothing of it was left.
  - **RECV:** two threads waiting on a port got 20 and 10 bytes from place
    0, the longest-waiting first, with the bytes and the sender's id. The
    sender slept 3 ticks for the 20 bytes and 2 for the 10: 1 plus the
    size over 16 bytes a tick, rounded up. A message to its own place
    arrived a tick after it was sent.
  - **PING:** across 3 hops of 1 + 2 ticks, 18 ticks there and back; to its
    own place, 2; past its timeout, −1. A neighbour SENSE took 4 ticks,
    twice the link's delay, and read the neighbour's free memory as it was
    when the request landed.
  - **Failures:** with every network device off, SEND failed, PING
    returned −1 at once, and a neighbour SENSE read the id and 0 for the
    rest. SEND failed with no bytes and with bytes it couldn't read. Of two
    100-byte SENDs in one tick into a 128-byte exit queue, the second
    failed.
  - **Inboxes:** a 256-byte inbox took two of three 100-byte messages.
    SENSE read its 200 bytes, and port 0's as 0. A cold start emptied it.
  - **Busy cores:** an arrival waited 50 ticks in port 0's queue while
    three threads kept two cores busy, and ran as soon as they ended.
  - **The tally:** prompt runs of arrivals were counted as making a
    program, travelling on, or neither. The traveller waited on its core
    for its SEND.
  - **A handler:** three messages to one port, landing at one barrier from
    both neighbours and the place itself, made one interrupt, whose handler
    took all three. A second burst made a second.
  - **Host threads:** a busy 3 × 3 grid came out the same on 1 and 4 host
    threads, with every place's invariant check holding. Each place
    SENT a replicator, SENSEd a neighbour and PINGed, for 400 ticks.
- **Gate 2's world, rerun** as at step 2 (`world.map = none`, the fields
  off): seed 5 to 1.6 million ticks. All 16,000 sample rows and the 5
  turnover windows matched `runs/gate2-2`'s byte for byte, at 336 million
  instructions a second.
- **Left for later steps:**
  - The network, the outboxes, inboxes and neighbour SENSE requests, and
    the new thread and core state, aren't yet in snapshots or the world
    hash (step 5). Until then the world test's small world
    (`tests/test_world.cpp`) has no map, since its snapshot must resume to
    the unbroken run. It gets one back at step 5.
- **Choices agreed by Damian (2026-11-16):**
  - **A SEND or PING to its own place is delivered locally,** after the
    device's latency, crossing no link. A PING to itself takes twice the
    latency, and the SEND's caller waits the latency alone.
  - **An idle run waits on its core** for its SEND's transmission and its
    neighbour SENSE's round trip, holding the core. A ready thread can
    still take it, which ends the run.
  - **A PING's request and echo are 8 bytes each,** a new placeholder in
    section 7.
- 30 of 30 test suites pass.

### Step 5: determinism, snapshots and cost (2026-11-16)

Built and checked. The benchmark found routes taking most of the tick, and
they were made faster, with the same routes (Damian's choice, below).

- **What was built** (`net.h`, `serialize.cpp`, `place.cpp`, `world.cpp`,
  `snapshot.h`; design.md 2 has the details, marked "details at build,
  step 5"):
  - **The network joins snapshots and the world hash.** With a map, the
    world section is the places, then the network:
    - each link end's queue, the message it's sending and its wire;
    - every message slot, a live one whole with its bytes;
    - the deliveries due, and the messages to their own place;
    - the bytes each place's network device has still to count;
    - which places are dark.

    The routes aren't saved. They follow from the dark places, so a
    resumed world works them out again as they're asked for, the same. A
    loaded network must pass its own check (`Network::check`): every live
    message in exactly one list, each queue's bytes adding up, and the
    free slots and the live count agreeing.
  - **Each place's new state** joins its section: its outbox (empty at the
    barrier), its comms ports' inboxes, its neighbour SENSE requests, each
    thread's RECV and PING waits, each core's wait for an idle run's SEND,
    and the network device's draw still to count.
  - **The snapshot format is version 3.** Snapshots from earlier builds,
    the gate 2 runs' among them, don't load in this one.
  - **Gate 3's traffic** (`app/traffic_world.h`, R7), shared by the tests
    and the benchmark. Each place gets a colonist sender, a talker, a
    listener with a data handler, and a traveller in its reset region.
  - **Timings, for reports only** (never saved or hashed): the world counts
    the cycles its network step takes, and the routes the cycles spent
    working columns out.
- **The tests:**
  - **The world test's small world has its map back**
    (`tests/test_world.cpp`), and its snapshot resumes to the unbroken
    run.
  - **The fuzz world with traffic** (`tests/test_fuzz.cpp`): gate 2's
    crowded fuzz world on its map, with the traffic, under gate 2's
    hardest supply, for 3,000 ticks. Every place's invariant check and the
    network's own check held after every tick.
    - 12,297 messages sent (7,973 to their own place), 11,809 delivered,
      82 PINGs answered.
    - Dropped: 245 full, 4 stranded, 1 lost, 5 cut, with network devices
      dark for 826 place-ticks.
    - 5,445 arrivals ran, 4,952 of them travellers. 630 messages joined
      inboxes, 97 were taken there, and 11 went straight to a waiting
      RECV.
  - **Determinism with traffic** (`tests/test_determinism.cpp`): gate 2's
    determinism world with the traffic on its map, for 2,400 ticks: 2,079
    messages sent, 2,063 delivered, 241 PINGs answered, 231 arrivals run,
    983 inbox joins, 14 cold starts.
    - The hash was the same every 400 ticks on 1, 2 and 24 host threads,
      with an observer and without.
    - Snapshots were taken at the first tick after 100 with each of: 12
      messages mid-hop (tick 101); messages in an inbox; a PING waiting
      for its echo; a neighbour SENSE waiting for its barrier; and an idle
      run waiting on its core for its SEND (tick 108).
    - Each, resumed on 2 host threads unwatched and on 24 watched, ended
      with the unbroken run's hash. Gate 2's own determinism test still
      passes.
- **The benchmark's gate 3 part** (`nova2 bench`, release build): the
  traffic world under gate 2's hard supply, whose drop-outs and nights
  take network devices down, timed for 400 ticks.
  - **The topology changed in 289 of the 400 ticks at 1,000 places, and in
    every tick at 10,000.** Each place's own drop-outs and nights switch
    its network device off and on, and each switch makes every column
    stale. So the cache rarely lasted a tick: about 153 columns were
    worked out a tick at 1,000 places, and about 392 at 10,000.
  - **First, with the routes as step 3 left them** (one column at a time,
    under the routes' lock):
    - 1,000 places on 24 host threads: 76 ticks a second, 13.2 ms a tick.
      The routes took 11.1 ms of it (84%), at 71 µs a column.
    - 10,000 places on 24 host threads: 2 ticks a second, 401 ms a tick.
      The routes took 385 ms of it (96%), at 982 µs a column. Part of
      that was SENDs' first hops in the places' ticks, under the lock, so
      the other threads waited.
  - **After the speed-up (below), with the same routes:**
    - 1,000 places on 1 host thread: 41 ticks a second (32 before), 24.1
      ms a tick. The network step took 4.4 ms (18%), and the routes 3.5
      ms, at 23 µs a column.
    - 1,000 places on 24 host threads: 367 ticks a second, 2.7 ms a tick,
      4.8 times as fast. The network step took 1.3 ms (47%). The routes
      took 4.6 ms of the threads' time, at 30 µs a column.
    - 10,000 places on 24 host threads: 31 ticks a second, 32 ms a tick,
      15 times as fast. The network step took 20.7 ms (65%). The routes
      took 167 ms of the threads' time, at 424 µs a column. Spread over
      24 threads that could be about 7 ms; the rest of the step isn't
      broken down yet.
  - **The traffic** (the same before and after): a tick's messages at
    1,000 places were 231 sent, 30 delivered and 2 PINGs answered.
    Dropped: 118 full, 23 stranded, 2 lost and 14 cut. 27,802 were in
    flight at the end. At 10,000 places: 2,195 sent, 59 delivered, 306,325
    in flight at the end. The load is meant to be hard: every place sends
    across the map every few ticks.
  - **The columns kept** at 10,000 places had reached 70 MB, and they grow
    toward a whole table's 191 MB as more destinations are asked for.
    Nothing bounds them yet.
  - **The routes alone** (11.3), after the speed-up (before it, in
    brackets):
    - At 1,000 places (2,089 links), a whole table, 1,000 columns (1.9
      MB), took 29.4 ms (76.5), 29 µs a column. With the 149 places in the
      map's western tenth dark (a nightfall's edge), 24 µs a column.
    - At 10,000 places (23,634 links), 500 columns found on demand took
      372 µs each (962), 19.5 KB each. A whole table would take 3.7 s
      (9.6) and 191 MB. With 1,553 places dark, 319 µs a column.
  - **Found and fixed by the benchmark:** the network grew its message
    bytes by exactly one 1 KB slot at a time (`Array::resize` grows to the
    size asked), so a network filling up copied its whole store for each
    new slot. At 1,000 places that took the network step to 58 ms a tick.
    It now grows by doubling, as `push` does, with the same bytes, so no
    result changed.
- **The routes made faster, with the same routes** (`net.h`, `net.cpp`,
  `world.cpp`; design.md 11.3, "details at build, step 5"):
  - **A cheaper search.** The links are laid out once, in one array a
    place, each with its far end, its delay and its index there. A search
    keeps a bucket for each distance in the window ahead (Dial's
    algorithm), or a heap when links are longer than 4,096 ticks. Each
    place takes its next hop as the search reaches it over each link: the
    lowest link among those on its shortest paths, the same tie rule as
    before, without a second pass over every link. About 2.6 to 3 times
    as fast a column.
  - **The next barrier's columns, worked out ahead in parallel.** After
    the network step, the destinations that messages arriving at the
    next barrier will ask for (and a PING's sender, for its echo) are
    worked out across the host threads, each task with its own working
    space.
  - **SENDs in parallel.** A place whose SEND finds a column stale works
    it out on its own, outside the lock, and the first to finish keeps
    it. Places asking for different columns no longer wait for each
    other.
  - **Checked:** every route matches a brute-force all-pairs search on 7
    landscapes with a tenth of their places dark, among them one with no
    delays at all and one with links of 4,096 ticks or more (the heap).
    Columns worked out in a batch match those worked out as asked. The
    fuzz and determinism worlds' counts and hashes are exactly as before.
- **Gate 2's world, rerun** as at step 2 (`world.map = none`, the fields
  off): seed 5 to 1.6 million ticks. All 16,000 sample rows and the 5
  turnover windows matched `runs/gate2-2`'s byte for byte, at 343 million
  instructions a second; and again after the routes' speed-up, at 327
  million.
- **Found and put right:** eight characters in step 4's records, here and
  in design.md 11.2, were mangled ("−" and "×"). PowerShell read the edit
  scripts that wrote them in the wrong encoding.
- **Choice agreed by Damian (2026-11-16):** the routes' cost when the
  topology changes every tick is met by working them out faster, with
  the same routes (build details, no design change), rather than by
  recomputing fewer, routing by region, or bounding the columns kept.
- **Still open, for later:**
  - The cost still grows as the places squared: a change makes every
    column stale, and each column covers every place. Step 7's nightfall
    test measures it under ordinary nights, at 1,000 and 10,000 places
    (11.3).
  - Nothing bounds the columns kept at 10,000 places: up to 191 MB.
  - With no delays at all (`map.delay.hop` and `map.delay.length` both
    0), every route ties, and the lowest-id choice can send a message
    back and forth for ever. It was so before, and the settings allow it.
- 30 of 30 test suites pass.

### Step 6: the gate 3 views (2026-11-16)

Built and checked, with Damian's three choices (below).

- **What was built** (`app/ui/viewer.cpp`, `viewer.h`, `theme.h`,
  `app/view_cmd.cpp`, `net.h`, `observe.h`; design.md 13.4 has the
  details, marked "details at build, step 6"):
  - **The map** in the middle, in the memory strips' place, the tiles
    staying on the left. V switches between the map and the memory.
    - The land and water come from the landscape's raster, a pixel row at
      a time. A grid or a ring is all land.
    - The links are lines, the bridges lighter and thicker.
    - The places are squares at their positions, about four tenths of a
      link across.
    - The inspected place is framed and its links marked. A click picks
      the nearest place.
  - **Four colourings**, which C steps through:
    - population: the tiles' scale, dimmer as the voltage falls;
    - lineage territories: each place's largest lineage (the observer's),
      dimmer as its share of the place falls;
    - network device: green on, red off;
    - queues and drops: each link by its fuller end's queued bytes, of its
      room; each place by the messages dropped there since the world was
      built or opened, on a log scale.
  - **Drops by place** (`net.h`). The network counts each place's drops:
    no room, stranded and lost where the message was, and cut at the dark
    place. Like its other counts they're for reports, never saved or
    hashed. `Network::clear_counts` clears both.
  - **The inspector**, with a map, adds the place's links and bridges, the
    bytes queued at its ends and the fullest, its inboxes and its drops.
    The places panel adds the world's messages sent, delivered and on
    their way.
  - **Gate 3's row of charts** takes gate 2's place when there's a map,
    and G switches back:
    - messages per sample: sent, delivered, PINGs answered;
    - messages dropped per sample: no room, stranded, lost at a dark
      place, cut at a dark device;
    - colonists per sample, with the colonies alive and settled.
  - **The samples** (`observe.h`) add the network's counts, the colonies
    founded, alive and settled, and the births by arrival. The observer
    settles colonies by the origin test's criteria
    (`Observer::settle_by`). `nova2 view` reads them from `origin.span`
    and the rest, with the origin test's own reader (`read_stages`). The
    origin test's samples file is unchanged.
  - **With no map nothing changes:** the keys do nothing, and the views,
    lines and charts are gate 2's.
- **The tests** (`tests/test_view.cpp`):
  - **Gate 3's stored image** is `tests/golden/view_gate3.png`. Gate 2's
    view world, grown to 30 places on the default landscape (36 links, 7
    bridges), has the traffic and a booter in every place, under gate 2's
    varying supply, and is watched.
    - It's drawn coloured by queues and drops at tick 400: the first
      moment after tick 400 with messages mid-hop (76), a network device
      off, and a place with drops (place 0, inspected).
    - It matched pixel for pixel, and the same frame drew the same twice.
    - The other three colourings are written beside it in the build tree,
      for a person to look at.
  - **Gate 2's stored image** still matches pixel for pixel.
  - **A snapshot taken mid-hop:** the same world, saved at tick 100 with
    61 messages mid-hop and place 28's network device off.
    - It opened with the same messages on their way.
    - It drew the same as its world in 27 renders: three places (the dark
      one, one with bytes queued on a link, and place 0), each with the map
      in its four colourings, the memory in its four, and gate 2's charts.
  - **The keys:** V, C and G change the view, not the world. With no map
    they change nothing.
- **Gate 2's world, rerun** as at step 2 (`world.map = none`, the fields
  off): seed 5 to 1.6 million ticks. All 16,000 sample rows and the 5
  turnover windows matched `runs/gate2-2`'s byte for byte, at 375 million
  instructions a second.
- **Choices agreed by Damian (2026-11-16):**
  - the map and the memory strips share the middle, a key switching
    between them, and the tiles stay;
  - with a map, gate 3's charts take gate 2's row, a key switching back;
  - drops are counted since the world was built or opened, report-only,
    on a log scale.
- **Still open, for later:**
  - The viewer only knows colonies founded while it watches. A world
    opened from a snapshot starts its colony records afresh, so a colony
    shows as settled only after `origin.span` ticks watched (100,000 by
    default).
  - Drawing the map at 10,000 places, with every link each frame, isn't
    timed yet.
- 30 of 30 test suites pass.

### Step 7: the test worlds at full size (2026-11-17)

Built and run, with Damian's four choices (below). All six runs ended
without errors and report their numbers. The batch, its binary and its
outputs are in `runs/gate3-worlds` (README.txt).

- **What was built:**
  - **`nova2 worlds`** (`app/worlds.cpp`; keys `worlds.test`,
    `worlds.ticks`, `worlds.report_every`). Its world is
    `runs/gate3-worlds/worlds.cfg`: `runs/gate2-2`'s world and supply on
    the default landscape at 1,000 places (47 landmasses, 2,089 links, 34
    bridges), the disk cut to 64 KB a place. Gate 3's noise is on: the
    floors 1 in 10,000 a result, cosmic rays 1 in a million a bit a tick.
  - **The migrants** (`app/traffic_world.h`): one program, run two ways.
    - Arriving at a port 0, its prompt run ALLOCs a page less a byte as
      data, CPYs itself there, PROTECTs it executable (so strict and lax
      places both run it) and SPAWNs it with A = 1. Then it ends: one copy
      per arrival, nothing multiplied at the place.
    - As a thread (A = 1), it SENDs itself to port 0s again and again:
      - **sensing** (60 bytes): SENSE a neighbour (the tick picks which),
        and SEND there only if it has an idle core;
      - **blind** (45 bytes): SEND to whatever A holds, 1 after a SEND
        that worked and 0 after one that failed;
      - **pushy**: SEND to a neighbour, busy or not (the busy test's).
  - **Two report-only counters in the region** (`region.h`; not saved or
    hashed), for the arrivals that ran and did neither:
    - `arrival_preempted`: a ready thread took their core (10.2);
    - `arrival_overwritten`: of the rest, those whose landed bytes had
      changed by the time they ended.
  - **The tests** (`tests/test_send.cpp`, `test_migrants`), in small grids
    with no noise:
    - the sensing migrant from place 0 of a 4 × 4 grid reached all 15
      others (8 strict) by tick 105;
    - the blind one's copies, from place 5, landed only at places 0 and 1;
    - pushy migrants beside a 3 × 3 grid's middle place, kept busy: 366
      arrivals landed there, and none ran.
- **Found while building: gate 3's noise ends a spinning thread within a
  tick or two.** A result goes wrong about once in 10,000, so a jump loop
  soon jumps somewhere random and faults. In the first try every busy
  place had lost threads by tick 200. The busy test now tops its threads
  up after every tick (below), and runs again with the noise off.
- **The map at 1,000 places, sensing** (20,000 ticks, from place 913,
  near the middle; the farthest place 41 hops away):
  - **It reached 424 of the 999 other places (42.4%), the last at tick
    19,059.** 10% were reached by tick 206; half never were.
  - **The wave went about 40 ticks a hop, then stalled.** The median
    place was reached at tick 34 one hop out, 177 at 5 hops and 395 at 10.
    Then: 527 at 15, 1,919 at 17, 3,969 at 18, 9,861 at 20 (7 of its 47
    places reached), and none at 21 or 22. Beyond 22 hops 20 places were
    reached all the same, the first at tick 625, far ahead of the wave:
    presumably SENDs whose address noise had changed, and the colonies
    they founded.
    - **Corrected afterwards (2026-11-17):** the stall is its lineage's,
      not its code's. Lineages that static runs began from its bytes
      founded colonies at the 575 places it didn't reach, so its code
      reached all 999 ("Asked afterwards", below). Since 13.2 changed,
      its lineage counts 930 ("Changed afterwards", below).
  - **Messages:** 81.8 million sent, 74.7 million delivered, 7.1 million
    dropped for want of room on a link.
  - **Arrivals:** 71.8 million landed at port 0s, and 53.4 million of
    them were dropped there for want of room. 18.4 million ran:
    - 207,215 made a program (1.1%), and 2,386 only sent themselves on;
    - 18.2 million did neither. A ready thread took the core of 6.7
      million of them (37%), and 0.3 million found their bytes written
      over. The other 11.1 million (61%) ended on their own, their bytes
      intact.
  - **Its copies:** 387,404 programs made by its arrivals, at 424 places;
    0.2% at places 0 and 1.
  - **Noise remade it.** Its 400,552 births had 31,465 genomes. 13,526
    (3.4%) had its own, 88,383 had a byte or two changed, 100 were its
    code from a few bytes in, and the rest differed more. The commonest
    changes were at byte 0 (37 to 35: SUB #1 became SUB [B]) and byte 3
    (1f to 1b: its first jump lands on its SPAWN).
    - A thread of the byte 0 variant no longer skips the copy-out. It
      makes one copy of itself where it lives, then loops: it multiplies
      at home. The byte 3 variant SPAWNs at home too.
  - **The places it reached filled up:** 168 threads alive a place on
    average, up to 1,151 (of 2,561 thread records), with 110 KB of memory
    still free. Arrivals that ran there made a program 1.1% of the time.
  - **Beyond its front** (68 places reached beside 98 not):
    - those 98 places had 82 programs alive each (none of its), 1.9 idle
      cores and 115 KB free;
    - 7.5 million arrivals landed there and 5.6 million were dropped for
      want of room. 1.9 million ran and 19,319 made a program (1.0%). Of
      those that did neither, a ready thread took the core of 660,874
      (35%).
- **The map at 1,000 places, blind** (20,000 ticks, from place 913):
  - **The original goes where 11.1 predicts.** Of the 2.9 million
    arrivals that landed anywhere, 39.9% landed at place 1 and 32.7% at
    place 0.
  - **Noise remade it, and its copies scattered.** It reached 523 of the
    999 other places (52.4%), more than the sensing one: 10% by tick
    1,716, half by tick 9,901, the last at 19,953.
    - Places were first reached at about the same time at every
      distance: from 6 hops out to 41, each hop's median fell between
      tick 1,181 and tick 2,829. That is scatter, not diffusion.
  - **Its genomes:** 461,481 births and 5,783 genomes; only 141 births
    had its own. Its three commonest variants all changed byte 0, 37 to
    b7: SUB #1 became a spare code, which does nothing.
    - A thread of it no longer skips the copy-out: it copies itself where
      it lives, as the sensing variant does.
    - Its first SEND goes to whatever the SPAWN returned, a number with
      no meaning as an address: the scatter.
    - The commonest (13.9%) also changed byte 43, 7f to 6f: its loop's
      JMP became SHL, so after that first SEND it doesn't loop back. Byte
      0 alone was 5.0%. With byte 33 as well (18 to 1c: PUSH A became
      PUSH IP, so it sends to an address) it was 3.7%.
  - **Its copies' commonest place was 45** (5.4%): 45 is the code's
    length, a byte in the code. That is 11.1's one-byte constant.
  - **Messages and arrivals:** 13.4 million sent, 10.2 million dropped
    for want of room on a link. 2.9 million arrivals landed, 2.5 million
    dropped at port 0s, 309,055 ran and 19,028 made a program. The core
    was taken from 210,969 of those that did neither (73%).
- **CPU saturation** (20,000 ticks):
  - **The setup:** the 500 places with even ids keep one spinning thread
    more than their cores, so a thread is always ready when one ends or
    its turn does. A pushy migrant runs at each of the other 500 places.
    - After every tick the test replaces the spinners that ended, and ends
      and replaces any noise left alive but off its jump (sleeping,
      waiting, or pinned to one kind of core).
    - A core can come free for an arrival only in a tick where two or more
      spinners went, counting any it was already short of, or the place
      cold-started: an "open" tick.
  - **10.4's barrier held:** no arrival ran at a busy place in a tick
    when its cores were all kept busy.
  - **With gate 3's noise:**
    - 20.7 million arrivals landed at the busy places and 513,712 ran
      (2.5%), all in open ticks, at all 500 places;
    - 6.7% of the place-ticks were open. 3.4 million spinners were
      replaced, 0.34 a place a tick, and 502,115 of them had stalled;
    - for scale, 46.1 million landed at the free places and 9.8 million
      ran (21%).
  - **With the noise off:** 11.7 million arrivals landed at the busy
    places and none ran. 515 place-ticks were open (2,320 spinners
    replaced), and no arrival ran in them either.
- **Nightfall** (one day, 2,000 ticks; step 5's traffic at every place;
  the network device's threshold raised to 900 mV, so ordinary nights
  take devices down, each place at its own phase):
  - **The topology changed in every tick but two at 1,000 places, and
    in every tick but one at 10,000:** one recompute a tick.
  - **At 1,000 places:**
    - 6.9 device switches a tick, 273 to 451 devices off on average per
      hundred ticks, 469 at the most;
    - 38 to 152 columns worked out a tick (98 on average), 6 to 20 µs
      each;
    - the routes 0.3 to 2.4 ms of the threads' time a tick, and the
      network step 0.2 to 0.7 ms of a 2.6 to 3.1 ms tick: 352 ticks a
      second;
    - 1.9 MB of columns kept at the end.
  - **At 10,000 places** (23,634 links, 79 bridges; memory cut to 32 KB a
    place, the least that holds the 4 KB reset region on its device):
    - 59.7 device switches a tick, 2,805 to 3,110 devices off on average,
      3,139 at the most;
    - 257 to 489 columns worked out a tick (430 on average), 121 to 280
      µs each;
    - the routes 55 to 121 ms of the threads' time a tick, about 2 to 5
      ms over 24 threads;
    - the network step 5.7 to 8.6 ms of a 16 to 22 ms tick: 52 ticks a
      second;
    - 189 MB of columns kept at the end, almost a whole table (191 MB).
  - **Messages:** at 1,000 places, 81,313 sent and 23,813 delivered. At
    10,000, 1.48 million sent, 87,609 delivered and 975,808 stranded.
    The traffic sends across the whole map, and about a third of the
    devices are off at any time.
- **Gate 2's world, rerun** as at step 2 (`world.map = none`, the fields
  off): seed 5 to 1.6 million ticks. All 16,000 sample rows and the 5
  turnover windows matched `runs/gate2-2`'s byte for byte, at 372 million
  instructions a second.
- **Choices agreed by Damian (2026-11-17):**
  - each migrant makes one copy per arrival, with PROTECT so strict and
    lax places both run it, and multiplies nowhere by itself;
  - a blind SEND goes to whatever A holds;
  - the test worlds run gate 3's supply (`runs/gate2-2`'s: a day of
    2,000 ticks, night 0.2, swing 0.25, one drop-out per place per
    million ticks);
  - nightfall raises the network device's threshold to 900 mV for the
    test: gate 3's nights leave a place at about 740 mV at their deepest,
    above the device's 600.
- **Still open, for Damian:**
  - **Arrivals at inhabited places rarely finish.**
    - 10.2 lets a ready thread take the core from any idle run, a prompt
      run included.
    - A copy-out costs about 27,300 cycles in calls: SPAWN 20,000,
      PROTECT 5,000, ALLOC 2,000.
    - **Corrected 2026-11-17:** this entry first said the whole 27,300
      cycles were a chance to lose the core. They aren't. A call takes
      effect when it's made, and its cost is paid afterwards. So the
      migrant is exposed only until it calls SPAWN: about 7,300 cycles
      (ALLOC, PROTECT, a SENSE and the copy), about 5 quanta of a P core
      at full voltage. A.5's 14-byte replicator on a lax place is exposed
      for its ALLOC alone, 1 to 2 quanta.
    - Across the sensing test 37% of the arrivals that did nothing lost
      their core this way, and 73% in the blind test.
    - Whether a prompt run should keep its core until it ends, or for a
      while, bears on gate 3's question. It wants settling before the gate
      3 runs.
    - **Decided (2026-11-17, Damian: "Keep 10.2"):** 10.2 stays as
      designed: a ready thread can take the core from a prompt run, as
      from any idle run. In the sensing test losing the core turned out
      not to be the main reason arrivals fail (counted afterwards, below:
      most of the rest found no room on the device they asked for),
      though in the blind test it is. And since 13.2 changed, an arrival
      that loses its core leaves its bytes, which a static run can still
      take up for its sender's lineage.
  - **Most of the rest ended on their own, their bytes intact.** Samples
    traced at the front (debug output, since removed) were arrivals that
    reached their last instruction without making a program. Whether they
    are mostly noise's variants of the migrant isn't counted yet.
  - **The sensing test with the noise off** (`sense-quiet.txt`, run
    afterwards and now part of the batch; 20,000 ticks):
    - **The wave stalled the same way.** It reached every place within 9
      hops, at about 30 ticks a hop (the median 34 at 1 hop, 296 at 9),
      then thinned out from 10 hops to 18. It reached 280 places by tick
      1,000 and 286 in all, the last at tick 12,579. So the stall isn't
      the noise's doing. (Nor is it the code's: see "Asked afterwards",
      below.)
    - **Losing the core isn't the main reason arrivals fail.** 22.4
      million arrivals ran and 37,845 made a program (0.17%). A ready
      thread took the core of 6.4 million (28%), 0.5 million found their
      bytes written over, and 15.5 million (69%) ended on their own, their
      bytes intact. Of the arrivals that kept their core to the end, 0.24%
      made a program.
    - **Those that ended on their own aren't mostly variants.** 53.8% of
      its 21,485 births had its own genome, and the commonest variant
      (34.5%) copies itself out the same way as an arrival. So the guess
      above was wrong.
    - **At the 47 unreached places beside its front,** 684,514 arrivals
      ran and 323 made a program. Those places had 126 programs alive
      each, 3.2 idle cores and 111 KB of memory free.
  - **Counted afterwards: what ends them (2026-11-17).** For the arrivals
    that kept their core and did neither, the region now also counts the
    first of their calls that failed and how they ended (`region.h`;
    report-only, not saved; `test_failures` in `tests/test_send.cpp`). The
    batch was run again with it, `sense-quiet` included, and every number
    above came out the same.
    - **Their ALLOC finds no room on the device it asks for, while another
      device has room.** That was the first failure of 74% of them in the
      sensing test (8.5 million of 11.4 million), 69% with the noise off
      (11.0 million of 16.0 million), 59% in the blind test, and 73% beyond
      the sensing test's front. In the busy test, where nearly all the
      arrivals that ran were at its free places, 79% (91% with the noise
      off). No device had room for 559 of them, all in the sensing test.
    - **Why:** the migrant's C is its landing spot, and A.5's convention
      clamps any address to the slowest memory device. Code born from
      static starts with C at its own address too, so unless it changes C
      it asks for the same device (Claude's reading, not counted). So that
      device fills while the place's others still have room: the places
      the sensing migrant reached had about 110 KB of their 160 KB free.
    - **ALLOCs refused as asked** were the first failure of 15% of them,
      and 30% with the noise off. Those traced (the first 400 ticks with
      the noise off; debug output, since removed) were mostly arrivals of
      the migrant's code from its byte 13 on.
      - Entered there, the migrant's code ALLOCs as many bytes as its own
        address, copies 60 bytes there and SPAWNs them. So it's a copier
        wherever its address is a size ALLOC takes (at most 4 KB).
      - `region.step` is 13. So a static run on a core whose last run was
        the migrant's arrival starts at the migrant's byte 13, and where
        the region lies below 4 KB it makes a program of it.
      - Those programs SEND themselves on as the migrant does. Where they
        land in a region above 4 KB, their ALLOC is refused.
    - **The rest:** a PROTECT failed first for 0.7%, a FREE for 0.3% and a
      SPAWN for 0.1%; no call failed for 10% (1.3% with the noise off).
      99% ended by a fault, as the migrant does after a failed ALLOC, about
      1% by a cut-out, and 13 by the cap.
    - **So the 10.2 question matters less than it looked.** Of the
      arrivals that did nothing, 37% lost their core (28% with the noise
      off), but most of the rest found no room on the device they asked
      for.
  - **Asked afterwards: a migrant that asks for any device, and the
    lineages static runs begin (2026-11-17).** Damian asked for the rerun
    ("sure").
    - **Built** (test worlds only): `worlds.migrant_device`, `spot` (the
      default, as before) or `any`. With `any` the migrant's ALLOC gets
      C = 0, and it finds its start from IP, since C no longer holds it:
      63 bytes for the sensing one, the smallest page less a byte.
      `test_any_device` (`tests/test_send.cpp`) checks that it makes a
      program where the slowest device is full, and the spot migrant
      doesn't.
    - **And a report line:** the other lineages that founded colonies,
      how many a static run began, their programs alive against the
      migrant's, and the places they reached. The batch was run again
      with both, the two new runs included (`sense-any`,
      `sense-any-quiet`), and every earlier number came out the same.
    - **Asking for any device, the migrant's lineage reached 392 places
      with the noise off (286 before) but 336 with it on (424 before),**
      the last at tick 12,688 and at tick 2,672.
      - The places it reached filled: 478 threads and 5.5 KB free a place
        with the noise on (168 and 110 KB before), 358 and 57 KB with it
        off (172 and 104 KB). The places beyond its front filled too: 483
        programs and 5.1 KB free a place (82 and 115 KB before), 350 and
        37 KB with the noise off (126 and 111 KB). Most of what filled
        them was the other lineages below.
      - The commonest first failure is now an ALLOC with no room on any
        device: 81% of the arrivals that kept their core and did neither,
        97% with the noise off.
      - So the device wasn't the whole barrier. It capped how many copies
        a place held and left the rest of its memory free. Without it the
        places fill to their memory.
    - **Lineages begun by static runs carried its code everywhere.** A
      static run that SPAWNs begins a lineage of its own (13.2: static
      gives no parent), and static runs run the migrant's landed bytes:
      `region.step` is 13, so a core's next static run after an arrival
      starts 13 bytes into it. (With `any`, one entered there copies the
      whole migrant, its start found from IP; before, it copied from byte
      13 on.)
      - Lineages a static run began founded colonies in every test: 1,233
        in the sensing test, 214 with the noise off, 1,216 in the blind
        test, and 7,289 and 11,940 asking for any device.
      - They had more programs alive at the end than the migrant's
        lineage: 70,178 against 32,846 in the sensing test, 111,341
        against 13,830 with the noise off, 72,831 against 7,742 blind,
        and 355,458 against 20,959 and 214,496 against 50,136 asking for
        any device.
      - Between them and the migrant's lineage, all 999 other places had
        a colony, in each of the five tests.
      - Gate 3's world with no migrant (the origin test on `worlds.cfg`,
        seed 1, 20,000 ticks) founded no colony: 202 programs were born
        from arrivals, and none was a colonist. So these lineages are the
        migrant's code.
    - **So step 7's stall is in the count, not in the spread.** The code
      reached every place within 20,000 ticks, whichever device it asked
      for (when isn't counted). The lineage breaks wherever a static run
      takes its bytes up. That bears on gate 3's measure (8).
  - **Changed afterwards: a static run in an arrival's landed bytes
    (2026-11-17).** Damian agreed the change ("Yes, change it"; 13.2 and
    13.3, logged in `design-history.md`): a static run that starts in the
    bytes an arrival last landed, while all of them are as they landed,
    takes the arrival's sender as its parent, and a program it makes is
    born from the arrival too.
    - **Built:**
      - the region lists the bytes each arrival from a sender landed
        (`Landing` in `region.h`: where, how many, the sender, their
        hash), at most 128, the oldest dropped first. A landing over any
        of them drops them. A static run that starts in them checks their
        hash: the same, and its parent is the sender; changed, and they're
        dropped. The list is saved in snapshots (tag `REG3`) and checked by
        `Region::check` (in the region, from a sender, none overlapping);
      - the observer counts such a birth as an arrival's: a colonist if
        it's its lineage's first member at the place, and not a copy made
        there. A dead sender's record is kept while its landed bytes are
        listed;
      - the worlds report counts the static runs that started in landed
        bytes, and those of them that made a program. Its line on the
        lineage's births beyond its first place now says what it counts:
        all its colonies' births, not only the programs its arrivals made
        (a wrong label from the start).
    - **Checked:** `test_static_in_landed` (`tests/test_spread.cpp`): a
      static run in an arrival's landed bytes makes the sender's
      colonist, and with one of those bytes changed first, a lineage of
      its own. `test_colonist` now expects a dead sender's record kept
      while its landed bytes are listed. 30 of 30 test suites pass. Gate
      2's world reran seed 5 to 1.6 million ticks byte for byte, at 370
      million instructions a second.
    - **The batch, run again with it:** the world ran the same (every
      count of messages, arrivals and threads came out as before). What
      changed is what counts as the migrant's lineage:
      - **sensing: 930 places reached (93.1%; 424 before),** half of them
        by tick 632 and 90% by tick 1,384, the last at tick 18,366;
      - **sensing with the noise off: 630 (63.1%; 286 before).** The wave
        no longer stops at 10 hops. It reached every place within 9 hops,
        then a thinning share: 77% to 94% at 10 to 13 hops, 61% to 72% at
        14 to 17, 39% to 56% at 18 to 23, and 12% to 68% from 24 to 38
        hops. The median place at
        each distance was reached about 30 ticks a hop later (352 at 10
        hops, 631 at 20, 933 at 30);
      - **blind: 603 (60.4%; 523 before);**
      - **asking for any device: 948 (94.9%; 336 before),** 90% by tick
        1,110; with the noise off 541 (54.2%; 392 before).
    - **Static runs in landed bytes:** 12.8 million started in them in the
      sensing test, and 6,046 made a program (21.6 million and 1,008 with
      the noise off; 141,142 and 5,403 blind).
    - **Lineages a static run began still founded colonies:** 414 in the
      sensing test (1,233 before), 18 with the noise off (214), 424 blind
      (1,216), and 2,131 and 3,350 asking for any device (7,289 and
      11,940). They reached 69 places the migrant's lineage didn't in the
      sensing test, and 369 with the noise off, where the 18 had 78,406
      programs alive against its 47,403. Claude's reading, not counted:
      those start in landed bytes that are no longer all as they landed
      (a later landing partly over them), or in the bytes of two
      landings.
  - **Noise remakes a hand-placed replicator within a few thousand
    ticks**, here into ones that multiply at home. So the test worlds
    measure the mechanism with its variants included, not the migrant as
    written.
  - **The columns kept still have no bound:** 189 MB at 10,000 places
    after one day (step 5).
- 30 of 30 test suites pass.

### Step 8: the gate 3 runs (2026-11-17)

Started on Damian's word ("Yes, start step 8"): the report's additions
and the run's settings first, then a pilot. The 20 seeds are running, a
second time (the end of this step).

- **What was built:**
  - **The arrivals' own counts** (`region.h`; report-only, not saved or
    hashed): those that joined port 0's queue, those dropped for want of
    room in it, and those a cold start cleared from it. The counts the test
    worlds used mixed in boot blocks (dropped and waiting), and lost the
    arrivals a cold start cleared; gate 2's supply makes cold starts. Every
    arrival that joined has now run, been cleared or is waiting.
  - **The report's arrival lines are shared** (`app/arrivals.h`): the test
    worlds and the gate 3 runs print the same lines. The test worlds'
    landed and dropped counts now leave out boot blocks and count cleared
    arrivals; the outputs in `runs/gate3-worlds` came from the binary
    before.
  - **The gate 3 runs' report** (`app/origin.cpp`), for each seed on a
    map, after the spread measure:
    - the SENDs let go, those to a port 0 apart;
    - the arrivals: landed, dropped at port 0, cleared, waiting, ran,
      made a program, only SENT, did neither, lost their core to a ready
      thread, and still running; the first call to fail of those that kept
      their core and did neither, and how they ended; the static runs in
      their landed bytes (agreed 2026-11-17);
    - the network's messages: sent, delivered, dropped, on their way;
    - **the named checks (14.1):** the places arrivals landed at, ran at,
      and where none ran (10.4); the messages delivered to each place's
      ports, and the busiest 5 places' share against an even spread's
      (11.1); the topology changes, the columns worked out and their time
      a tick, against the network step's and the tick's, and the share of
      place-ticks with the network device off (11.3); and each seed's map
      (6.1), reported already.
  - **`runs/gate3-1/origin.cfg`:** `runs/gate2-2`'s settings, with its
    own output folders, `origin.spread_share` 0.25 (5 of 20), and
    `world.map = landscape` and `map.fields = true` written out. The rest
    of section 7's numbers are the defaults.
- **Checked:** `test_boot` checks the new counts (an arrival joined, two
  dropped, a boot block's drop left out, one cleared by a cold start). 30
  of 30 test suites pass. Gate 2's world reran seed 5 to 1.6 million ticks
  byte for byte, at 378.5 million instructions a second, and its report
  has no gate 3 lines (no map).
- **The pilot** (seeds 1 and 2 to 200,000 ticks, side by side, 32
  seconds; not kept in `runs/`):
  - **The report checks by hand.** For seed 1, every sum closes exactly:
    - 10,097,014 arrivals landed = 8,968,118 dropped + 28 cleared + 11,575
      waiting + 1,117,293 ran;
    - those that ran = 881 made a program + 768,280 only SENT + 347,892
      did neither + 240 still running at the end;
    - the first failed calls of the 347,747 that kept their core and did
      neither sum to 347,747, and so do how they ended;
    - the messages delivered to the places' ports (10,402,896) are the
      network's delivered count, and its 11,351,947 sent = delivered +
      944,011 dropped for no room + 98 stranded + 25 lost + 33 cut + 4,884
      on their way. The observer's SENDs let go are the same 11,351,947.
    Seed 2's sums close the same way.
  - **The network floods with idle runs' SENDs.** Across the two seeds,
    25.5 million messages in 200,000 ticks, 94% of them to a port 0.
    20.8 million arrivals landed and 13.4 million (64%) were dropped at
    full port 0 queues. Of the 7.5 million that ran, 6.5 million (87%)
    only SENT, and 1,015 made a program.
  - **Static gets less of the cores than at gate 2.** Against
    `runs/gate2-2`'s same seeds at tick 200,000: idle runs 4.5 million
    against 42.9 million (seed 1) and 21.2 million against 43.1 million
    (seed 2); instructions 2.7 billion against 11.3, and 5.2 against 10.7;
    births 1,462 against 3,674, and 1,137 against 1,960. Seed 2 reached
    stage 1 at tick 28,060 at gate 2, and not by tick 200,000 here.
  - **Claude's reading, not counted:** an idle run's registers all start
    at its own address, so a SEND it makes sends its own bytes, to the
    place and port its address names. Landed at a port 0, those bytes run
    as a prompt run from the same registers and SEND themselves on. So the
    first replicators of gate 3's world are bytes that copy themselves
    through the network without making a program, and they hold cores
    in SEND's waits. 13.3's measure doesn't count them: they found no
    colony.
  - **The named checks, so far:** arrivals landed and ran at all 48 places
    in both seeds. The busiest 5 places took 29.1% and 24.6% of the
    messages (an even spread: 10.4%); place 0 alone took 10.9% in seed 1,
    as 11.1 predicts of blind SENDs to A = 0. Routes cost almost nothing:
    18 and 12 topology changes, the network devices off for 0.02% and
    0.00% of place-ticks (gate 3's nights stay above their 600 mV). The
    network step took 21 and 37 µs of 125 and 161 µs ticks (17% and 23%),
    on one thread.
  - **Speed:** 106 and 160 million instructions a second a seed, two side
    by side.
- **Asked: the 20 seeds now, or a longer pilot first?** Damian: "Longer
  pilot first".
- **The second pilot** (`runs/gate3-pilot-1`: seeds 1 to 10 to 2 million
  ticks, 10 side by side, 28.6 minutes), against `runs/gate2-2`'s same
  seeds at tick 2 million:
  - **Copying began in 1 seed of the 10** (seed 8, stage 1 at tick
    252,962), against 6 at gate 2 (seeds 2, 3, 4, 6, 7 and 10; seed 3 had
    reached stage 3 by tick 59,600). 2 faithful births in all, against
    12,932; 32,160 births against 281,954.
  - **Static got 2% to 11% of gate 2's idle runs** (270 million in all
    against 4.2 billion), **and 6% to 51% of its instructions.**
  - **The flood went on:** 2.7 billion messages, 96% of them to a port 0;
    idle runs (prompt runs among them) called SEND 13.5 billion times,
    against 7 to 15 million a seed at gate 2. 2.3 billion arrivals
    landed, and 93% of them were dropped at full port 0 queues; of the
    157 million that ran, 73% only SENT and 20,648 made a program. One
    colony was founded (seed 7), and none settled.
  - **The named checks:** arrivals landed and ran at all 48 places in
    every seed (no place shut out). The busiest 5 places took 16% to 24%
    of the messages (an even spread: 10.4%), the busiest place 3% to 6%:
    less piled up than in the first pilot. Routes cost almost nothing
    (130 to 352 topology changes in 2 million ticks; network devices off
    0.01% to 0.02% of place-ticks). The network step took 66 to 138 µs of
    622 to 858 µs ticks (8% to 17%), on one thread.
  - **Speed:** 5 to 32 million instructions a second a seed, 10 side by
    side, against gate 2's 37 to 70; about 1,200 to 1,600 ticks a second
    a seed. With 3,000 to 27,000 instructions a tick, several seeds would
    reach the 10⁸-tick cap before the 10¹²-instruction budget. At these
    rates the 20 seeds would take a day and a half to two days.
  - **Claude's reading, not counted:** port 0's queue is full nearly all
    the time, and a core that comes free runs the queue's head as a
    prompt run before any static run (10.4). So the arrivals, mostly bytes
    that SEND themselves on, take the cores static used to have, and the
    origin of copying (gate 1's and 2's) starves. As it stands, gate 3's
    run would most likely fail the origin regression its pass mark needs,
    whatever it says about spread.
- **Asked: find out what floods first, or run the 20 seeds as agreed?**
  Damian: "Find what floods".
- **Built to find it** (report-only; the world is unchanged):
  - **A flood test** (`nova2 worlds`, `worlds.test = flood`): the world as
    its config has it, nothing placed. Every 1,000 ticks it reads every
    place's port 0 queue and groups the arrivals waiting by their bytes.
    After every tick it catches each idle run that has just SENT, with the
    code it ran from its start to the SEND (when that's 64 bytes or
    fewer), where it sent, from where and how much. It reports the queues,
    the messages, the runs started (static and prompt), the cycles each
    kind used, and at the end the commonest arrivals and sending code,
    disassembled.
  - **Region counters** (`region.h`): the cycles of the static runs and of
    the arrivals' prompt runs that ended, and the static runs that SENT;
    and the bytes of an arrival waiting (`waiting_bytes`).
- **What floods** (`runs/gate3-flood`: `runs/gate3-1/origin.cfg`, seeds 1
  and 2 to 200,000 ticks each; each reran the same, tick for tick. Gate
  2's world reran seed 5 to 1.6 million ticks byte for byte with the new
  counters, at 345 million instructions a second):
  - **Code that copies itself from port 0 to port 0.** Of the SENDs idle
    runs made (3.0 million in seed 1, 5.0 million in seed 2), over 99%
    sent bytes from the run's own start, and 84% and 86% went to a port 0.
    80% and 94% were made by prompt runs: arrivals sending themselves on.
    It's 10.4's "code that never becomes a program, which only sends
    itself on" (R4), which the design expected "may spread before any
    program does".
  - **It takes the cores.** The static runs' share of the cycles of the
    runs that ended, by blocks of 20,000 ticks: in seed 1, 91%, 76%, about
    60% to tick 120,000, 36%, then under 1% after tick 140,000; in seed 2,
    100% to tick 60,000, then 91%, 25%, 8%, 9%, 6%, 5% and 2%. A core that
    comes free takes the queue's head before any static run (10.4), and
    once the queues are rarely empty, static barely runs. A SEND also holds
    its core while the device transmits: a 1 KB message takes 5 to 65
    ticks on 16 to 256 bytes a tick.
  - **It evolves.** At first a SEND straight from an idle run's start
    sends the message limit, 1 KB, since D holds the run's address. Seed
    1's messages stayed about 1 KB. Seed 2's shrank from about 950 bytes
    to about 60 by tick 180,000, its commonest senders sending 34 to 47 bytes;
    and the share dropped at full queues fell from 80% to about 30%, since
    a 4 KB queue holds about 70 such copies, not 4.
  - **The commonest sending code:** in seed 1, families that start `nop C;
    SHL D; SHL D; SYS [D]` (the call's number read from memory) and SEND
    1 KB; in seed 2, sequences of 19 to 28 bytes ending in `SYS # -53`,
    sending 34 to 47 bytes. 69% and 40% of the SENDs came more than 64
    bytes on from the run's start, or behind it, and their code wasn't
    caught.
- **Open, for Damian:** whether to change the world, and how. Each way is
  a design change (10.4 or R4), and section 7's numbers stay as agreed:
  - **Arrivals share the cores with static:** a prompt run takes at most
    a set share of a place's cores (half, say), so static always keeps
    the rest. The travellers still carry bytes between reset regions, as
    10.4 wants, but can't crowd static out.
  - **Idle runs don't SEND (R4 reversed):** code travels only as a
    program's SEND. No travellers; arrivals still run as prompt runs and
    may SPAWN. Spread then needs a program that sends, which section 8's
    first risk says nothing has rewarded yet.
  - **Leave the world as it is:** the 20 seeds would most likely fail the
    origin regression, and 13.3 wouldn't count the travellers as spread.
  - Claude's recommendation: build the first two as switches, off by
    default so the world as agreed is unchanged, and run the 10-seed pilot
    with each against `runs/gate2-2`'s 6 seeds of 10 at stage 1 by tick 2
    million; then Damian chooses with those numbers in hand.
- **Damian's decision:** "Build and pilot both but NOT off by default".
  Both rules are design changes, now in `design.md` (9.2, 10.3, 10.4,
  11.2, 13.1, 13.3) and `design-history.md` (2026-11-17), and both are on
  by default:
  - **Idle runs don't SEND** (`os.idle_send`, default off): an idle run's
    SEND fails soft (A = 0), as DWRITE does, so code leaves a place only in
    a program's SEND. The attempts are still counted (`idle_sends`), which
    keeps gate 2's samples byte for byte.
  - **Arrivals share the cores with static** (`region.arrival_share`,
    default 0.5): arrivals from other places hold at most that share of a
    place's cores at once, rounded down but at least 1. While they hold
    it, a core that comes free runs static instead, and a halted core
    stays halted. A boot block is never held back. The cores held are
    counted when needed, so nothing new is saved; at 1 the world is as it
    was.
- **Checked:** `test_send` gained two tests: an idle run's SEND fails soft
  and nothing lands; and on a 4-core place with 4 spinning arrivals, 2
  hold cores at 0.5 and 4 at 1. The tests that need travellers
  (`test_tally`, the fuzz traffic case) turn `os.idle_send` on, and a new
  fuzz case runs traffic with the defaults (no travellers). The gate 3
  view's golden image was redrawn, since the world changed, and checked by
  eye. 30 of 30 test suites pass. Gate 2's world reran seed 5 to 1.6
  million ticks byte for byte, at 355.6 million instructions a second.
- **The third pilots** (seeds 1 to 10 to 2 million ticks, 10 side by side
  each; the three ran at once, 96 minutes), against `runs/gate2-2` and
  the second pilot at tick 2 million:
  - **Both rules** (`runs/gate3-pilot-both`, the defaults): **copying began
    in 6 seeds of the 10** (seeds 1, 2, 5, 7, 9 and 10; seed 1 reached stage
    2), as many as at gate 2, though not the same seeds, since the worlds
    differ. Static runs as it did at gate 2: 4.18 billion idle runs against
    4.2 billion, 250,101 births against 281,954, and 94 to 112 billion
    instructions a seed, as at gate 2. 179 faithful births against 12,932
    (12,723 of those in gate 2's seed 4; 209 in the other nine).
  - **The network carries programs' copies now.** 27,832 messages in all
    (2.7 billion in the second pilot), 85% of them to a port 0; idle runs
    tried 94.6 million SENDs, which failed. Seed 3 had a program that sent
    20,479 copies; the other seeds sent 114 to 2,532 messages. 21,778
    arrivals landed, 2,884 were dropped at full port 0 queues (nearly all
    in seed 3), 18,894 ran and 7 made a program; 11 static runs in landed
    bytes made a program of their sender's lineage. 16 colonies were
    founded, in 7 of the 10 seeds; none was alive at the end and none
    settled.
  - **The named checks:** arrivals landed at 22 to 47 of the 48 places,
    and ran wherever they landed (no place shut out). The busiest 5 places
    took 36% to 98% of the messages (an even spread: 10.4%): the few
    senders pile them up. Routes cost almost nothing (132 to 354 topology
    changes; network devices off 0.01% to 0.02% of place-ticks); the
    network step took 11 to 15 µs of 2,400 to 2,900 µs ticks, under 1%.
  - **No idle SEND alone** (`runs/gate3-pilot-nosend`, the share at 1):
    the same as both rules, tick for tick, in 8 seeds of the 10. Seed 5
    parted at tick 1,778,600 and ended 8 births apart. Seed 3 parted at
    tick 318,600, where its sending program filled queues: the share held
    arrivals back there, and seed 3 then had 38,251 births without the
    share against 26,979 with it. Copying began in 7 seeds (seed 3 too).
  - **The share alone** (`runs/gate3-pilot-share`, idle runs SENDing):
    **the flood went on.** 2.7 billion messages, as in the second pilot;
    idle runs tried 9.8 billion SENDs; 2.24 billion arrivals landed and
    95% were dropped at full queues; of the 123 million that ran, 101
    million only SENT. Idle runs, the arrivals among them, numbered 572
    million, 14% of gate 2's; copying began in 3 seeds (2, 3 and 4);
    79,661 births and 5 faithful.
    Claude's reading, not counted: static runs are idle runs too, and they
    SEND themselves on as well, so the half of the cores that static keeps
    is also spent in SEND's waits.
  - **Speed:** 17 to 21 million instructions a second a seed with both
    rules, 30 seeds at once on 24 hardware threads (60 host threads). Ten
    side by side should run about 2.5 to 3 times faster, near gate 2's 46
    to 53 a seed (seeds 1 to 10, bar seed 3's 16), and with about 51,000
    instructions a tick, as at gate 2, a seed reaches the
    10¹²-instruction budget at about 20 million ticks, well short of the
    10⁸ cap. Claude's estimate, not measured: the 20 seeds
    would take about as long as `runs/gate2-2`'s 17 hours.
  - **Claude's reading, not counted:** the rule that matters is no idle
    SEND. With it, gate 3's world runs as gate 2's did, and the origin of
    copying comes back at gate 2's pace to tick 2 million. The share
    changes little once only programs send, but it bit in seed 3, where
    one program filled the queues, and that's the case it's there for.
    Spread now needs a program that sends, and section 8's first risk
    stands: nothing rewards sending yet, and none of the 16 colonies
    lasted. Gate 2's stage 4 came at 8.7 to 17.3 million ticks in seeds 1
    to 10, so 2 million ticks can't say whether the origin regression
    holds.
- **Asked: run the 20 seeds (`runs/gate3-1`) with both rules, as they are
  by default?**
  Damian: "Go ahead with the 20 seed run" (2026-11-17).
- **The first run, `runs/gate3-1`, stopped unfinished (2026-11-18).**
  - Started 2026-11-17 19:46 and stopped on Damian's word ("stop it and
    rerun all 20") after about 23 hours: 11 seeds done, 9 running.
  - **Done:** seeds 5, 7, 8, 9, 10, 14, 16 and 17 reached stage 5; seed 3
    stage 4, seed 18 stage 3 and seed 6 stage 2. **Still running,** all
    at stage 5: seed 11 at 90% of its budget, seeds 4, 12, 13 and 19 at
    80%, seeds 1 and 20 at 60%, seeds 2 and 15 at 50%. So stage 4 was
    reached in 17 seeds, against the mark's 10.
  - **Why stopped:** its build slowed more and more late in a run. The
    observers copied their whole run-list pool for each new group
    (`optimization-plan.md`, "The late-run slowdown"), and each tenth of
    a seed's budget took 2 to 10 times as long as the one before. The
    9 could have taken days more. It also held 62 GB.
  - **What was lost:** its report. That build wrote the report only at
    the end and could be neither saved nor watched, so the spread and
    turnover readings of the 11 finished seeds went with the process.
    Their samples, windows, genome lists and snapshots are in the
    folder. The genome lists show colonies at other places in all 8
    finished seeds that reached stage 5, with members alive at the end
    in 6 of them (5, 7, 8, 14, 16, 17): life spreads, but a colony alive
    isn't one that settled, so the mark is not read from them.
- **The second run, `runs/gate3-2` (started 2026-11-18 19:11).**
  - The same seeds, world and marks, on the build of 2026-11-18: the
    slowdown's fix, the noise's fast log and pass 7
    (`optimization-plan.md`), none of which changes a world.
  - **What differs from the first,** each of it Damian's word: the
    observers as redesigned (`build-plan-observe.md`: a genome kept when
    its line lasts 3 generations, the records on disk); a save of each
    seed every 5 million ticks, 2 kept, so a stop loses nothing
    (`origin.resume` carries the run on); and the window on the run
    ("with the view please").
  - **Checked as it started** (`runs/gate3-2/compare.sh`): the first 10
    seeds' samples against `runs/gate3-1`'s, about 3,000 rows each. Every
    column the world decides is the same. The observers' own counts
    differ as the redesign means them to: genomes and groups (kept, not
    all), the scavenger label, and a few rows of the hijack count.
  - **It ended on 2026-11-19 at 03:58,** after 8 hours 47 minutes
    (`runs/gate3-2/report.txt`). The first run was a day in with 9 seeds
    to go. Three seeds ran their 100 million ticks (1, 2 and 20); the
    other 17 reached the 10¹² instructions first, at 16.9 to 99.8 million
    ticks.
  - **The report reads PASS against 7's mark,** each part of it:
    - **Spread: a lineage settled a place beyond the one it began in, in
      17 of the 20 seeds** (the mark is 5). The three without are the
      three that never got going: seed 6 (stage 2), seed 18 (stage 3)
      and seed 3 (stage 4 only 900,000 ticks before its budget ran out).
      In the 17, the first settlement came within 9.2 million ticks of
      stage 4, and in four of them (2, 4, 7, 8) before it; each seed
      settled from 10 times (seed 10) to 1,557 (seed 4), 9,187
      settlements in all, of 1.57 million colonies founded. At the end
      colonies were alive in 17 seeds, 642 of them.
    - **The origin regression: stage 4 in 18 of 20 seeds** (the mark is
      10; gate 2's run had 13), and stage 5 in 17.
    - **Gate 2's turnover mark:** 57,803 of 57,808 counted windows
      turned over, and 50,251 of 50,255 crowded ones, from 17 seeds (the
      mark is 90% of each, on 100 crowded windows from 5 seeds). The 5
      that didn't are in seeds 1 (3 of them), 4 and 15, no two in a row.
  - **What else it shows:**
    - **Strict places never passed stage 3;** every lax kind of place
      reached stage 5. Strict places had 235 counted windows against the
      lax ones' 57,573: life lives in the lax places.
    - **Most copies sent are lost at the door.** In seed 1, 423 million
      SENDs went to a port 0; 179 million landed, 167 million of those
      were dropped for want of room, 11.9 million ran and 67,361 made a
      program. Of those that ran and failed, nearly all failed at SPAWN,
      "not at an allocation of its own", as step 7's test worlds found.
    - **Late worlds thin out.** Seed 15 went from 7,684 programs at 20
      to 25 million ticks to 59 at its end, each holding most of a place
      with hundreds of threads (see the bug scan below). The stages and
      marks were all met long before.
    - The observers held up to 7.9 GB in a seed (seed 2), and the run's
      folder is 87 GB, 64 of it the saves.
  - **Gate 3 passes.** Damian confirmed it on 2026-11-19 ("Yes,
    passed").
- **The bug scan of the run's worlds (2026-11-19).** Damian, watching the
  memory map: the patterns "seem unusual at the end of the run compared
  to the start... more uniform and less noisy. Could be a natural thing,
  but it also could be due to memory corruption or something."
  - **The reading: natural.** The map colours a page by the program
    that holds it. Early, a place holds hundreds of small programs;
    late, one program holds most of it. Seed 15's last save: 59 programs
    with 33,559 threads; 47 of 48 places with 3 programs or fewer; in 32
    places one program holds half the memory or more. The program is a
    thread that SENDs itself onward, SPAWNs its own start as another
    thread of the same program with a 64-byte stack, and faults. With
    hundreds of threads ready no core is idle, so no idle run starts and
    nothing else is born there. Not every seed does it: seed 2 kept
    about 6,000 programs to the end.
  - **No corruption found** (`runs/bugscan/report.txt`):
    - every one of the run's 43 saved worlds passes the engine's own
      checks on every place (`nova2 check`, added for this: the memory's
      tables, the OS's, the reset region's, every holder alive);
    - the bytes add up in all 2,018 place snapshots (memory less free,
      held, page 0 and the region is never negative and always a slab's
      tail), with no drift over a run;
    - a late world replayed gives the run's own later save bit for bit
      (seed 15 from 95 million ticks to its end; seed 2 from 95 to 100
      million), and the same hash on 1, 3 and 4 host threads;
    - the two builds agree: every whole row of `runs/gate3-1`'s samples
      matches, to 71.9 million ticks.
  - **One real fault found in the OS,** which doesn't touch the tables
    or the map: a thread whose stack its program gave away or freed
    still has its registers saved there. 48 of the 1,015,757 threads in
    the 43 saves are such. Damian chose the rule on 2026-11-19, after
    the gate passed: a live thread's stack counts as in use, so a SPAWN
    into it starts a thread and a FREE of it fails (`design.md` 9.4;
    `os.stack_held`, on by default). This run was made under the old
    rule, which `os.stack_held = false` brings back.
  - **Noted, no change:** the ready queues' room (twice the threads and
    one) is enough at today's costs, where stale entries stay under 0.4
    of the live ones, but the comment that says a thread is queued once
    is wrong; and no run has ever called the engine's checks on its
    world, which `nova2 check` now lets a save be given.
