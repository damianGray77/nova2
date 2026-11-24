cmake_minimum_required(VERSION 3.21)

project(nova2
    VERSION 0.1.0
    DESCRIPTION "Nova 2: self-replication, evolution and ecology from static (see design.md)"
    LANGUAGES CXX)

# --- global language settings ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Predictable output location so the verify loop can find the binaries.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The code rules (design.md 2): no exceptions and no RTTI, enforced by the
# compiler rather than by review. CMake's MSVC default adds /EHsc, so it is
# removed before /EHs-c- goes in; types.h also refuses to compile if either
# is switched back on.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/EHs-c- /GR- /W4 /permissive-)
    # fopen, strtod and the like are used deliberately (the thin C runtime,
    # design.md 2); silence MSVC's "use the _s variant" noise.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0)
endif()

# core/ is the dependency-free engine and its observers: no STL, no
# third-party code (design.md 2). app/ is the `nova2` command line around it.
add_subdirectory(core)
add_subdirectory(app)

option(NOVA2_BUILD_TESTS "Build the Nova 2 unit tests" ON)
if(NOVA2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
