# Nova 2 — Observation build plan

> **Status (2026-11-18):** agreed. Damian decided the four choices in 2:
> a genome is kept when its line lasts three generations, with every
> generation recorded; groups are made of kept genomes only; no memory
> is kept of dead programs' genomes for the scavenger label; records
> reach the disk every 30 seconds and at each save. Steps 1 to 6 are
> built and measured (section 9); step 6's big place was measured at 250 MB
> on 2026-11-19, and 3.5's forgetting for runs with no end built then.
> Damian asked for it on 2026-11-18, after gate 3's run
> (`runs/gate3-1`) was found holding 58 GB with nine seeds running, on
> a machine whose page file left it about 2 GB of headroom. His
> direction, in his words: "I don't think there's a good reason to keep
> the entire run in memory without saving it to a file on the fly. If
> disk IO is too slow, then we can just batch it to save the difference
> every 30 seconds or something so that we aren't continually needing
> more memory. We also probably need to be a bit more selective on what
> genomes are stored. [...] I think we only really need to save programs
> that last at least 3 generations. All the rest is really just noise."
> It also matters for his plan once development is done: about 20
> places of 1 GB each, run overnight for months, stopped and started
> from saves.
> Numbers in brackets, like (13.2), are sections of `design.md`, which
> stays the authority. When the two disagree, the design wins and the
> plan is fixed.

## 1. Where the memory goes now

The observers (13) keep every record they make for the whole of a seed,
and none of it is ever let go. The world itself is small beside them:
a gate 3 seed's 48 places hold 160 KB of memory and 1 MB of disk each,
about 56 MB in all.

Counts from the run's sample files, at about 13:30 on 2026-11-18, with
record sizes from the code (the split is an estimate, not a measurement
inside the running process):

- **Seed 2** (half its budget run): 60.6 million births, 14.4 million
  distinct genomes, 3.3 million genome groups, 0.37 million lineages.
- **Seed 8** (finished): 117.8 million births, 15.7 million distinct
  genomes, 1.9 million groups, 0.43 million lineages.
- **Genomes:** each is a record (56 bytes), an entry in the hash index,
  and its bytes, mostly 32 to 64 of them: about 180 bytes a genome, so
  about 2 GB a seed.
- **Groups:** each keeps its reference genome's distinct 3-byte runs,
  sorted, 8 bytes a run, and its place on those runs' lists in the
  groups' index: about 800 bytes a group, so about 2 GB a seed.
- **Lineages, samples and voltages:** under half a gigabyte a seed
  together. A sample is about 400 bytes every 100 ticks; seed 1 has
  600,000 of them.
- **Spare room:** the tables grow by doubling, so up to half of each can
  be room not yet used, and a doubling needs the old and the new copy at
  once for a moment.

So about 6 GB a seed. Most of it is genomes that were born once and never
again: seed 8's 117.8 million births made 15.7 million genomes, and the
genome list it wrote shows its commonest few born thousands of times.

**A save carries all of it.** The observers' file beside a save
(`build-plan-viewer.md` step 9) is their whole memory, so a save of a
seed like seed 2 would be several GB, and grow with every save.

**A bug found on the way.** `dump_genomes` (`app/origin.cpp`) sorts the
genomes by packing each one's index into 24 bits. Past 16,777,215
genomes, the list it writes (`genomes-seed-N.txt`) shows the wrong
genomes, in the wrong order. Seed 2 of the running gate 3 run will very
likely pass that, and seeds 11, 12 and 15 may. The report's stages and
pass marks don't use the list, so gate 3's verdict is safe; the running
program can't be changed, so those seeds' lists will need to be read
with this in mind. The same packing is used for lineages, which are far
fewer. Fixed in step 1.

## 2. Choices for Damian

Each has Claude's recommendation first.

1. **What "lasts three generations" means for a genome.**
   **Decided 2026-11-18: by descendants, with every generation
   recorded.** Damian chose the second way offered ("I'd like a record
   of each generation in the genome so we can look at how it mutated
   and branched out over time"):
   - A genome is kept once a program carrying it has a grandchild: the
     carrier, a child and a grandchild are three generations, whatever
     genomes the child and grandchild have. The setting
     `observe.keep_generations` says how many (3); at 1, every genome is
     kept, as now.
   - Every kept genome records the genome it came from (its first
     carrier's parent's) and its generation in its lineage. So the
     genebank is a tree of genomes: each line that lasted, generation by
     generation, with where each mutation happened and where lines
     branched.
   - It keeps more than the first way offered (the same genome three
     generations in a row) would have, since a mutant with grandchildren
     is kept. Step 1's pilot measures how many.
   - The first way offered: a genome kept only once it is carried three
     generations in a row. It keeps fewer, but drops the generations in
     between that changed, so the tree has gaps.
2. **Which genomes the groups are made of.** **Decided 2026-11-18:
   kept genomes only, as recommended** (Damian asked for more on what
   this means first; the explanation is below).
   - **What groups are (13.2):** the observers sort genomes into
     species-like groups by likeness. A group's first genome is its
     reference; a new genome joins the first group whose reference
     shares at least 80% of its 3-byte runs, else it founds a group of
     its own. Groups are how the observers tell a new kind of program
     from a variation on an old one. Stage 5 (10.6) counts them: a
     lineage that has held its place makes 3 new groups while its place
     is crowded, so it is diversifying, not just copying.
   - **Recommended: kept genomes only.** A genome joins a group, or
     founds one, when it is kept. The groups were most of the memory, and
     most groups today are founded by a mutant born once.
   - This changes what stage 5 measures (10.6: "3 new groups while
     crowded"): it would count only groups founded by genomes whose
     lines lasted three generations (choice 1). That is stricter, and
     closer to what stage 5 is for. Gate 3's run measured it the old
     way, so later runs' stage 5 figures won't compare directly with
     gate 3's; at `observe.keep_generations = 1` they would.
   - Or: group every genome as now, and keep only the genebank
     selective. The groups' 2 GB a seed stays.
3. **What the observers say about a birth into a forgotten genome's
   corpse.** Damian asked (2026-11-18) why there should be a rule here
   at all; the answer changed the recommendation.
   - **This is about labels, not the world.** In the world nothing
     changes: a program that halts gives its pages back to the free pool
     with their bytes intact (9.3, "Nothing is zeroed"), and any program
     may ALLOC them and SPAWN into the dead program's code. That is
     ordinary behaviour, and nothing in it depends on the observers.
     The observers only notice it afterwards: a birth with nothing
     written whose bytes match a genome they remember is labelled a
     scavenger (13.3), and takes that genome as its own.
   - **Decided 2026-11-18: no memory of corpses.** Damian: "for the
     great majority of simulations, this really isn't going to matter a
     whole lot. So I don't think we need to keep this memory of genome
     corpses around." Nothing is held for the sake of the label. The
     observers hold a genome only while it could still be kept (a
     carrier, or a carrier's child, is alive) and for good once kept. A
     birth into the code of a genome held for those reasons is labelled
     as now, at no cost; a birth into any other leftover code is counted
     as a birth into unknown leftovers.
   - **What can be traced later:** the kept genomes and the lineages
     are in the streams, so leftover code from a line that lasted can
     be matched to it afterwards. Births themselves aren't logged one
     by one in long runs (13.1's volume: about 55 GB a day), so code
     from a line that ended within two generations can't be traced.
   - The first recommendation, set aside: remember every genome for
     200,000 ticks after its last carrier died, so a scavenger into it
     would still be named.
4. **When records reach the disk.**
   **Decided 2026-11-18: every 30 seconds of the run's time on the
   clock (Damian's number), and at every save.** Records go into
   buffers in memory as they are finished, and the buffers are written
   out together. A crash loses at most the last 30 seconds of records,
   and the files are the same whatever the timing, since only when they
   are written changes, not what.

## 3. The design

### 3.1 Records written as they finish

- **A record stream** (`core`, new): a table that is only added to,
  whose rows are fixed in size. It has two homes:
  - **in memory**, as now, for tests and for worlds run without a
    folder for records;
  - **in a file**, with a buffer: rows go into the buffer, and the
    buffer goes to the file every 30 seconds and at each save (choice
    4). Rows are read back from the file by number, so a reader (the
    charts) needs only the rows it shows.
- **What is streamed,** each to its own file per seed:
  - **samples** (13.5) and each place's **voltages** per sample. The CSV
    the origin test writes now stays as it is; the stream is the binary
    copy the charts read back;
  - **finished turnover windows** (written today only at a seed's end);
  - **kept genomes** (3.2): the genebank, each genome once, with when,
    where, its lineage, its group and its bytes;
  - **finished lineages** (3.3).
- **What stays in memory:** what the observers still act on. The
  programs alive and the dead ones still remembered (boot writers, dead
  senders); the genomes they carry; the kept genomes' index, groups and
  counts; the lineages not yet finished; running totals.
- **Saves:** the observers' file beside a save holds what stays in
  memory, plus how many rows each stream had. Carrying on from a save
  cuts each stream back to that many rows before going on, so the files
  match the run as it carries on, as `origin.resume` does now for the
  CSVs. A save becomes small: the living population's records and the
  kept genomes, not the whole history.
- **Folders:** one new origin test setting, `origin.records`, a folder
  for the streams (`seed-N.samples`, `.volts`, `.windows`, `.genomes`,
  `.lineages`). Without it, the streams stay in memory, as the
  observers do now.

### 3.2 Genomes kept, and the rest forgotten

- **A new genome is provisional.** Its record and bytes are held while a
  program carries it, as now, so faithful births and near copies are
  judged exactly as now: a parent is alive at its child's SPAWN, so its
  genome is always at hand.
- **Kept** when a program carrying it has a grandchild (choice 1, at
  `observe.keep_generations = 3`). Each program's record carries its
  parent's and grandparent's genomes, so at each birth the
  grandparent's genome is kept, if it isn't already. Then it joins or
  founds a group (choice 2), and is written to the genebank stream with
  the genome it came from and its generation. It stays in memory with
  its counts, since later births with it update them.
- **Every generation of a line is kept.** When a grandchild is born,
  its grandparent's genome is kept; the grandparent's own grandparent
  was kept when the grandparent's parent had its grandchild, and so on
  up. So a line that lasts is kept whole, mutations included.
- **Forgotten** once it can no longer be kept (choice 3): no carrier is
  alive and no carrier's child is. Programs remembered after they die
  (a boot block's last writer, a sender whose message may still arrive)
  count as alive for this, since a boot or an arrival can carry their
  line on. Each provisional genome counts those that hold it, and is let
  go when the count reaches 0.
- **Births with a forgotten genome** start it again as new: a mutation
  back to an old genome is a new genome to the observers.
- **A birth into leftover code** (13.3's scavenger) names the genome it
  ran if the observers still remember it, and is otherwise counted as a
  birth into unknown leftovers (choice 3).
- **Groups (13.2)** are unchanged in method: greedy, 3-byte runs, the
  0.8 threshold. Only which genomes go into them changes.
- **A program's group** is its genome's once kept. A program whose
  genome isn't kept has none, and a lineage's groups (for stage 5) are
  counted when one of its genomes is kept, with "crowded" judged at
  that birth's place then.
- **The samples' counts** say what they count: genomes kept, genomes
  provisional (held now), groups.

### 3.3 Lineages let go when finished

- A lineage is **finished** when it is extinct and nothing can bring it
  back: no place's boot block names a writer of it, and no dead sender
  of it is still remembered (13.2, 13.3). Its stages can't change after
  that.
- A finished lineage is written to the lineages stream with everything
  its record holds, and let go. The origin test's per-lineage stage
  clocks go with it.
- Most lineages are a single program from static that never copied,
  so this keeps the table to the lineages alive or able to return.
- The genome list at a seed's end (`genomes-seed-N.txt`) reads its
  deepest lineages from the stream as well as from memory.

### 3.4 Sizes to check at 1 GB places

With the history gone, what is left grows with the living population.
A 1 GB place could hold millions of programs at once; the observers
keep about 100 bytes for each (13.3's records), and the OS its own
tables beside them. Step 6 measures this on a 1 GB place before Damian
sets up 20 of them, and says what a place of that size needs.

### 3.5 Forgetting, for a run with no end [2026-11-19; built overnight on Damian's approval, and confirmed by him that day]

Section 3.2 keeps a kept genome and its group in memory for good, which
a gate's run needs (its counts are of everything ever kept) and a run
with no end can't afford: gate 3's second run (`runs/gate3-2`) reached
4.6 GB of observers in a seed at 100 million ticks, about 37 bytes a
birth, and a busy world of 200 places of 4 MB makes 160,000 births a
second. So `observe.forget` (off by default):

- **What goes.** A kept genome that no program holds: none alive or
  remembered carries it, or is the child of one that did (3.2's holders,
  which already say when a held genome can be forgotten). A group goes
  when its last genome does.
- **When.** At a sample, between two ticks, once the kept genomes in
  memory reach twice what the last pass left (and at least 65,536). So
  the memory stays within about twice what the living population needs,
  and the passes cost a constant share of the run.
- **What a pass does.** It frees the genomes' slots and bytes, numbers
  the groups left afresh in their order, builds the index of runs again
  from them, and counts the pairs of lineage and group again from the
  genomes and programs left.
- **Nothing is lost to the record.** Every kept genome is a row of the
  genebank's file from the tick it was kept (3.1).
- **What changes in the counts,** with it on:
  - a sample's `groups` is the groups with living members, not every
    group ever founded (the progress line gives that, and the kept
    genomes let go);
  - a genome or a group that went and turns up again is new: a second
    row in the genebank, a group founded again, and for a lineage a new
    group once more, stage 5's count included;
  - the scavenger and hijack labels, and through them a few of the
    faithful and near-copy counts, name only what is still remembered,
    as with 3.2's choice 3;
  - group numbers in the genebank's rows are the numbers of their time:
    a pass renumbers.
  The world is untouched: its sample columns are the same, row for row.
- **Still growing, slowly:** the lineages that copied faithfully or
  founded a colony, and the colonies, are never let go (3.3). In gate 3
  that was under 2% of the observers' memory. `origin.memory_floor`
  stops a run with a save before the machine runs out.

## 4. The code: what changes

- `core/include/nova2/record.h`, `core/src/record.cpp` (new): the
  record stream, in memory or in a file, its buffer and its 30-second
  flush, read back by row, cut back on carrying on.
- `core/include/nova2/observe.h`, `core/src/observe.cpp`: samples,
  voltages, windows, kept genomes and finished lineages through streams;
  provisional genomes with carrier counts and the forgetting queue;
  groups made at keeping; lineages let go when finished; save and load
  of the smaller memory, with the streams' row counts; a measure of its
  own memory, table by table.
- `core/src/keys.inc`: `observe.keep_generations` (3),
  `observe.flush_seconds` (30), `origin.records` (empty: in memory).
- `app/origin.cpp`: the records folder; the genome list's sort fixed
  (step 1) and reading finished lineages back; the stage clocks let go
  with their lineages; the memory measure on the progress lines.
- `app/ui/viewer.cpp`, `app/ui/figures.cpp`: the charts read samples
  through the stream; figures count kept and provisional genomes.
- Tests: `tests/test_record.cpp` (new), `tests/test_observe.cpp`,
  `tests/test_view.cpp` (where the origin test's saves and carrying on
  are tested now), `tests/test_determinism.cpp`.

## 5. The order of work

Each step ends with all suites passing (`.claude/verify.ps1`), and with
the world's hash the same with or without the observers, as always
(13.1).

1. **Measure, and fix the list.**
   - The observers report their memory, table by table; the origin
     test's progress lines show it.
   - `dump_genomes` sorts by count and then index without packing them
     into one word, for any number of genomes and lineages. A test
     sorts keys whose indexes pass 2^24 and checks the order.
   - A pilot of one gate 3 seed for a few million ticks records where
     its memory goes, to set against the estimates in 1.
2. **The record stream,** with its tests: rows in and read back, in
   memory and in a file; the flush on its clock and at a save; a stream
   cut back on carrying on; the same files whatever the flush timing.
3. **Samples, voltages and windows through streams.** The charts read
   them back. The viewer's goldens are unchanged, and a run carried on
   from a save writes the same streams as one that wasn't stopped.
4. **Genomes kept and forgotten, and groups of kept genomes.**
   - At `observe.keep_generations = 1`, every count and stage is the
     same as today's, checked against today's goldens: the old measure
     stays reachable.
   - At 3: a genome is kept when a carrier has a grandchild and not
     before, with the genome it came from and its generation; a line of
     mutants is kept whole; a genome whose line ends sooner is forgotten
     when its last carrier's children are gone; a line carried on by a
     boot or an arrival from a dead program keeps every generation; a
     scavenger into a remembered genome is still named; a stage 5 test
     world still reaches stage 5.
5. **Lineages let go when finished,** with a boot and an arrival that
   bring back a lineage tested (neither may be let go while it can
   return), and the genome list's lineages read back.
6. **The whole, measured.**
   - One gate 3 seed run to the same tick both ways (as gate 3 ran, and
     redesigned), comparing memory, speed and stage results.
   - A save and a carry-on at that size: the save's size, and the same
     files and results as a run not stopped.
   - A 1 GB place filled with copiers: its programs, and the observers'
     and the OS's memory per program (3.4).
   - The results go under this step, and to Damian.

## 6. Tests that run throughout

- All suites, with the determinism goldens: the world's hash never
  changes, since the observers never change the world.
- Same results on 1 and 24 threads, and across a save and carry-on.
- At `observe.keep_generations = 1`, today's observer results exactly.

## 7. Risks

- **Stage 5 measures something different at 3** (choice 2). Gate 4
  and 5's readings would be on the new measure; gate 3's stay on the
  old. Running a check seed at 1 keeps the two comparable when needed.
- **A disk that fills or fails mid-run.** A stream that can't write
  stops the seed with a clear message rather than running on without
  its records; a save before that can be carried on from.
- **The charts reading a file being written.** They read only rows
  already flushed; the newest up to 30 seconds' worth are read from the
  buffer in memory.
- **Lineages let go too early.** A lineage brought back by a boot or an
  arrival after it was let go would start a new record and lose its
  history. Step 5's tests cover both ways back; a lineage is only let go
  when neither is possible.

## 8. Size

About 1,800 lines: 300 for the record stream, 600 for the observers'
changes, 250 for the origin test and the views, 650 of tests.

## 9. Results so far

### Step 1: the measure, and the list fixed (2026-11-18)

- `count_key` (`nova2/array.h`) sorts by a count held to 32 bits and an
  index of 32, in place of the genome list's 24-bit packing. The genome
  list and its deepest lineages use it. A test sorts indexes past 2^24
  and counts past 2^32.
- `Observer::memory()` gives the observers' memory by what it is for
  (genomes, groups, lineages, programs, history, scratch), room taken but
  not yet used included; each progress line of the origin test shows it.
- A pilot of seed 2 for 4 million ticks matched the gate 3 run's own
  samples tick for tick, but seed 2 doesn't start copying until about 8
  million ticks, so it showed only the early cost: 54 MB, mostly samples
  (history 33 MB) and the groups' index (11 MB). A pilot of seed 7, which
  starts copying at about 3.5 million ticks, ran to 7 million: 5.5
  million births, and the observers held 531 MB (groups 244, genomes
  142, lineages 77, history 66). Groups grew fastest, 126 MB at 4.9
  million ticks to 244 MB at 7 million.

### Step 2: the record stream (2026-11-18)

- `nova2/record.h`, `core/src/record.cpp`: a stream of fixed-size rows,
  in memory or in a file. In a file, rows go into a buffer written out
  every `observe.flush_seconds` (30), when it reaches 16 MB, and when
  asked; rows are read back by number through a 64 KB block cache;
  carrying on cuts the file back to the rows a save counted. `Records<T>`
  reads a stream like an array, rows by value.
- `tests/test_record.cpp` (a new suite, 31 in all): rows read back from
  memory, buffer and file; a file cut back and carried on is the same,
  byte for byte, as one written in one go, and so is one flushed at every
  tick; a save counting more rows than the file has is refused; a full
  buffer is written without being asked.

### Step 3: samples, voltages and windows through streams (2026-11-18)

- The observers' `samples`, `volts` and `windows` are `Records`. With
  `origin.records` set, they go to `seed-N.samples`, `.volts` and
  `.windows` in that folder; empty, they stay in memory as before.
- A save holds each stream's row count (the rows themselves when in
  memory); `origin.resume` reopens the files cut back to that count. A
  record file that can't be written stops its seed with a message.
- The observers' save format moved to `NOV2OBS2`; saves of the old
  format are refused.
- Tests (`test_view`): a run with its records in files writes the same
  samples, windows, genomes and report as one keeping them in memory;
  stopped at tick 5,000 and carried on from its save of tick 4,000, it
  cuts its files back and comes to the same record files, byte for
  byte, as a run never stopped. The viewer's goldens are unchanged. All
  31 suites pass.

### Step 4: genomes kept and forgotten, groups of kept genomes (2026-11-18)

- `observe.keep_generations` (default 3, 1 to 4). Every program carries
  the genomes of its last three forebears. A genome is kept when a birth
  makes it that many generations deep (at 3: a carrier's grandchild),
  and only then joins or founds a group and counts for its lineage's
  groups (stage 5). Each kept genome goes to the genebank stream with
  the genome it came from, its generation, the tick it was kept and its
  bytes, in `seed-N.genomes` and `seed-N.genome_bytes` when
  `origin.records` is set.
- A genome not yet kept is held by its carriers alive, their children,
  and the records a boot or an arrival may still need (the boot block's
  dead writer, a dead sender). When the last lets go it is forgotten:
  its slot is reused and its bytes are dropped, the bank compacted once
  forgotten bytes are most of it. A birth into the code of a forgotten
  genome is a birth into unknown leftovers (choice 3).
- Saves hold the genebank's slots, counts and streams. The samples CSV
  gained a `genomes_held` column at its end, and its `genomes` column
  counts the kept. The genome list, the migrant report and the viewer's
  figures show kept genomes; a program's genome not yet kept shows as
  "not yet kept".
- At `keep_generations = 1` every test passes against the goldens of
  step 3, pixel for pixel (checked by running the view suite at 1). At
  the default of 3 two goldens changed, both as designed: the
  replicator's group is 0 instead of 1 (the empty genome that founded
  group 0 is never kept), and the world's figures list 3 kept genomes
  in 3 groups and 1 held, where they listed 6 in 5.
- Tests (`test_observe`): at 1, a genome kept at its first birth; at 3,
  kept at the grandchild and not before, a line of mutants kept
  generation by generation with the genome each came from, a line that
  ends at a child forgotten when both are dead and its slots reused, a
  dead sender's arrival carrying a line on, a scavenger named while
  another carrier holds the genome and not once none does, a save read
  back going on as one never saved, and a save under other keeping
  refused; stage 5's count in a crowded place, five kinds counted at 1
  and three at 3. `test_view`'s record files now include the genebank's
  two, byte for byte after a stop and carry-on. All 31 suites pass.
- Not tested in a world: stage 5 itself. The observers' small test world
  never crowds a place (about half its memory stays free), so no group
  there counts toward stage 5 at either setting; the gate 3 seed run of
  step 6 compares the stages both ways.

### Step 5: lineages let go when finished (2026-11-18)

- A lineage now counts its dead members' records kept for a way back
  (`LineageRec::remembered`: the boot block's dead writer, dead
  senders). When it is extinct and that count is 0, it can't return.
- **Narrower than 3.3, by choice (Damian agreed 2026-11-18):** only a
  finished lineage that never copied faithfully and founded no colony
  is let go. Those are most lineages (single programs from static that
  never copied) and nothing refers to them by their slot. Copied
  lineages stay in memory, because the origin test's stage clocks, its
  stage 4 record and the colony tables all point at lineages by
  position; letting those go would mean reworking how the origin test
  counts stages, at the risk of changing them. If step 6 shows the
  copied lineages are a large share, that is the next thing to do.
- A lineage let go goes to the `finished_lineages` stream
  (`seed-N.lineages` under `origin.records`) and its slot is reused by
  the next new lineage. The samples' lineage count, and the viewer's,
  count the let-go lineages too, so every count stays as it was.
  Saves hold the free slots and the stream.
- Tests: a never-copied lineage let go when its last member dies, its
  record in the stream, its slot reused; a copied one kept; one whose
  dead sender is remembered, or whose dead member wrote the boot block
  (`test_boot`'s dead writer), kept with its count at 1; a save read
  back with the same slots, counts and stream; the record-files test
  compares the lineages file too. The goldens are unchanged. All 31
  suites pass.
- The genome list's deepest lineages need nothing from the stream:
  they must have copied faithfully, so they are never let go.

### Step 6: the whole, measured (2026-11-18; the 1 GB place still to do)

Seed 7 under gate 3's settings to 7 million ticks, as gate 3 ran
(`observe.keep_generations = 1`, everything in memory) and redesigned
(3, records to files), side by side with the gate 3 run on the same
machine (`runs/observe-6`):

- **The same world:** programs, births (5,542,898), deaths, instructions
  and every other world column of the samples identical, sample by
  sample; the lineage count too (298,569), the let-go ones included.
- **Memory:** observers 692 MB the old way at the end (genomes 304,
  groups 244, lineages 75, history 67), 125 MB redesigned (genomes 51,
  groups 49, lineages 22, history 1).
- **Saves:** the test's save at 7 million ticks 527 MB the old way, 114
  MB redesigned; the world's 130 MB either way.
- **Speed:** 1,527 s against 1,215 s for the whole run, the redesign
  about a fifth faster (both shared the machine with the gate 3 run).
- **Genomes and groups:** 684,918 genomes kept the old way, 164,066
  redesigned (24%), with 921 held at the end; 185,694 groups against
  52,583 (28%). The record files at the end: 10 MB of genebank rows and
  7.5 MB of genome bytes, 38 MB of finished lineages, 29 MB of samples.
- **Stages:** 1 to 4 on the same ticks. Stage 5 at tick 3,481,600
  against 3,481,100: 500 ticks later, since only kept genomes found new
  groups now, so a new kind counts once its line has lasted.
- **Labels (choice 3):** 73,244 births named scavengers against 85,125;
  the 11,881 others went into the code of a genome no longer held. With
  them, 1,145 fewer births count as faithful, 1,682 fewer as near copies
  and one fewer hijack.
- **Carrying on:** the redesigned run carried on from its save of tick 6
  million cut its six record files back and came to the same files,
  samples, windows, genome list and report as the run never stopped,
  byte for byte.
- **What gate 3's full runs would need:** a projection, not a measure.
  The kept shares above (about a quarter of genomes and groups) would
  take gate 3's 6 GB a seed of observers to about 1.5 GB, most of it
  the kept genomes and their groups, which stay in memory.
- **Still to do:** the 1 GB place filled with copiers (3.4), waiting for
  the gate 3 run to end: that run commits about 62 GB, and the machine
  had about 16 GB of headroom left.

### Step 6's place, measured at 250 MB (2026-11-19)

Damian asked for a run of 200 places of 250 MB. Measured on the release
build before setting it up (`runs/big-test`):

- **A 250 MB place takes 2.8 GB of memory as built,** 11 bytes for each
  of its own, and its save is 2.6 GB. The tables are sized for the worst
  case at the start: an allocation record for every 8 bytes (32 bytes
  each, 1 GB of the 2.8), a thread and a program record for every 64
  bytes (the smallest stack), three ready queues of two entries a
  thread, and a holder's list head for every record. So 200 such places
  need about 560 GB; the machine has 63.
- **Even with every table following use,** a full place would need
  about two and a half times its bytes (the bytes, a word for every 8 of
  them, and records for a few million allocations and a million
  threads), so 200 places of 250 MB would still need over 100 GB.
- **Speed falls with memory.** A busy world of 200 places of 4 MB
  (4 to 32 cores) filled to 1.5 million programs and 2.1 million threads
  within 5,000 ticks and then ran 310 ticks a second on 24 host threads,
  100,000 instructions a tick. The cost of a tick follows the threads
  alive (30 to 40 ns each), and the threads follow the memory, so places
  of 250 MB would run about 5 ticks a second.
- **The observers** held 180 bytes for each living program (263 MB for
  1.47 million), as 3.4 expected.
- **What a place of that size needs:** tables that follow use (the
  allocation, thread and program records, and the ready queues), and a
  tick whose cost doesn't follow the threads that are only waiting.
  Neither is built. The open-ended run of 2026-11-19 (`runs/open-1`)
  uses 200 places of 4 MB: 9 GB of memory, 8.6 GB a save.

### Step 7: forgetting (3.5), built and measured (2026-11-19)

- **Built:** `observe.forget`, the pass (`Observer::forget_extinct`),
  its counts (groups ever founded, kept genomes let go) in the save and
  on the progress line, and a test (`test_forget`): three extinct kept
  genomes and their groups leave at a pass, the group left is numbered
  afresh and still takes a near copy by its runs, a genome that comes
  back is kept as new, a save read back goes on the same, and with
  forgetting off all four stay. The save's tag is "NOV2OBS3": a save of
  an earlier build is refused.
- **Measured** on the busy 200-place world of 4 MB places, 30,000 ticks
  both ways (`runs/big-test/b4` and `b4f`): the world's sample columns
  the same in all 301 rows; kept genomes in memory 57,334 against
  152,502, groups 15,200 against 23,820, observers 343 MB against 381
  (266 of it the living programs' records either way). The labels that
  differ: hijack, scavenger, near copies, and faithful in 75 rows.
