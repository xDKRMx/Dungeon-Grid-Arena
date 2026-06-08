# Dungeon Grid Arena — User Manual

A turn-based dungeon roguelike written in C++17 with two interchangeable
renderers: a portable ASCII console renderer and an optional Raylib graphical
renderer. This manual covers everything you need to install, run, play, save,
load, and troubleshoot the game.

---

## Table of Contents

1. [System Requirements](#1-system-requirements)
2. [Installation and Build](#2-installation-and-build)
3. [Running the Game](#3-running-the-game)
4. [Main Menu](#4-main-menu)
5. [Gameplay Controls](#5-gameplay-controls)
6. [Heads-Up Display (HUD)](#6-heads-up-display-hud)
7. [Combat Rules](#7-combat-rules)
8. [Items and Pickups](#8-items-and-pickups)
9. [Abilities](#9-abilities)
10. [Wave Cleared Menu (Free Upgrade + Paid Shop)](#10-wave-cleared-menu)
11. [Saving and Loading](#11-saving-and-loading)
12. [Settings](#12-settings)
13. [High Scores](#13-high-scores)
14. [Death Dungeon Mode](#14-death-dungeon-mode)
15. [Audio](#15-audio)
16. [Operational Restrictions and Input Rules](#16-operational-restrictions-and-input-rules)
17. [Troubleshooting](#17-troubleshooting)


---

## 1. System Requirements

| Component        | Minimum                                                                                                         |
| ---------------- | --------------------------------------------------------------------------------------------------------------- |
| Operating system | Windows 10 (64-bit). Linux and macOS are supported by the C++17 codebase but the prebuilt binaries are Windows. |
| CPU              | Any 64-bit x86 CPU that supports SSE2 (anything from the last 15 years).                                        |
| Memory           | 200 MB free RAM is more than enough.                                                                            |
| Disk             | 30 MB for the source tree + binaries; ~10 MB extra for the streaming OGG music tracks under `assets/`.          |
| Display          | Any monitor capable of showing a 1280 × 760 window for the Raylib build. The console build runs in any 80 × 30 terminal. |
| Audio            | Optional. The Raylib build runs silently if no audio device is present; the console build does not produce sound at all. |
| Graphics driver  | Any OpenGL 3.3 compatible driver (Raylib build only). The console build needs no GPU. |
| Compiler         | g++ 9 or newer / any C++17-conforming compiler if you want to rebuild from source.                              |
| External libs    | **Raylib 5.5** (already vendored in `libs/raylib/` for the prebuilt Windows binary). The console build has no external dependencies. |

---

## 2. Installation and Build

The release archive ships **two ready-to-run executables** and the full source.

### 2.1  Use the prebuilt binaries (no build required)

The repository root contains:

```
game.exe          ← console (ASCII) build, no Raylib needed
game_raylib.exe   ← graphical Raylib build, requires the bundled raylib.dll
libs/raylib/lib/raylib.dll
assets/normal_theme.ogg
assets/boss_theme.ogg
data/highscores.txt        ← created on first leaderboard write
```

Just double-click either `.exe` and the game starts. `game_raylib.exe` finds
`raylib.dll` and the OGG tracks automatically because they sit beside the
binary; do **not** move them out of those folders.

### 2.2  Build from source

From the repository root, with `g++` on `PATH`:

#### Console build (no external dependencies)

```
g++ -std=c++17 -Wall -Wextra -I src ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/systems/*.cpp src/world/*.cpp ^
    -o game.exe
```

#### Raylib (graphical) build

```
g++ -std=c++17 -Wall -Wextra -DDGA_WITH_RAYLIB ^
    -I src -I libs/raylib/include ^
    src/main.cpp src/abilities/*.cpp src/combat/*.cpp src/core/*.cpp ^
    src/entities/*.cpp src/io/*.cpp src/items/*.cpp ^
    src/render/ConsoleRenderer.cpp src/render/RaylibRenderer.cpp ^
    src/systems/*.cpp src/world/*.cpp ^
    -L libs/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm ^
    -o game_raylib.exe
```

Both commands are warning-clean: any warning is treated as a defect.

### 2.3  Build the test runner

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

Then `./test_runner.exe`. See `tests/README.md` for the full test
documentation.

---

## 3. Running the Game

Open a terminal in the project root and run **one** of the binaries:

```
.\game.exe          (console / ASCII version)
.\game_raylib.exe   (graphical version, recommended)
```

The graphical version opens a 1280 × 760 window titled **Dungeon Grid Arena**.
The console version takes over the current terminal window.

To quit the game cleanly use the **Quit** option in the main menu (or press
`Q` while a menu is active). On the Raylib build you can also click the
window's close button.

---

## 4. Main Menu

The main menu lists the following options. Select with `W` / `S`
(or arrow keys) and confirm with `Space` or `Enter`.

| Option        | When shown                  | What it does                                                                      |
| ------------- | --------------------------- | --------------------------------------------------------------------------------- |
| **Resume Game** | A run is currently paused. | Returns to the in-progress run exactly where you left it (no reset).             |
| **New Game**  | Always.                     | Starts a fresh run with a time-based RNG seed; replaces any previous run state.   |
| **Load Game** | Always.                     | Restores a saved run from `data/savegame.txt`. Errors are reported on screen.         |
| **High Scores** | Always.                   | Shows the top 10 scores from `data/highscores.txt`.                              |
| **Settings**  | Always.                     | Adjust master music volume.                                                       |
| **Quit**      | Always.                     | Exit the game cleanly.                                                            |

`Q` is treated as **Quit** at the main menu. Pressing the window close button
on the Raylib build is also treated as Quit.

---

## 5. Gameplay Controls

The game is **strictly turn-based**: every key press resolves at most one
player action, then the enemy phase runs, then the next turn begins.

### 5.1  Movement

| Key                   | Action                                          |
| --------------------- | ----------------------------------------------- |
| `W` / `Up Arrow`      | Move one tile north.                            |
| `S` / `Down Arrow`    | Move one tile south.                            |
| `A` / `Left Arrow`    | Move one tile west.                             |
| `D` / `Right Arrow`   | Move one tile east.                             |
| `Space` / `Enter`     | Wait one turn (skip the player phase).          |

Walking onto an item tile collects the item automatically — no separate
pickup key. Walking into an enemy tile performs a melee attack instead of a
move.

### 5.2  Combat

| Key sequence                | Action                                                                                       |
| --------------------------- | -------------------------------------------------------------------------------------------- |
| `F` then `W` / `A` / `S` / `D` | Fire a ranged shot in the chosen direction (consumes 1 ammo, shows a 4-lane preview while picking the direction). |
| `R` (during a Fire prompt)  | Cancel the shot. No ammo is spent and no turn is consumed.                                   |
| `F` while holding a ranged weapon | Omnidirectional 360° burst (the direction prompt is skipped — every cardinal AND diagonal lane fires from a single ammo). |

### 5.3  Abilities

| Key | Ability       | Notes                                                                                       |
| --- | ------------- | ------------------------------------------------------------------------------------------- |
| `1` | Dash          | Asks for a direction (`W`/`A`/`S`/`D`). Press `R` to cancel without spending the cooldown.  |
| `2` | Nova          | Press alone. **Requires a full Charge meter** (kills fill it).                              |
| `3` | Shield        | Press alone. Grants 4 turns of damage immunity.                                             |
| `4` | Blink         | Press alone. Random teleport to a visible empty floor tile.                                 |

### 5.4  System

| Key      | Action                                                                                  |
| -------- | --------------------------------------------------------------------------------------- |
| `Q`      | Quit to main menu (the run is paused — Resume Game restores it).                       |
| `Ctrl+S` | Save the current run to `data/savegame.txt`.                                                |

### 5.5  Menu navigation (any in-game menu)

| Key                   | Action                                                                |
| --------------------- | --------------------------------------------------------------------- |
| `W` / `Up`            | Move the cursor up. Skips over disabled entries (e.g. unaffordable shop items). |
| `S` / `Down`          | Move the cursor down (also skips disabled entries).                  |
| `A` / `Left` / `D` / `Right` | Adjust a slider (currently used by the music volume slider).  |
| `Space` / `Enter`     | Confirm the highlighted entry.                                        |
| `Q`                   | Cancel / Skip / Back, depending on the menu.                          |

---

## 6. Heads-Up Display (HUD)

The Raylib build draws a HUD panel on the right side of the window. The
console build prints a similar block of text under the map. Both show:

| Line               | Meaning                                                                                                                         |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| `Wave: N`          | Current wave number (1-based). Every fifth wave (5, 10, 15, …) is a **boss wave**.                                              |
| `Score: N`         | Accumulated run score (wave × 100 + kills × 10 + treasure value). Used by the leaderboard.                                       |
| `Turn: N`          | Total turns elapsed this run.                                                                                                   |
| `Kills: N`         | Enemies defeated this run.                                                                                                      |
| `HP: x/y`          | Current / maximum Health. Bar colour: green ≥ 66 %, yellow ≥ 33 %, red below.                                                   |
| `ARMOR: N`         | Damage-absorbing armour pool. Armour is consumed before HP on every hit; only excess damage spills through to HP.                |
| `AMO: N`           | Ranged ammunition.                                                                                                              |
| `RANGE: N`         | How many cells a fired projectile can travel. Raised by ranged-weapon pickups.                                                  |
| `GOLD: N`          | Spendable currency (treasures grant Gold *and* Score). Spent in the post-wave Shop.                                             |
| `FIRE: …`          | Either `READY` (green bar full) or `cooldown N` (orange bar fills as it ticks down).                                            |
| `SHL: …`           | `OFF` or `ON [x turns]`.                                                                                                        |
| `CHG: x/100`       | Nova charge meter. Each kill adds charge; at 100 the bar pulses magenta and the prompt becomes `CHG: FULL — press 2 for NOVA!`.  |
| `Abilities:` block | One row per owned ability. `(RDY)` when ready, `(N)` when N turns of cooldown remain. Nova reads `READY!` or `needs full Charge`. |
| `ABILITY KEYS` panel | Cheat-sheet of every ability's input contract.                                                                                 |
| `LEGEND` panel     | Two-column glyph → meaning table (player, every enemy type, every item type).                                                    |
| `RECENT EVENTS`    | Rolling buffer of the latest event-log messages (combat outcomes, pickups, deaths, ability casts, save / load).                  |

Below the map the Raylib build prints a four-line **controls reminder strip**
listing every key. The console build prints the same reminder once per
turn.

---

## 7. Combat Rules

### 7.1  Damage formula

Damage equals the attacker's **attack** stat. Targets with armour absorb
damage from the armour pool first; any remainder spills through to HP.

```
incoming = attacker.attack
absorbed = min(incoming, target.armor)
target.armor  -= absorbed
target.health -= max(0, incoming - absorbed)
```

The **player Shield** ability fully blocks all incoming damage during its
duration: armour is not consumed and HP does not drop.

The Twin Strike shop item, when active, **doubles** the player's outgoing
damage on a melee strike (one charge consumed per strike).

### 7.2  Player Fire (ranged)

Fire traces a Bresenham ray from the player along the chosen direction up to
the player's `RANGE`. The first enemy in line of sight is hit; walls and
out-of-bounds cells stop the ray with no hit. Each shot:

* Costs **1 ammo** (the shot is rejected if `AMO == 0`).
* Triggers the fire **cooldown** (default 2 turns; the cooldown bar fills
  back up before the next shot is allowed).
* Shows a **4-lane (or 8-lane with a ranged weapon) live preview** of where
  the projectiles would land before you commit to a direction. Enemies in
  range are highlighted red; reachable cells are tinted yellow.

The Piercer Round shop item, when active, lets the next 3 shots **pass
through walls** (one charge consumed per shot).

### 7.3  Enemy attacks

Enemies move and attack on every turn following yours. Enemy types:

| Glyph | Name        | Rule (chess-inspired)                                                              |
| ----- | ----------- | ---------------------------------------------------------------------------------- |
| `M`   | Melee       | Attacks any orthogonally adjacent player tile.                                     |
| `R`   | Rook        | Fires along straight rows / columns when the player is in the same line.            |
| `B`   | Bishop      | Fires along diagonals; falls back to melee on Chebyshev distance 1.                |
| `Q`   | Queen       | Can fire on any straight or diagonal line (rook + bishop).                         |
| `F`   | Fast        | Takes up to 2 movement steps per turn before checking attack eligibility.          |
| `X`   | Boss        | Spawns alone every 5th wave. Higher HP, attack, and area-of-effect on death.       |

Ranged enemies have a **distance-weighted hit chance**: 95 % at 1 tile,
falling 8 % per extra tile, with a 35 % minimum. A miss still draws a dim
beam so you can read where the shot came from.

### 7.4  Player death

When `HP` reaches 0 a final frame is drawn, the game-over chord and a
"villain laugh" cackle play, the boss / normal music stops, the leaderboard
is updated with the run, and the game-over panel waits for `Space` or `Q`
to return to the main menu. **Resume Game** is hidden until a new run starts.

---

## 8. Items and Pickups

Items lie on the floor and are collected automatically when you walk over
them — there is no separate pickup key.

| Glyph | Name           | Effect                                                                                  |
| ----- | -------------- | --------------------------------------------------------------------------------------- |
| `!`   | Health Potion  | Heals the player by a configured amount; grants 2 bonus Shield turns.                   |
| `/`   | Weapon (melee) | Replaces the equipped weapon if its attack value is higher; never weakens the hero.     |
| `/`   | Weapon (ranged)| Adds a fire-range bonus AND grants the **omnidirectional spread shot** (8-way fire).    |
| `=`   | Ammo           | Adds the configured number of rounds to your ammo reserve.                              |
| `]`   | Armor          | Adds armour points to the absorbing buffer. Higher armour = more hits before HP drops. |
| `$`   | Treasure       | Adds its value to **Score AND Gold**. Score is permanent; Gold is spent in the Shop.    |

---

## 9. Abilities

Every player starts with all four abilities. Ability cooldowns tick down at
the end of every turn (one-turn-per-tick). Nova is gated by a separate
"Charge meter" that kills fill, not by a turn cooldown.

| Ability  | Effect                                                                                                                | Cooldown                  |
| -------- | --------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| Dash     | Move up to N cells in the chosen direction along open floor (configurable; default 3).                                | 4 turns.                  |
| Nova     | Area-of-effect blast around the player with two-tier damage (full inside the inner radius, half on the outer ring).   | None — needs full Charge. |
| Shield   | Grants 4 turns of full damage immunity (all incoming damage is absorbed before any HP/armour math).                   | 8 turns.                  |
| Blink    | Random teleport to a visible empty floor tile (line-of-sight from current position).                                  | 6 turns.                  |

Ability prompts that ask for a direction (Dash) accept `R` to cancel
without consuming the cooldown.

---

## 10. Wave Cleared Menu (Free Upgrade + Paid Shop)

After every wave a **single combined menu** opens listing two halves:

```
=== Wave Cleared! Choose an Upgrade or Shop Item ===

=== [ FREE ] ===
> Patch Up: +10 max health
  Sharpen: +3 attack
  Tempered: +2 armor

=== [ PAID — gold 27 ] ===
  Quickstep (25g) - next 4 turns: 2 moves per turn
  Coin Cache (0g) - small change — costs nothing
  Blink Chain (40g) - next 3 Blinks ignore cooldown  [CAN'T AFFORD]

  Skip
```

* Three **FREE** upgrade cards are always selectable. Picking one applies
  the effect and ends the menu.
* Three **PAID** shop items are selectable only if you can afford them.
  Unaffordable rows are decorated `[CAN'T AFFORD]` and the cursor skips over
  them — `Space` cannot select them.
* Picking a PAID item deducts its cost in Gold and applies the effect.
* `Skip` (or `Q`) closes the menu without picking anything.

### 10.1  Free upgrade pool

| Card           | Effect                          |
| -------------- | ------------------------------- |
| Patch Up       | +10 max health                  |
| Fortify        | +20 max health                  |
| Resilience     | +30 max health                  |
| Sharpen        | +3 attack                       |
| Battle Honed   | +5 attack                       |
| Tempered       | +2 armor                        |
| Bastion        | +4 armor                        |
| Resupply       | +10 ammo                        |
| Extra Charge   | +15 ammo (bonus)                |
| Penny Pinch    | +5 gold (boosts your wallet)    |

Three are drawn at random per wave.

### 10.2  Paid shop catalogue

| Item            | Cost | Effect                                                              |
| --------------- | ---- | ------------------------------------------------------------------- |
| Piercer Round   | 30 g | Next **3 fire shots pierce walls** (also bypass blocked LOS).       |
| Quickstep       | 25 g | Next **4 turns** the player gets **2 movement actions per turn**.   |
| Blink Chain     | 40 g | Next **3 Blink uses ignore cooldown** (chain instantly).            |
| Twin Strike     | 35 g | Next **5 melee hits deal 2× damage**.                              |
| Coin Cache      | 0 g  | Filler row that is always affordable; awards a token amount of Gold. |

Three are drawn at random per wave.

---

## 11. Saving and Loading

### 11.1  Saving

Press **`Ctrl+S`** at any time during gameplay to save the run to
`data/savegame.txt`. On success the message reads
`Game saved to data/savegame.txt.`; on failure (e.g. read-only data
folder) the message reads `SAVE FAILED — see event log` so you know
the on-disk state did **not** change. Saving is a full overwrite —
there is exactly one save slot.

The save format is **plain UTF-8 text** with one tagged record per line
(`SEED 12345`, `WAVE 7`, `TILE 4 9 F`, `ENEMY Rook 12 8 24`, …) so a save
can be inspected or hand-edited if needed.

### 11.2  Loading

From the main menu select **Load Game**. The save is parsed completely into
**staging variables** before any change is committed to the live game state:

* If parsing fails halfway through, the live state is **untouched** and an
  error message names the offending line. You return to the main menu.
* If parsing succeeds, the staged state replaces the live one, the player's
  abilities are re-granted (saves do not persist abilities), and the run
  resumes on the saved wave.

### 11.3  Backwards compatibility

Save files written by older versions that did not yet record the player's
**Gold** reserve still load cleanly — the missing field defaults to 0.

### 11.4  Limits

* Only one save slot.
* The save file path is `data/savegame.txt` (configurable via `Config`).
* The directory `data/` must be writable; the save fails silently to disk
  but logs an error on screen if it is read-only.

---

## 12. Settings

Open **Settings** from the main menu to access the in-game options panel.

| Row                | Effect                                                                                                            |
| ------------------ | ----------------------------------------------------------------------------------------------------------------- |
| `Music: NN%`       | Adjust master music volume in 10 % steps with `A` / `D` (or LEFT / RIGHT). Live — the change is audible at once. |
| `Back`             | Return to the main menu (`Space` / `Enter`).                                                                      |

The volume value is stored **in memory only**. Restarting the game resets it
to 100 %.

---

## 13. High Scores

Selecting **High Scores** from the main menu shows the top 10 entries in
`data/highscores.txt`. An entry is appended automatically every time the
player dies, sorted by descending score. Press any key to return to the
menu.

The leaderboard formula is:

```
score = wave * 100 + kills * 10 + treasure value
```

The default name on the entry is `Player`; the file can be edited by hand
between runs if you want to rename a top score.

---

## 14. Death Dungeon Mode

Every wave starts in a calm, neutral palette. After **9.5 seconds** of real
time on a normal wave (or **19 seconds** on a boss wave) the renderer flips
into **Death Dungeon** mode:

* The wall and floor palette swaps to bloody-brick / blood-stain stone.
* A pulsing red **vignette** darkens the borders of the map.
* Glowing **ember particles** drift upward across the play area.
* A pulsing `DEATH DUNGEON` banner fades in for ~2 seconds.
* A villain-laugh cackle plays exactly once at the trigger boundary.

The mode is **purely visual / atmospheric** — it does not affect movement,
combat, or any game stat. The HUD panel and event log stay readable.

---

## 15. Audio

The Raylib build plays:

* **Per-wave streaming music**:
  * Boss waves (5, 10, 15, …) play `assets/boss_theme.ogg`.
  * Normal waves play `assets/normal_theme.ogg`.
  * Each wave restarts its track from the beginning.
* A quiet **procedural ambient pad** under the streaming track (a slow
  4-voice A-minor chord with a gentle LFO).
* Procedurally synthesised SFX for fire, melee, hit, enemy hit, nova,
  pickup, dash, shield, blink, wave-clear chime, game-over sting, and
  villain laugh. **No SFX files are shipped** — every sound is built at
  startup from sine / noise samples.

The console build is silent.

If `assets/boss_theme.ogg` or `assets/normal_theme.ogg` is missing the
Raylib build degrades gracefully: the corresponding wave plays only the
ambient pad. No crash.

---

## 16. Operational Restrictions and Input Rules

The following restrictions are enforced by the game and apply to every
player input:

1. **Movement** is one tile at a time, orthogonally only (no diagonal
   movement keys). Diagonal *firing* is allowed only when a ranged weapon
   is equipped (omnidirectional spread shot).
2. **Fire requires a direction.** The direction prompt accepts only
   `W`/`A`/`S`/`D` or arrow keys. Pressing `R` cancels.
3. **Fire is rejected** on three conditions, each producing an instant
   on-screen notice with no ammo / cooldown spent:
   * Ammo is 0 (`NO AMMO — can't fire!`).
   * Fire cooldown > 0 (`FIRE ON COOLDOWN — N turn(s) left`).
   * Direction is invalid (Vec2 of zeros, e.g. you pressed an unmapped key
     during the prompt).
4. **Walking into a wall or out of bounds** cancels the move with a
   `Move blocked` log line and **does not consume the turn** — you can
   immediately try a different action.
5. **Picking up an item is implicit**: walking onto an item tile collects
   it in the same turn the move resolves. There is no separate key to
   pick things up, and picking does not waste an action.
6. **Save** (`Ctrl+S`) does not consume a turn. **Quit** (`Q`) returns to
   the main menu without saving — use Save first if you want to resume
   later via Load Game.
7. **Resume Game** appears in the main menu only while a run is in memory
   (after New Game / Load Game and until the player dies).
8. **Game Over input** accepts only `Space`, `Enter`, `Q`, or `Esc` —
   every other key is intentionally ignored to prevent the screen from
   accidentally routing into a fire / dash direction prompt.
9. **Settings** changes are in-memory only. They reset to defaults on
   process restart.
10. The **save file** must be a single line per record; corrupted files
    are detected during the staging parse and rejected without altering
    the live state.
11. **Ability index** in the save file is **not** persisted. After a
    successful load, the four canonical abilities (Dash / Nova / Shield /
    Blink) are re-granted in order so the `1` / `2` / `3` / `4` keys map
    to the same abilities they always do.

---

## 17. Troubleshooting

| Symptom                                                              | Likely cause and fix                                                                                                         |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `game_raylib.exe` does not start; pop-up about `raylib.dll`.         | The DLL must sit in `libs/raylib/lib/` *or* beside the executable. Don't move it.                                            |
| The game starts but is silent (Raylib build).                         | Either no audio device is available, or `assets/normal_theme.ogg` / `assets/boss_theme.ogg` is missing. Procedural SFX still play if the device works. |
| `Could not load save file` after Load Game.                          | `data/savegame.txt` is missing or corrupted. The error log line names the offending record. Start a New Game instead.            |
| `data/highscores.txt` does not exist.                                | It is created on the first death. Until then High Scores reads as empty.                                                     |
| The game window freezes during a fire / dash direction prompt.       | Press `R` (cancel) or close the window. The prompt deliberately accepts only directional keys plus `R`.                      |
| Nova won't fire when I press `2`.                                    | The Charge meter is not full yet (it shows `CHG: x/100`). Land more kills until the bar pulses magenta.                       |
| The fire / dash prompt freezes after I died.                         | Fixed in the current build (the Game Over screen now uses a narrow Space / Enter / Q / Esc-only loop). Press one of those keys. |
| Build fails with `'raylib.h' not found`.                             | You forgot `-I libs/raylib/include` on the compile command.                                                                  |
| Build fails with linker errors mentioning `Init…`/`DrawText`.         | You forgot `-L libs/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm` for the Raylib build.                                    |
| `test_runner.exe` reports any non-`0 failed` row.                    | A property test's invariant was violated. The output names the file, line, and seed; reproduce the case and fix.             |

---

## Appendix A — File and directory layout

```
c++ final project/
├── README.md                 ← short overview / build commands
├── USER_MANUAL.md            ← this file
├── Final_Project_Description.txt
├── CMakeLists.txt            ← optional CMake build
├── game.exe                  ← prebuilt console binary
├── game_raylib.exe           ← prebuilt graphical binary
├── assets/                   ← OGG music tracks streamed by the Raylib build
│   ├── normal_theme.ogg
│   └── boss_theme.ogg
├── data/                     ← runtime files (created/written by the game)
│   ├── highscores.txt
│   └── savegame.txt              (created by Ctrl+S)
├── libs/raylib/              ← vendored raylib 5.5 (header + import lib + DLL)
├── src/                      ← all C++ source, layered as below
│   ├── core/                 (Vec2, Direction, Enums, Config, Rng, Grid<T>, RingBuffer<T>)
│   ├── world/                (Tile, GridMap, MapGenerator, Pathfinder, LineOfSight)
│   ├── entities/             (Entity, Player, Enemy hierarchy)
│   ├── items/                (Item, HealthPotion, Weapon, AmmoItem, Armor, Treasure, Inventory)
│   ├── combat/               (CombatSystem)
│   ├── abilities/            (Ability, Dash/Nova/Shield/Blink)
│   ├── systems/              (TurnManager, GameState, GameStateMachine, AbilitySystem,
│   │                          WaveManager, UpgradeSystem, Shop, Game, EventLog)
│   ├── io/                   (SaveManager, ScoreBoard)
│   └── render/               (IRenderer, ConsoleRenderer, RaylibRenderer)
└── tests/                    ← property-based test suite (doctest)
    ├── README.md             ← test methodology and run instructions
    ├── doctest.h
    ├── test_main.cpp
    ├── test_grid.cpp
    ├── test_line_of_sight.cpp
    ├── test_pathfinder.cpp
    ├── test_mapgen_invariant.cpp
    └── test_spawn_placement.cpp
```

---

## Appendix B — Default key bindings reference card

```
MOVEMENT:    W A S D    (or Arrow Keys)        Walk into enemy = Melee attack
COMBAT:      F + W/A/S/D                       Fire (R cancels)
                                                With ranged weapon: F alone fires 8-way
ABILITIES:   1=Dash (+direction)   2=Nova       3=Shield   4=Blink
SYSTEM:      Space=Wait/Confirm                 Q=Quit to menu / Cancel
             Ctrl+S=Save game                   R=Cancel direction prompt
GAME OVER:   Space / Enter / Q / Esc             (any other key ignored)
SETTINGS:    Music slider: A or D (Left/Right)  Back: Space/Enter
```

---

*Document version: final submission. Last updated alongside the source code
release.*
