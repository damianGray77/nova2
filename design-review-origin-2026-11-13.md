# Design review after the origin test's levers (2026-11-13)

Design 10.6: if the origin test fails, its five levers are tried in order,
"then the design is revisited with Damian." All five have been tried. This
is that review: what the runs showed, and a proposal for each question,
for Damian to decide. Evidence: build-plan-gate1.md section 11.

## Where gate 1 stands

- **The pass mark:** stage 4 in at least 5 of 10 seeds, within 10^12
  instructions a seed.
- **Runs 1 and 2** (the design as written): no stage 4. Run 1 had no stage
  1.
- **Runs 3 to 6:** stage 4 in 1 of 10 seeds, every time. Run 3 added two
  experimental rules, `os.thread_regs = start` and `os.alloc_d`. By run 5
  all of 10.6's levers were in use, and run 6 added lever 5's
  `os.spawn_top`.
- **Copying is common; holding a place is rare.** In runs 5 and 6, stage
  1 came in 8 or 9 seeds of 10, and stage 3 in 4 or 5.
- **What the copiers are like:**
  - they appear only on lax places, and no strict place has reached stage
    1 in any run;
  - they share pages: they copy, and run, past their own blocks;
  - those traced work only where their ALLOC's permission comes out as
    "both", which depends on their own address mod 3;
  - before spawn_top they were chains, one child each, so one failed copy
    ended the line. With it they leave many children, but they boom and
    die out, or, once, hold their place and freeze;
  - every copier that reached stage 4 needed `thread_regs = start` and
    `alloc_d`.

## 1. spawn_top breaks the design's own rules

- **9.1:** "the OS restarting, moving or rescuing programs unasked" is not
  fine. SPAWN asks for a new program, not for the caller to be moved.
- **Principle 12, no bootstraps:** no call hands a program a behaviour.
  spawn_top hands every copier the loop that appendix A.5's replicator
  writes for itself (`JMP # -16`).
- **Principle 9, real anchors:** no real OS sends a process back to its
  start after it creates another.
- **It didn't change the count:** stage 4 in 1 of 10, as in run 5 without
  it. It changed which seed got there.
- TOP (the spare-code jump to the start of its allocation) did no better
  than run 5 in the pilot.

**Proposal:** remove both experiments from the code. They showed what's
missing (copiers that go round again), not a rule the world should have.

## 2. Adopt thread_regs = start and alloc_d, with one fix

- **Both pass the design's tests.** They are argument conventions (a
  structural constant, 2), stated in registers and addresses:
  - `thread_regs = start`: a new thread starts with B, C and D at its start
    address, as an idle run does (A.3), instead of 0. A real thread gets its
    start argument in a register.
  - `alloc_d`: ALLOC's result also lands in D, where CPY writes. RECV
    already returns a second value in B, and real calling conventions
    return results in register pairs.
- **Nothing reached stage 4 without them** (runs 1 and 2).
- **The fix:** when ALLOC fails, `alloc_d` leaves D as it was, pointing at
  the last child. A.5 relies on a failed ALLOC putting 0 where the CPY
  goes, so that a copier in a full place faults and frees its memory ("that
  gives early turnover"). With D left on its last child, a copier in a full
  place may copy over that child instead and live on. ALLOC should put its
  result, the address or 0, in both A and D. Whether this explains seed 1's
  freeze is untested.

**Proposal:** write both into appendix A as the default (A.3's registers,
A.4's ALLOC), with ALLOC's result in D on failure too.

## 3. On a lax place, data pages are executable

- **The trap:** under `thread_regs = start`, B is a copier's own address,
  and ALLOC takes its permission from B mod 3. So a copier is fertile only
  where its address mod 3 is 2 ("both"):
  - at 1 its new block is executable, and its CPY faults writing it;
  - at 0 its new block is data, and its SPAWN fails, since SPAWN needs an
    executable page.

  The copiers traced use 24- and 48-byte slots, whose addresses share one
  value mod 3 across a page, so a whole page is fertile or barren for them,
  and two pages in three are barren.
- **The proposal:** a lax place has no no-execute bit, as x86 had none
  before NX (2004): any page a program can read, it can run. On a lax place
  ALLOC's "data" then gives "both". "Executable" still gives read-only
  code, so a program can still protect its code, and the trade-off between
  writable and protected code survives. Strict places are unchanged.
- **Effect:** the copiers found so far become fertile at two addresses in
  three, not one. The rules get no longer: it changes what "lax" means
  (9.3, 9.6), not the call.

**Proposal:** adopt it, and test it in a pilot against run 5's settings.

## 4. Gate 1 has no turnover, as the design expected

- **Seed 1's hold froze:** 95 members alive for 9 million ticks, and 54
  faithful births in that time. Nothing at gate 1 ends a program that
  doesn't fault (R30). Memory fade and cut-outs arrive at gate 2, whose
  question this is.
- **It doesn't block the pass:** stage 4 counts a frozen hold. But:
  - stage 5, adapting while crowded, needs turnover, so it may never come
    at gate 1;
  - the plainest fix for one-shot copiers, IP wrapping at the end of its
    allocation (as a 16-bit x86 IP wraps within its code segment, and as
    Avida's genomes are circular), was ruled out because nothing would end
    the junk loops it makes.
- **The design's own argument applies.** Gate 1 builds every call because
  "a call stubbed at gate 1 but live later would make the origin test
  answer a different question" (2). The same is true of death: in the full
  world, origins happen where memory fades and cores cut out.
- **The options:**
  - a. leave turnover at gate 2, and report stage 5 without expecting it
    at gate 1;
  - b. build gate 2's turnover (memory fade, cut-outs, a varying supply)
    before the next round of origin runs. A larger build, and the one that
    would make IP wrapping safe.

**Proposal:** a, for now. Revisit b if changes 1 to 3 still leave stage 4
at 1 seed in 10.

## 5. Strict places are barren at gate 1

- No strict place reached stage 1 in six runs. A strict copier needs a
  whole page and PROTECT: 24 bytes at least (A.5), more on bigger pages.
- The design already expects life to start in rough places and spread
  (10.2). The pass counts seeds, not places, and about half of each seed's
  places are lax.

**Proposal:** record it as a gate 1 finding. Strict places are for
migrants to settle at gate 3.

## 6. Seeing inside a place

- Neither seed 1's freeze nor the busts could be traced. The copiers
  depend on their surroundings (seed 1's sizes its ALLOC from SENSE), and
  the runs keep no copy of a place.
- Snapshots are in the design (2, principle 20) and wanted (Damian:
  periodic snapshots).

**Proposal:** build snapshots of a seed at chosen ticks, and let `nova2
trace` start from a place in one, before the next run.

## 7. The pass mark

- Stage 4 in 5 of 10 seeds needs about 0.7 established lineages per seed's
  budget. Runs 3 to 6 give about 0.1: seven times short.
- **The options:**
  - keep it;
  - raise the budget a seed, about seven times (a run of about a day);
  - if principled changes can't get near it, reopen 10.1's choice never to
    seed an ancestor in runs (Tierra and Avida seed one). The largest
    change, and Damian's call.

**Proposal:** keep the mark until changes 1 to 3 are tested.

## The next steps, if all the proposals are accepted

1. Remove spawn_top and TOP.
2. Write `thread_regs = start` and `alloc_d` (with D on failure) into
   appendix A as the default, and lax places' lack of a no-execute bit into
   9.3 and 9.6. Update the tests.
3. Build snapshots, and trace from a snapshot.
4. A pilot of the new defaults against run 5's settings (5 million ticks,
   10 seeds), then run 7 to the full budget.

## Decisions

Damian accepted all seven proposals on 2026-11-13 ("Accept all").

- **1 to 3 are done** in the code and the design: spawn_top and TOP are
  removed; the two conventions are the rules (A.3, A.4), with ALLOC's
  result in D on a failure too; a lax place has no no-execute bit (9.3).
  A.5's replicators are rewritten for the new conventions: 14 bytes on a
  lax place, 20 on a strict one, both traced in tests/test_os.cpp. All 17
  test suites pass.
- **4, 5 and 7 are recorded** in design.md 10.6.
- **6, snapshots,** is being built.
- **Pilot 4** (`runs/pilot-4`) runs run 5's settings under the new rules,
  10 seeds of 5 million ticks, against run 5 at the same ticks.
