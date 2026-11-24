Run analysis scripts (open-2 rundowns)

These read a running world's observer records without disturbing it, and trace genomes alone with the run's own exe.
They were written for the open-2 rundowns of 2026-11-20 and default to runs/open-2/records beside this folder;
pass -Dir <records folder> for another run (the seed's file names are found from the one *.genomes file there).

Rules they keep
- Every file is opened read-only with FileShare.ReadWrite|Delete, so the running world can keep writing and rotating.
- Nothing reads runs/<run>/saves. A save copy is a separate, memory-heavy job: not done here.
- Output goes where -Out says; keep it out of the project (a scratch folder).
- Windows PowerShell 5.1 with inline C# (Add-Type), plus Git Bash and awk for the trace batch. No other dependency.
  Run a .ps1 with: powershell -NoProfile -ExecutionPolicy Bypass -File <script> <args>

Record layouts used
- <seed>.samples: the Sample struct (core/include/nova2/observe.h) of the build that wrote it. Gate 3's build (open-2):
  51 u64 a row. Gate 5's build (open-3): 138 values a row, i.e. 9 death causes, gate 4's power and heat, and gate 5's
  calls, overruns, injections and failures; region_ratio (value 21) is a double. samples.ps1 tells them apart.
- <seed>.genomes: 64-byte rows. +0 hash, +8 parent hash, +16 lineage origin, +24 first tick kept, +40 offset into
  <seed>.genome_bytes, +48 length, +52 group, +56 place, +60 generation.
  A lineage origin is (founding place + 1) << 40 | serial; the scripts print it as "place+1.serial".
- <seed>.windows: 40-byte rows. u32 place, u64 window start, u32 starters, died, births, deaths, flags.

The scripts, in the order a rundown uses them
1. samples.ps1 -From <tick> -Step <ticks> [-Layout auto|gate3|gate5]
   Per window: programs, threads, births, faithful share, thread starts, deaths by cause, kept genomes, colonies,
   network, cold starts and boots. Start here. The row layout is found from the file (its first rows' ticks), or forced
   with -Layout. Under gate5 the deaths show nine causes, the last three being heat, oom and watchdog; the other
   columns are the same measures as gate3's, except the network part: under gate5 it prints window totals (sent,
   delivered, pings answered, full, stranded, cut). Its "full" counts dropped PING requests and echoes as well as data,
   while "sent" counts data only, so full is not a share of sent there. All 138 positions follow observe.h's Sample.
2. places.ps1 -Out inhabited.txt [-MinKept 2] [-Places N]
   Which places have ever kept a genome ("place 1/0"). In open-2 the never-inhabited half is the strict places.
   The place count is world.places from the run's origin.cfg (the folder above -Dir), so open-3 lists 100 places and
   open-2 200; -Places overrides it. It stops if a kept genome's place falls outside the count.
3. windows.ps1 -Inhabited inhabited.txt -Periods 'lo-hi,lo-hi,lo-'
   Births per tick by period, in inhabited and never-inhabited places, and the places that changed most.
4. gscan.ps1 -Out gscan.txt -From <tick> -Bin <ticks> -Slices 'A,B,C' -NewSince <tick>
   One pass over the genebank: per bin kept genomes, lineages, new lineages, lengths, max generation; the top
   lineages in three slices; which lineage leads each place in slice A against slice C; new lineages that spread.
   It takes about 10 s for 100 million rows.
5. founders.ps1 -Out founders.txt -Since <tick>  and  contain.ps1 -Out contain.txt -Since <tick> -RefFrom <tick>
   Is a new lineage new code? founders: each new lineage's first kept genome, exact matches to older genomes of
   other lineages. contain: the share of its 3-byte runs found in the best single older genome of another lineage
   (a 1-in-RefEvery sample, so counts are lower bounds), with a random-bytes control.
6. firstbyte.ps1 -Out firstbyte.txt -Bin <ticks>
   Share of kept genomes whose first byte (signed) equals their length, and where that byte lands in the call table
   (open-2: slot s mod 24; 3,17,19,21,23 SPAWN; 0,16,18,20,22 ALLOC).
7. anc.ps1 -Action pick -Out picks.txt -N 600 -Since <tick> [-Until <tick>] [-Seed n]
   A random sample of kept genomes, with their bytes, for tracing. -Action chain walks parents instead.
8. bash batch.sh picks.txt out.txt [run_dir]
   Traces each pick alone for 400 instructions (lax place, 1024-byte pages) and writes one line per genome.
   Split the picks and run three at once to go faster (600 in three parts took under a minute).
   Lines whose bytes are not written yet (the newest rows) are skipped, so expect a few less than picked.
   Then: awk -v lo=<tick> -v hi=<tick> -f sum.awk out.txt   (copy alone, first byte = length, own-thread spawns,
   relay / loop / handler loop / slide / no copy, and the calls made from inside the genome).
9. Lookups: rows.ps1 -Rows r1,r2 (print rows with bytes), lins.ps1 -Ids a.b,c.d (one lineage's totals),
   place.ps1 -Place p -Out f (one place's kept genomes over time, and its windows), strict.ps1 -Out f -Inhabited f
   (kept genomes in never-inhabited places, and lineages founded there), gaps.ps1 -Inhabited f (per bin: inhabited
   places with no new kept genome, and home-founded vs colonist leaders).

To read one genome by hand: runs/<run>/nova2.exe trace <hex> runs/<run>/origin.cfg --steps 400 [--strict] [--page N]
and count only calls made at offsets inside the genome.

Limits worth saying in a report
- Traces are lone runs: no neighbours, no real memory around the program, 400 steps, one kind of place, and each kept
  genome counts once rather than by how many programs carry it.
- observe.forget is on and group ids are reused, so groups are not identities; lineages are.
- A genome is kept once a carrier has a grandchild (observe.keep_generations 3), so short-lived lines never show.
- The hijack figure in the samples is a heuristic (code changed and now matches another lineage's genome).
