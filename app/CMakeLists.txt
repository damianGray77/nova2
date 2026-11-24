# The viewer's drawing half (build-plan-gate1.md step 12): the canvas
# (Direct2D, DirectWrite and WIC, approved for the UI in design.md 2) and the
# views. A library so the tests can render it headless.
add_library(nova2_viewer STATIC
    ui/canvas.cpp
    ui/figures.cpp
    ui/power_view.cpp
    ui/programs.cpp
    ui/settings.cpp
    ui/viewer.cpp
    ui/wear_view.cpp
    ui/widgets.cpp)
target_link_libraries(nova2_viewer PUBLIC nova2::core d2d1 dwrite windowscodecs ole32)
target_include_directories(nova2_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Test worlds the command line and the tests share (R7): 9.4's memory-taker
# (build-plan-gate2.md step 9) and gate 3's traffic (build-plan-gate3.md
# step 5).
add_library(nova2_worlds STATIC
    taker_world.cpp
    traffic_world.cpp)
target_link_libraries(nova2_worlds PUBLIC nova2::core)
target_include_directories(nova2_worlds PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The `nova2` command line: run, resume, origin, bench, taker, worlds, trace and view.
add_executable(nova2 main.cpp
    origin.cpp   # the origin test (build-plan-gate1.md step 11)
    watch_report.cpp # gate 5's watching in its report (build-plan-gate5.md step 11)
    bench.cpp    # the first benchmark
    trace.cpp    # one genome alone, instruction by instruction
    taker.cpp    # 9.4's test world: a memory-taker in every place (gate 2's step 9)
    worlds.cpp   # gate 3's test worlds at full size (step 7)
    power_worlds.cpp # gate 4's test worlds at full size (step 10)
    wear_worlds.cpp  # gate 5's test worlds at full size (step 14)
    arrivals.cpp # gate 3's reports of arrivals and messages (steps 7 and 8)
    genomes.cpp  # a seed's kept genomes read whole from its records
    view_cmd.cpp) # the viewer's window (step 12)
target_link_libraries(nova2 PRIVATE nova2::core nova2_viewer nova2_worlds user32)
