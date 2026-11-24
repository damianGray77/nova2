# The engine and its observers (build-plan-gate1.md 4.1): a static library
# with no STL and no third-party code. Sources are added step by step as each
# mechanism is built (build-plan-gate1.md 5).
add_library(nova2_core STATIC
    src/fatal.cpp
    src/str.cpp
    src/io.cpp
    src/config.cpp
    # The settings registry (design.md 2): its table is src/keys.inc.
    src/keys.cpp
    # A place's memory (design.md 6.3, 9.3; build-plan-gate1.md step 3).
    src/memory.cpp
    # The interpreter (design.md 8, appendix A; build-plan-gate1.md step 4).
    src/cpu.cpp
    src/disasm.cpp
    # The OS: programs, threads, the calls (design.md 9; build-plan-gate1.md step 5).
    src/os.cpp
    # The scheduler: rounds of rotating quanta (design.md 9.5; build-plan-gate1.md step 6).
    src/sched.cpp
    # The reset region's idle runs (design.md 10.2-10.3; build-plan-gate1.md step 7).
    src/region.cpp
    # The noise floors (design.md 7.5; build-plan-gate1.md step 8).
    src/noise.cpp
    # Power on the device side: ranges, speed against voltage, draw (design.md 4.4, 7.1; build-plan-gate2.md step 2).
    src/power.cpp
    # The supply: a source for each place, its day, swings and drop-outs (design.md 4.2, 4.3; build-plan-gate2.md step 3).
    src/supply.cpp
    # Gate 4's sources on the map: the catalogue, landing by area, lives, the day's sweep, swells (design.md 4.2,
    # 4.3; build-plan-gate4.md step 3).
    src/sources.cpp
    src/solve.cpp
    src/heat.cpp
    src/blocks.cpp
    src/visit.cpp
    src/links.cpp
    src/events.cpp
    src/current.cpp
    # Gate 5: wear, each device's life and worn threshold (design.md 7.4; build-plan-gate5.md step 2).
    src/wear.cpp
    # The map: land, places, data links and bridges, the fields (design.md 6.1, 6.2, 7.3; build-plan-gate3.md step 2).
    src/field.cpp
    src/map.cpp
    # The network: routes, link-end queues, store and forward (design.md 11.3-11.5; build-plan-gate3.md step 3).
    src/net.cpp
    # Places, the world, threads and snapshots (build-plan-gate1.md step 9).
    src/pool.cpp
    src/snapshot.cpp
    src/counters.cpp
    src/serialize.cpp
    src/place.cpp
    src/world.cpp
    # The observers (design.md 13; build-plan-gate1.md step 10).
    src/compress.cpp
    src/genome_code.cpp
    src/observe.cpp
    src/watch.cpp
    # The observers' records written as they finish (design.md 13.1; build-plan-observe.md step 2).
    src/record.cpp
)
add_library(nova2::core ALIAS nova2_core)

target_include_directories(nova2_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(nova2_core PUBLIC cxx_std_20)
