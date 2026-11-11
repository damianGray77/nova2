# Nova 1 survey (2026-11-10)

A read-only look at Nova 1 (`E:\Projects\nova`) as it stood on 2026-11-10,
for deciding what Nova 2 keeps and what it simplifies. Sizes are from the
working tree that day.

## 1. Size

| Part | Files | Lines |
|------|------:|------:|
| `core/` (the engine library) | 85 | ~44,900 |
| `app/` (command line, viewer, setup page) | 17 | ~11,200 |
| `tests/` (one executable, ~45 suites) | 48 | ~23,900 |
| `docs/` (design, settings reference, ~40 plans) | 42 | ~8,600 |

Built over ten phases between 2026-11-05 and 2026-11-09, each adding features
with their own switches and settings.

## 2. How it is put together

1. **World** (`world.cpp`, `graph`, `landscape`): a graph of nodes, the tick
   loop, and the barrier where cross-node effects (cargo, signals, disasters,
   collapse) are applied in a fixed order.
2. **Soup per node** (`soup.cpp`, `soup.h`): the bodies living there and the
   node's ledgers (millicores, GPU and NPU units, memory, disk), heat,
   hazards, death, dormancy, sparks and vents.
3. **Interpreter** (`interp.cpp`, `isa`): the codon tables and the
   instructions.
4. **GPU warps** (`warp.cpp`, `warp_isa.inc`, `warp_kernel.hlsl`): a second
   interpreter, one source compiled as C++ and as HLSL, running bodies in
   lockstep lanes.
5. **Real devices** (`accel_dx.cpp`, `accel_ref.cpp`): Direct3D 12 and
   DirectML for the in-world GPU and NPU ops and for the warp kernel, each with
   a CPU reference that gives the same results bit for bit.
6. **Observers** (`lineage`, `genebank`, `profile`, `lifemap`, `analysis/`,
   `learn/`, `view`, `app/viewer`, `app/setup`): detection, lineages,
   analysis, and the Win32 viewer and setup page. These don't change the world.
7. **Plumbing** (`keys`, `config`, `run`, `snapshot`, `replay`, `stats`,
   `budget`, `mapped`, `pool`, own containers): settings, runs, snapshots,
   replay logs, CSV output, budgets and the thread pool.

## 3. Where it is heaviest or most tangled

### 3.1 Four files hold most of the engine

| File | Size | Lines |
|------|-----:|------:|
| `core/src/soup.cpp` | 199 KB | 5,098 |
| `core/src/world.cpp` | 169 KB | 4,013 |
| `core/src/accel_dx.cpp` | 167 KB | 4,004 |
| `core/src/interp.cpp` | 144 KB | 3,546 |
| `core/include/nova/soup.h` | 98 KB | 1,708 (28 types) |
| `app/viewer.cpp` | 116 KB | 2,783 |

### 3.2 Two ways to use the GPU

- **Accelerator ops:** the GPU verb's BX 0–19. Twenty op kinds (compare, copy,
  reductions, arithmetic) plus NPU dot, matvec and matmul, in instant or async
  mode with a latency. The NPU works through float16 matrix multiplies of
  base-8 digits.
- **Warps:** whole programs running on GPU lanes, with per-node warp widths,
  each warp's own lanes, FXC and DXC kernels, and `warp.device = auto` sharing
  each round between the CPU and the GPU.

Both have CPU references and their own verification. This is the biggest
overlap.

### 3.3 Eating and contact built in layers

Engulf bites, wounds and bleeding, digestion of swallowed prey, a surface
shield and contest, salvage of remnants, and INJECT moving a whole program in
as a parasite. Each phase reworked the one before (design.md 6.16, 6.26, 6.27,
6.29).

### 3.4 Overlapping systems

- **Sources of material:** sparks, vents, decay to INERT, cosmic-ray flips,
  substrates, multipliers.
- **Storage:** memory, memory tiers, disk, disk tiers, volatility,
  memory-mapped files for the disk tier, budgets.
- **Endings:** heat and throttling, CPU wear, node endurance and collapse,
  ruins and recovery, the 200-tick watchdog, dormancy and drift, natural
  disasters.
- **Cores:** threads, P and E cores, slices, millicores.

### 3.5 Old options kept beside new ones

- Two world models (`demo`, `soup`).
- Three graph types (`ring`, `procedural`, `landscape`).
- Two cost tables (`classic`, `home_v1`).
- Several codon tables (`nova_v1`, `nova_v1_plain`, `nova_v1_inv2`,
  `nova_v2`) and shuffled selector numbering.
- Replay of old knob-change records.
- `Config::set_fallback` keeping old defaults so the tests can test one feature
  at a time.

Under the no-backward-compatibility rule these are the easiest cuts.

### 3.6 Settings

385 settings (83 from 14 families, 11 hidden). Rows in `docs/keys.md` by
group, where a family counts as one row:

| Group | Rows | Group | Rows |
|-------|-----:|-------|-----:|
| Disasters | 64 | Hazards | 18 |
| Sparks | 23 | Run and output | 18 |
| World and land | 22 | CPU and cores | 17 |
| Paths and bridges | 22 | Detection and lineages | 17 |
| GPU | 21 | Heat | 14 |
| Resources | 11 | Contact and eating | 11 |
| Disk | 10 | Substrates | 9 |
| Signals | 7 | Memory, Dormancy, Budgets | 6 each |
| NPU, Life and death | 5 each | Costs | 2 |

### 3.7 Docs

`docs/design.md` is 300 KB, numbered 6.1–6.29 by phase, and reads as a
history rather than a description of the current design. About 40 separate
plan files sit beside it. The README's status section is also a phase
history.
