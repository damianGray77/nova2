# Hand-rolled test executable: no third-party framework, no STL (from Nova 1).
# test_main.cpp owns main(); with no argument it runs every suite, with a suite
# name it runs just that one, so each suite gets its own CTest entry below.
# Add a suite by writing test_<thing>.cpp, registering it in test_main.cpp,
# listing the source here, and adding its name to NOVA2_SUITES.
add_executable(nova2_tests
    test_main.cpp
    test_smoke.cpp
    test_containers.cpp
    test_str.cpp
    test_rng.cpp
    test_config.cpp
    test_keys.cpp
    test_memory.cpp
    test_cpu.cpp
    test_os.cpp
    test_sched.cpp
    test_region.cpp
    test_noise.cpp
    test_pool.cpp
    test_world.cpp
    test_observe.cpp
    test_power.cpp
    test_supply.cpp
    test_cutout.cpp
    test_fade.cpp
    test_fails.cpp
    test_boot.cpp
    test_fuzz.cpp
    test_determinism.cpp
    test_view.cpp
    test_taker.cpp
    test_spread.cpp
    test_map.cpp
    test_net.cpp
    test_send.cpp
    test_record.cpp
    test_sources.cpp
    test_solve.cpp
    test_heat.cpp
    test_lowv.cpp
    test_gate4.cpp
    test_gate5.cpp
    test_wear.cpp
    test_blocks.cpp
    test_visit.cpp
    test_links.cpp
    test_respawn.cpp
    test_events.cpp
    test_hammer.cpp
    test_flaky.cpp
    test_straits.cpp
    test_current.cpp
    test_guards.cpp
    test_watch.cpp
    test_determinism5.cpp
)
target_link_libraries(nova2_tests PRIVATE nova2::core nova2_viewer nova2_worlds)
target_compile_definitions(nova2_tests PRIVATE NOVA2_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    NOVA2_EXE="$<TARGET_FILE:nova2>")
add_dependencies(nova2_tests nova2)

# One CTest entry per suite; verify.ps1 runs them all via ctest. Tests that
# write files do so in the build tree (the CTest working directory).
set(NOVA2_SUITES
    smoke containers str rng config keys memory cpu os sched region noise pool world observe power supply cutout fade fails boot fuzz determinism view taker spread map net send
    record sources solve heat lowv gate4 gate5 wear blocks visit links respawn events hammer flaky straits current
    guards watch determinism5)
foreach(suite IN LISTS NOVA2_SUITES)
    add_test(NAME ${suite} COMMAND nova2_tests ${suite})
    set_tests_properties(${suite} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# The code-rules guard (see tools/check_rules.cmake).
add_test(NAME rules
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tools/check_rules.cmake)
