# Dungeon Grid Arena

A turn-based, grid-based dungeon roguelike written in C++17. The codebase
ships with **two interchangeable renderers** — a portable ASCII console
renderer and a Raylib graphical renderer — sharing a single game core.

This README is the short overview. For full instructions see
**[`USER_MANUAL.md`](USER_MANUAL.md)**; for archived design / requirements
notes see [`docs/archive/`](docs/archive/).

---

## Quick start (prebuilt Windows binaries)

```
.\game.exe          (console / ASCII version)
.\game_raylib.exe   (graphical version, recommended)
```

Both binaries sit in the project root. `game_raylib.exe` ships with
`libs/raylib/lib/raylib.dll` and the OGG music tracks under `assets/`.

## Building from source

C++17, no platform-specific code. The console build needs nothing beyond
`g++`. The graphical build links against the vendored Raylib 5.5 in
`libs/raylib/`.

```bat
:: Console build (no external deps)
g++ -std=c++17 -Wall -Wextra -I src ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/systems/*.cpp src/world/*.cpp ^
    -o game.exe

:: Raylib (graphical) build
g++ -std=c++17 -Wall -Wextra -DDGA_WITH_RAYLIB ^
    -I src -I libs/raylib/include ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/render/RaylibRenderer.cpp ^
    src/systems/*.cpp src/world/*.cpp ^
    -L libs/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm ^
    -o game_raylib.exe

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

All three commands are warning-clean: any compiler warning is treated as a
real defect and must be fixed before merging.

A `CMakeLists.txt` is also provided as an alternative build path.

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
docs/                   tasks.md and an archive/ subfolder with the
                        original specs and report outlines
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
* **Renderer abstraction**: `IRenderer` interface with two concrete
  implementations; the game core never knows which renderer is active.
* **Property-based tests**: 7 cases / 540 094 assertions covering grid
  template invariants, Bresenham symmetry, BFS shortest-path, map
  connectivity, and spawn placement.
* **Dynamic difficulty**: per-wave HP scaling, distance-weighted enemy
  hit chance, dramatic "Death Dungeon" mode-shift after 9.5 s
  (19 s on boss waves).
* **Procedural audio**: every SFX synthesised in memory at startup;
  optional streaming OGG music tracks per wave type.

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
