# Dungeon Grid Arena

A turn-based, grid-based dungeon roguelike written in C++17. The
**graphical Raylib build** is the canonical experience for grading and
demos. A minimal **ASCII console build** is also provided as a
smoke-test renderer behind the same `IRenderer` interface, mainly to
prove that the renderer layer is loosely coupled — it lacks the
graphical effects, audio, and Death Dungeon visual mode that define
the full game.

This README is the short overview. For full instructions see
**[`USER_MANUAL.md`](USER_MANUAL.md)**.

---

## Quick start (prebuilt Windows binaries)

```
.\game_raylib.exe   (graphical version — canonical experience)
.\game.exe          (ASCII smoke-test renderer; gameplay only, no audio
                     or visual effects)
```

Both binaries sit in the project root. `game_raylib.exe` ships with
`libs/raylib/lib/raylib.dll` and the OGG music tracks under `assets/`.

## Cloning the repository

```bat
git clone https://github.com/xDKRMx/Dungeon-Grid-Arena.git
cd Dungeon-Grid-Arena
```

Everything needed to build is already inside the clone. **You do not
need to install Raylib separately** — the library headers and the
prebuilt MinGW-w64 binaries (`libraylib.a`, `libraylibdll.a`,
`raylib.dll`) are vendored under [`libs/raylib/`](libs/raylib/) so
the project compiles and runs on a fresh Windows machine without any
package manager step.

## Prerequisites

You only need a C++17 compiler. The reference toolchain on the
submission machine is:

| Tool                  | Tested version | Notes                                        |
| --------------------- | -------------- | -------------------------------------------- |
| `g++` (MinGW-w64)     | 13.2.0         | Anything 11+ with C++17 will compile cleanly.|
| `cmake` *(optional)*  | 3.15+          | Only if you prefer the CMake build path.     |

No additional libraries to install. Raylib (graphical build),
doctest 2.4.11 (test runner), and the OGG music are all vendored
inside this repository.

## Building from source

C++17, no platform-specific code in the game core. The graphical build
links against the vendored Raylib 5.5 in `libs/raylib/`; the
smoke-test ASCII build needs nothing beyond `g++`.

### Path A — `g++` shell commands (the path I used day-to-day)

```bat
:: Raylib (graphical) build — canonical
g++ -std=c++17 -Wall -Wextra -DDGA_WITH_RAYLIB ^
    -I src -I libs/raylib/include ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/render/RaylibRenderer.cpp ^
    src/systems/*.cpp src/world/*.cpp ^
    -L libs/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm ^
    -o game_raylib.exe

:: ASCII console smoke-test build (no external deps)
g++ -std=c++17 -Wall -Wextra -I src ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/systems/*.cpp src/world/*.cpp ^
    -o game.exe

:: Test runner (property-based suite)
g++ -std=c++17 -Wall -Wextra -I src -I tests ^
    tests/test_main.cpp tests/test_grid.cpp tests/test_line_of_sight.cpp ^
    tests/test_pathfinder.cpp tests/test_mapgen_invariant.cpp ^
    tests/test_spawn_placement.cpp ^
    src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/systems/*.cpp src/world/*.cpp ^
    -o test_runner.exe
```

After `game_raylib.exe` is built, copy the vendored DLL next to it so
Windows can find it at runtime:

```bat
copy libs\raylib\lib\raylib.dll .\
```

### Path B — CMake (portable / IDE-friendly alternative)

A `CMakeLists.txt` is also provided. Set `BUILD_WITH_RAYLIB=ON` to
enable the graphical build; leave it OFF (the default) for the ASCII
smoke-test build.

```bat
:: Graphical build
cmake -S . -B build -DBUILD_WITH_RAYLIB=ON
cmake --build build --config Release

:: ASCII smoke-test build (no external deps)
cmake -S . -B build_ascii
cmake --build build_ascii --config Release

:: Run the test suite via CTest
ctest --test-dir build --output-on-failure
```

Both build paths are warning-clean: any compiler warning is treated as
a real defect and must be fixed before merging.

## Repository layout

```
src/                    layered C++17 source (core < world < entities <
                        items < combat < abilities < systems < io < render)
tests/                  property-based unit-test suite (doctest)
test_cases_evidence/    manual gameplay test screenshots, demo video,
                        and MANUAL_TEST_CASES.md
libs/raylib/            vendored Raylib 5.5 (Windows MinGW prebuilt)
assets/                 streamed OGG music for the Raylib build
data/                   runtime files (highscores, save slot)
docs/                   tasks.md (the per-task implementation log).
USER_MANUAL.md          end-user documentation
PROJECT_FINAL_REPORT.md reference Final Report (Proposal + Requirements
                        + Design)
SUMMARY_REPORT.md       reference Summary Report
README.md               this file
```

## Highlights

* **Inheritance + polymorphism** in three subsystems: `Entity` →
  `Player` / `Enemy*`, `Item` → `HealthPotion`/`Weapon`/`Ammo`/`Armor`/
  `Treasure`, `Ability` → `Dash`/`Nova`/`Shield`/`Blink`.
* **Templates**: `Grid<T>` and `RingBuffer<T>`.
* **Hand-rolled linked-list event log** (`EventLog`) feeding the HUD.
* **File I/O**: tagged-text **transactional save / load** with full
  staging-then-commit semantics, plus a top-10 leaderboard.
* **Renderer abstraction** (`IRenderer`): the canonical Raylib
  graphical renderer plus a minimal ASCII console renderer used as a
  smoke-test for the abstraction. The game core never knows which
  renderer is active.
* **Property-based tests**: 7 cases / 540 094 assertions covering grid
  template invariants, Bresenham symmetry, BFS shortest-path, map
  connectivity, and spawn placement.
* **Dynamic difficulty**: per-wave HP scaling, distance-weighted enemy
  hit chance, dramatic "Death Dungeon" mode-shift after 9.5 s
  (19 s on boss waves) — Raylib build only.
* **Procedural audio**: every SFX synthesised in memory at startup;
  optional streaming OGG music tracks per wave type — Raylib build only.

## Tests

```
.\test_runner.exe
```

```
[doctest] doctest version is "2.4.11"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:      7 |      7 passed | 0 failed | 0 skipped
[doctest] assertions: 540094 | 540094 passed | 0 failed |
[doctest] Status: SUCCESS!
```

See **[`tests/README.md`](tests/README.md)** for the full test methodology
(purpose, environment, file structure, run instructions, classifications,
result judgment).
