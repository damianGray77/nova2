# Nova 2 — Viewer build plan

> **Status (2026-11-17):** agreed. Damian agreed the five choices in 2 as
> recommended, with one addition: every control is a button on screen
> that can be clicked, not a key to remember (2, choice 6). Steps 1
> to 5 are done (their results are under them in 5); step 6, the setup
> page, waits for his word.
> Steps 7 to 9 were added on 2026-11-18, on his word, and are done: a
> window that can be put on a run already going; a seed's world saved
> mid-run to be loaded again; and the origin test itself carried on
> from a save, with its family tree and counts as they stood.
> Step 10 was built on 2026-11-19, overnight on his approval: the window
> no longer waits on a seed that is saving, and a run with no end saves
> by the clock. Step 11 followed that day: a big place's memory map is
> drawn as one picture, after he saw it lag on the open-ended run.
> Damian asked for the plan while watching gate 3's run
> (`runs/gate3-1`), naming what he missed against Nova 1's viewer: the
> programs that are running and their code, with live figures; zooming
> the map; a settings window; and switching between seeds when more than
> one is running. All of it is in `design.md` 13.4 already, as views that
> "come when they're needed", so this plan changes no design.
> Numbers in brackets, like (13.4), are sections of `design.md`, which
> stays the authority.

## 1. What it delivers

- **The programs of a place, and their code** (13.4's genome browser):
  a list of the inspected place's programs, and for the one picked, its
  code disassembled with every thread's position marked as it runs.
- **Live figures:** what the observers already count, shown for the
  place and the program picked, not only for the world.
- **A map that zooms and pans.**
- **A view on a run:** the window attached to the origin test's seeds as
  they run, with a way to switch between the seeds running at the time.
- **A settings window:** the view's switches as controls on screen, and
  the world's settings to read.
- **A setup page** (Nova 1's): a world's settings chosen in the window
  before it starts. The largest piece, and the one that can wait (2).

A view only looks. None of this changes what a world computes: a world
watched gives the same hash as one that isn't (6).

## 2. Choices for Damian

Each has Claude's recommendation first. **Agreed 2026-11-17, each as
recommended,** with choice 6 added by Damian.

1. **How the window attaches to a run.**
   - **Recommended: inside the run.** `nova2 origin` with a new setting,
     `origin.view = true`, opens the window in the run's own process. A
     seed's world is read only between its ticks, so what the window
     shows is a whole tick's state, and the run's files are the same
     with the window open or shut.
   - The other way is for the run to save whole worlds now and then, for
     a separate viewer to open. That is 120 MB a save for a seed of 48
     places, and never live.
2. **What the window may do to a run it is attached to.**
   - **Recommended: nothing.** It watches. Space, N and the speed keys
     do nothing there, since a gate's run isn't one to pause by a slip
     of a key. The seed watched runs a little slower while it is
     watched (7); the others don't.
   - Or: Space pauses every seed at once, shown plainly in the window.
3. **The settings window.**
   - **Recommended: two parts now, the third later.** Now: the view's
     switches (colourings, which rows of charts, what the lists sort by)
     as controls on screen beside their keys; and the world's settings,
     every one with its label and help from the registry (`keys.inc`),
     to read. Later, as its own step: the setup page, where settings are
     changed and a run started from the window (step 6).
   - Changing a running world's settings is not offered. A world keeps
     the settings it was built with, and a rule for changing them
     mid-run would be a design change.
4. **What counts as a program's code.**
   - **Recommended: the observers' genome** (13.2), the bytes the
     program's lineage is known by, which is what the genome dumps and
     the grouping already use. The page view of memory stays for
     everything else a program holds.
5. **Where it sits among the gates.**
   - **Recommended: before gate 4's build,** steps 1 to 5 in order,
     while gate 3's readings are being agreed. Step 6 after, or when the
     lack of it is felt.
6. **Buttons, not keys** (Damian, 2026-11-17: "Rather than keyboard
   shortcuts, I'd rather have dedicated buttons that can be clicked").
   - Everything the viewer can be told to do has a button on screen,
     the controls it has today among them: pause and run, one tick,
     slower and faster, the memory's colours, the map's colours, map or
     memory, and which row of charts. A button that chooses among
     several says which is chosen.
   - Nothing new is reached by a key alone. The keys the viewer has
     today keep working beside their buttons, since they cost nothing
     to keep; the line of key hints at the top goes, and the buttons
     take its place.

## 3. The views

- **Programs** (step 2). In the inspector's column, a list of the
  place's programs that scrolls: each one's lineage (by its colour on
  the memory strips), threads, bytes held, age, and how it was born
  (from a thread, from static, an arrival). It sorts by any of them. A
  click picks a program; its allocations light up on the memory strips.
- **Code** (step 2). For the program picked: its genome disassembled in
  the 22 instructions, an address a line, with a mark on each line a
  thread is at and the count of threads there. Below it the threads:
  state, core, instruction pointer and registers. They move as the
  world runs. A second click on a line's thread follows that thread.
- **Family** (step 3). For the program picked: its parent and children
  (each a click away, while they live), its lineage's members here and
  in the world, its genome's group, and how many places hold the
  lineage.
- **Figures** (step 3). For the place: births, deaths by cause,
  faithful copies, the commonest genomes and lineages, each with its
  share, and the turnover window's figures. For the world: the same,
  and the stages each seed has reached when it is a run's seed. All of
  it is what the observers and the origin test hold already; no new
  counters go into the engine.
- **The buttons** (step 1; 2, choice 6). A bar across the top, where
  the key hints are now: pause or run, one tick, slower, faster with
  the speed between them, and the choosers for the memory's colours,
  the map's colours, map or memory, and the row of charts.
- **The map** (step 1). The wheel zooms about the pointer, a drag pans,
  and buttons on the map zoom in, zoom out and show the whole map
  again. Place labels and link detail appear as there is room.
- **Seeds** (step 4). A dropdown in the top bar when the window is on
  a run (Damian, 2026-11-17, on seeing a tab a seed: "seed selection
  should probably be done via dropdown rather than having all those tabs
  at the top"). Its button says the seed shown; its list has a row a
  seed, with its number, its progress, its best stage and its births. A
  click on a running seed's row switches.
  Seeds that have finished stay in the list with their result, but
  have no world left to look into.
- **Settings** (step 5). A window over the view (a button opens it):
  the view's switches at the top, the world's settings below,
  grouped as the registry groups them, each with its help.

## 4. The code: what changes

| Part | Files | What |
|---|---|---|
| Input and widgets | `app/ui/viewer.h`, `viewer.cpp`, `view_cmd.cpp`, a new `app/ui/widgets.h/.cpp` | The wheel and drags; a list that scrolls; buttons, tabs and a window over the view |
| The map | `app/ui/viewer.cpp` | Zoom and pan |
| Programs, code, family, figures | a new `app/ui/programs.cpp`, `viewer.cpp`; `core/src/disasm.cpp` | The lists and panes; the disassembler gains a form a view can lay out line by line |
| What the views read | `core/include/nova2/observe.h`, `os.h` | Read-only accessors where a view needs one (a place's programs in order, a program's genome); no new state |
| The run's window | `app/origin.cpp`, `app/view_cmd.cpp`, `core/src/keys.inc` | `origin.view`; each seed's world and observer offered to the window between ticks |
| Settings | a new `app/ui/settings.cpp`; `core/include/nova2/keys.h` | The registry walked for the page |
| Tests | `tests/test_view.cpp`, new stored images | 6 |

The engine's tick is not touched. `app/ui` keeps to the code rules
(`design.md` 2): no `std::`, no lambdas, Direct2D and DirectWrite only, no new
dependency.

## 5. The order of work

**Step 1. Input, widgets and the map's zoom.**

- The wheel and drags reach the views; a scrolling list, buttons and
  tabs exist as widgets with tests of their own.
- The map zooms and pans.
- Done when: made-up input drives each widget in the tests, and
  headless renders of the map at two zooms match stored images.
- **Results (2026-11-17): done.** The 30 suites pass (the view suite
  with 370 checks), and the release build is made.
  - The widgets are `app/ui/widgets.h/.cpp`: a button, a row of buttons
    laid rightwards or leftwards, and a list that scrolls by the wheel
    and by its bar. Tabs are buttons with one marked as chosen. A test
    finds a button by its label and clicks where it was drawn
    (`ButtonLog`), so the tests go through the same hit test a mouse
    does.
  - The bar across the top holds run, pause, one tick, slower, the
    speed, faster and, with a map, which row of charts shows. The key
    hints are gone; the keys still work.
  - One change from 3's wording: the choosers for map or memory and for
    their colourings sit in the middle panel's head, on the panel they
    change, not in the top bar, which had no room for them at the
    window's default size.
  - The map zooms from 1 (the whole map) to 32, by the wheel about the
    pointer and by `-`, `+` and `whole map` on the map (Home too), and a
    drag pans it. A click picks the nearest place when the button is let
    go, so a drag picks nothing. Places show their numbers once their
    squares are 16 pixels across. Link detail waits for a later step.
  - Stored images: `view_gate2.png` and `view_gate3.png` were redrawn
    once for the new bar and head and looked at; `view_gate3_zoom.png`
    is new, the gate 3 moment zoomed four times on the inspected place.
  - Not checked by a test: the real window's wheel and mouse capture
    (`view_cmd.cpp`), which only a person at the window exercises.

**Step 2. Programs and code.**

- The program list, the code pane and the thread list (3).
- Done when: on a test world with a hand-placed replicator (R7) the
  list shows it and its copies, the code pane's lines match
  `nova2 trace`'s disassembly of the same genome, the marks sit where
  the threads' instruction pointers are, and a headless render matches
  a stored image.
- **Results (2026-11-17): done.** The 30 suites pass (the view suite
  with 761 checks), and the release build is made.
  - The inspector has two pages, chosen by buttons in its head: the
    place, as before, and its programs (`app/ui/programs.h/.cpp`).
  - The list shows each living program's lineage (its colour and its
    first program), id, threads, bytes held, age and how it was born.
    The buttons over its columns sort it; a second click turns the
    order round. A click picks a program, and the pages of memory it
    holds are framed in white on the memory view.
  - Ids are written as place, dot, number ("20.279": the 279th program
    born in place 20), since the uid itself is 13 digits.
  - The code pane shows the genome the program was born with, an
    instruction a line with its offset, the text the same as
    `nova2 trace`'s disassembler writes (it is the same function). A
    line a thread stands at is lit and says how many; a line whose
    bytes in memory have changed since the birth is marked with a star.
  - The thread list shows id, state, core and where each stands (an
    offset in the code, or an address outside it). A click follows a
    thread: its line stays in view in the code and its registers show
    under the list. A click on a lit line of code follows a thread
    there.
  - A program that dies while picked keeps its code, greyed, and says
    it has died; `let go` clears it. A program picked in another place
    stays picked, with a button back to its place.
  - A program the observer has no record of (born before the observer
    began to watch, as in a world opened from a snapshot) shows the
    bytes of its oldest allocation as they lie now, and says so.
  - One addition to the observer, not the engine: `ProgramRec` keeps
    where the genome began in its allocation (`wlo`), so the view knows
    the genome's address. The observer is not part of a world's state
    or its hash.
  - The test world is gate 2's with the replicator in every lax region:
    10 programs listed, all the replicator's, 10 threads marked on
    their code; a program picked died about 370 ticks on and kept its
    14 bytes. Stored images: `view_programs.png` is new; the three
    others were redrawn once for the inspector's new head and looked
    at.
  - Not checked by a test: a click on a lit line of code (the tests
    follow a thread from the thread list).

**Step 3. Family and figures.**

- The family pane and the place's and world's figures (3).
- Done when: each figure shown equals the observers' own number in a
  test, and a headless render matches a stored image.
- **Results (2026-11-17): done.** The 30 suites pass (the view suite
  with 1,977 checks), and the release build is made. The code is
  `app/ui/figures.h/.cpp`.
  - Family: the program picked has two panes, chosen by buttons, its
    code and its family. The family shows its parent (a button that
    picks it, in whatever place it lives, while it lives), its children
    alive in the place (buttons, the six oldest), its lineage (members
    alive in the world and here, the places it lives in, births and
    faithful births), its generation, how it was born (faithful, a near
    copy or unlike its parent), and its genome's length, group and
    births in the world.
  - Figures: a third page of the inspector, for the place or for the
    world. The place's: programs, threads, births, deaths, the living
    born faithful and as copies, deaths by cause, the five commonest
    lineages and genomes among the living with their shares, and its
    turnover record with the window under way. The world's: births,
    faithful births, deaths and cold starts, births and deaths by cause,
    lineages alive, the five lineages with the most alive, colonies, and
    the five genomes born most often.
  - Two limits, both from showing only what is held already. A
    program's children are the ones alive in its own place: the
    observer keeps no list of children, and one born elsewhere from an
    arrival is not shown. The world's commonest genomes are by births,
    not by how many live now, which only the places' own programs say.
  - The world's figures are worked out every frame while the observer
    holds fewer than 200,000 lineages and genomes, and every fifteenth
    frame after that, since they walk both lists.
  - The stages each seed has reached wait for step 4, where a window
    knows it is on a run.
  - The test world is gate 2's with the replicator: at tick 450 the
    figures of a place of 136 programs and the world's 1,124 births
    matched the observers'; 93 programs had a living parent and 80 had
    children. That place held one lineage and one genome, so the test of
    the five commonest is strongest on the world's lists (several
    lineages). Stored images: `view_family.png`, `view_figures.png` and
    `view_figures_world.png` are new; the four others were redrawn for
    the inspector's third tab and looked at.

**Step 4. The window on a run, and its seeds.**

- `origin.view`; the seeds' dropdown; switching (2, choices 1 and 2).
- Done when: a short origin test with the window open and one with it
  shut write the same samples, windows, genomes and report (timings
  aside), on 1 and 24 host threads; switching seeds mid-run in a test
  changes neither; and the run ends cleanly with the window open or
  closed by hand.
- **Results (2026-11-17): done.** The 30 suites pass (the view suite
  with 2,037 checks), and the release build is made. The code is
  `app/run_view.h`, the window's thread in `app/view_cmd.cpp`, the
  seeds' dropdown in `app/ui/viewer.cpp`, and five lines in the seed's
  loop in `app/origin.cpp`.
  - `origin.view = true` (off by default) opens the window in the run's
    own process, on a thread of its own, for `nova2 origin` and `nova2
    busy`. The seeds start once the window is open. If it can't open,
    the run says so and goes on without it.
  - The seeds are chosen from a dropdown in the top bar. It was first
    built as a strip of tabs under the bar, a tab a seed, and changed
    the same day on Damian's word: twenty tabs took two rows of the
    window. Its button says the seed shown ("seed 6  20%  stage 1") and
    opens a list under it with a row a seed: its number, how far
    through it is (or "done", or "waiting"), its best stage and its
    births. A running seed's row shows that seed and shuts the list;
    the others have no world to show and do nothing. A click anywhere
    else, Escape, or the button again shuts the list, and while it is
    open nothing under it answers the pointer. The list scrolls if the
    seeds outgrow the window. When the seed shown ends, the window moves
    to the first one still running. The world's figures page lists the
    tick the seed reached each stage at.
  - The window only watches: in place of the run, pause, step and speed
    buttons it says "watching a run" and the seed's ticks a second, and
    the keys for them do nothing. Everything else is as in `nova2
    view`.
  - How a seed is read: the window asks the seed it shows to hold; the
    seed's thread sees that between two ticks and waits; the window
    draws one frame from the world and the observer and lets it go. A
    seed nobody watches pays one read of a flag a tick. Frames come 20
    a second at most, and fewer if a frame's hold is more than a tenth
    of the time between frames (7).
  - Measured on gate 3's world, three young seeds side by side in the
    release build, the tests' window drawing flat out and changing
    seed, place and page every frame: a seed was held 2.6 ms a frame,
    waiting for the tick to end included. At 20 frames a second that
    is about a twentieth of the watched seed's time, and nothing of
    the others'. A full, old world has more to draw; the tenth is the
    cap either way.
  - The tests: `nova2 busy` on a small world of three seeds, 12,000
    ticks, run five times. With the window open (hidden, flat out,
    moving between the three seeds, the places and the pages every
    frame) it wrote the same samples, turnover windows and genomes,
    byte for byte, and the same report with its timings masked, as with
    it shut, on 1 and on 24 host threads; and the same again with the
    window closed after 5 frames, the run going on to its end. The
    tests' window is `NOVA2_VIEW_TEST` in the environment; nothing else
    reads it.
  - By hand, the real window: three seeds of gate 3's own config for
    150,000 ticks wrote the same files with the window open as shut,
    and the run ended cleanly with the window open. Closing the real
    window with the mouse is the one thing no test does; the tests'
    close takes the same path from the moment the window says it is
    closed.
  - Found on the way: the canvas has to be gone before COM is shut
    down on the window's thread, or the process dies at the window's
    end. Fixed, and the tests above would catch it again.
  - A run already going can't be watched this way: the window is part
    of the run's process and is asked for when the run starts. So
    `runs/gate3-1` is watched as before, by a second `nova2 view` of
    one of its seeds.
  - Stored image: `view_run.png` is new (the dropdown open on 26
    seeds). The seven others did not change. With the dropdown the view
    suite has 3,592 checks.

**Step 5. The settings window.**

- The view's switches and the world's settings to read (2, choice 3).
- Done when: every setting in the registry appears once with its value,
  label and help; a switch on the page does what its key does; and a
  headless render matches a stored image.
- **Results (2026-11-17): done.** The 30 suites pass (the view suite
  with 3,557 checks), and the release build is made. The code is
  `app/ui/settings.h/.cpp`.
  - A "settings" button at the right of the top bar opens the page over
    everything under the bar. Its own
    "close" button, the top bar's button again, or Escape shuts it;
    Escape shuts the viewer only when the page is not open.
  - The view's switches are at the top, each a caption and a row of
    buttons with the one in force lit: what the middle shows, the map's
    colours, the memory's colours, the charts' row, the panel on the
    right, the figures of the place or the world, the programs' order
    and a program's pane. With no map, the map's switches are not
    there.
  - Below is every setting of the registry but the nine test-only ones:
    185 settings in 16 groups, in the registry's order. Each has its
    label, its value, its name as a config file writes it, and its help
    under it. A value that is not the default is blue, with the default
    beside it. A setting whose switch is off is greyed, with what it
    needs. The list scrolls by the wheel or its bar.
  - The values are what the world read. The registry already had a hook
    that reports each value a reader gives back, kept for the setup
    page; the page's list is filled from it while the world is built. A
    setting nothing read (another command's, say) shows the config's
    text for it or its default, and says "not read by this world".
  - In the window on a run the settings are a seed's: before the seeds
    start, one world is built from the run's config to be asked, and
    let go. The world's seed reads "1 to 20, one a seed". The noting
    ends before any seed starts, because the seeds read their settings
    side by side and the hook is one for the process.
  - They are to read, as agreed (2, choice 3). Changing them is step 6.
  - The tests: every registry entry is on the page once, with a label
    and help; values read, given, both and neither come out right; a
    setting that needs the landscape map is greyed in a world with no
    map; the help of every setting survives being cut into lines whole;
    scrolled from top to end, every setting's line is drawn; each
    switch's button does what its key does (V, C, M and G), and the
    rest do what their buttons elsewhere do; and the world's hash is
    unchanged. The run's window in step 4's tests now opens the page on
    every seventh frame, and the run's files are still the same.
  - Stored images: `view_settings.png` is new. The eight others were
    redrawn for the top bar's new button and looked at; below the bar
    nothing moved.

**Step 6. The setup page (later, on Damian's word).**

- Settings changed in the window before a world is built, checked as
  the config file's are, and a world or a run started from it.
- Its own plan when it comes: it needs text entry, a choice of where a
  run's files go, and Nova 1's rules for what is greyed when.

**Step 7. A window on a run already going.**

- Damian, 2026-11-18, on finding that the window he had watched all
  night was a second copy of seed 1 opened beside the run: "Is there a
  way to (programatically) hook into a running run and attach a viewer
  to it instead ... so that even if we don't start a run with a viewer
  we can still utilize one at any point in the run?" Of the ways put to
  him he chose the window inside the run, opened on a signal, over a
  viewer in a process of its own reading the run's memory ("sounds more
  robust, but way more work").
- Every `nova2 origin` run carries what step 4's window needs (a slot a
  seed, and the settings it read), with `origin.view` on or off, and a
  thread that waits. `nova2 watch [pid]` signals it and the run opens
  its window; closing the window sends the thread back to waiting, so
  the window can be opened and closed any number of times. `origin.view`
  now means only that the window opens at the start.
- A run says how at its start, on the progress stream: its process's
  number and the command.
- Unwatched, a seed pays what step 4's seeds paid: one read a tick.
- Tests: a run with the window opened from outside, closed, and opened
  again writes the files of a run never watched.
- **Done 2026-11-18.** 30 suites green.
  - The run's window thread now lives from the run's start to its end
    (`run_view_thread` in `app/view_cmd.cpp`): it opens the window at
    the start if `origin.view` is on, and otherwise waits on named
    Windows events, which `nova2 watch` sets. A window closed sends it
    back to waiting. A second `nova2 watch` while the window is open
    brings it to the front.
  - Two names a signal: the run's own, by its process's number (`nova2
    watch 12345`), and one any run answers to (`nova2 watch`). With
    more than one run going the bare command reaches one of them, no
    saying which, so the number is the way then. A run prints its number
    and both commands as the first line of its progress stream.
  - `nova2 watch` waits up to two seconds for a run that has only just
    started, then says no run is listening and ends with 1.
  - Every run builds one world before its seeds start and lets it go,
    to note the settings a world reads for the settings page; before,
    only a run with `origin.view` on did.
  - Test (`test_run_attach`): a run of three seeds started with no
    window was asked for one by `nova2 watch <pid>`, then every 20 ms
    to its end; each window closed itself after 5 frames. It opened 8
    windows, and its samples, turnover windows, genomes and report were
    those of the run never watched. (Its seeds wait for the first window
    before they start, the tests' way of making sure there is one.)
  - By hand, on gate 3's settings (two seeds, 2 host threads each): the
    window opened on `nova2 watch` 4 s into the run, was closed, and
    opened again on a second `nova2 watch`, with the charts' history
    whole each time, since the observers are the run's and not the
    window's.

**Step 8. A seed's world saved mid-run.**

- Damian, 2026-11-18: "The ability to snapshot a run at any point so
  that we can reload from that saved point in a new run. I believe we
  had this ability in Nova 1". Nova 1 had `output.snapshot_every`,
  `nova resume` and `nova view <snapshot>`. Nova 2 has had the last two
  since gate 1, and `nova2 run --save` at a run's end; what it lacks is
  a save in the middle, and any save from the origin test.
- `nova2 save [pid]` signals a run, and every seed running then saves
  its whole world between two ticks, as
  `<folder>/seed-S-tick-T.world.snap`. The window on a run has a button
  that does it for the seed shown. `origin.save_every` saves every so
  many ticks without being asked (0, the default: never).
- The folder is `origin.saves`.
- A save's world opens in `nova2 view` (and draws to a PNG with no
  window) and carries on under `nova2 resume`, tick for tick as the run
  did. There the observers start afresh; step 9 added a second file
  beside the world's, from which the test itself goes on.
- Tests: a run that saves writes the files of one that doesn't; a save
  at tick T carried on to tick U has the hash of the same run's save at
  tick U.
- **Done 2026-11-18.** 30 suites green; the view suite has 3,646 checks.
  - A save is asked through the slot the window uses: a second bit of
    `want`, which the seed's thread takes between two ticks and answers
    by saving its world itself (`save_world` in `app/origin.cpp`). So a
    save is a whole tick's state, as a frame is.
  - Settings: `origin.saves`, the folder (empty: the folder the run was
    started in), and `origin.save_every`, ticks between saves (0, the
    default: only when asked). 187 settings now.
  - The window on a run has a "save world" button in the top bar, lit
    until the seed shown has saved; the world's figures page then says
    "its world last saved at tick N". A window on a world of its own
    has no such button yet.
  - `nova2 resume` now takes a save whose settings include the origin
    test's: before, it stopped at "unknown setting 'origin.seeds'".
    Only `run.*` settings can still be changed on a resume.
  - A save of gate 3's world is 128 MB a seed (48 places, each with its
    1 MB disk), so `nova2 save` on twenty seeds writes 2.6 GB, and
    `origin.save_every` wants a long interval.
  - Tests (`test_run_saves`, `test_run_attach`): a run saving its three
    seeds' worlds every 4,000 ticks wrote the files of one that doesn't;
    each save of tick 4,000, carried on 4,000 ticks by `nova2 resume`,
    had the hash of that seed's save of tick 8,000, on 1 host thread
    and on 24; and `nova2 save <pid>` on a run going got a save from
    each of its three seeds, each reading back as a whole world of that
    seed and tick.
  - By hand, on gate 3's settings: `nova2 save` wrote both seeds' worlds
    near tick 82,000; `nova2 view <save> --png` drew one with no window,
    and `nova2 resume` carried it on.
  - Stored images: `view_run.png` was redrawn for the button, and
    `view_settings.png` for the two new settings (the list is longer, so
    its scroll bar's thumb is shorter). Both were looked at.
  - Not tried by a person: the "save world" button under a real mouse.

**Step 9. A test carried on from a save.**

- For the origin test itself to go on from a save (after a power cut,
  or to give a seed more ticks) the observers' memory has to be saved
  beside the world: the family tree, the stages reached, the turnover
  windows (13.1: "Observer memory is saved beside each snapshot,
  outside the hash, so the family tree survives a resume"; 13.2 left it
  "until a run needs it"). Then `nova2 origin` takes a folder of saves
  and carries each seed on, and its report covers the whole of each
  seed's life.
- **Done 2026-11-18,** on Damian's word ("Yes, build it"). 30 suites
  green; the view suite has 3,700 checks.
  - **A save is now two files.** `seed-S-tick-T.world.snap` is the
    world, as at step 8. `seed-S-tick-T.test.snap`, beside it, holds
    what the world's save leaves out and the test needs:
    - the counters kept for reports, every place's and the network's
      (`core/src/counters.cpp`). A snapshot leaves them out by design,
      since they are not part of the world or its hash (2), but the
      samples and the report are made of them;
    - the engine's side of two measures: each reset region's table of
      paths tried, and what is remembered of an idle run under way for
      the counts;
    - the observers' whole memory (`Observer::save` and `load`): the
      programs alive, the family tree, the colonies, the genebank with
      its groups and their index, the samples, the turnover windows;
    - the test's own record of the seed: the stages reached and when,
      each lineage's and colony's hold clock, its running time.
  - **`origin.resume = <folder>`** carries each seed on from its newest
    save there. A seed with no save starts at tick 0, and the run says
    so. A file takes its name only once it is whole, so a run that ends
    part way through a save leaves nothing to trip on.
  - **The settings must be the saves'.** A seed is refused, with the
    setting named, if any differs but those that shape the run and its
    files: `run.*`, the ticks and the budget, the folders and
    intervals, the window, the pass marks. So a seed can be given more
    ticks, and not a different world or different stage criteria.
  - **A seed that saves by the clock saves where it stops too,** so a
    run that reached its ticks can be carried on with more.
  - **`origin.saves_kept`** (2): as a seed takes a save by the clock,
    its older clocked saves go. A save asked for (`nova2 save`, the
    button) is never removed. 189 settings now.
  - **The report says it was carried on,** in a line above the seeds'
    table, naming each seed's tick. Each seed's time and speed are of
    its whole life; "wall time" is this run's alone.
  - **Sizes.** The test's file is streamed to disk (`FileOut`,
    `core/include/nova2/io.h`), not built in memory first. On gate 3's
    settings it was 26 to 30 MB a seed at tick 230,000, beside a world
    of 128 MB. It grows with the genebank: late in a long seed (seed 8
    of `runs/gate3-1` ended with 15.7 million genomes in 1.9 million
    groups) it will be a few gigabytes a seed.
  - **Its records go as they lie in memory,** padding and all, so a
    test's file is read back only by the build that wrote it; a table
    whose record size has changed is refused. The world's file stays
    field by field as before.
  - **One count runs ahead:** the routes' "columns worked out". A world
    carried on works its routes out again as they are asked for (2),
    so that count, which is of work done, is higher than a run never
    stopped would show. Nothing else in the report differs but the
    timings.
  - Tests (`test_run_resume`): a run of three seeds stopped at tick
    5,000 and carried on to 12,000 wrote the samples, turnover windows,
    genomes and report (timings and that one count aside) of a run
    never stopped, on 1 host thread a seed and on 24; so did a run
    killed part way, as by a power cut, and carried on from each seed's
    newest save; and a run under another `observe.sample_every` was
    refused, naming it.
  - By hand, on gate 3's settings (two seeds, 48 places, with their
    drop-outs, cold starts and network traffic): stopped at tick 230,000
    and carried on to 400,000, the samples, turnover windows and genomes
    were byte for byte those of a run never stopped, and the report's
    110 lines differed only in the carried-on line, the timings and the
    columns count (70 for 69, and 50 for 49).
  - Not tried: a save late in a long seed (gigabytes); the largest
    tried is 30 MB.

**Step 10. The window never waits on a seed; saves for a run with no end.**

- Built on 2026-11-19, overnight on Damian's approval. He confirmed it
  that day ("Yes, they stand").
- **What he saw:** late in gate 3's second run the whole window locked
  up for seconds at a time, as it does at Save World.
- **The cause:** to draw a frame the window's thread waited for the
  watched seed to come between two ticks, and a seed writing its save
  didn't come for as long as that took: late in that run a seed's test
  save, the observers' whole memory, was 2 to 6 GB. A window that waits
  doesn't answer.
- **The window asks and looks back.** The hold is one word a seed
  (`SeedSlot::hold`: none, asked, held), changed only by interlocked
  steps. The window asks, goes on answering, and draws when the seed
  holds; it lets go of a seed that ended, or when another is picked.
  A frame that hasn't come in half a second is replaced by a line:
  "this seed is saving its world", or "waiting for this seed's tick to
  end". The seed's tab shows it saving.
- **A world's save goes to its file as it is made** (`SnapshotFile`),
  not built whole in memory first: the same bytes and checksum, tested
  at eight odd lengths. A busy world of 200 places of 4 MB saves 8.6 GB
  in about 10 seconds, during which that seed stands still, as before.
- **Saves by the clock** (`origin.save_minutes`), progress lines by the
  clock (`origin.progress_minutes`: programs, lived-in places, lineages
  alive and the largest, the deepest generation alive), and a floor on
  the machine's free memory (`origin.memory_floor`: under it the run
  saves and stops). A run with no end has no tenths of its ticks to
  hang them on.
- **`nova2 check <saves>`** runs the engine's own checks
  (`Place::check`) on every place of a saved world, which no run did
  before, and counts the threads whose stack is no longer their
  program's. All 43 saves of gate 3's second run pass, and the busy
  200-place saves with 2 million threads.
- **Not done: the save split into what changed in the last 30 seconds**
  (Damian's suggestion). Nearly all of a save is state that changes in
  place: memory, the OS's tables, the observers' records. What only
  grows (samples, the genebank, finished lineages) already goes to
  files every 30 seconds. A save of only what changed needs a record of
  what was written since the last one, page by page and table by table;
  that is a design of its own, put to him. Cheaper first steps: write a
  place's tables only as far as they are used (about half the bytes),
  and write the save from a copy while the seed runs on.
- **The window's picture to a file** (`origin.picture`, every
  `origin.picture_minutes`), drawn off screen from the seed as it is
  held, as the stored images are. Reading the window off the screen
  (PrintWindow) gave a white picture, or a frame minutes old, with the
  display asleep: Windows stops putting windows on screen then. The
  open-ended run's updates use it (`runs/open-1/status.ps1`).
- **`nop` reads `NOP`** in a program's code, as the other instructions
  do.

**Step 11. A big place's memory map in one picture.**

- Built on 2026-11-19, the day Damian saw it on the open-ended run
  (`runs/open-1`, 200 places of 4 MB): "larger nodes seem to cause
  significant UI lag on the memory map. It's probably running at around
  .5 fps".
- **The cause:** the map drew a rectangle for every page, and a place of
  4 MB has 65,536 pages of 64 bytes. That took 22 to 43 ms a frame, all
  of it with the watched seed held between two ticks. Step 7's rule then
  waits ten times as long as a seed was held before the next frame, so
  the window costs a run at most a tenth of its time: a frame every
  quarter to half a second on a quiet machine, and slower beside a run
  using every host thread.
- **The change:** the pages are one picture, a pixel a page, drawn
  enlarged to the page's cell (`Canvas::image`); the gaps between cells,
  where a cell is 5 pixels or more, are lines in the panel's colour over
  it. A program's colour is worked out once a frame, not once for each
  page it holds. What is drawn is the same, pixel for pixel: the stored
  images still match.
- **Measured** on a copy of the run's world at tick 8,707,200, since
  removed, with place 101 shown in a window of 1,900 by 1,000, the world
  paused, the run working beside it: the memory map from 22 to 43 ms a
  frame to 1.5 to 1.8 ms, and the whole frame from 24 to 45 ms to about
  3 ms, the same as with the map of places in the middle. Under 5 ms a
  frame, step 7's rule gives its 20 frames a second.
- **Checked** by a new test (`test_big_map`): six places of 1 MB, some
  with pages too small for a gap, every page's cell its colour from
  corner to corner, the gaps and everything past the last page the
  panel's.
- **Not measured:** the window on the run itself, which holds a seed
  among 24 busy host threads; and the charts with a long run's samples,
  which the copy opened without. The inspector's programs page still
  lists and sorts a place's programs every frame: 2 to 3 ms with 2,370
  programs, so about 5 ms a frame with it open.

## 6. Tests that run throughout

- **The 30 suites,** as now, with the views' own added.
- **A view only looks:** a world drawn every tick, with every pane
  open, has the hash of one never drawn. Checked on gate 3's world.
- **Stored images:** each new view rendered headless, pixel for pixel,
  as the existing views are.
- **The old views move once:** the button bar replaces the line of key
  hints, so the stored images of gates 1 to 3 are drawn again at step
  1 and checked by eye against the old ones; below the bar nothing
  moves. After that they match with nothing zoomed or opened.

## 7. Risks

- **A frame's cost on a run.** A watched seed waits while its frame is
  read. The window on a run draws 20 frames a second at most, and what
  a frame reads under the seed's lock is kept to copying what it draws.
  If a frame still costs a watched seed more than a tenth of its speed,
  the window draws less often.
- **Long lists.** A place can hold thousands of threads. Lists draw
  only the rows on screen.
- **A program that dies while picked.** The panes say so and keep its
  last state, greyed, until another is picked.
- **The window closed mid-run.** The run carries on; that is tested
  (step 4).

## 8. Size

About 2,500 lines for steps 1 to 5: 400 for input and widgets, 700 for
programs, code, family and figures, 350 for the run's window, 300 for
settings, 750 of tests. Step 6 is about 800 more. The viewer is 1,700
lines now; Nova 1's was 5,000.
