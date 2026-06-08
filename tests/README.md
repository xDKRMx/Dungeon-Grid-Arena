# Dungeon Grid Arena — Test Cases README

## 1. Purpose

This test suite verifies the correctness of the world / pathfinding / map-generation
modules that drive every gameplay decision in *Dungeon Grid Arena*. The tests are
deliberately **property-based**: instead of asserting one fixed expected output for
one fixed input, each test exercises the system on a *generated stream* of randomised
inputs (different map sizes, different RNG seeds, different start / goal pairs) and
checks an **invariant** that must hold for every input. The result is a single
`test_runner.exe` execution that drives the production code through hundreds of
thousands of distinct scenarios.

The functions under test are:

| File                               | Module under test                      | Property verified                                                                                                                                       |
| ---------------------------------- | -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `test_grid.cpp`                    | `core/Grid<T>` template                | Bounds-safe access, default-construction, value persistence after `set()`, dimensions are preserved.                                                    |
| `test_line_of_sight.cpp`           | `world/LineOfSight` (Bresenham)        | Symmetry of the cell list, the line always starts at `from` and ends at `to`, no diagonal "tunneling" through walls.                                   |
| `test_pathfinder.cpp`              | `world/Pathfinder` (BFS)               | Returned path is connected, every step is orthogonal, the path is the shortest possible, unreachable goals correctly return an empty path.              |
| `test_mapgen_invariant.cpp`        | `world/MapGenerator` (drunkard's walk) | The walkable region is **fully connected** (one flood-fill from any floor reaches every floor), reaches the configured floor-density threshold.         |
| `test_spawn_placement.cpp`         | `world/MapGenerator::pickSpawns`       | Every chosen spawn is on a floor tile, no two spawns coincide, the player spawn is distinct from every enemy spawn.                                     |
| `test_main.cpp`                    | doctest entry point                    | (No assertions of its own — defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` so the test runner executable has a single `main`.)                            |

The combat / ability / save-load / shop / upgrade systems are **not** unit-tested
here because they are integration-level features exercised by full play-throughs
(see the *Demo video* for end-to-end coverage). The chosen modules are the ones
whose correctness is hardest to eyeball during gameplay, which is why they receive
formal property tests.

## 2. Environment

| Item              | Required version / value                                                                  |
| ----------------- | ----------------------------------------------------------------------------------------- |
| Compiler          | **g++ 9 or newer** (any C++17-conforming compiler works; tested with MinGW-w64 g++ 13.2). |
| Language standard | **C++17** (`-std=c++17`).                                                                 |
| Compiler flags    | `-Wall -Wextra` for warning-clean builds.                                                 |
| Operating system  | Windows 10 / 11. The test code itself is platform-independent and also compiles on Linux. |
| Test framework    | **doctest 2.4.11**, vendored as a single header in `tests/doctest.h`. No installation needed. |
| External libs     | **None**. The test runner does NOT link against raylib — only the headless logic layers. |
| Disk space        | < 5 MB build output (`test_runner.exe` is roughly 1.1 MB).                                |

The tests are completely deterministic: given the same compiler and standard
library, the same seeds reproduce the same scenarios on every machine.

## 3. File structure

```
tests/
├── README.md                    ← this file
├── doctest.h                    ← single-header test framework (vendored, 2.4.11)
├── test_main.cpp                ← doctest entry point (defines main())
├── test_grid.cpp                ← Grid<T> template invariants
├── test_line_of_sight.cpp       ← Bresenham cell list & symmetry properties
├── test_pathfinder.cpp          ← BFS shortest-path invariants
├── test_mapgen_invariant.cpp    ← Generated map full-connectivity invariant
└── test_spawn_placement.cpp     ← Player + enemy spawn placement invariants
```

* **Input files**: NONE. Every test generates its own input — random map sizes,
  random RNG seeds, random start / goal pairs — through a deterministic seeded
  `dga::Rng`. This is intentional: the suite tests *invariants*, not *fixtures*,
  so committing reference inputs to disk would only constrain coverage.
* **Expected output**: a single `[doctest] Status: SUCCESS!` line on stdout
  alongside the assertion totals. There are no per-test expected-output files —
  the assertion comparisons happen inside the binary at run time.
* **Test code**: all assertions are written with the `CHECK(...)` and
  `REQUIRE(...)` macros from `doctest.h`.
  * `CHECK` continues after a failure so the runner reports as many failures as
    possible per test case (useful when an invariant first breaks).
  * `REQUIRE` aborts the surrounding test case immediately on failure (used for
    pre-conditions that, if violated, would corrupt every later assertion in
    that test).
* **Production source under test**: linked from `../src/` (every module the
  tests touch — `core`, `world`, `entities` headers — is recompiled into the
  test runner so the suite verifies *exactly* the same code the game ships).

## 4. How to run

The test runner is a small standalone executable. Build it once, then run it as
many times as you like.

### 4.1  Build the test runner

From the repository root, on Windows (cmd or PowerShell):

```
g++ -std=c++17 -Wall -Wextra -I src -I tests ^
    tests/test_main.cpp tests/test_grid.cpp tests/test_line_of_sight.cpp ^
    tests/test_pathfinder.cpp tests/test_mapgen_invariant.cpp ^
    tests/test_spawn_placement.cpp ^
    src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/systems/*.cpp src/world/*.cpp ^
    -o test_runner.exe
```

(On Linux / macOS, replace the `^` line continuations with `\` and the
glob form is the same.)

The build is **warning-clean**. Any compiler warning is treated as a real
defect.

### 4.2  Execute the suite

```
.\test_runner.exe
```

The runner prints a header, then a pass / fail summary at the end. Typical
successful output:

```
[doctest] doctest version is "2.4.11"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:      7 |      7 passed | 0 failed | 0 skipped
[doctest] assertions: 540094 | 540094 passed | 0 failed |
[doctest] Status: SUCCESS!
```

A non-zero exit code indicates at least one assertion failed. doctest's `--help`
flag lists every available switch (e.g. running a single test case by name).

### 4.3  Useful one-liners (optional)

* Run only one test case:
  `.\test_runner.exe --test-case="*Pathfinder*"`
* Verbose output (every assertion printed even on success):
  `.\test_runner.exe -s`
* List the test cases without running them:
  `.\test_runner.exe --list-test-cases`

## 5. Test classification

The 7 test cases collectively cover the three classifications required by the
project specification: **normal-case**, **edge-case**, and **invalid-input** behaviour.

### 5.1  Normal-case coverage

These assertions exercise the modules with inputs that fall well inside their
specified domain — the bread-and-butter scenarios that occur during a regular
play-through.

* `test_grid.cpp / Grid stores and retrieves values`: write distinct ints into
  every cell of a `Grid<int>`, read them back, confirm exact match.
* `test_line_of_sight.cpp / Bresenham line traversal`: line from `(0,0)` to
  `(10,7)` includes both endpoints, every step is orthogonal or diagonal, and
  the cell count matches `max(|dx|,|dy|) + 1`.
* `test_pathfinder.cpp / Pathfinder returns shortest connected path`: on an
  open arena, the BFS path between two random floor tiles equals the
  Chebyshev distance (no shorter is possible on an orthogonal grid).
* `test_mapgen_invariant.cpp / Generated map is fully connected`: across 100
  random seeds the flood-fill from one floor tile reaches every other floor
  tile (the central design invariant of `MapGenerator`).
* `test_spawn_placement.cpp / Player and enemy spawns are distinct floors`:
  every drawn spawn lands on a Floor tile and no two spawns share a position.

### 5.2  Edge-case coverage

These assertions deliberately push each module toward its boundary conditions
to catch off-by-one and clamping errors.

* `Grid` zero-area case: a `Grid<int>(0, 0)` is still constructible; every
  bounded access is safely refused.
* `LineOfSight` degenerate line: `lineCells(p, p)` returns exactly one cell
  (a length-zero "ray" is a single dot, not an empty list).
* `LineOfSight` symmetry: for every random `(from, to)` the cell list is the
  reverse of `lineCells(to, from)` — the projectile path is direction-
  independent.
* `Pathfinder` start equals goal: returns a single-cell path of length 1.
* `Pathfinder` adjacent-but-blocked goal: a wall directly between origin and
  goal forces a detour, never produces a phantom diagonal step.
* `MapGenerator` smallest legal map: a `(min_w, min_h)` arena still satisfies
  the connectivity invariant.

### 5.3  Invalid-input coverage

These assertions confirm each module *fails safely* on malformed input rather
than crashing, indexing out of bounds, or returning corrupted data.

* `Grid` out-of-bounds access: `Grid::at(x, y)` for negative or oversize
  coordinates returns the default value of `T` and never reads past its
  internal buffer (asserted under doctest's bounds-safe accessors).
* `Pathfinder` unreachable goal: a goal placed inside a sealed wall pocket
  produces an empty path and no infinite loop.
* `MapGenerator` zero-floor map (defensive): if the random generator
  somehow produces a wall-only candidate the connectivity check still returns
  a deterministic answer (vacuously true, no flood-fill needed).
* `MapGenerator::pickSpawns` over-request: asking for more spawns than the
  map contains free cells returns the maximal sub-set without duplicating
  positions or hanging the algorithm.

### 5.4  Property-based test design

A few of the cases — `Pathfinder shortest path`, `LineOfSight symmetry`, and
`MapGenerator connectivity` — are scaled out into property runs of 100–10 000
randomly seeded scenarios per case. This is what produces the
**540 094 total assertions** seen in the runner output even though there are
only 7 test cases. The assertions are independent (each iteration uses a fresh
seed) so a failure on iteration 4 712 still gives a fully reproducible
counterexample (the seed and inputs are printed on failure thanks to doctest's
`INFO(...)` blocks scattered through the tests).

## 6. Result judgment

A test run **passes** when **all** of the following are true:

* The `g++ … -o test_runner.exe` build returns exit code 0 (no warnings, no
  errors).
* `.\test_runner.exe` returns exit code **0**.
* The final line printed by the runner is **`[doctest] Status: SUCCESS!`**.
* The penultimate line shows `0 failed` for both **test cases** and
  **assertions** counters.

A test run **fails** when any of the following is true:

* The build emits any warning or error (`-Wall -Wextra` is enforced).
* `test_runner.exe` returns a non-zero exit code.
* The runner prints `[doctest] Status: FAILURE!`.
* Any individual assertion is reported failing in the doctest output (the
  failure block names the file, line number, the failed expression, and the
  values of every variable referenced by the assertion).

Re-running with `-s` (verbose mode) makes every assertion print on success too,
which is useful when investigating a borderline failure ("did this case really
run?").

### Reference run on the submission machine

```
[doctest] doctest version is "2.4.11"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:      7 |      7 passed | 0 failed | 0 skipped
[doctest] assertions: 540094 | 540094 passed | 0 failed |
[doctest] Status: SUCCESS!
```

This output is the canonical **PASS** result for the submitted code base.
