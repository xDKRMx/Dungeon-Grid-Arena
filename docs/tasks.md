# Implementation Plan: Dungeon Grid Arena

## Overview

This plan converts the C++17 design into incremental, test-driven coding steps built bottom-up along
the module layering (`core` → `world` → `entities`/`items`/`abilities` → `combat` → `systems` → `io`
→ `render` → wiring). Each task builds on previous ones and ends by wiring its component into the
running system, so there is no orphaned code. The renderer-independent core is implemented and tested
headlessly via the deterministic seeded `Rng` and the `doctest` harness.

Property-based tests (minimum 100 randomized iterations each) implement the 28 Correctness Properties
from the design; each is its own optional sub-task, placed next to the code it validates. Unit/example
tests cover error conditions and state transitions. Test sub-tasks are marked optional with `*`.

**Language/build:** C++17, CMake, `doctest` single-header framework. `BUILD_WITH_RAYLIB` defaults OFF
so the console build always compiles and runs.

## Tasks

- [x] 1. Set up project structure, build system, and core utilities
  - [x] 1.1 Create CMake project, directory layout, and doctest harness
    - Add `CMakeLists.txt` (C++17, `BUILD_WITH_RAYLIB` option default OFF, a `game` target and a `tests` target)
    - Create the `src/` module folders (`core`, `world`, `entities`, `items`, `abilities`, `combat`, `systems`, `io`, `render`) and `tests/`, `data/`, `assets/`
    - Vendor `tests/doctest.h` and add `tests/test_main.cpp` with the doctest entry point
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 8.1, 8.6_

  - [x] 1.2 Implement core value types, enums, and config
    - `core/Vec2.h` (header-only): `x`, `y`, `operator+/-`, `operator==`, `manhattan`, `chebyshev`
    - `core/Direction.h`: `Direction` enum + `toOffset` + `allDirections` (4-directional, OD-2)
    - `core/Enums.h`: `TileType`, `EntityKind`, `ItemKind`, `AbilityKind`, `GameStateId`
    - `core/Config.h/.cpp`: tunable constants exposed through `const` getters (no magic numbers)
    - _Requirements: 1.1, 1.2, 1.3, 2.1, 8.5_

  - [x] 1.3 Implement deterministic seedable RNG
    - `core/Rng.h/.cpp`: `std::mt19937` seeded from a stored seed; `rangeInt`, `choice` template, `seed()`
    - Guarantees reproducible maps/spawns/drafts for deterministic tests and save/load
    - _Requirements: 8.3, 26.4_

  - [x] 1.4 Implement the `Grid<T>` and `RingBuffer<T>` templates
    - `core/Grid.h`: header-only generic 2D container (`width`, `height`, row-major `std::vector<T>`), `inBounds`, checked `at`, `width`/`height`
    - `core/RingBuffer.h`: header-only bounded buffer template
    - _Requirements: 5.1, 7.1, 7.2_

- [x] 2. Implement the world module: map, generation, pathfinding, line of sight
  - [x] 2.1 Implement `Tile` and `GridMap` (arrays showcase)
    - `world/Tile.h/.cpp`: `TileType`, `isWalkable`
    - `world/GridMap.h/.cpp`: `Grid<Tile> tiles`, `inBounds`, `isWalkable`, `typeAt`, `setType`, `floorTiles`; all access bounds-guarded
    - _Requirements: 5.1, 5.2, 5.3, 9.1, 9.2_

  - [x]* 2.2 Write property test for bounds-safe grid access and round-trip
    - **Property 3: Grid access is bounds-safe and round-trips**
    - **Validates: Requirements 5.1, 5.2, 5.3**

  - [x] 2.3 Implement `MapGenerator` with connectivity guarantee
    - `world/MapGenerator.h/.cpp`: `generate` (carve floors/walls, border walls), `isFullyConnected` (flood fill), `repairConnectivity`, `pickSpawns`
    - Guarantee full floor connectivity and enough floors for player + spawns before play begins
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.6_

  - [x]* 2.4 Write property test for generated map invariant
    - **Property 1: Generated map invariant**
    - **Validates: Requirements 9.1, 9.2, 9.3, 9.5**

  - [x]* 2.5 Write property test for spawn placement validity
    - **Property 2: Spawn placement is valid and distinct**
    - **Validates: Requirements 9.4, 9.6**

  - [x] 2.6 Implement `Pathfinder` (BFS shortest path)
    - `world/Pathfinder.h/.cpp`: `findPath` using a local `Grid<int>` distance buffer (second `Grid<T>` instantiation) treating walls/occupied tiles as non-walkable; `nextStepToward`
    - _Requirements: 12.1, 12.4, 12.5, 7.2_

  - [x]* 2.7 Write property test for BFS path correctness and minimality
    - **Property 4: BFS path correctness and minimality**
    - **Validates: Requirements 12.1, 12.4, 12.5**

  - [x] 2.8 Implement `LineOfSight` (Bresenham)
    - `world/LineOfSight.h/.cpp`: `hasLineOfSight` (wall on intermediate cell blocks), `lineCells` (reused by projectiles)
    - _Requirements: 13.1, 13.2, 13.3, 13.4_

  - [x]* 2.9 Write property test for line-of-sight correctness
    - **Property 5: Line-of-sight correctness**
    - **Validates: Requirements 13.1, 13.2, 13.3, 13.4**

- [x] 3. Checkpoint - Ensure all world-layer tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 4. Implement entities, items, abilities, and the state container
  - [x] 4.1 Implement the `Entity` abstract base class
    - `entities/Entity.h/.cpp`: protected `position`/`health`/`maxHealth`/`attack`/`armor`/`kind`; virtual destructor; `isAlive`; `takeDamage`/`heal` clamped to `[0, maxHealth]`; accessors; pure virtual `glyph`
    - _Requirements: 4.1, 4.7, 15.1, 15.3, 1.2_

  - [x] 4.2 Implement `Player` and `Inventory`
    - `items/Inventory.h/.cpp`: non-owning `std::vector<Item*>`, `add`, `remove`, `contents`
    - `entities/Player.h/.cpp`: `ammo`, `chargeMeter`, equipped weapon, owned abilities and inventory; `equip`, `addAmmo`, `spendAmmo`; `glyph` returns `@`
    - _Requirements: 4.1, 15.1, 16.2, 20.1, 20.2_

  - [x] 4.3 Implement the `Item` hierarchy with virtual effects
    - `items/Item.h/.cpp` abstract base (virtual `applyTo(Player&)`, `isConsumable`, virtual dtor, `glyph`)
    - `HealthPotion`, `Weapon`, `AmmoItem`, `Armor`, `Treasure` each override `applyTo`
    - _Requirements: 4.3, 4.7, 19.1, 19.2, 19.3, 19.4, 19.5, 19.6_

  - [ ]* 4.4 Write property test for item effects via virtual dispatch
    - **Property 12: Item effects apply correctly via virtual dispatch**
    - **Validates: Requirements 19.1, 19.2, 19.3, 19.4, 19.5, 19.6**

  - [ ]* 4.5 Write property test for inventory consume round-trip
    - **Property 13: Inventory consume round-trip**
    - **Validates: Requirements 20.3**

  - [x] 4.6 Implement the `Ability` hierarchy (cooldown mechanics)
    - `abilities/Ability.h/.cpp` abstract base: `cooldown`/`cooldownDuration`, virtual `activate`, `isReady`, `tick`, `putOnCooldown`, virtual dtor
    - `DashAbility`, `NovaAbility`, `ShieldAbility`, `BlinkAbility` class skeletons with cooldown wiring (effect bodies wired in 8.1)
    - _Requirements: 4.4, 4.7, 21.1, 21.2, 21.4_

  - [ ]* 4.7 Write property test for ability cooldown invariant
    - **Property 14: Ability cooldown invariant**
    - **Validates: Requirements 21.2, 21.4**

  - [x] 4.8 Implement the `GameState` authoritative run state
    - `systems/GameState.h/.cpp`: owns `GridMap`, `Player`, `vector<unique_ptr<Enemy>>`, `vector<unique_ptr<Item>>`, `waveNumber`/`score`/`turnCount`/`enemiesKilled`, `Rng`; const read-only queries for renderers/tests
    - _Requirements: 8.3, 1.1_

- [ ] 5. Implement the enemy hierarchy and AI (inheritance + polymorphism)
  - [x] 5.1 Implement `Enemy` base and chess-pattern subtypes
    - `entities/Enemy.h/.cpp` base: `range`, `contactDamage`, virtual `canAttackPlayer`, `glyph`
    - `MeleeEnemy`, `RookEnemy`, `BishopEnemy`, `QueenEnemy` override `canAttackPlayer` as (geometry) AND in-range AND clear LOS
    - _Requirements: 4.2, 4.6, 4.7, 14.1, 14.2, 14.3, 14.4, 14.6_

  - [ ]* 5.2 Write property test for enemy firing eligibility
    - **Property 6: Enemy firing eligibility matches the chess pattern**
    - **Validates: Requirements 14.1, 14.2, 14.3, 14.4, 14.6**

  - [x] 5.3 Implement enemy `decideAction`, `FastEnemy`, and `BossEnemy`
    - Add `decideAction` (attack-if-eligible else step along BFS path; hold if no path) to `Enemy`
    - `FastEnemy` moves 1-2 tiles per step; `BossEnemy` adds `phase`, threshold-based `onPhaseChange`, and `summonMinions`
    - _Requirements: 12.2, 12.3, 12.4, 14.5, 18.2, 18.3, 18.4_

  - [ ]* 5.4 Write property test for enemy movement when not attacking
    - **Property 7: Enemy movement when not attacking**
    - **Validates: Requirements 12.2, 12.3, 14.5**

- [ ] 6. Checkpoint - Ensure all entity/item/ability tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 7. Implement combat and the event log
  - [x] 7.1 Implement `EventLog` (linked-list showcase)
    - `systems/EventLog.h/.cpp`: hand-written singly-linked list with tail-append / head-evict, `capacity`, `append`, `recent`
    - _Requirements: 6.1, 6.2, 6.3, 29.2, 29.3_

  - [ ]* 7.2 Write property test for event-log capacity invariant
    - **Property 28: Event log capacity invariant (linked list)**
    - **Validates: Requirements 6.1, 6.2, 29.3**

  - [x] 7.3 Implement `CombatSystem` damage and death resolution
    - `combat/CombatSystem.h/.cpp`: `applyAttack` (`damage = max(0, attack - armor)`, logs to `EventLog`); `resolveDeaths` (remove `health <= 0`, increment kills and Charge_Meter, detect player death → Game Over)
    - _Requirements: 15.2, 15.3, 15.4, 15.5, 10.6, 22.1_

  - [ ]* 7.4 Write property test for the damage formula
    - **Property 8: Damage formula**
    - **Validates: Requirements 15.2**

  - [ ]* 7.5 Write property test for death resolution and kill accounting
    - **Property 9: Death resolution and kill accounting**
    - **Validates: Requirements 15.3, 15.4, 22.1**

  - [x] 7.6 Implement ranged player projectiles and ammo
    - Add `firePlayerProjectile` to `CombatSystem`: travel via `LineOfSight::lineCells`, damage first enemy with clear LOS, decrement ammo, miss on wall/bounds, reject on 0 ammo with an `EventLog` message
    - _Requirements: 16.1, 16.2, 16.3, 16.4_

  - [ ]* 7.7 Write property test for ranged projectile resolution
    - **Property 11: Ranged projectile resolution**
    - **Validates: Requirements 16.1, 16.2, 16.4**

- [ ] 8. Implement gameplay systems: abilities, turns, waves, upgrades, state machine
  - [x] 8.1 Implement `AbilitySystem` and wire ability effects
    - `systems/AbilitySystem.h/.cpp`: `activate` (reject on cooldown / charge-not-full with `EventLog` message), `tickCooldowns`, `addCharge` (clamped)
    - Wire `DashAbility` (bounded floor move), `ShieldAbility` (timed immunity), `BlinkAbility` (teleport to visible empty floor), `NovaAbility` (adjacent AoE + reset charge)
    - _Requirements: 21.2, 21.3, 21.5, 21.6, 21.7, 22.1, 22.2, 22.3_

  - [ ]* 8.2 Write property test for Dash bounds
    - **Property 15: Dash is bounded and wall-respecting**
    - **Validates: Requirements 21.5**

  - [ ]* 8.3 Write property test for Shield immunity
    - **Property 16: Shield grants damage immunity for its duration**
    - **Validates: Requirements 21.6**

  - [ ]* 8.4 Write property test for Blink targeting
    - **Property 17: Blink places the player on a valid target**
    - **Validates: Requirements 21.7**

  - [ ]* 8.5 Write property test for Nova AoE and charge reset
    - **Property 18: Nova damages adjacent enemies and resets charge**
    - **Validates: Requirements 22.2**

  - [x] 8.6 Implement `TurnManager` and player action resolution
    - `systems/TurnManager.h/.cpp`: fixed turn order (player → enemy → death resolution → end-of-turn check → redraw), increment `turnCount` after both phases, deterministic enemy order, end wave when all enemies dead
    - Resolve player action by destination: Floor moves, Wall/out-of-bounds rejected (no turn consumed), enemy tile → melee, item tile → move + pickup into Inventory
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 11.1, 11.2, 11.3, 11.4, 20.1_

  - [ ]* 8.7 Write property test for player move resolution by destination
    - **Property 10: Player move resolution by destination**
    - **Validates: Requirements 11.1, 11.2, 11.3, 11.4, 20.1**

  - [ ]* 8.8 Write property test for the turn-count invariant
    - **Property 24: Turn-count invariant**
    - **Validates: Requirements 10.4**

  - [ ]* 8.9 Write property test for enemy-phase determinism
    - **Property 25: Enemy phase is deterministic under a fixed seed**
    - **Validates: Requirements 10.3**

  - [x] 8.10 Implement `WaveManager` (escalation and boss waves)
    - `systems/WaveManager.h/.cpp`: `startWave` (count/types scale with wave number; spawn `BossEnemy` when `waveNumber % 5 == 0`), `isWaveCleared` (all enemies incl. minions dead), `advance`
    - _Requirements: 17.1, 17.2, 17.3, 17.4, 18.1, 18.5_

  - [ ]* 8.11 Write property test for wave clearance and escalation
    - **Property 26: Wave clearance and monotonic escalation**
    - **Validates: Requirements 17.1, 17.2, 17.3, 17.4**

  - [ ]* 8.12 Write property test for boss spawn cadence and phases
    - **Property 27: Boss spawn cadence and phase monotonicity**
    - **Validates: Requirements 18.1, 18.2, 18.3**

  - [x] 8.13 Implement `UpgradeSystem` (between-wave draft)
    - `systems/UpgradeSystem.h/.cpp`: `draftCards` (3 distinct random cards), `apply` (apply chosen effect, discard rest)
    - _Requirements: 23.1, 23.2, 23.3, 23.4_

  - [ ]* 8.14 Write property test for the upgrade draft
    - **Property 19: Upgrade draft yields three distinct cards**
    - **Validates: Requirements 23.1, 23.2, 23.4**

  - [x] 8.15 Implement `GameStateMachine`
    - `systems/GameStateMachine.h/.cpp`: `transition` guarding legal moves among Main Menu, Playing, Paused, Upgrade Draft, Game Over; halts the turn loop on Paused; menu option models
    - _Requirements: 27.1, 27.2, 27.3, 27.4, 30.1, 30.2, 30.3, 30.4_

- [ ] 9. Implement the I/O module (file persistence)
  - [x] 9.1 Implement `ScoreBoard` (scoring + persistence + explicit sort)
    - `io/ScoreBoard.h/.cpp`: `computeScore` (`wave*100 + kills*10 + treasure*1`), `append`, `loadTop` with an in-house insertion sort (descending); missing file → empty leaderboard; malformed line skipped with an `EventLog` message
    - _Requirements: 3.1, 3.3, 3.4, 24.1, 24.2, 25.1, 25.2, 25.3, 25.4_

  - [ ]* 9.2 Write property test for the scoring formula
    - **Property 20: Scoring formula**
    - **Validates: Requirements 24.1**

  - [ ]* 9.3 Write property test for the explicit high-score sort
    - **Property 21: High-score sort is a descending permutation**
    - **Validates: Requirements 25.2, 25.3**

  - [ ]* 9.4 Write property test for scoreboard persistence round-trip
    - **Property 22: Scoreboard persistence round-trip**
    - **Validates: Requirements 3.1, 25.1**

  - [x] 9.5 Implement `SaveManager` (run snapshot save/load)
    - `io/SaveManager.h/.cpp`: `save` (map, player, enemies, items, ability cooldowns, Charge_Meter, wave, score, turn count, RNG seed → tagged text); `load` (restore equivalent state); missing/malformed file → report via `EventLog`, leave state unchanged (never partial overwrite)
    - _Requirements: 3.2, 3.3, 3.4, 26.1, 26.2, 26.3, 26.4_

  - [ ]* 9.6 Write property test for save/load round-trip equivalence
    - **Property 23: Save/load round-trip equivalence**
    - **Validates: Requirements 3.2, 26.1, 26.2, 26.4**

- [ ] 10. Implement the render module (polymorphism showcase)
  - [x] 10.1 Define the `IRenderer` interface and input/menu models
    - `render/IRenderer.h`: pure-virtual `drawFrame`, `pollInput` (abstract `InputCommand`), `drawMenu` (`MenuModel`), virtual dtor; the core interacts with rendering/input only through this interface
    - _Requirements: 28.1, 28.2, 4.5_

  - [x] 10.2 Implement `ConsoleRenderer` (baseline + test harness)
    - `render/ConsoleRenderer.h/.cpp`: ASCII grid/entities/items/HUD/Event_Log; keystrokes mapped to `InputCommand`; skip a frame when a required component is unavailable; no external dependency
    - _Requirements: 28.3, 28.5, 28.6, 29.1, 29.3, 4.5_

  - [ ] 10.3 Implement `RaylibRenderer` (optional graphics)
    - `render/RaylibRenderer.h/.cpp`: tiles/colors/HP bars/HUD via Raylib, keys mapped to `InputCommand`; compiled only when `BUILD_WITH_RAYLIB=ON`; core untouched
    - _Requirements: 28.4, 28.5, 28.6, 29.1_

- [ ] 11. Wire the application together and verify end to end
  - [x] 11.1 Implement the `Game` orchestrator and `main.cpp`
    - `systems/Game.h/.cpp`: owns `GameState`, systems, `EventLog`, `GameStateMachine`, and a reference to the active `IRenderer`; `run()` drives the state machine and turn loop and requests redraws (orchestration only, no rules)
    - `src/main.cpp`: parse flags, select renderer (Raylib when built, else Console), construct and run `Game`; clean shutdown on Quit
    - _Requirements: 2.4, 8.2, 10.1, 27.1, 30.1, 30.3, 30.4_

  - [ ]* 11.2 Write integration and core-independence smoke tests
    - Console build (`BUILD_WITH_RAYLIB=OFF`) compiles and runs with no external dependency
    - `core`/`world`/`entities`/`systems`/`io` translation units compile without any render header
    - Scripted fixed-seed run through `ConsoleRenderer` reaches Wave 2 with a sane final score
    - _Requirements: 8.2, 28.2, 28.3_

- [x] 12. Final checkpoint - Ensure the full suite passes
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional test sub-tasks (property, unit, integration) and can be skipped for a faster MVP; core implementation tasks are never optional.
- Each property test runs a minimum of 100 randomized iterations driven by the seeded `Rng`, and is tagged with a comment referencing its design property (`// Feature: dungeon-grid-arena, Property N: ...`).
- All 28 Correctness Properties are covered exactly once (P1-P28); structural rubric criteria (classes, multi-file layout, inheritance wiring, renderer abstraction) are verified by compilation and review rather than properties.
- Each property test is implemented in its own test translation unit (registered with the `tests` target), satisfying "one test per property" and letting independent test tasks in the same dependency-graph wave run without write conflicts.
- Each task references specific requirement clauses for traceability; checkpoints provide incremental validation points.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2", "1.3", "1.4"] },
    { "id": 2, "tasks": ["2.1", "4.1", "4.6", "7.1", "8.15", "10.1"] },
    { "id": 3, "tasks": ["2.2", "2.3", "2.6", "2.8", "4.2", "4.7", "7.2", "10.2", "10.3"] },
    { "id": 4, "tasks": ["2.4", "2.5", "2.7", "2.9", "4.3"] },
    { "id": 5, "tasks": ["4.4", "4.5", "4.8"] },
    { "id": 6, "tasks": ["5.1", "7.3", "9.1"] },
    { "id": 7, "tasks": ["5.2", "5.3", "7.4", "7.5", "7.6", "8.1", "9.2", "9.5"] },
    { "id": 8, "tasks": ["5.4", "7.7", "8.2", "8.3", "8.4", "8.5", "8.6", "8.10", "8.13", "9.3", "9.4", "9.6"] },
    { "id": 9, "tasks": ["8.7", "8.8", "8.9", "8.11", "8.12", "8.14", "11.1"] },
    { "id": 10, "tasks": ["11.2"] }
  ]
}
```
