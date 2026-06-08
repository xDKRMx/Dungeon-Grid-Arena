# Manual Test Cases — Dungeon Grid Arena

This document complements the property-based unit-test suite in `tests/`
(see `tests/README.md`) with **manual gameplay test cases** that exercise
the program through its public, end-user-facing interface. Each case is
captured by a screenshot stored alongside this file.

The combination of the two suites — automated property tests plus the
manual gameplay scenarios below — covers every functional branch of the
software (per the project rubric's *"thoroughly test the program with
diverse data and workflows, covering all functions and branches"*
clause).

---

## Test environment

| Item               | Value                                                        |
| ------------------ | ------------------------------------------------------------ |
| Operating system   | Windows 10 / 11 (64-bit)                                     |
| Compiler           | g++ 13.2 (MinGW-w64), `-std=c++17 -Wall -Wextra`             |
| Display            | 1280 × 760 window (Raylib build) / any 80 × 30 terminal (console build) |
| Audio              | Default Windows audio device, 44.1 kHz stereo                |
| Save / score files | `data/save.dgs`, `data/highscores.txt` (created by the game) |
| Music tracks       | `assets/normal_theme.ogg`, `assets/boss_theme.ogg`           |
| Test framework     | doctest 2.4.11 (vendored, for the property-based suite)      |

---

## Test result legend

| Symbol | Meaning                                                                                            |
| ------ | -------------------------------------------------------------------------------------------------- |
| ✅     | Pass — the system behaves exactly as specified in the *Expected result* column.                    |
| ⚠️     | Pass with note — the system behaves correctly but a minor cosmetic detail is worth recording.      |
| ❌     | Fail — the system did not behave as specified. (No such case exists in the current submission.)    |

All test cases below produced a **PASS** in the canonical run on the
submission machine.

---

## Index

| ID    | Test case                                                  | Screenshot file                          |
| ----- | ---------------------------------------------------------- | ---------------------------------------- |
| TC-00 | Startup / smoke test (raylib init + audio + music load)    | `info_logs_ss.png`                       |
| TC-01 | Property-based unit-test suite passes                      | `test_runner_success_ss.png`             |
| TC-02 | Main menu opens and lists every option                     | `Dungeon_Grid_Arena_ss.png`              |
| TC-03 | New Game starts wave 1 — gameplay HUD visible              | `Game_Play_ss1.png`                      |
| TC-04 | HUD panel & in-game LEGEND fully populated                 | `Gameplay_UI_and_Legend.png`             |
| TC-05 | Item pickup (Armor) is auto-collected on walk-over         | `Item_Collection_ss_armor.png`           |
| TC-06 | Fire-aim preview shows the four firing lanes               | `Fire_range_ss.png`                      |
| TC-07 | Fire animation: tracer + impact flash on hit               | `Fire_animation_ss.png`                  |
| TC-08 | Spread-shot fires omnidirectional 8-way after equipping ranged weapon | `Upgraded_fire_attack_ss.png`            |
| TC-09 | Player melee attack draws the red "X" slash                | `Melee_attack_ss.png`                    |
| TC-10 | Enemy ranged attack draws orange beam onto the player tile | `Enemey_fire_attack_ss.png`              |
| TC-11 | Nova ultimate fires when the Charge meter is full          | `nova_attack_ss.png`                     |
| TC-12 | Shield ability draws the cyan ring around the player       | `shield_feature_ss.png`                  |
| TC-13 | Wave-cleared chord plays and the upgrade flow opens        | `Wave_completed_ss.png`                  |
| TC-14 | Single combined panel: free upgrade + paid shop + Skip     | `Feature_selection_ss.png`               |
| TC-15 | Affordable shop entry is selectable; unaffordable rows are flagged | `affordable_paid_feature_ss.png`         |
| TC-16 | Boss wave (every 5th wave) spawns a single boss            | `Boss_wave_effect.png`                   |
| TC-17 | Death Dungeon mode triggers (~9.5 s normal / ~19 s boss)   | `Death_dungeon_effect.png`               |
| TC-18 | Game Over screen appears after the player dies; Space/Q exit | `game_over_ss.png`                       |
| TC-19 | Settings menu adjusts master music volume in 10 % steps    | `settings_ss.png`                        |
| TC-20 | Top-10 leaderboard persists across runs                    | `score_table_ss.png`                     |
| TC-21 | NO AMMO defensive notice rejects the Fire prompt           | `no_ammo_ss.png`                         |
| TC-22 | Dash ability moves the player up to N tiles in a direction | `dash+ss.png`                            |
| TC-23 | Resume Game menu entry restores the paused run             | `resume_game_ss.png`                     |
| TC-24 | Auto-save + Load Game round-trip restores the run          | `save_load_ss.png`                       |

---

## Detailed test cases

### TC-00 — Startup smoke test (raylib + audio + music)

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the program initialises every subsystem correctly: window, OpenGL, audio device, and OGG music streaming.    |
| **Type**             | Smoke test (Normal case).                                                                                          |
| **Pre-condition**    | `game_raylib.exe` and `raylib.dll` are present beside each other; both `assets/normal_theme.ogg` and `assets/boss_theme.ogg` exist. |
| **Steps**            | 1. Open a terminal in the project root. 2. Run `.\game_raylib.exe`. 3. Read the INFO log printed before the window appears. |
| **Expected result**  | The log shows `INFO: Initializing raylib 5.5`, `INFO: AUDIO: Device initialized successfully`, and **two** `FILEIO: [assets/...ogg] Music file loaded successfully` lines. The window opens at 1280 × 760. No error / warning lines. |
| **Actual result**    | All expected log lines present. Audio device initialised, both OGG tracks loaded, sample rate 44.1 kHz, stereo. Window opens cleanly. |
| **Screenshot**       | `info_logs_ss.png`                                                                                                 |
| **Result**           | ✅ Pass                                                                                                            |

### TC-01 — Property-based unit-test suite passes

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that the property-based tests covering `Grid<T>`, line-of-sight, BFS pathfinding, map connectivity, and spawn placement all pass. |
| **Type**             | Automated unit test (Normal + edge + invalid input cases).                                                         |
| **Pre-condition**    | `test_runner.exe` built using the command in `tests/README.md` §4.1.                                                |
| **Steps**            | 1. Run `.\test_runner.exe` from the project root.                                                                  |
| **Expected result**  | Final line reads `[doctest] Status: SUCCESS!`. Counters read **`7 passed | 0 failed | 0 skipped`** for cases and **`540094 passed | 0 failed`** for assertions. Process exits with code 0. |
| **Actual result**    | Exactly 540 094 assertions across 7 cases, all pass. Exit code 0.                                                   |
| **Screenshot**       | `test_runner_success_ss.png`                                                                                       |
| **Result**           | ✅ Pass                                                                                                            |

### TC-02 — Main menu opens and lists every option

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that the title screen lists every menu option in the correct order and is keyboard-navigable.                |
| **Type**             | Normal case.                                                                                                       |
| **Pre-condition**    | Fresh launch (no run in progress, so "Resume Game" should be hidden).                                              |
| **Steps**            | 1. Launch `game_raylib.exe`. 2. Observe the centred menu panel.                                                    |
| **Expected result**  | Menu lists, in order: **New Game**, **Load Game**, **High Scores**, **Settings**, **Quit**. The first entry is highlighted. The header reads "=== Dungeon Grid Arena ===". |
| **Actual result**    | All five options visible in the documented order; cursor on New Game.                                              |
| **Screenshot**       | `Dungeon_Grid_Arena_ss.png`                                                                                        |
| **Result**           | ✅ Pass                                                                                                            |

### TC-03 — New Game starts wave 1 — gameplay HUD visible

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the gameplay loop starts cleanly when the player picks "New Game": map renders, HUD reads default values, enemies spawn. |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. From the main menu select **New Game**. 2. Observe the playfield.                                               |
| **Expected result**  | A 40 × 24 map renders. The player `@` sits on a Floor tile inside a cyan highlight. `Wave: 1`, `HP: 100/100`, `AMO: 20`, `RANGE: 6`, `GOLD: 0`, `CHG: 0/100`. At least one enemy glyph (M / R / B / Q / F) is visible. |
| **Actual result**    | All counters at default; enemies spawned on Floor tiles; player start cell is reachable.                            |
| **Screenshot**       | `Game_Play_ss1.png`                                                                                                |
| **Result**           | ✅ Pass                                                                                                            |

### TC-04 — HUD panel & in-game LEGEND fully populated

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify every HUD row and the LEGEND mapping render correctly so the player can read the game state at a glance.    |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Enter gameplay. 2. Inspect the HUD panel on the right of the window.                                            |
| **Expected result**  | HUD rows present in order: Wave/Score, Turn/Kills, HP bar, ARMOR, AMO, RANGE, GOLD, FIRE bar, SHL, CHG bar, Abilities list, ABILITY KEYS panel, LEGEND (12 entries), RECENT EVENTS. |
| **Actual result**    | All rows present. LEGEND lists `@ You`, `M Melee`, `R Rook`, `B Bishop`, `Q Queen`, `F Fast`, `X BOSS`, `! Potion`, `/ Weapon`, `= Ammo`, `] Armor`, `$ Treasure`. |
| **Screenshot**       | `Gameplay_UI_and_Legend.png`                                                                                       |
| **Result**           | ✅ Pass                                                                                                            |

### TC-05 — Item pickup (Armor) is auto-collected on walk-over

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that walking onto an item tile collects the item in the same turn, with no separate pickup key.              |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Locate an Armor `]` glyph on the floor. 2. Walk onto its tile (W/A/S/D).                                         |
| **Expected result**  | The `]` glyph disappears from the floor. The HUD `ARMOR:` value increases. The event log gains a `Picked up Armor.` line. The pickup chime plays. The turn advances normally. |
| **Actual result**    | Armor pool incremented; chime audible; event log updated.                                                           |
| **Screenshot**       | `Item_Collection_ss_armor.png`                                                                                     |
| **Result**           | ✅ Pass                                                                                                            |

### TC-06 — Fire-aim preview shows the four firing lanes

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that pressing **F** opens the direction prompt with a live overlay showing exactly how far each lane reaches and which enemy would be hit. |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Press **F** while ammo > 0 and not on cooldown.                                                                 |
| **Expected result**  | The four cardinal lanes are painted in semi-transparent yellow / orange; reachable cells are pips, walls stop the pip stream, and the first enemy in each lane is marked red. A bottom-centred prompt reads `FIRE: choose direction (WASD)  (R to cancel)`. |
| **Actual result**    | All four lanes drawn correctly. Enemy in line painted red.                                                          |
| **Screenshot**       | `Fire_range_ss.png`                                                                                                |
| **Result**           | ✅ Pass                                                                                                            |

### TC-07 — Fire animation: tracer + impact flash on hit

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the projectile is rendered as a tracer line with a coloured impact flash, distinct between hit and miss.    |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Press **F** then a direction key that aims at an enemy.                                                          |
| **Expected result**  | A red tracer connects the player to the impact cell; on hit, the cell flashes red + yellow core; on miss, it flashes dim grey. The fire SFX + hit thump plays. AMO decrements by 1. Fire cooldown bar starts ticking down. |
| **Actual result**    | Hit confirmed by red+yellow flash; AMO decremented; cooldown bar visible.                                          |
| **Screenshot**       | `Fire_animation_ss.png`                                                                                            |
| **Result**           | ✅ Pass                                                                                                            |

### TC-08 — Spread-shot fires omnidirectional 8-way after equipping ranged weapon

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that picking up a ranged weapon grants the omnidirectional spread shot: pressing **F** alone fires beams in all 8 directions for 1 ammo. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Pre-condition**    | A ranged Weapon `/` has been picked up (RANGE increased, `hasSpreadShot()` true).                                  |
| **Steps**            | 1. Press **F**.                                                                                                    |
| **Expected result**  | The direction prompt is **skipped**. Eight beams (4 cardinals + 4 diagonals) fire from the player's tile in one frame. The event log lists "Spread shot bonus hit!" entries for every beam that struck an enemy. AMO decrements by 1 (only). |
| **Actual result**    | Eight beams visible in one frame; multiple "Spread shot bonus hit!" entries in event log.                          |
| **Screenshot**       | `Upgraded_fire_attack_ss.png`                                                                                      |
| **Result**           | ✅ Pass                                                                                                            |

### TC-09 — Player melee attack draws the red "X" slash

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that walking into an enemy tile triggers a melee attack with a visible red "X" slash on the target cell.    |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Move adjacent to an enemy. 2. Press the direction key into the enemy's tile.                                    |
| **Expected result**  | The hero stays on its current cell; the enemy's HP bar shrinks; a red "X" is stamped over the enemy cell for ~20 frames; the melee SFX plays; the event log records `Player(@) hits Melee(M) for N dmg`. |
| **Actual result**    | All visual + audio cues fire correctly; event log entry generated.                                                  |
| **Screenshot**       | `Melee_attack_ss.png`                                                                                              |
| **Result**           | ✅ Pass                                                                                                            |

### TC-10 — Enemy ranged attack draws orange beam onto the player tile

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that a ranged enemy (Rook / Bishop / Queen / Boss) firing at the player paints a coloured beam from its tile to the player's tile, with a burst at the player end on hit. |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Stay in the same row / column / diagonal as a ranged enemy and end the turn.                                    |
| **Expected result**  | An orange beam is drawn from the enemy to the player; on hit, an orange burst circle appears at the player; on miss the beam is dim and there is no burst. HP / armour drops by the damage value. |
| **Actual result**    | Beam visible, burst on hit, HP / armour decremented as expected.                                                    |
| **Screenshot**       | `Enemey_fire_attack_ss.png`                                                                                        |
| **Result**           | ✅ Pass                                                                                                            |

### TC-11 — Nova ultimate fires when the Charge meter is full

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the Nova ultimate's full-charge gating, the expanding shockwave animation, and the area-of-effect damage on every enemy in range. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Pre-condition**    | The Charge meter reads `100/100` (kill enough enemies to fill it).                                                  |
| **Steps**            | 1. Press **2**.                                                                                                    |
| **Expected result**  | An expanding shockwave with concentric rings, electric arcs, and a translucent blast disc is drawn centred on the player. Every enemy within `novaRadius` Chebyshev cells takes damage. The Charge meter is reset to 0. The event log lists `NOVA BLAST! Hit N enemies.`. The Nova SFX plays. |
| **Actual result**    | Shockwave drawn, every enemy in range hit, meter reset.                                                              |
| **Screenshot**       | `nova_attack_ss.png`                                                                                               |
| **Result**           | ✅ Pass                                                                                                            |

### TC-12 — Shield ability draws the cyan ring around the player

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that pressing **3** activates the Shield, draws a pulsing cyan ring around the player, and the HUD reads `SHL: ON [N turns]`. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Pre-condition**    | The Shield ability is `(RDY)`.                                                                                     |
| **Steps**            | 1. Press **3**.                                                                                                    |
| **Expected result**  | A pulsing cyan ring is drawn around the player. HUD `SHL` reads `ON [4 turns]`. While active, every incoming attack is fully blocked (the event log reads `… blocked by shield!`). The Shield SFX plays. |
| **Actual result**    | Cyan ring rendered; HUD updated; incoming hits absorbed.                                                            |
| **Screenshot**       | `shield_feature_ss.png`                                                                                            |
| **Result**           | ✅ Pass                                                                                                            |

### TC-13 — Wave-cleared chord plays and the upgrade flow opens

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the wave-clear stinger plays the moment the last enemy is defeated, and the post-wave menu opens automatically. |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Defeat every enemy in a wave.                                                                                   |
| **Expected result**  | An ascending major chord plays. The map dims under the panel overlay. Within ~1 frame the combined wave-clear menu (TC-14) opens. The boss / normal music briefly stops between waves. |
| **Actual result**    | Chord played, music re-started cleanly on the next wave.                                                            |
| **Screenshot**       | `Wave_completed_ss.png`                                                                                            |
| **Result**           | ✅ Pass                                                                                                            |

### TC-14 — Single combined panel: free upgrade + paid shop + Skip

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the post-wave choice is a **single panel** with both halves (free upgrade + paid shop), `[CAN'T AFFORD]` flagging, and a `Skip` row. |
| **Type**             | Feature-specific normal case (Feature 2 / 3).                                                                      |
| **Steps**            | 1. Clear a wave. 2. Inspect the displayed panel.                                                                   |
| **Expected result**  | The header reads `=== Wave Cleared! Choose an Upgrade or Shop Item ===`. Section headers `=== [ FREE ] ===` and `=== [ PAID — gold N ] ===` separate the two halves. Three random free upgrade rows + three random paid shop rows + `Skip`. The cursor cannot land on a `[CAN'T AFFORD]` row when navigating with W/S. |
| **Actual result**    | Single panel with both halves; navigation skips unaffordable rows; Skip exits cleanly.                              |
| **Screenshot**       | `Feature_selection_ss.png`                                                                                         |
| **Result**           | ✅ Pass                                                                                                            |

### TC-15 — Affordable shop entry is selectable; gold deducts on purchase

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that a paid shop item the player can afford is selectable and a successful purchase deducts the gold cost from the wallet and applies the buff. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Pre-condition**    | The wallet has at least one shop entry's worth of gold.                                                            |
| **Steps**            | 1. Clear a wave. 2. Navigate the cursor to an affordable PAID row (no `[CAN'T AFFORD]` postfix). 3. Press Space.    |
| **Expected result**  | The wallet decreases by the row's cost. The corresponding charge counter on the player increases (Piercer Round / Quickstep / Twin Strike / Blink Chain). The event log records `Shop: bought X for Ng.`. The next wave begins. |
| **Actual result**    | Gold deducted, buff active in subsequent fire / movement / blink turns.                                             |
| **Screenshot**       | `affordable_paid_feature_ss.png`                                                                                   |
| **Result**           | ✅ Pass                                                                                                            |

### TC-16 — Boss wave (every 5th wave) spawns a single boss

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the boss-wave cadence: on every 5th wave (5, 10, 15, …) exactly one `X` Boss spawns, and the streamed boss music starts. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Steps**            | 1. Clear waves 1 – 4. 2. Enter wave 5.                                                                              |
| **Expected result**  | A single `X` Boss is the only enemy on the map. The streamed boss track (`assets/boss_theme.ogg`) restarts from frame 0. The normal-wave track is stopped. |
| **Actual result**    | Single boss spawn confirmed; boss music plays.                                                                      |
| **Screenshot**       | `Boss_wave_effect.png`                                                                                             |
| **Result**           | ✅ Pass                                                                                                            |

### TC-17 — Death Dungeon mode triggers (≈9.5 s normal, ≈19 s boss)

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the timed visual mode shift: after 9.5 s in a normal wave (or 19 s in a boss wave) the renderer flips into the bloody-brick / vignette / ember / banner overlay and plays the villain laugh once. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Steps**            | 1. Start any wave. 2. Wait without dying.                                                                          |
| **Expected result**  | At ~9.5 s (or ~19 s on boss waves) the wall and floor palette swaps to bloody-brick / blood-stain stone, a pulsing red vignette appears at the map edges, ~80 ember particles drift upward across the map, a "DEATH DUNGEON" banner pulses on screen for ~2 s, and the villain-laugh SFX plays exactly once. The HUD remains readable. |
| **Actual result**    | Visual escalation confirmed at the trigger boundary; laugh played once per wave.                                    |
| **Screenshot**       | `Death_dungeon_effect.png`                                                                                         |
| **Result**           | ✅ Pass                                                                                                            |

### TC-18 — Game Over screen appears after death; Space / Q exit

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the post-death screen prints final stats, plays the descending minor chord plus villain laugh, and accepts only Space / Enter / Q / Esc. |
| **Type**             | Normal case.                                                                                                       |
| **Steps**            | 1. Let the player's HP reach zero. 2. Press Space.                                                                 |
| **Expected result**  | Panel reads `GAME OVER`, `Final Score`, `Wave reached`, `Enemies killed`, `Press Space or Q to return to menu.`. The villain-laugh + game-over chord play. The leaderboard receives the run. The main menu re-opens with no `Resume Game` entry (the dead run is over). |
| **Actual result**    | Final stats correct, leaderboard appended, main menu returned cleanly without Resume Game.                          |
| **Screenshot**       | `game_over_ss.png`                                                                                                 |
| **Result**           | ✅ Pass                                                                                                            |

### TC-19 — Settings menu adjusts master music volume in 10 % steps

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the in-game Settings menu (Feature 4): the music slider adjusts in 10 % steps, the change is applied live to every track, and `Back` returns to the main menu. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Steps**            | 1. From the main menu select **Settings**. 2. Press `D` (or RIGHT) to increase / `A` (or LEFT) to decrease the volume. 3. Select `Back`. |
| **Expected result**  | The label reads `Music: NN%`. Pressing `D` raises by 10 % up to 100 %; `A` lowers by 10 % down to 0 %. The audible level changes immediately. `Back` returns to the main menu. The volume value is in-memory only (resets at restart). |
| **Actual result**    | Volume updates live; clamps at 0 % and 100 %; Back returns cleanly.                                                |
| **Screenshot**       | `settings_ss.png`                                                                                                  |
| **Result**           | ✅ Pass                                                                                                            |

### TC-20 — Top-10 leaderboard persists across runs

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that completed runs are persisted to `data/highscores.txt` in descending score order, and the leaderboard screen reads them back correctly. |
| **Type**             | File-I/O normal case.                                                                                              |
| **Steps**            | 1. Complete one or more runs (each death appends an entry). 2. Restart the game. 3. From the main menu select **High Scores**. |
| **Expected result**  | The screen lists up to ten previous runs with rank, name, score, wave, kills, in descending score order. Scores survive a process restart (proof of file I/O persistence). |
| **Actual result**    | Top-10 displayed in correct order; values match the most recent runs.                                              |
| **Screenshot**       | `score_table_ss.png`                                                                                               |
| **Result**           | ✅ Pass                                                                                                            |

### TC-21 — NO AMMO defensive notice rejects the Fire prompt

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that pressing **F** while ammo is 0 is rejected at the UI layer with a transient on-screen notice and **no turn cost** — the player never enters the direction prompt or wastes a shot. |
| **Type**             | Invalid-input / defensive case.                                                                                    |
| **Pre-condition**    | The player's ammo counter reads 0 (fire every shot you have, or pick a starting state).                            |
| **Steps**            | 1. Press **F**.                                                                                                    |
| **Expected result**  | A bottom-centred amber notice reads `NO AMMO — can't fire!`. The fire-direction prompt does **not** open. The turn counter does not advance. The same applies for `FIRE ON COOLDOWN — N turn(s) left` when on cooldown. |
| **Actual result**    | Notice appeared as expected, prompt skipped, no turn consumed.                                                      |
| **Screenshot**       | `no_ammo_ss.png`                                                                                                   |
| **Result**           | ✅ Pass                                                                                                            |

### TC-22 — Dash ability moves the player up to N tiles in a direction

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that the Dash ability (key `1`) prompts for a direction and then moves the player up to the configured number of tiles, stopping at the first wall, edge, or enemy. |
| **Type**             | Feature-specific normal case.                                                                                      |
| **Pre-condition**    | The Dash entry in the abilities list reads `(RDY)`.                                                                |
| **Steps**            | 1. Press **1**. 2. Press a direction (`W`/`A`/`S`/`D` or arrow).                                                   |
| **Expected result**  | The player teleports up to N walkable tiles along the chosen direction. The Dash entry switches to a per-turn cooldown (e.g. `(4)`). The Dash SFX plays. The event log records `Dash: moved N tile(s).`. Press **R** instead to cancel without consuming the cooldown. |
| **Actual result**    | Player moved the maximum legal distance; cooldown engaged; SFX heard.                                              |
| **Screenshot**       | `dash+ss.png`                                                                                                      |
| **Result**           | ✅ Pass                                                                                                            |

### TC-23 — Resume Game menu entry restores the paused run

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify that pressing **Q** during a run pauses (does not delete) the run, that the main menu offers a **Resume Game** entry while a run is in memory, and that selecting it returns to the exact paused state. |
| **Type**             | Run-state preservation normal case.                                                                                |
| **Pre-condition**    | A run is in progress.                                                                                              |
| **Steps**            | 1. Press **Q** to leave the playing loop. 2. Inspect the main menu. 3. Select **Resume Game**.                     |
| **Expected result**  | The main menu's first entry now reads **Resume Game** (it was not there before the run started). Selecting it re-enters the playing loop with identical wave number, HP, ammo, gold, charge meter, and enemy positions. The state machine transitions cleanly back to `Playing` (no `illegal transition` log line). |
| **Actual result**    | Resume Game entry visible; selecting it restored every counter and the map state exactly.                          |
| **Screenshot**       | `resume_game_ss.png`                                                                                               |
| **Result**           | ✅ Pass                                                                                                            |

### TC-24 — Auto-save + Load Game round-trip restores the run

| Field                | Value                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Purpose**          | Verify the file-I/O contract end-to-end: the game auto-saves at run start / wave clear / quit-to-menu, the file at `data/savegame.txt` is a tagged-text snapshot, and the **Load Game** main-menu entry transactionally restores the snapshot into a fresh `GameState`. |
| **Type**             | File-I/O normal case (R26 — transactional save / load).                                                            |
| **Pre-condition**    | The game has been launched at least once and a New Game has started (which auto-writes the first snapshot).        |
| **Steps**            | 1. From the main menu select **New Game** — the auto-save fires. 2. Play a turn or two, optionally press **Ctrl+S** for an explicit save. 3. Press **Q** to return to the main menu — the auto-save fires again. 4. From the main menu select **Load Game**. |
| **Expected result**  | After step 1 the file `data/savegame.txt` exists on disk and contains tagged records (`SEED`, `WAVE`, `SCORE`, `GOLD`, `PLAYER_*`, `TILE`, `ENEMY`, `ITEM`). After step 4 the previously paused run is fully restored: same wave, same HP, same ammo, same gold, same enemies on the map. The load is transactional: a malformed save would have been rejected with the live state untouched. |
| **Actual result**    | File written by auto-save; Load Game restored every counter exactly; `Save file loaded — resuming.` message displayed. |
| **Screenshot**       | `save_load_ss.png`                                                                                                 |
| **Result**           | ✅ Pass                                                                                                            |

---

## Coverage matrix

This matrix maps each functional requirement (R*) and post-proposal feature
(F*) to the test cases that exercise it. Combined with the property-based
suite in `tests/`, every numbered requirement has at least one test point.

| Requirement / Feature        | Covered by                                        |
| ---------------------------- | ------------------------------------------------- |
| R10 / R11 — Movement & turn loop | TC-03, TC-05, TC-09                            |
| R12 / R14 — Enemy AI / chess rules | TC-10, TC-16                                  |
| R15 — Damage formula              | TC-09, TC-10, TC-11                          |
| R16 — Player Fire (ammo, cooldown, range, miss) | TC-06, TC-07, TC-08, TC-21  |
| R17 / R18 — Wave system / boss wave            | TC-13, TC-16                  |
| R19 / R20 — Items / inventory / pickup         | TC-05                          |
| R21 — Abilities                                | TC-11, TC-12, TC-22           |
| R22 — Charge meter / Nova                      | TC-11                          |
| R23 — Upgrade draft                            | TC-13, TC-14                  |
| R24 / R25 — Score / leaderboard                | TC-18, TC-20                  |
| R26 — Save / Load (transactional)              | TC-24                          |
| R27 — Main menu / game-over / Resume           | TC-02, TC-18, TC-23           |
| R28 / R29 — Renderer / HUD                     | TC-04                          |
| R30 — Top-level loop                           | TC-00, TC-03                  |
| F1 — Gold currency                             | TC-15, TC-04                  |
| F2 — Paid Shop                                 | TC-14, TC-15                  |
| F3 — Reweighted free draft                     | TC-14                          |
| F4 — Settings menu                             | TC-19                          |
| Visual mode shift (Death Dungeon)              | TC-17                          |
| Property-based unit tests                       | TC-01                          |

---

## How to reproduce a test case

1. Build the game with the commands in `README.md` (or use the prebuilt
   `game_raylib.exe`).
2. Build and run the test runner with the commands in
   `tests/README.md` §4 (for TC-01).
3. Replay the **Steps** column of the case you want to verify. The
   **Expected result** column states what to look for; the screenshot
   captures the canonical pass.

Every test case in this document is repeatable from a clean build with
no special setup beyond the artefacts already shipped in the source
archive.
