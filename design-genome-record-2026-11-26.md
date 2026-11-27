# The record of what lasts: programs, their real parents, and changes: a proposal (2026-11-26)

Damian, 2026-11-26, on open-6's records growing by 35 GB a day: "I'd
rather our next phase be to optimize what data gets saved, how often it
gets saved, whether we actually need it to be saved". On the genomes:
"rather than losing any of that information, it would be better to have
something like a "types" list ... a "type" can just be a base program,
and each genome can just store its relationship and any changes to the
type (for originals) or changes to the parent for children". Then:
"lineage and generation are implicit data that one can get from tracing a
program back through the parent, so I don't think we need to retain that
information. I'm also fine with just keeping the original base type and
diffs of the rest rather than key frames every 16 generations. It might
take a little time to trace but I think we can write a script to return
lineage and generation for a given id or group of ids". And, told that a
faithful copy has no record of its own today: "that sounds like a bug in
the way we are handling copies ... it could be solved by setting the
parent id to the actual parent and then setting a bit flag to indicate it
is the same genome".

This is Claude's write-up of that design for Damian to sign off. Nothing
in design.md or the code changes until he does, and `runs/open-6` is not
touched. Section 9 lists the decisions, each with a lean.

## 1. Where things stand (measured on open-6)

- **The genome record is 99% of what a run writes.** At tick 8.4 million
  it was 21.0 of 21.2 GB. It grows by about 35 GB a day.
- **It is a record of genomes, not of programs.** design.md 13.2: "The
  genebank saves each new genome once, with when, where and its parent."
  A faithful copy has its parent's genome, so it adds nothing to the
  record. A stored generation number covers the copies in between.
- **That is not what Damian asked for on 2026-11-18:** "we only really
  need to save programs that last at least 3 generations" and "a record
  of each generation". The simulation itself is right: the observer knows
  every living program's real parent, lineage and generation. Only the
  file folds the copies away.
- **Each kept genome takes 82 bytes:** a fixed 64-byte row of facts and
  an entry of 18 bytes on average for its bytes.
- **The bytes are already stored as changes** from the parent, since
  2026-11-20. 92% of entries are changes, 14 bytes each. 8% are whole
  copies of about 60 bytes, most of them only because a whole copy is
  forced every 16 steps along a line.
- **The changes are small.** One byte in 43% of them, several bytes at
  the same length in 46%, a shorter genome in 2.6%, a longer one in 2.7%.
- **open-6 keeps about 21 new genomes a tick** (20 to 40 over the run).
  Between a kept genome and the one it changed from there are 1.14
  faithful copies on average: the generation step is 2.14.

Figures are from `build-scratch/threads/types.ps1`, which reads the
record without writing. It rebuilt 2,000,000 genomes kept at ticks 12.33
to 12.43 million, all of them, and 2,000,000 kept at ticks 2.57 to 2.61
million with the same picture.

## 2. The record: every program that lasts, and its real parent

**One file, one record for each program that lasts,** in the order they
earn it. A program lasts when it has a grandchild: today's rule
(`observe.keep_generations` = 3), applied to the program and no longer to
its genome. A record holds:

- **its parent:** how far back in the file its real parent's record is;
- **its genome:**
  - a flag, if it is the same as its parent's, and nothing more;
  - or the changes from its parent's bytes, as today;
  - or a pointer to another record, if that genome is already on record
    from another program;
- **when:** the tick it earned the record, as a step from the record
  before, and how many ticks before that it was born;
- **its place, only where it differs from its parent's.**

Not stored: lineage, generation, the genome's hash, its parent's hash and
its group number.

**A base record** is a program with no parent on record: a founder. It
states its lineage and its place. Its genome is written whole, as a base
type, unless that genome is already on record.

**The parent is always on record first.** A program's parent gets its
first grandchild no later than the program's own first child, so the
parent has earned its record before the program can earn one.

## 3. What is traced instead of stored

- **Lineage:** a program's lineage is its parent's, back to the base
  record, which states it. With real parents there are no exceptions.
- **Generation:** the number of steps back to the base record. Every
  step is one birth, so the count is exact.
- **The genome's bytes:** the nearest base type or whole genome above it,
  with each record's changes applied on the way down. Records flagged
  "same genome" change nothing.

## 4. Base types, and no more whole copies along a line

- **A genome is written whole only as a base type:** a founder's genome
  that is not yet on record (about 0.6% of today's kept genomes), or a
  genome whose changes would be no shorter than itself (0.8%).
- **The whole copy every 16 steps goes.** It was chosen on 2026-11-20 and
  this reverses it.
- **Two cases that are whole today disappear:** a parent that was never
  kept (0.8%), because the parent is now always on record, and a parent
  already let go from the observer's memory (1.2%). For the second the
  observer has to hold on to a recorded program's genome and its record's
  position while a child or grandchild that could still earn a record is
  alive.

## 5. The tracing tool

`nova2 genomes` reads the record today. It gains a trace:

- **For an id, or a list of ids:** each program's lineage, generation,
  base type, place, birth tick and genome length, the number of changes
  to the genome on the way, and the bytes if asked for.
- **For a stretch of ticks, or the whole record:** the same for every
  record in it, written to a file. It reads forward once and remembers
  each record's answer for its children, so it does not walk one trace a
  record.
- **An id is where a record starts in the file.** The parent is named by
  how many bytes back it is, so each step of a trace is one read. A small
  index beside the file gives the Nth record's id.
- **The run's analysis scripts** (`tools/run-analysis`) get lineage and
  generation from the tool's output and no longer read rows directly.

## 6. What it costs

- **Size, the part that is measured:** a record with a changed genome
  takes about 20 bytes, against 82 today.
- **Size, the part that is not:** a "same genome" record takes about 7
  bytes (flag, parent, ticks). How many there are is not known, because
  nothing on disk counts the programs that last.
  - At least 1.14 for each changed genome: the copies in between, which
    counting the generation needs.
  - More under the rule of section 2, which also records lasting copies
    that no changed genome descends from. My estimate is 28 to 42 a tick,
    from a guess that 40 to 60% of faithful copies have a grandchild.
  - So about 12 to 14 GB a day at open-6's pace, against 35 today. The
    floor is 11.5. A stored one-byte generation step in place of the copy
    records would be 9, and would lose the copies.
- **A trace takes steps.** The deepest line alive is 11,600 generations,
  so 11,600 small reads at most, and a typical record now is about 3,300.
  Both drives are solid-state. Traces get longer as a run ages.
- **Damage spreads further.** Today a damaged entry spoils its line up to
  the next whole copy. With no whole copies it spoils every record below
  it. The hash that let a reader notice a wrongly rebuilt genome is gone
  too. Lean: a check value for each block of a few thousand records,
  which catches damage at almost no cost, and the full hash kept on base
  types only.
- **Ids change.** Anything that names a genome by its hash would name a
  record by its position.
- **The group number goes from the record.** Only one analysis script
  reads it. It is the observer's own sorting of genomes, and a child
  shares its parent's group in only 27% of genomes. Section 7.
- **Whole-record tallies need a scratch table.** Counting records by
  lineage across the whole record means the tool first works out every
  record's lineage, several GB of scratch at open-6's size. A stretch of
  ticks needs far less.
- **In the observer:** it has to know, for each living program, whether
  its parent and grandparent have earned a record and where. That is new
  bookkeeping, and its memory and speed are measured before and after.
  Anything in the engine that reads a genome back from the file has to
  stay out of the run's way, since that read is now a trace.

## 7. What this does not do

- **It does not stop the growth.** The record still grows with every
  program that lasts, at 35 to 40% of today's pace. A year of open-6
  would be 4 to 5 TB. If that is not viable the next question is which
  programs earn a record, and that one does lose something. Options can
  be measured.
- **It leaves the observer's groups alone.** A group is a base genome and
  everything sharing 80% of its 3-byte runs, the nearest thing to a type
  today. open-6 has made 128 million of them for 358 million kept
  genomes. The table of each lineage's groups is never trimmed, and it is
  why the observers' memory grows (2.2 GB at tick 13.7 million). Stage 5
  is judged by counting new groups, so changing them is a decision about
  the gates and belongs in a proposal of its own.
- **It leaves the saves and the samples alone.** The samples are 94 MB in
  8 million ticks. The saves are bounded at two pairs, about 5 GB.

## 8. Getting there

- **First a count.** The build session counts, in a short test world, how
  many programs earn a record a tick, copies and changed apart, and
  reports it before the rest is built. That settles the unmeasured part
  of section 6.
- **Then the build:** the record's writer in the observer, the reader and
  the trace in `nova2 genomes`, and a converter from the present files.
- **Checks before it is used:**
  - the world comes out byte for byte the same as on the present build
    (the observer only watches);
  - in a test world recorded both ways, every genome kept the old way is
    found the new way with the same lineage, generation, place, ticks and
    bytes.
- **Records made before this cannot become real program records.** The
  copies between two kept genomes were never written. The converter
  writes stand-in copy records, marked as such, as many as the stored
  generation step says, so a trace counts the same generation. Every
  converted genome is then traced and compared with its old row, stored
  hash included. The stand-ins say nothing true about which copies were
  brothers.
- **open-6** would take it up by a save and a carry-on on the new build,
  with its record so far converted. The world's bytes do not change. The
  observer's saved state does change shape, so the new build has to read
  the old one. Whether and when open-6 switches is Damian's to decide
  once the checks are in.
- **Old record files are deleted only on Damian's word,** after the
  converted ones have passed the comparison.

## 9. Decisions for Damian

Each has Claude's lean. "Yes, as leaned" to the lot is enough to start
the count and the build.

1. **The record:** one for each program that lasts, with its real parent,
   and a flag, a pointer or the changes for its genome. Lean: yes (as
   Damian set out).
2. **Which copies earn a record:** every program with a grandchild, copy
   or not, or only the copies a changed genome descends from. Lean: every
   program with a grandchild. It is the rule of 2026-11-18 as Damian
   worded it, the parent is always on record first, and each copy's own
   place and birth tick are kept. The count comes first (section 8).
3. **Lineage and generation:** not stored, counted by the trace. Lean:
   yes (as Damian set out).
4. **Whole copies:** base types only, with no whole copy every 16 steps.
   Lean: yes (as Damian set out).
5. **The hash:** dropped from each record, kept on base types, with a
   check value for each block of records. Lean: yes.
6. **The group number:** dropped from the record. Lean: yes.
7. **Ids:** a record's id is its position in the file. Lean: yes.
8. **The tool:** `nova2 genomes` traces an id, a list, a stretch of ticks
   or the whole record, and the analysis scripts use it. Lean: yes.
9. **Records made before this:** converted with marked stand-in copies
   and checked genome by genome before anything old is deleted. Lean:
   yes.
10. **Not in this proposal:** which programs earn a record, and the
    observer's groups. Lean: decide each after this is built.

## 10. What was decided (2026-11-26)

- **The ten decisions:** Damian, "Yes, as leaned". The design is in
  `design.md` 13.2 as agreed and not yet built, with an entry in
  `design-history.md`.
- **The count comes first.** The build session counts how many programs
  earn a record a tick, copies and changed apart, in a short test world,
  and reports before the rest is built.
- **open-6 is not touched** until the checks of section 8 are in and
  Damian has said whether and when it switches.
