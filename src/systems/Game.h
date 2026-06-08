// =============================================================================
// systems/Game.h
//
// Purpose:
//   Game is the top-level orchestrator that drives the entire game loop
//   (R2.4, R10.1, R27.1, R30.1-R30.4). It owns every system and pieces of
//   shared state together, then runs the run() function that:
//
//     1. Shows the main menu.
//     2. Starts waves (via WaveManager).
//     3. Loops processTurn() until the wave is cleared or the player dies.
//     4. On wave clear: shows the upgrade draft and applies the chosen upgrade.
//     5. On player death: shows the game-over screen, records the score, and
//        returns to the main menu or exits.
//     6. Handles Save / Load via SaveManager whenever the player requests it.
//     7. Passes the EventLog to drawFrame() each turn so the renderer displays
//        the event history (R29.3).
//
//   Game.cpp MUST NOT call std::cout / std::cin / _getch directly; ALL I/O goes
//   through the IRenderer& that was injected at construction time (R8.2).
//
// Ownership:
//   Game owns (by value) all the logic-layer systems and the GameState.
//   It holds the IRenderer by NON-OWNING reference: main.cpp owns the concrete
//   renderer object and passes it by reference to Game.  This keeps the
//   renderer's lifetime simple (it lives as long as the process).
//
// Requirements: 2.4, 8.2, 10.1, 27.1, 30.1, 30.3, 30.4
//
// Why a .h/.cpp split:
//   Game has real orchestration logic (menu navigation, wave loop, upgrade
//   draft) that must live in a .cpp (R2.1).
//
// Layer: systems (depends on all logic layers; NOT on the render layer — the
//   render type is accessed only through the IRenderer interface, which IS a
//   render-layer header, but the systems layer is permitted to include it
//   because the dependency flows THROUGH the abstract interface, not into any
//   concrete renderer).
// =============================================================================
#pragma once

#include <string> // std::string — showMainMenu() returns the selected option label.

// Game owns one instance of each system by value.
#include "core/Config.h"              // Config  — balancing constants.
#include "systems/AbilitySystem.h"    // AbilitySystem — ability activation.
#include "systems/EventLog.h"         // EventLog — gameplay message history.
#include "systems/GameState.h"        // GameState — the owned run state.
#include "systems/GameStateMachine.h" // GameStateMachine — high-level state.
#include "systems/TurnManager.h"      // TurnManager + InputCommand.
#include "systems/UpgradeSystem.h"    // UpgradeSystem — between-wave draft.
#include "systems/Shop.h"             // Shop — paid post-wave draft (Feature 2).
#include "systems/WaveManager.h"      // WaveManager — spawn + advance waves.
#include "combat/CombatSystem.h"      // CombatSystem — damage / projectile.
#include "world/Pathfinder.h"         // Pathfinder — BFS for enemy AI.
#include "render/IRenderer.h"         // IRenderer — abstract I/O interface.

namespace dga {

// =============================================================================
// Game — top-level orchestrator
// =============================================================================

/// Drives the complete game loop (R2.4, R30.1-R30.4).
///
/// Constructed once from main.cpp with a Config and an IRenderer&; the caller
/// then invokes run() to enter the main loop. Game terminates only when the
/// player quits from the main menu.
class Game {
public:
    // ---- Construction / destruction ---------------------------------------

    /// Construct the Game, wiring together all owned systems and the renderer.
    ///
    /// Initialises the GameState with the given config and seed, instantiates
    /// each system by value, and stores a reference to the renderer.
    ///
    /// @param config   the balancing configuration; Game stores its own copy so
    ///                  the caller can pass a local Config without worrying about
    ///                  lifetime.
    /// @param renderer the concrete renderer to use for all drawing and input;
    ///                  Game does NOT take ownership — the caller (main.cpp) must
    ///                  ensure the renderer outlives the Game object.
    /// @param seed     the RNG seed that makes the run deterministic; pass 0 to
    ///                  use a time-based seed (computed in the constructor).
    Game(const Config& config, IRenderer& renderer, unsigned int seed = 0);

    /// Destructor — default is sufficient; all members clean up themselves.
    ~Game() = default;

    // ---- Primary entry point ----------------------------------------------

    /// Run the full game loop until the player quits (R30.1-R30.4).
    ///
    /// Loop structure (R30):
    ///   * Show main menu (New Game / Load / High Scores / Quit).
    ///   * On "New Game": reset the run state, call WaveManager::startWave, enter
    ///     the playing loop.
    ///   * On "Load": try SaveManager::load; on success enter the playing loop;
    ///     on failure redisplay the main menu.
    ///   * Playing loop:
    ///       — drawFrame().
    ///       — pollInput().
    ///       — processTurn().
    ///       — If wave cleared  → show upgrade draft (R23); WaveManager::advance.
    ///       — If player died   → record score, show game over, return to menu.
    ///       — If save requested → SaveManager::save; continue loop.
    ///       — If quit requested → return to main menu.
    ///   * On "High Scores": show top-10 leaderboard, wait for any key.
    ///   * On "Quit": exit the loop and return from run().
    void run();

private:
    // ---- Internal game-flow helpers ---------------------------------------

    /// Display the main menu and return the player's chosen action.
    ///
    /// The menu is built DYNAMICALLY: when a run is currently in progress
    /// (runInProgress_ == true) the first option is "Resume Game", which lets
    /// the player drop back into the paused run they left with Q. When no run
    /// is in progress the list starts with "New Game" as before.
    ///
    /// Returning the selected option STRING (rather than a fragile fixed index)
    /// keeps run() correct no matter how many leading options the dynamic menu
    /// has; run() simply compares the returned string against the known labels.
    /// @return the label of the selected option (e.g. "Resume Game", "Quit").
    std::string showMainMenu();

    /// Run the wave-based playing loop for a single continuous run.
    ///
    /// Drives the turn loop (drawFrame → pollInput → processTurn) until either:
    ///   (a) The player quits / requests main menu.
    ///   (b) The player dies → game over screen.
    /// Between each cleared wave the upgrade draft is presented.
    void playingLoop();

    /// Present the wave-clear menu as a SINGLE panel: a free upgrade pick
    /// (always selectable) and a paid shop purchase (selectable only when
    /// affordable). Section headers `[ FREE ]` and `[ PAID — gold N ]`
    /// separate the two halves visually; a trailing "Skip" row lets the
    /// player exit without spending. W/S navigates only between selectable
    /// rows so the cursor never lands on a header or an unaffordable item.
    /// Replaces the old runUpgradeDraft + runShop two-menu flow which the
    /// player reported as confusing.
    void runWaveClearMenu();

    /// Open the in-game Settings menu (Feature 4): a small two-row menu
    /// where the only configurable control is the master music volume,
    /// adjustable in 10% increments via Left/Right (or A/D). The screen is
    /// driven by the renderer's drawMenu / drawMessage / pollInput primitives
    /// like every other menu in the game and exits when the player confirms
    /// the "Back" row. Volume changes are pushed live to the renderer via
    /// IRenderer::setMasterMusicVolume so the player hears the effect of
    /// each adjustment immediately.
    void runSettingsMenu();

    /// Display the game-over screen, prompt for a player name, and record the
    /// final score via ScoreBoard (R25.1).
    void showGameOver();

    /// Display the top-10 high-score leaderboard and wait for a key (R25.2).
    void showHighScores();

    /// Reset the run state for a fresh game (new wave, cleared enemies, etc.).
    /// @param seed the RNG seed to use for the new run.
    void resetRun(unsigned int seed);

    /// Give the hero its full starting loadout of activatable abilities (R21).
    ///
    /// Root-cause fix: a freshly constructed Player owns an EMPTY abilities_
    /// vector, and nothing else in the game ever populated it, so the HUD read
    /// "Abilities: (none)" and every UseAbility command fell through to the
    /// "invalid ability index" branch in TurnManager. This helper constructs one
    /// instance of each of the four concrete abilities and hands ownership to
    /// the player, in the canonical order Dash, Nova, Shield, Blink so that the
    /// 1/2/3/4 keys (which map to abilityIndex 0/1/2/3) line up with the ABILITY
    /// KEYS panel labels. It must be called AFTER the GameState (and therefore
    /// the Player) has been (re)constructed and BEFORE the playing loop begins,
    /// from both the New Game path (resetRun) and the Load path (loaded saves do
    /// not persist abilities, so they are re-granted on restore).
    void grantStartingAbilities();

    /// Push the current wave number to the renderer's boss-music hook so the
    /// streamed boss-fight track plays only on boss waves (waveNumber % 5 == 0
    /// and waveNumber >= 1, i.e. waves 5, 10, 15, ...). Called every time the
    /// active wave changes (run start, wave advance, save load) and explicitly
    /// silenced when the run ends (game over / quit to main menu) so the boss
    /// theme never bleeds into the menus. ConsoleRenderer's default no-op
    /// override means this call is harmless on non-graphical builds.
    void syncBossMusicForCurrentWave();

    /// Tell the renderer to stop the boss-fight track unconditionally. Called
    /// on game over and on quit-to-menu so the track does not keep playing
    /// underneath the menu.
    void stopBossMusic();

    // ---- Owned systems (by value) -----------------------------------------

    Config            config_;    ///< Balancing constants; immutable after construction.
    GameState         state_;     ///< The authoritative run state (map, player, counters).
    TurnManager       turns_;     ///< Turn-loop orchestrator (stateless service).
    CombatSystem      combat_;    ///< Damage / projectile / death service (stateless).
    AbilitySystem     abilities_; ///< Ability activation + cooldown ticking (stateless).
    WaveManager       waves_;     ///< Wave spawn + advancement service (stateless).
    UpgradeSystem     upgrades_;  ///< Between-wave card draft service (stateless).
    Shop              shop_;      ///< Paid post-wave shop draft (stateless, Feature 2).
    GameStateMachine  gsm_;       ///< High-level state machine.
    EventLog          log_;       ///< Linked list of recent gameplay messages (R6).
    Pathfinder        pathfinder_;///< BFS shortest-path service (stateless).

    // ---- Non-owning reference to the renderer ----------------------------

    IRenderer& renderer_; ///< The concrete renderer injected by main.cpp (non-owning).

    // ---- Ephemeral run flags ---------------------------------------------

    bool quitToMenu_; ///< Set to true by pollInput/Save to return to main menu.

    /// True while a run exists in memory that the player can return to.
    ///
    /// Set true when a fresh run is started (resetRun) or a save is loaded, and
    /// cleared only when the run actually ends (the player dies, showGameOver).
    /// Crucially it is NOT cleared when the player presses Q to leave the
    /// playing loop: that run is merely PAUSED, still fully resumable, so the
    /// main menu offers a "Resume Game" option while this flag is true.
    bool runInProgress_;

    /// Master music volume in [0.0, 1.0], adjustable from the Settings menu
    /// (Feature 4). Defaults to 1.0 (full). Persisted in memory only — does
    /// NOT survive a process restart by design (per the feature spec). The
    /// value is pushed to the renderer through IRenderer::setMasterMusicVolume
    /// so it scales every playing track.
    float masterMusicVolume_;
};

} // namespace dga
