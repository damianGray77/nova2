# The GPU and NPU, fleshed out: a proposal (2026-11-23)

Damian, 2026-11-23: "Its fine for open 3 to not include gpu and npu but I
do want these aspects fleshed out. Its fine if we don't use the real thing
because it's slower than using CPU but I want evolving programs ro have
access to this virtual hardware."

**On hold** (Damian, 2026-11-23: "maybe we can wait until tuesfay's fable
reset to do the design for gpu and npu integration"): the design is done
after the Fable limit resets on Tuesday, 2026-11-24 (6 am Chicago). This
draft is a starting point for it, not decided.

This is Claude's proposal for Damian to decide. Nothing in design.md or the
code changes until he does. Each choice below has a lean; the decisions
Damian needs to make are listed together at the end (section 9).

## 1. Where things stand

- **Designed in outline, never built.** design.md has:
  - the two kinds in the hardware catalogue (7.2): the GPU as lanes
    against draw, the NPU as multiply-adds per tick;
  - two calls, GLAUNCH and NMATMUL (9.2), left without numbers;
  - a sketch of each (9.5).
- **Parked on 2026-11-11 (R20, the design review).** The reason given was
  that neither had a condition in which using it wins: "Lanes can't
  reproduce or travel, and the world has no matrix-shaped work." 14.2
  lists what was left open:
  - EXIT for lanes;
  - a cap on a launch's run;
  - how programs share the device;
  - NMATMUL's calling convention and number format.
- **Nova 2 has no GPU or NPU code at all.** The engine survey found none.
- **Nova 1 built both as virtual devices** (Nova 1's design.md 6.7, 6.8 and
  6.14), and reported these findings on short three-seed checks, which
  give a direction only:
  - random code grabbed both units and ran millions of operations on them;
  - GPU copying arose from random code, and the GPU sped up the first
    replicators;
  - the NPU halved the rate of first replicators. Nova 1's reading was
    that random code spent its time on NPU work that did nothing for
    copying. A lineage that had already replicated did later use it;
  - Nova 1's lanes ran its own instruction set in lockstep. The rules for
    that (6.14) carry over almost unchanged, and are used below.

## 2. Virtual, on the host's CPU

- Both devices are simulated in the engine, on the host's CPU threads, like
  every other device. The host's real GPU and NPU aren't used. That keeps:
  - every world bit for bit deterministic;
  - no new dependency (no Direct3D or DirectML);
  - saves and resumes exactly as now.
- **The cost to the host.** Lanes are interpreted like threads, so a world
  whose programs use their GPUs heavily runs slower on the host. Two
  things soften this:
  - a group of 32 lanes decodes each instruction once for all of them, so
    it costs less than 32 threads would;
  - an NPU's multiply-adds are plain integer loops, cheap on the host.
- **A world with no GPU or NPU is gate 5's world exactly.** With the
  devices set off (the default), the build gives byte-identical worlds,
  saves and hashes. That is the regression check, as at gates 4 and 5.

## 3. The hardware

- **Two new device kinds,** as the catalogue already names them:
  - **GPU:** a lane count (groups of 32), a cost per lane-step about 5 to
    10 times a P core's on the same instruction, a launch overhead, and a
    high draw at about 1 V;
  - **NPU:** multiply-adds per tick, a launch overhead, a low draw, and
    about 5 to 10 times less energy per multiply-add than the GPU. It does
    nothing else.
- **Each model is a point on its kind's trade-off curve:**
  - a GPU with more lanes draws more;
  - a faster NPU draws more.
- **Where they are.** Lean: each place has none or one of each, dealt from
  a field over the map like the cooling devices. That way GPU-rich and
  GPU-poor regions form, with neighbours alike. The share of places with
  each is a setting.
- **Full members of the physics,** wired in the same way as the cooling
  device:
  - **power:** draw while working, idle draw otherwise;
  - **heat:** passed to the place, with heat limits from real parts (GPUs
    85 to 90 °C);
  - **voltage:** cut-outs below their threshold;
  - **wear:** failures, and replacement at visits;
  - **respawns:** drawn fresh at a respawn.
- **Programs find them with SENSE.** Each appears in the place's device
  list after the disk, as device kind 7 (GPU) or 8 (NPU), with its speed,
  threshold and state. SENSE's readings keep their numbers, so what
  existing programs read doesn't change. Only places with the devices
  report the extra entries.

## 4. GLAUNCH: programs on the GPU

**The call:**

- A: the code's address, in an executable allocation the program holds.
- B: the lane count, from 1 to the GPU's lane count (clamped).
- C: the address of one data allocation the program holds. The lanes may
  read and write it.
- D: the cap, the most instructions a lane may run. It is clamped to a
  setting, which closes 14.2's "a cap on its run".

**Each lane:**

- **Registers:** its own A, B, C, D and IP, running the same 22
  instructions as a core.
- **Start:** IP at the code, A = its lane number, B = the data
  allocation's address, C = the lane count, D = 0.
- **Memory:** it reads the code allocation, and reads and writes the data
  allocation; nothing else. CPY works within those, at its cost per byte,
  so lanes can copy in parallel.
- **No stack** (lean): PUSH and POP fault, as they do in the reset region's
  idle runs.
- **SYS:** any SYS ends the lane, whatever its number. That is 14.2's
  "EXIT for lanes": lanes never reach the OS, as real GPU threads don't.
- **Faults** (lean): a fault ends that lane only. design.md's sketch had a
  fault end the whole launch; ending only the lane fails softer, as calls
  do.

**Lockstep, as Nova 1 ran it:**

- Lanes run in groups of 32. Each step runs the instruction at the lowest
  IP among the group's running lanes, for every lane at that IP.
- Lanes that branch apart take turns, which is the real cost of
  divergence. Lanes that reach the same address again merge without
  extra cost.
- Within a step, every lane reads before any lane writes. When two lanes
  write the same byte, the highest lane's value stays. That keeps it
  deterministic.

**When it runs:**

- The calling thread sleeps, as on DREAD.
- The GPU runs at the place's barrier, after the cores' turns, on that
  tick's budget of group-steps. The budget follows the GPU's speed,
  voltage and heat, as a core's cycles do.
- A launch that isn't finished carries on next tick.
- When every lane has ended, by SYS, a fault or the cap, the thread wakes
  with A = the number of lanes that ended by SYS (the clean ones). It is
  returned the way RECV's result is returned now.

**Sharing:**

- Launches queue in the order they were made: by core, then by the order
  of the calls, as disk requests are now.
- All queued launches' groups take turns, a step each. One long launch
  can't shut the others out. That is 14.2's "a rule for sharing the
  device".

**Other rules:**

- **Cost:** a launch overhead on the calling core, in the order of SPAWN's
  (about 10,000 cycles). The GPU's draw comes on top while it works.
- **Errors:** lanes run at the GPU's clock with its error floor (7.5).
  Running hot or undervolted brings copy errors, as on a core.
- **Rowhammer** (lean): lanes' accesses count as row activations, as a
  thread's do. Rowhammer from GPUs is real.
- **Idle runs** (the reset region's code) can't launch: the call fails
  soft. So do launches on a place with no GPU, or with its GPU cut out or
  failed. They still charge the call's cost, as SEND did with the network
  device off.

## 5. NMATMUL: the NPU

**The call:**

- A: the address of X, m by k bytes, row by row.
- B: the address of W, k by n bytes.
- C: the address of the result Y, m by n bytes.
- D: the shape, one per byte: m, k and n, each 1 to 256 (a 0 byte means
  256), and the shift, from 0 to 31.

**The number format** (lean), which closes 14.2's open convention:

- Every input byte is a signed number from -128 to 127, as the
  instruction set already reads memory.
- Sums are kept in 32 bits.
- Each result is the sum shifted right by the shift, held within -128 to
  127, and written as a byte.
- This is how real NPUs run small integer networks. It keeps everything in
  bytes, and an identity W with shift 0 copies exactly.

**Access:**

- X and W must be readable by the program, and Y must be in an allocation
  it holds.
- Otherwise the call fails soft: A = 0, and nothing is written.

**When it runs:**

- As DREAD does: the thread sleeps, and the NPU works at the barrier
  through its multiply-adds for the tick. A big job spans ticks.
- The result is written all at once, at the end.
- The thread wakes with A = 1.
- Jobs queue in order and run one at a time.

**Cost:** a launch overhead on the calling core, plus the NPU's small draw
per multiply-add.

**Idle runs, and places with no working NPU:** the call fails soft, as
GLAUNCH does.

## 6. The calls' numbers

- **GLAUNCH and NMATMUL become calls 16 and 17.** The slot tables of every
  run so far stay valid, and those worlds don't change.
- **A run with the devices lists them in its slot table** (os.slots), as
  open-3's lever already adds slots for other calls.
- **Neighbouring numbers matter (9.2).** A one-bit flip turns one call into
  another. Where the new calls sit in the table is something to test, as
  the first lever was.

## 7. What makes them worth using (R20's condition)

R20 asked for a condition in which each wins. This is how it could happen
in this world, with no new rule imposed.

**The GPU: parallel work, at a high draw.**

- A program can copy itself into many allocations in one launch, one lane
  per byte or per slice, and then SPAWN each copy.
- On a crowded place the cores are shared fairly among threads. The GPU is
  extra compute that only its users get.
- Its high draw is affordable in a strong supply (by day, near sources),
  and costly at night and in sag.
- So it should make a niche shaped by the physics, which is gate 4's
  question again.

**The NPU: the cheapest work per joule.**

- A W of 1 (1 by 1), or an identity, copies bytes with one multiply-add a
  byte, for much less energy than CPY on a core.
- A dot product of a copy with its parent catches corruption from the
  noise. Nova 1's bench replicator did exactly that.
- So in dim places (cut-outs, low voltage), programs that copy or check
  through the NPU should outlast those that don't.

**No matrix work is added to the world** (lean). Giving the world tasks
that pay for matrix maths would be a new mechanic with an imposed goal. It
can be designed later if the NPU sits unused.

**The risk:**

- The NPU could become the copier everywhere, and the cores matter less.
- The launch overhead is the lever: short copies stay cheaper on a core,
  and long ones go to the NPU.
- The starting numbers would be set by a pilot, as gate 4's and 5's were.

## 8. Seeing them

- **The calls columns** (origin.calls) gain GLAUNCH and NMATMUL.
- **The samples** gain launches, lane-steps, multiply-adds, and the GPU's
  and NPU's draw and heat.
- **The lineage records** mark GPU and NPU users: the first launch or job
  by a lineage, and copies whose bytes were mostly written by lanes or by
  the NPU. These are Nova 1's gpu_replicator and npu_user milestones.
- **The viewer's place page** shows the two devices, with their power,
  heat and wear lines.

## 9. Decisions for Damian

**1. When to build it.** Lean: as gate 6, after gate 5 closes. Its
question would be "do programs take up the GPU and NPU where they have
them?", which names R20's condition. Its pass mark would be set in its
build plan, for example lineages that use a device holding more of the
places that have it than of those that don't, over at least 5 seeds.

**2. The leans above, to take or change:**

- one GPU and one NPU at most a place, dealt by fields, the share a
  setting (3);
- a lane's fault ends that lane, not the launch (4);
- lanes have no stack (4);
- any SYS in a lane ends the lane (4);
- launches share the GPU a step at a time (4);
- lanes count for rowhammer (4);
- signed bytes in, 32-bit sums, a shift and a clamp back to bytes (5);
- the NPU may be used as a copier, held in check by its launch overhead
  (7);
- no matrix work added to the world (7).

## 10. What would follow

On Damian's word:

1. The decisions go into design.md, which unparks R20, and into a build
   plan (build-plan-gate6.md).
2. The build session builds it after gate 5's step 15 closes, as the one
   writer of the code.
3. First a regression check: with the devices off, gate 5's world exactly.
4. Then a pilot to set the starting numbers.
