# The thread limit's pilot: open-6 and pilot B at 5 million ticks

2026-11-26. For Damian, who decides from it whether open-6 keeps running as
it is or starts again with other settings
(design-thread-limit-2026-11-25.md, section 6).

## The short answer

- **The limit does what it was built to do.** Where a program's threads are
  limited, threads no longer pile up, and the world holds 3.4 times the
  programs open-5 held at the same age.
- **It is not a balance.** The kinds of place are not led by different
  lineages or different ways of living. One plain one-thread copier wins
  under every limit, and one lineage tops 12 places across all four kinds.
- **The lower flip rate does not give longer genomes.** In open-5, open-6
  and pilot B alike, almost nothing is longer than 127 bytes. 127 is the
  largest number one instruction can hold, so my reading is that this step
  holds length down, not the flips. That reading is from the design, not
  tested.
- **What the lower rate does give:** lives 3.5 times as long, more faithful
  copying, a quarter of the births and a third of the generations.
- **My recommendation:** let open-6 keep running as it is, and keep the
  flip rate at 1e-6. Nothing here says a different list of limits or the
  lower rate would be better. "No one king" is still open and needs
  another mechanic.
- **One thing needs your word within two days:** where open-6's records
  go. Section 6.

## 1. The runs

- **open-6:** open-5's settings and seed with `os.thread_limits = 1 4 32 0`.
  28 places allow 1 thread a program, 32 allow 4, 20 allow 32 and 20 have
  no limit. Started at 01:53 from tick 0 on one build (commit 9c92eec). It
  passed 5 million ticks at 08:16 and is still running.
- **Pilot B:** the same world at a tenth of the flip rate (1e-7, starting
  at 1e-6). It reached 5 million ticks at 05:56 and stopped.
- **open-5** is the comparison for the whole world. Its records were
  cleared this morning, so its figures come from its rundowns and its
  progress lines.
- **Both runs are cut at tick 5,000,000.** A place's kind is its limit at
  tick 0. A place that is rebuilt after a failure draws its limit again, so
  a place or two may have changed kind (pilot B ended with 27, 32, 21 and
  20).

## 2. The world against open-5 at the same age (ticks 4.5 to 5 million)

- **Programs:** about 685,000, against open-5's 201,000. 3.4 times as many.
  The count has levelled off at about 680,000.
- **Places with programs:** 62 to 71 of 100, against open-5's 82. More
  life, in fewer places. Why is not known.
- **Births:** 205 a tick, against 42. They peaked at 263 a tick at 1 to 2
  million ticks and have fallen by a fifth since.
- **Faithful copies:** 32.5% of births, against 13 to 14%.
- **How long a program lives:** 3,477 ticks, against 4,696. Shorter than
  open-5's, but it has doubled since 2 million ticks.
- **Threads a program:** 2.2 to 2.4, against about 24.
- **Deepest line:** 7,557 generations at 5.3 million ticks. open-5 reached
  about 3,300 by 37 million.
- **Deaths:** memory lost 58%, fault 39%, cold start 1.5%, cut-out 1.0%,
  heat 0.3%. open-5 had more faults (46 to 49%) and more cold deaths.
- **Power and heat:** fewer places short of power (about 26 under 850 mV,
  against 33), and the hottest place has cooled from 76 to 62 degrees.
- **Genome length:** the middle length is 55 bytes and one in ten is over
  100. open-5's middle was 51 to 56 at this age. So genomes have not
  shrunk.

## 3. By kind of place, open-6

In the order 1 thread, 4 threads, 32 threads, no limit. Programs and
threads are means over ticks 4 to 5 million.

- **Programs:** 201,000, 376,000, 75,000 and 33,000.
- **Places with programs:** 13, 20, 11 and 11.
- **Threads a program:** 1.00, 1.88, 3.83 and 10.7.
- **The most threads one program held:** 1, 4, 32 and 65,389.
- **Births a tick:** 82, 98, 18 and 7.5. The 1- and 4-thread places make
  88% of the births.
- **Share of kept genomes:** 51%, 41%, 5% and 2%. In open-5, at 1.5 to 1.9 million ticks, the same 20
  places that now have no limit held 24%.
- **Genome length (middle, and one in ten over):** 60 and 109 bytes; 50
  and 70; 55 and 88; 66 and 115.
- **Held wholly by one lineage:** 8 of 12 places, 14 of 18, 5 of 6 and 6
  of 9.

What this says:

- **The tighter the limit, the more life.** The no-limit places went from
  a quarter of the world's genomes to a fiftieth.
- **Where there is no limit, one program still hoards.** One program there
  holds 43,000 to 65,000 threads, as in open-5. Those 20 places are the
  control, and they behave as open-5 did.
- **The flood did re-form as programs,** as section 7 of the proposal
  warned it might. A 1-thread place holds about 15,000 programs, where an
  open-5 place held about 2,000 with some 20 threads each. They are whole
  copiers, not empty shells: the kept genomes there are as long as
  anywhere.

## 4. Pilot B against open-6

Pilot B in brackets.

- **Programs:** 685,000 and level (570,000 and falling; it peaked near
  790,000 at 1 to 2 million ticks).
- **Births a tick:** 205 (49).
- **Faithful copies:** 32.5% (46 to 49%).
- **How long a program lives:** 3,477 ticks (12,316).
- **Deepest line:** 7,557 generations (2,690).
- **Deaths:** fault 39%, memory lost 58%, cold 1.5%, cut-out 1.0% (fault
  51%, memory lost 38%, cold 5.4%, cut-out 4.5%).
- **Turnover in pilot B:** all 914 counted windows turned over, the 744
  crowded ones among them. So places still turn over at the lower rate.
- **Genome length, the middle:** 55 bytes (63 to 64).
- **The longest lines that last:** 88 to 106 bytes (78 to 91).
- **By kind:** much the same split. The 1- and 4-thread places make 86% of
  pilot B's births. Threads a program are 1.00, 1.83, 5.98 and 8.46.
- **Refused SPAWNs in pilot B:** 597 million, a quarter of all thread
  SPAWNs. All of them in the 1-thread places, 35% in the 4-thread places
  and 28% in the 32-thread places. For open-6 this count is only written
  when the run stops.
- **Disk-write backlogs:** open-6 none. Pilot B ten, the biggest 25,273
  writes in one row. It ran on through all of them.

### The 127-byte wall

- In both runs under 0.005% of kept genomes are longer than 127 bytes. In
  open-6's 1-thread and no-limit places, the length that one genome in a
  hundred reaches is 126 and 127 bytes.
- A constant in an instruction is one byte, from -128 to 127 (design.md,
  the operand table). A copier that sets its length with one instruction
  cannot copy more than 127 bytes. A longer genome needs two steps to
  build the number, and design.md's own example is `MOV # 64; SHL # 2`.
- So my reading is that genomes stop at 127 because growing past it needs
  a new trick, not because flips cull them. Pilot B could not show what
  the flip rate does to long genomes, because neither run has any.
- This is inferred from the design and the lengths. I have not tested it.

## 5. The five tests from the proposal

1. **Threads a program fall where there is a limit, and the world's thread
   count falls.** Yes for threads a program: 1.0, 1.9 and 3.8 where
   limited, against 10.7 where not, and 2.3 world-wide against open-5's 24.
   The world's total is 1.5 million threads, about half of open-5's later 3
   million, with 3.4 times the programs.
2. **The kinds of place end up led by different lineages or ways of
   living.** No. Lineage 22.85 (21.85 in the viewer) holds 30% of kept
   genomes and tops 12 places: two 1-thread, six 4-thread, two 32-thread
   and two with no limit. Its working form is a 45-byte loop that copies
   itself with one thread. In pilot B no lineage dominates, but its leaders
   do not sort by kind either.
3. **In pilot B longer genomes hold, and places still turn over.** Places
   turn over. Longer genomes do not hold: the middle length is 8 bytes
   longer and the longest lasting lines are no longer. See the 127-byte
   wall above.
4. **Limit-one places don't fill with empty programs.** Not measured
   directly: nothing counts empty programs. What there is points the right
   way. The 1-thread places' kept genomes are full length (middle 60
   bytes), and those places make 40% of the births.
5. **Speed and memory are no worse.** Close to it. Near 5 million ticks
   open-6 runs 261 ticks a second and open-5 ran 263, with open-6 carrying
   3.4 times the programs. open-5 was then on a build about 15% slower, so
   like for like open-6 ticks somewhat slower. Memory peaked at 11 GB,
   against open-5's 12.

## 6. What needs deciding

**Does open-6 keep running as it is?** I recommend yes.

- The limits cap the thread flood and the world is far fuller for it.
- Nothing here points to a better list of limits. Variety by kind has not
  appeared, but 5 million ticks is early: in open-5 the first lineages to
  reach 10 places came after 30 million.
- The lower flip rate is not better for what you want. It slows evolution
  to a third and does not lengthen genomes.
- Starting again would cost the pure run its first 5.5 million ticks for
  no change I can argue for.

**Where do open-6's records go?** This needs your word within two days.

- open-6 keeps about ten times the genomes open-5 did, so its records
  grow by 50 to 60 GB a day. Drive E: has 153 GB free: two and a half to
  three days.
- Drive F: has 830 GB free, about two weeks at this pace.
- Moving them means a save, a stop of about 15 minutes, moving the
  records folder to F:, and a carry-on. The run's settings and build stay
  the same, and the world's bytes do not change: on this build a run
  carried on from a save gives the same files as an unbroken one (checked
  on a small world before open-6 started).
- I recommend the move. The other choice is to let the run stop when E:
  fills.

**Two things for later, not for today:**

- **No one king.** The limit did not deliver it. If it matters as much as
  before, it needs another mechanic, and I can set out options.
- **The 127-byte wall.** If you want longer genomes, this looks like the
  thing in the way. Whether to change it is a design question.

## 7. Where the figures come from, and what is not checked

- **Programs, threads and births by kind:** each run's per-place window
  records, read by `build-scratch/threads/ground.ps1`. On pilot B its
  output matches pilot B's own finished file on all 2,500 rows. Its births
  by kind agree with the rundown's, which come from another file, within a
  few percent.
- **Everything else in sections 2 to 4:** the rundown session's
  `open6-rundown-tick-5.42M.txt`, pilot B's own `report.txt`, and each
  run's progress lines. I have not re-derived the rundown's figures.
- **Not known:** why open-6 has life in fewer places than open-5; why
  place 34 keeps founding lines that copy alone in both runs; why open-6's
  hottest place cooled at 3.5 million ticks; whether open-6's refused
  SPAWNs match pilot B's shares.
- **Still to come:** the rundown's addendum on how new lineages are
  founded since 4.3 million ticks.
