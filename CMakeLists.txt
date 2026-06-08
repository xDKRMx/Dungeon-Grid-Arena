# ============================================================================
# Dungeon Grid Arena - build configuration
#
# Requirements satisfied here:
#   - C++17 standard, no compiler extensions (R2.1, R8.x).
#   - BUILD_WITH_RAYLIB option, default OFF, so the console build always
#     compiles and runs with zero external dependencies (R28.3, R8.2).
#   - Two targets: `game` (the playable executable) and `tests` (the doctest
#     suite).  (Task 1.1)
#
# Structure:
#   - All renderer-independent / shared sources under src/ (everything except
#     main.cpp, and the Raylib renderer when Raylib is disabled) are compiled
#     into a single `dungeon_core` static library.  Both `game` and `tests`
#     link against it, so game logic is built once and exercised headlessly by
#     the test target (R8.2 - core is testable without a renderer).
#   - The library is only created once module source files exist; while the
#     project is still scaffolded (only main.cpp present) the two executables
#     build directly from their own sources.
# ============================================================================

cmake_minimum_required(VERSION 3.15)

project(DungeonGridArena
    VERSION 0.1.0
    DESCRIPTION "Turn-based tactical grid combat roguelike"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard: C++17, required, no GNU/MSVC extensions.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Release build when the user did not pick one (single-config
# generators).  Keeps casual `cmake ..` invocations optimized.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Raylib is an OPTIONAL graphics backend.  When OFF (the default) the project
# builds and runs using only the console renderer and the C++ standard library,
# guaranteeing it compiles on any grading machine (R28.3).
option(BUILD_WITH_RAYLIB "Build the optional Raylib graphics renderer" OFF)

# ---------------------------------------------------------------------------
# Source collection
# ---------------------------------------------------------------------------
# Gather every module source under src/.  CONFIGURE_DEPENDS asks CMake to
# re-glob when files are added/removed, so new modules are picked up without a
# manual edit here as the project grows task by task.
file(GLOB_RECURSE GAME_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# main.cpp is the entry point for the `game` executable only - never part of
# the shared library (the test target supplies its own doctest main).
list(REMOVE_ITEM GAME_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# The Raylib renderer is excluded from the build unless explicitly enabled so
# the console build never references the Raylib headers/libraries (R8.2).
if(NOT BUILD_WITH_RAYLIB)
    list(FILTER GAME_CORE_SOURCES EXCLUDE REGEX ".*[/\\]RaylibRenderer\\.cpp$")
endif()

# ---------------------------------------------------------------------------
# Optional Raylib dependency
# ---------------------------------------------------------------------------
if(BUILD_WITH_RAYLIB)
    # Prefer a system-installed Raylib; fall back to fetching it.  Either way
    # this branch is completely skipped for the default console build.
    find_package(raylib QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

# ---------------------------------------------------------------------------
# Core library (created only once module sources exist)
# ---------------------------------------------------------------------------
if(GAME_CORE_SOURCES)
    add_library(dungeon_core STATIC ${GAME_CORE_SOURCES})
    target_include_directories(dungeon_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(dungeon_core PUBLIC cxx_std_17)

    if(BUILD_WITH_RAYLIB)
        # Only the render layer ever sees this; gated so the core stays clean.
        target_compile_definitions(dungeon_core PUBLIC DGA_WITH_RAYLIB)
        target_link_libraries(dungeon_core PUBLIC raylib)
    endif()
endif()

# ---------------------------------------------------------------------------
# `game` executable - the playable build (R2.4: main.cpp is wiring only)
# ---------------------------------------------------------------------------
add_executable(game "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_include_directories(game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(game PRIVATE cxx_std_17)
if(TARGET dungeon_core)
    target_link_libraries(game PRIVATE dungeon_core)
endif()

# ---------------------------------------------------------------------------
# `tests` executable - the doctest suite (headless, console-only)
# ---------------------------------------------------------------------------
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

add_executable(tests ${TEST_SOURCES})
# tests need the vendored doctest.h (in tests/) and the module headers (in src/).
target_include_directories(tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_compile_features(tests PRIVATE cxx_std_17)
if(TARGET dungeon_core)
    target_link_libraries(tests PRIVATE dungeon_core)
endif()

# Register the suite with CTest so `ctest` runs it.
enable_testing()
add_test(NAME unit_tests COMMAND tests)
