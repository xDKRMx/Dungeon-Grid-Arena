// =============================================================================
// systems/Game.cpp
//
// Purpose:
//   Defines the Game class — the top-level orchestrator that wires every system
//   together and drives the main game loop (R2.4, R8.2, R10.1, R27.1,
//   R30.1-R30.4).
//
//   STRICT I/O RULE (R8.2): This file MUST NOT call std::cout, std::cin, _getch,
//   or any other direct terminal I/O. Every screen update goes through
//   renderer_.drawFrame(), renderer_.drawMenu(), renderer_.drawMessage(), or
//   renderer_.pollInput(). The renderer is the only place I/O happens.
//
// =============================================================================

#include "systems/Game.h"

// Extra system-layer headers needed for save/load and scoring.
#include "io/SaveManager.h"   // SaveManager::save() / ::load()
#include "io/ScoreBoard.h"    // ScoreBoard::computeScore(), ::append(), ::loadTop()

// Entity headers needed for the placement-new reset in resetRun().
// Game.cpp triggers the destructor and reconstructor of GameState, which owns
// unique_ptr<Enemy> and unique_ptr<Item> — the full types must be visible here
// so the generated destructors can call sizeof(Enemy) / sizeof(Item).
#include "entities/Enemy.h"   // full Enemy type — required by unique_ptr<Enemy> dtor.
#include "items/Item.h"       // full Item type — required by unique_ptr<Item> dtor.

// Ability headers needed so grantStartingAbilities() can construct one instance
// of each concrete ability and hand ownership to the player (R21). The full
// types are required here because std::make_unique must see their definitions.
#include "abilities/DashAbility.h"   // DashAbility   — ability index 0 (key 1).
#include "abilities/NovaAbility.h"   // NovaAbility   — ability index 1 (key 2).
#include "abilities/ShieldAbility.h" // ShieldAbility — ability index 2 (key 3).
#include "abilities/BlinkAbility.h"  // BlinkAbility  — ability index 3 (key 4).
#include "entities/Player.h"  // full Player type — addAbility() ownership transfer.

// String and utility headers.
#include <string>             // std::string — player name prompt, menu items.
#include <vector>             // std::vector  — menu options, upgrade cards.
#include <ctime>              // std::time()  — fallback seed.
#include <limits>             // std::numeric_limits — flush input.
#include <new>                // placement new — used in resetRun() reconstruction.
#include <memory>             // std::make_unique — building the player's abilities.

// Rng needed directly in resetRun to construct a fresh Rng with the given seed.
#include "core/Rng.h"

// ---------------------------------------------------------------------------
// Anonymous-namespace constants — no magic numbers in the logic (R8.5).
// ---------------------------------------------------------------------------
namespace {

    /// Number of upgrade cards presented to the player between waves (R23.1).
    constexpr int UPGRADE_DRAFT_CARDS = 3;

    /// Top-N entries shown on the high-score leaderboard (R25.2).
    constexpr int LEADERBOARD_DISPLAY_COUNT = 10;

    /// Initial enemy spawn count for wave 1.
    constexpr int BASE_ENEMY_SPAWN_COUNT = 3;

    /// How much the spawn count grows per wave.
    constexpr int ENEMY_SPAWN_GROWTH_PER_WAVE = 1;

    /// Main menu option labels. The menu returns the selected label as a
    /// string (see Game::showMainMenu), and run() compares against these names
    /// instead of fragile fixed indices — so the dynamic insertion of the
    /// "Resume Game" option at the front never shifts a switch case out of sync.
    constexpr const char* MENU_LABEL_RESUME      = "Resume Game";
    constexpr const char* MENU_LABEL_NEW_GAME    = "New Game";
    constexpr const char* MENU_LABEL_LOAD        = "Load Game";
    constexpr const char* MENU_LABEL_HIGH_SCORES = "High Scores";
    constexpr const char* MENU_LABEL_SETTINGS    = "Settings";
    constexpr const char* MENU_LABEL_QUIT        = "Quit";

    /// Upgrade menu: maximum selectable index (3 cards indexed 0-2).
    constexpr int MAX_UPGRADE_INDEX = UPGRADE_DRAFT_CARDS - 1;

} // anonymous namespace

namespace dga {

// =============================================================================
// Construction
// =============================================================================

Game::Game(const Config& config, IRenderer& renderer, unsigned int seed)
    : config_(config)
    // GameState needs a seed; if 0 is passed, substitute the current time.
    , state_(config, (seed == 0) ? static_cast<unsigned int>(std::time(nullptr)) : seed)
    // Stateless systems: all default-constructed (they hold no data).
    , turns_()
    , combat_()
    , abilities_()
    , waves_()
    , upgrades_()
    // GameStateMachine starts in MainMenu; give it the EventLog for logging.
    , gsm_(&log_)
    // EventLog capacity comes from config.
    , log_(config.eventLogDisplayCapacity())
    , pathfinder_()
    // Non-owning reference to the concrete renderer injected by main.cpp.
    , renderer_(renderer)
    , quitToMenu_(false)
    // No run exists yet at construction: the main menu opens without a
    // "Resume Game" option until the first New Game / Load creates one.
    , runInProgress_(false)
    // Master music volume defaults to full (1.0) so the renderer behaves the
    // same as before the Settings menu existed; the player adjusts it through
    // runSettingsMenu (Feature 4).
    , masterMusicVolume_(1.0f)
{}

// =============================================================================
// resetRun — prepare the GameState for a brand-new game
// =============================================================================

/// Rebuilds the GameState from scratch using the given seed (R26.4).
/// All enemies, items, and counters are cleared and wave 1 is started.
///
/// GameState is not copy/move-assignable (it owns unique_ptrs and has a
/// user-declared destructor that suppresses implicit moves). So we reset in
/// place using the public mutators and the clear operations exposed by the
/// owned collections, rather than reassigning the state_ member.
void Game::resetRun(unsigned int seed)
{
    // ---- Clear the enemy and item collections ----------------------------
    // GameState exposes its vectors by non-const reference so we can clear
    // them directly without needing a dedicated "reset" method on the class.
    state_.enemies().clear();
    state_.items().clear();

    // ---- Reset all run counters ------------------------------------------
    state_.setWaveNumber(1);
    state_.setScore(0);
    state_.incrementTurnCount(); // We can't "set" turnCount directly; work
    // around by using the fact that the only external mutation is increment.
    // The cleanest path: re-seed the rng and reset counters via the available
    // mutators. Since there is no setTurnCount(), we reconstruct the piece of
    // state that carries turn/kill counts by reinitializing those members.
    // In practice GameState exposes incrementTurnCount and incrementEnemiesKilled
    // but no set-to-zero counterpart. We therefore build a fresh GameState in a
    // temporary and use the Rng seed as the only stateful difference, relying on
    // wave / turn / score setters for everything else. The temporary is only
    // used to re-seed the Rng — everything else is set via mutators.

    // Re-seed the Rng so the new run is deterministic from the given seed.
    state_.rng() = Rng(seed);

    // Reconstruct the player at the origin (the WaveManager will place it
    // on a real Floor tile when startWave is called).
    // Because Player is not default-constructible and we cannot reassign it
    // (unique_ptr member), we reconstruct the GameState object entirely using
    // placement-style destruction + re-construction inside a helper scope.
    // The cleanest and most correct solution: use `~GameState()` + placement
    // new. We do this with a lambda scope to avoid scoping issues:
    {
        // Destroy the existing state in place and re-construct a fresh one.
        // This is safe because we immediately re-construct before any access.
        using GS = GameState;
        state_.~GS();                             // Destroy (frees unique_ptrs).
        new (&state_) GS(config_, seed);          // Reconstruct in same storage.
    }

    // ---- Reset the event log ---------------------------------------------
    // Same approach: EventLog has its copy-assign deleted (raw-pointer linked
    // list) but is destructible + constructible, so we do the same in-place
    // destruction + re-construction.
    {
        using EL = EventLog;
        log_.~EL();
        new (&log_) EL(config_.eventLogDisplayCapacity());
    }

    // Start the first wave (map generation + enemy spawn).
    const int spawnCount = BASE_ENEMY_SPAWN_COUNT;
    waves_.startWave(state_, config_, spawnCount);

    // Give the freshly built hero its four abilities (R21). The GameState (and
    // therefore the Player) was reconstructed above with an EMPTY ability list,
    // so without this call the player could never use Dash/Nova/Shield/Blink.
    grantStartingAbilities();

    // Sync boss-fight background music with the freshly-started wave. Wave 1
    // is not a boss wave, so this typically silences the track if a previous
    // run had left it on; the next boss wave will turn it back on.
    syncBossMusicForCurrentWave();

    // A fresh run now exists in memory, so the main menu should offer a
    // "Resume Game" option if the player later leaves with Q (see showMainMenu).
    runInProgress_ = true;

    // Auto-save right away so the player can return to this exact starting
    // state with Load Game even if they never press Ctrl+S during the run.
    // This is the safety net that turned a previously-broken Load Game (no
    // file on disk) into a reliable feature.
    (void)SaveManager::save(state_, config_.saveFilePath(), log_);
}

// =============================================================================
// grantStartingAbilities — give the hero one of each concrete ability (R21)
// =============================================================================

/// Populate the player's owned-ability list with the four concrete abilities in
/// the canonical order Dash, Nova, Shield, Blink. See Game.h for the full
/// rationale (this is the root-cause fix for the "Abilities: (none)" / "invalid
/// ability index" defect). The order matters: InputCommand::abilityIndex 0..3
/// (the 1/2/3/4 keys) is used by TurnManager to look up the ability by position
/// in this vector, so index 0 must be Dash, 1 Nova, 2 Shield, 3 Blink to match
/// the ABILITY KEYS panel.
void Game::grantStartingAbilities()
{
    Player& p = state_.player();

    // If abilities are somehow already present (e.g. a double-call), clear them
    // first so the hero never ends up with duplicate Dash/Nova/Shield/Blink
    // entries that would desynchronise the 1/2/3/4 key mapping.
    p.abilities().clear();

    // Construct one of each ability, reading its cooldown / tuning from Config,
    // and transfer ownership to the player. Order is significant (see above).
    p.addAbility(std::make_unique<DashAbility>(config_));
    p.addAbility(std::make_unique<NovaAbility>(config_));
    p.addAbility(std::make_unique<ShieldAbility>(config_));
    p.addAbility(std::make_unique<BlinkAbility>(config_));
}

// =============================================================================
// syncBossMusicForCurrentWave — start/stop the boss-fight track per wave
// =============================================================================

namespace {
    /// Cadence used for boss waves throughout the run: every fifth wave is a
    /// boss wave (5, 10, 15, ...). Mirrors WaveManager.cpp's local constant
    /// kBossWaveModulus, kept in sync by hand because the renderer-facing
    /// helper here lives in a different translation unit. Used by
    /// syncBossMusicForCurrentWave to decide whether to play the streamed
    /// "action" track (normal waves only) or fall back to the procedural
    /// ambient pad (boss waves).
    constexpr int BOSS_MUSIC_WAVE_MODULUS = 5;
} // anonymous namespace

/// Push the current wave number to the renderer's music hooks. Each wave
/// activates EXACTLY ONE streamed track depending on its type:
///   * Boss waves (waveNumber > 0 and waveNumber % 5 == 0) → bossMusic_
///     (Sonne / dramatic theme).
///   * Normal waves (waveNumber > 0, others)               → normalMusic_
///     (Doom-Eternal-style action theme).
/// The renderer is idempotent on repeat-same-state calls, so calling this on
/// every wave advance is cheap. ConsoleRenderer's default no-op overrides
/// make this safe on non-graphical builds.
void Game::syncBossMusicForCurrentWave()
{
    const int  wave         = state_.waveNumber();
    const bool isAnyWave    = wave > 0;
    const bool isBossWave   = isAnyWave && (wave % BOSS_MUSIC_WAVE_MODULUS) == 0;
    const bool isNormalWave = isAnyWave && !isBossWave;

    renderer_.setBossMusicActive(isBossWave);
    renderer_.setNormalMusicActive(isNormalWave);
}

/// Tell the renderer to stop both streamed tracks unconditionally. Called on
/// game over and on quit-to-menu so neither track bleeds into the menus.
void Game::stopBossMusic()
{
    renderer_.setBossMusicActive(false);
    renderer_.setNormalMusicActive(false);
}

// =============================================================================
// showMainMenu — display main menu and return the player's choice
// =============================================================================

/// Builds the main-menu option list DYNAMICALLY and returns the label of the
/// option the player selects. When a run is in progress the list is led by
/// "Resume Game"; otherwise it starts at "New Game". The cursor is driven with
/// Up/Down + Wait/Enter; Q is treated as choosing "Quit".
std::string Game::showMainMenu()
{
    // Assemble the options. The ONLY variation is whether "Resume Game" is
    // prepended; every other entry is always present and in the same order, so
    // run() can compare the returned label without caring about the offset.
    std::vector<std::string> options;
    if (runInProgress_) {
        options.push_back(MENU_LABEL_RESUME);
    }
    options.push_back(MENU_LABEL_NEW_GAME);
    options.push_back(MENU_LABEL_LOAD);
    options.push_back(MENU_LABEL_HIGH_SCORES);
    // Settings (Feature 4): a small menu that lets the player adjust the
    // master music volume. Inserted above Quit so the layout reads
    // logically (configuration first, exit last) and so the dynamic
    // "Resume Game" prepend does not shift it relative to Quit.
    options.push_back(MENU_LABEL_SETTINGS);
    options.push_back(MENU_LABEL_QUIT);

    int selected = 0; // Start with the first (top) option highlighted.

    for (;;) {
        // Draw the menu header + options.
        renderer_.drawMessage("=== Dungeon Grid Arena ===");
        renderer_.drawMenu(options, selected);
        renderer_.drawMessage("Use W/S (or arrow keys) to navigate, Space/Enter to select.");

        // Poll for a key and handle navigation.
        InputCommand cmd = renderer_.pollInput();

        switch (cmd.type) {
            case InputCommand::Type::Move:
                // Up/North → move cursor up.
                if (cmd.direction.y < 0) {
                    selected = (selected - 1 + static_cast<int>(options.size()))
                                % static_cast<int>(options.size());
                }
                // Down/South → move cursor down.
                if (cmd.direction.y > 0) {
                    selected = (selected + 1) % static_cast<int>(options.size());
                }
                break;

            case InputCommand::Type::Wait:
                // Wait / Space / Enter → confirm selection; hand back its label.
                return options[static_cast<std::size_t>(selected)];

            case InputCommand::Type::Quit:
                // Q key → treat as the "Quit" option directly.
                return MENU_LABEL_QUIT;

            default:
                break; // Ignore all other keys in the menu.
        }
    }
}

// =============================================================================
// runWaveClearMenu — combined free upgrade + paid shop menu (Feature 2/3)
// =============================================================================

/// Present the wave-clear menu as a SINGLE panel that lists, in order:
///   * an "[ FREE ]" decorative section header.
///   * the three drafted UpgradeCards (always selectable).
///   * a "[ PAID — gold N ]" decorative section header showing the wallet.
///   * the three drafted ShopItems (selectable only when affordable; rows
///     the player cannot afford are postfixed " [CAN'T AFFORD]" and the
///     navigator skips over them, so Space simply cannot reach them).
///   * a final "Skip" entry the player picks when they do not want to spend.
///
/// Replaces the previous two-menu flow (runUpgradeDraft followed by runShop)
/// which the player reported as confusing because it presented two separate
/// panels back-to-back. The combined panel makes the FREE/PAID split obvious
/// at a glance and confines the entire post-wave decision to one screen.
///
/// W/S (or arrow keys) navigates ONLY between selectable rows — section
/// headers and unaffordable shop entries are skipped automatically so the
/// cursor never lands on a row the player cannot act on. Space confirms.
/// Q is treated as Skip so the player can always exit cleanly.
void Game::runWaveClearMenu()
{
    // Transition the state machine to the upgrade phase (the gameplay phase
    // tag for "between waves") so logging / save-during-draft branches stay
    // accurate.
    gsm_.transition(GameStateId::UpgradeDraft);

    // ---- Draw the offers up front ---------------------------------------
    //
    // Both pools draft from the same deterministic run Rng so the same seed
    // reproduces the same wave-clear hand. Drafting once here and reusing the
    // results in every redraw means the displayed catalogue cannot drift if
    // the player navigates the menu repeatedly.
    std::vector<UpgradeCard> upgradeCards = upgrades_.draftCards(state_.rng());
    std::vector<ShopItem>    shopItems    = shop_.draftItems(state_.rng());

    // ---- Build a per-row metadata table ---------------------------------
    //
    // Each row in the visible menu is one of:
    //   * a decorative SECTION HEADER (not selectable),
    //   * a FREE upgrade row (always selectable),
    //   * a PAID shop row (selectable only when affordable),
    //   * the SKIP sentinel at the bottom.
    enum class RowKind { Header, FreeUpgrade, PaidShop, Skip };
    struct Row {
        RowKind     kind;
        std::size_t index;     // index into upgradeCards / shopItems (unused for Header/Skip).
        bool        selectable;
    };
    std::vector<Row>          rowsMeta;
    std::vector<std::string>  options;     // strings drawMenu actually renders.

    // FREE section header.
    rowsMeta.push_back({ RowKind::Header, 0, false });
    options.push_back("=== [ FREE ] ===");

    // FREE rows.
    for (std::size_t i = 0; i < upgradeCards.size(); ++i) {
        rowsMeta.push_back({ RowKind::FreeUpgrade, i, true });
        options.push_back("  " + upgradeCards[i].name + ": " +
                          upgradeCards[i].description);
    }

    // PAID section header — wallet shown live so the player knows what they
    // can spend.
    rowsMeta.push_back({ RowKind::Header, 0, false });
    options.push_back("=== [ PAID — gold " +
                      std::to_string(state_.gold()) + " ] ===");

    // PAID rows. A row is selectable only when the player can afford it; the
    // [CAN'T AFFORD] postfix is decorative so the player still sees what they
    // would have bought.
    for (std::size_t i = 0; i < shopItems.size(); ++i) {
        const ShopItem& it = shopItems[i];
        const bool affordable = (state_.gold() >= it.cost);
        std::string row = "  " + it.name + " (" +
                          std::to_string(it.cost) + "g) - " +
                          it.description;
        if (!affordable) {
            row += "  [CAN'T AFFORD]";
        }
        rowsMeta.push_back({ RowKind::PaidShop, i, affordable });
        options.push_back(row);
    }

    // Skip sentinel — always selectable.
    rowsMeta.push_back({ RowKind::Skip, 0, true });
    options.push_back("Skip");

    // ---- Pick an initial selectable row ---------------------------------
    int selected = 0;
    for (int i = 0; i < static_cast<int>(rowsMeta.size()); ++i) {
        if (rowsMeta[static_cast<std::size_t>(i)].selectable) {
            selected = i;
            break;
        }
    }

    // Navigation helper: move the selection to the next selectable row in
    // the requested direction (+1 down, -1 up), wrapping past unselectable
    // headers and unaffordable rows. Returns the new index. Falls back to
    // the original index if no other selectable row exists (defensive — the
    // Skip row is always selectable, so the table always has at least one).
    auto findNextSelectable = [&](int current, int delta) -> int {
        const int total = static_cast<int>(rowsMeta.size());
        int idx = current;
        for (int step = 0; step < total; ++step) {
            idx = (idx + delta + total) % total;
            if (rowsMeta[static_cast<std::size_t>(idx)].selectable) {
                return idx;
            }
        }
        return current; // No other selectable row found.
    };

    for (;;) {
        renderer_.drawMessage("=== Wave Cleared! Choose an Upgrade or Shop Item ===");
        renderer_.drawMenu(options, selected);
        renderer_.drawMessage("W/S to navigate, Space to confirm, Q to skip.");

        InputCommand cmd = renderer_.pollInput();

        switch (cmd.type) {
            case InputCommand::Type::Move:
                if (cmd.direction.y < 0) {
                    selected = findNextSelectable(selected, -1);
                }
                if (cmd.direction.y > 0) {
                    selected = findNextSelectable(selected, +1);
                }
                break;

            case InputCommand::Type::Wait: {
                // Confirm — apply whatever the row points at.
                const Row& row = rowsMeta[static_cast<std::size_t>(selected)];
                if (!row.selectable) {
                    // Defensive: the navigation skipper should already prevent
                    // landing on a non-selectable row, but if it ever happens
                    // (e.g. via a future input path) just stay on the menu.
                    break;
                }
                if (row.kind == RowKind::FreeUpgrade) {
                    const UpgradeCard& card = upgradeCards[row.index];
                    upgrades_.apply(card, state_.player(), state_, config_);
                    log_.append("Upgrade applied: " + card.name);
                    gsm_.transition(GameStateId::Playing);
                    return;
                }
                if (row.kind == RowKind::PaidShop) {
                    const ShopItem& chosen = shopItems[row.index];
                    // Affordability was already enforced by the row's
                    // selectable flag, so the player cannot reach this branch
                    // with insufficient gold. We still spendGold which clamps
                    // at 0 so a stray race could only ever zero the wallet.
                    state_.spendGold(chosen.cost);
                    shop_.purchase(chosen, state_.player(), state_);
                    log_.append("Shop: bought " + chosen.name + " for " +
                                std::to_string(chosen.cost) + "g.");
                    gsm_.transition(GameStateId::Playing);
                    return;
                }
                // RowKind::Skip falls through to the same exit path.
                log_.append("Wave-clear menu: skipped.");
                gsm_.transition(GameStateId::Playing);
                return;
            }

            case InputCommand::Type::Quit:
                // Q is treated as Skip so the player can always exit cleanly
                // (mirrors the other menus).
                log_.append("Wave-clear menu: skipped.");
                gsm_.transition(GameStateId::Playing);
                return;

            default:
                break; // Ignore everything else while the menu is open.
        }
    }
}

// =============================================================================
// runSettingsMenu — main-menu Settings screen (Feature 4)
// =============================================================================

/// Display the Settings screen and let the player adjust the master music
/// volume in 10% increments. The screen has TWO rows:
///   * "Music: NN%" — the current volume; A/D (or LEFT/RIGHT) decrement /
///     increment by 10%, clamped to [0%, 100%]. Each change is pushed live
///     to the renderer via setMasterMusicVolume so the player hears the
///     adjustment immediately.
///   * "Back"        — Space/Enter on this row exits.
///
/// Volume is persisted only in memory (Game::masterMusicVolume_); a process
/// restart resets it to 1.0 by design.
void Game::runSettingsMenu()
{
    // Step size for the volume control (10% per A/D press, per the spec).
    constexpr float VOLUME_STEP = 0.10f;
    // Indices into the option list. Musical row first, Back row second.
    constexpr int ROW_MUSIC = 0;
    constexpr int ROW_BACK  = 1;
    constexpr int ROW_COUNT = 2;

    int selected = 0;

    for (;;) {
        // Build the labels from the live state. Volume is rendered as an
        // integer percentage to match the spec's "Music: 50%" example exactly.
        const int volumePct = static_cast<int>(masterMusicVolume_ * 100.0f + 0.5f);
        std::vector<std::string> options;
        options.reserve(ROW_COUNT);
        options.push_back("Music: " + std::to_string(volumePct) + "%");
        options.push_back("Back");

        renderer_.drawMessage("=== Settings ===");
        renderer_.drawMenu(options, selected);
        renderer_.drawMessage(
            "W/S to navigate.  A/D (or LEFT/RIGHT) to adjust music volume.\n"
            "Space/Enter on Back to return."
        );

        InputCommand cmd = renderer_.pollInput();

        switch (cmd.type) {
            case InputCommand::Type::Move: {
                // Up/Down navigate between rows; Left/Right adjust the
                // currently selected row when it has a numeric value.
                if (cmd.direction.y < 0) {
                    selected = (selected - 1 + ROW_COUNT) % ROW_COUNT;
                }
                if (cmd.direction.y > 0) {
                    selected = (selected + 1) % ROW_COUNT;
                }
                if (selected == ROW_MUSIC && cmd.direction.x != 0) {
                    // A/Left decrements by VOLUME_STEP, D/Right increments.
                    if (cmd.direction.x < 0) {
                        masterMusicVolume_ -= VOLUME_STEP;
                    } else {
                        masterMusicVolume_ += VOLUME_STEP;
                    }
                    if (masterMusicVolume_ < 0.0f) { masterMusicVolume_ = 0.0f; }
                    if (masterMusicVolume_ > 1.0f) { masterMusicVolume_ = 1.0f; }
                    // Push the new master scalar to the renderer immediately
                    // so the change is audible right away (no "press apply"
                    // step, no wait until the next wave).
                    renderer_.setMasterMusicVolume(masterMusicVolume_);
                }
                break;
            }

            case InputCommand::Type::Wait:
                // Confirm: only the Back row exits. Pressing Space/Enter on
                // the Music row is harmless and stays on the menu.
                if (selected == ROW_BACK) {
                    return;
                }
                break;

            case InputCommand::Type::Quit:
                // Q is treated as Back so the player can always escape.
                return;

            default:
                break;
        }
    }
}

// =============================================================================
// showGameOver — record score and display the game-over screen (R25.1)
// =============================================================================
//
// Bug-fix history (Bug 1, "Game Over screen freezes — Space doesn't continue"):
//
//   The previous implementation issued TWO consecutive pollInput wait loops:
//     1. The first showed "GAME OVER ... Press Space to continue." and waited
//        for Wait/Quit.
//     2. The second showed "Enter player number (1-9) for your initials, or
//        Space for 'Player'" and waited for UseAbility/Wait/Quit.
//
//   In the raylib build the first poll returned on KEY_SPACE and set
//   pendingReset_ = true. The next drawMessage call cleared the cached
//   message overlay (correct) and stacked the new "Enter player number"
//   prompt on top of the still-cached game frame; the second pollInput then
//   blocked. To the user this looked exactly like a freeze: the GAME OVER
//   message disappeared, the dead playing field was redrawn underneath the
//   second prompt, and a single press of Space appeared to do nothing —
//   because the player did not realise they had moved on to a different
//   prompt that ALSO accepted Space (the second loop did consume it, but in
//   the heat of "the game just hung" the second prompt's brief flash was
//   missed and further Space presses were eaten by an already-returned poll).
//
//   The two-step "type your initials via the ability keys" UI was awkward in
//   the first place: the user just wants to acknowledge death and return to
//   the menu. The fix is therefore to delete the second poll loop entirely
//   and record the score under a default "Player" name, leaving a single,
//   obvious "press a key to return to menu" interaction. A future text-input
//   helper could reinstate name entry, but that is a feature, not the fix
//   for this bug.
//
void Game::showGameOver()
{
    gsm_.transition(GameStateId::GameOver);

    // This run has ended: clear the in-progress flag so the main menu drops the
    // "Resume Game" option — a dead run must not be resumable (it would resume
    // into a player with 0 HP). A new run can only begin via "New Game"/"Load".
    runInProgress_ = false;

    // Compute the final score values once so the message and the persisted
    // leaderboard entry agree.
    const int finalScore = state_.score();
    const int wave       = state_.waveNumber();
    const int kills      = state_.enemiesKilled();

    renderer_.drawMessage(
        "GAME OVER\n"
        " Final Score : " + std::to_string(finalScore) + "\n"
        " Wave reached: " + std::to_string(wave) + "\n"
        " Enemies killed: " + std::to_string(kills) + "\n"
        "\n Press Space or Q to return to menu."
    );

    // Wait for the player to acknowledge the death screen with ANY of the
    // obvious "I am done" keys. Using waitForAnyKey instead of pollInput
    // prevents the (previously reported) freeze where pressing F or 1/2/3/4
    // on the dead screen would route the user into the fire / dash direction
    // prompt machinery, which has no game state to act on and looked like a
    // hang. waitForAnyKey is narrow on purpose: only Space, Enter, Q and ESC
    // (or the window close button) end it — every other keystroke is ignored.
    renderer_.waitForAnyKey();

    // Record the score with a default name so the leaderboard still gets the
    // run (R25.1). No interactive name prompt — see the bug-fix comment above.
    ScoreEntry entry;
    entry.playerName = "Player";
    entry.score      = finalScore;
    entry.wave       = wave;
    entry.kills      = kills;
    ScoreBoard::append(entry, config_.highScoreFilePath());

    // Return the state machine to the main menu so run() loops back to it.
    gsm_.transition(GameStateId::MainMenu);
}

// =============================================================================
// showHighScores — display the top-10 leaderboard (R25.2)
// =============================================================================

void Game::showHighScores()
{
    const auto entries = ScoreBoard::loadTop(LEADERBOARD_DISPLAY_COUNT,
                                             config_.highScoreFilePath(),
                                             log_);

    std::string display = "=== High Scores (Top " +
                          std::to_string(LEADERBOARD_DISPLAY_COUNT) + ") ===\n";

    if (entries.empty()) {
        display += " (No scores recorded yet)\n";
    } else {
        int rank = 1;
        for (const auto& e : entries) {
            display += " " + std::to_string(rank) + ". "
                     + e.playerName
                     + "  Score: " + std::to_string(e.score)
                     + "  Wave: "  + std::to_string(e.wave)
                     + "  Kills: " + std::to_string(e.kills)
                     + "\n";
            ++rank;
        }
    }
    display += "\n Press any key to return.";

    renderer_.drawMessage(display);

    // Wait for any key to return to the main menu. Using waitForAnyKey
    // (instead of pollInput) keeps the screen from accidentally routing
    // game keys (F / 1-4) into a fire / dash direction prompt the way the
    // dead screen used to.
    renderer_.waitForAnyKey();
}

// =============================================================================
// playingLoop — the inner turn-based game loop
// =============================================================================

/// Drives the playing loop: draw → poll → process → check outcomes.
/// Exits when the player dies, quits to the main menu, or quits the game.
void Game::playingLoop()
{
    gsm_.transition(GameStateId::Playing);
    quitToMenu_ = false;

    for (;;) {
        // ---- Draw the current game state ----------------------------------
        renderer_.drawFrame(state_, config_, log_);

        // ---- Poll one player action --------------------------------------
        InputCommand cmd = renderer_.pollInput();

        // ---- Handle Save --------------------------------------------------
        if (cmd.type == InputCommand::Type::Save) {
            const bool ok = SaveManager::save(state_, config_.saveFilePath(),
                                              log_);
            renderer_.drawMessage(
                ok ? std::string("Game saved to ") + config_.saveFilePath() + "."
                   : std::string("SAVE FAILED — see event log "
                                 "(could not write ") + config_.saveFilePath() + ")."
            );
            continue; // Re-draw and wait for the next real action.
        }

        // ---- Handle Quit (return to main menu) ---------------------------
        if (cmd.type == InputCommand::Type::Quit) {
            quitToMenu_ = true;
            // Make sure the boss track does not keep playing under the menu.
            stopBossMusic();
            // Bring the high-level state machine back to MainMenu so the next
            // entry into the playing loop (Resume Game / New Game / Load) can
            // legally transition Playing again. Without this the gsm stays
            // pinned at Playing across the menu and the next playingLoop call
            // produces a "GameStateMachine: illegal transition" warning.
            gsm_.transition(GameStateId::MainMenu);
            // Auto-save the run on quit-to-menu so the player never loses
            // their progress just because they forgot to press Ctrl+S. The
            // result is reported in the event log; no on-screen message is
            // shown because the menu is taking over the screen anyway.
            (void)SaveManager::save(state_, config_.saveFilePath(), log_);
            return;
        }

        // ---- Process the turn --------------------------------------------
        TurnResult result = turns_.processTurn(state_, cmd, combat_,
                                               abilities_, pathfinder_,
                                               config_, log_);

        // ---- Forward Fire visual effect to the renderer ------------------
        // If the player fired this turn, hand the trail / impact data to the
        // renderer so it can draw a transient tracer + impact flash for the
        // next ~half-second of frames. ConsoleRenderer's default no-op
        // implementation simply ignores the call.
        if (result.fireEffect.fired) {
            renderer_.showFireEffect(result.fireEffect.trail,
                                     result.fireEffect.impact,
                                     result.fireEffect.hit);
        }

        // ---- Forward Nova ultimate visual effect to the renderer ---------
        // If the player unleashed Nova this turn, ask the renderer to draw an
        // expanding shockwave centred on the blast. This fires even when the
        // blast caught no enemies, so pressing 2 with a full Charge_Meter is
        // ALWAYS visibly rewarded. ConsoleRenderer's default no-op ignores it.
        if (result.novaFired) {
            renderer_.showNovaEffect(result.novaCenter, result.novaRadius);
        }

        // ---- Forward enemy-attack visual effects to the renderer ---------
        // For every enemy that struck the player this turn, ask the renderer
        // to draw a cue (a ranged beam from the attacker's tile, or a melee
        // slash on the player's tile) so the player can SEE incoming attacks
        // and where they came from. ConsoleRenderer's default no-op ignores
        // these. The player's current tile is the common target / flash cell.
        for (const EnemyAttackInfo& attack : result.enemyAttacks) {
            renderer_.showEnemyAttackEffect(attack.enemyPos,
                                            state_.player().position(),
                                            attack.ranged,
                                            attack.hit);
        }

        // ---- Forward player-melee visual effect to the renderer (Fix 4) --
        // When the player walked into an enemy (melee attack), ask the renderer
        // to draw a brief red slash/X on the target cell so there is clear
        // visual feedback for the melee hit. ConsoleRenderer's default no-op
        // ignores this call.
        if (result.playerMeleed) {
            renderer_.showPlayerMeleeEffect(result.playerMeleeTarget);
        }

        // ---- Forward audio cues to the renderer --------------------------
        //
        // The graphical RaylibRenderer synthesises every sound effect at
        // startup (no .wav files on disk) and plays them through these hooks.
        // ConsoleRenderer's default no-ops ignore each call so the console
        // build stays free of any audio code.

        // Pickup chime: a small ascending two-note sound when the player
        // walked onto an item tile and collected the pickup this turn.
        if (result.itemPickedUp) {
            renderer_.showPickupSound();
        }

        // Per-ability activation sound (whoosh / shimmer / zap / boom).
        // Fired only when the ability actually went off (TurnManager sets the
        // flag false for rejected activations such as on-cooldown casts).
        if (result.abilityActivated) {
            renderer_.showAbilitySound(result.abilityKind);
        }

        // Save/Quit are handled above before processTurn; the result flags
        // should not duplicate them, but we guard just in case.
        if (result.quitRequested) {
            quitToMenu_ = true;
            return;
        }
        if (result.saveRequested) {
            const bool ok = SaveManager::save(state_, config_.saveFilePath(),
                                              log_);
            renderer_.drawMessage(
                ok ? std::string("Game saved to ") + config_.saveFilePath() + "."
                   : std::string("SAVE FAILED — see event log "
                                 "(could not write ") + config_.saveFilePath() + ")."
            );
            continue;
        }

        // ---- Player death → game over (R10.6) ----------------------------
        if (result.playerDied) {
            // Audio sting (descending minor chord) before any visual
            // change so the death lands with sound. ConsoleRenderer ignores it.
            renderer_.showGameOverSound();
            // Silence the boss track so the death sting / game-over screen are
            // not drowned out by it.
            stopBossMusic();
            // Draw the final frame so the player can see the killing blow.
            renderer_.drawFrame(state_, config_, log_);
            showGameOver();
            return; // Return to main menu loop.
        }

        // ---- Wave cleared → upgrade draft then next wave (R17.2, R23) ----
        if (result.waveCleared) {
            // Triumphant ascending major chord BEFORE the upgrade draft so the
            // player feels the beat-the-wave reward as a sound, not just text.
            renderer_.showWaveClearedSound();
            // Update the score to reflect the completed wave (R24.1).
            const int newScore = ScoreBoard::computeScore(
                state_.waveNumber(),
                state_.enemiesKilled(),
                0 /* treasure accumulated incrementally via addScore */
            );
            // Preserve any treasure bonuses already added to state_.score();
            // recompute only the wave+kill component and add it if the formula
            // result is larger (safe conservative approach).
            if (newScore > state_.score()) {
                state_.setScore(newScore);
            }

            // Show the combined wave-clear menu (free upgrade + paid shop)
            // and let the player pick exactly one row. Replaces the previous
            // runUpgradeDraft + runShop sequence so the post-wave decision is
            // a SINGLE panel rather than two separate ones.
            runWaveClearMenu();

            // Advance the wave number and start the next wave (R17.2, R17.4).
            const int nextSpawnCount = BASE_ENEMY_SPAWN_COUNT
                + (state_.waveNumber() * ENEMY_SPAWN_GROWTH_PER_WAVE);
            waves_.advance(state_, config_);
            (void)nextSpawnCount; // advance() computes its own spawn count.

            // Start (or stop) the boss-fight track to match the new wave. The
            // renderer is idempotent on repeat-same-state calls, so we
            // explicitly stop both streams first and then re-sync — that
            // guarantees the track restarts from the top of the file even if
            // the previous wave used the SAME track type (normal → normal),
            // giving the player a fresh musical hit at every wave boundary.
            stopBossMusic();
            syncBossMusicForCurrentWave();

            log_.append("Wave " + std::to_string(state_.waveNumber()) + " begins!");

            // Auto-save the run at the start of every fresh wave so the
            // player can always recover their progress with Load Game even
            // if they forget to press Ctrl+S. Failures are logged but never
            // surfaced on-screen here — the wave already shows its own
            // banner and we do not want to interrupt the player's flow.
            (void)SaveManager::save(state_, config_.saveFilePath(), log_);

            continue; // Re-enter the loop for the new wave.
        }

        // ---- Normal turn: just loop back and redraw ----------------------
        // (turnConsumed may be false for a blocked move; the loop simply asks
        //  for another input next iteration which is the correct R11.2 behaviour.)
    }
}

// =============================================================================
// run — main game loop
// =============================================================================

/// Drives the full application lifecycle until the player quits.
void Game::run()
{
    for (;;) {
        // Show the main menu and get the player's chosen option label. Using
        // the label string (rather than an index) keeps this dispatch correct
        // whether or not the dynamic "Resume Game" entry is present.
        const std::string choice = showMainMenu();

        if (choice == MENU_LABEL_RESUME) {
            // ---- Resume Game ---------------------------------------------
            // The previous run is still intact in state_; just re-enter the
            // playing loop. playingLoop() transitions the state machine back to
            // Playing (MainMenu→Playing is a legal transition), so the paused
            // run picks up exactly where Q left it. NO reset is performed.
            if (runInProgress_) {
                // Restore the boss-fight track if the paused run was on a
                // boss wave (it was stopped when the player quit-to-menu).
                syncBossMusicForCurrentWave();
                playingLoop();
            }

        } else if (choice == MENU_LABEL_NEW_GAME) {
            // ---- New Game ------------------------------------------------
            // Seed from current time for a unique run, build fresh state, play.
            const unsigned int seed =
                static_cast<unsigned int>(std::time(nullptr));
            resetRun(seed);
            playingLoop();
            // After playing (game over or quit-to-menu) fall back to the menu.

        } else if (choice == MENU_LABEL_LOAD) {
            // ---- Load Game -----------------------------------------------
            // Attempt to restore a saved run (R26.2, R26.3).
            const bool loaded =
                SaveManager::load(config_.saveFilePath(), state_, log_);
            if (loaded) {
                // Saved games do not persist the player's abilities, so
                // re-grant the full Dash/Nova/Shield/Blink loadout before
                // resuming — otherwise a loaded run would have no usable
                // abilities (same defect resetRun fixes for new games).
                grantStartingAbilities();
                // A run now exists in memory: mark it resumable so a later Q
                // exit still offers "Resume Game".
                runInProgress_ = true;
                // Sync boss-fight music with the loaded wave number — if the
                // save was made on a boss wave, the track starts immediately.
                syncBossMusicForCurrentWave();
                renderer_.drawMessage("Save file loaded — resuming.");
                gsm_.transition(GameStateId::Playing);
                playingLoop();
            } else {
                // SaveManager already appended an error message to the log;
                // show it via drawMessage so the player sees it immediately.
                renderer_.drawMessage(
                    "Could not load save file.\n"
                    "Check the event log for details.\n"
                    "Press any key to return."
                );
                renderer_.waitForAnyKey();
            }

        } else if (choice == MENU_LABEL_HIGH_SCORES) {
            // ---- High Scores ---------------------------------------------
            showHighScores();

        } else if (choice == MENU_LABEL_SETTINGS) {
            // ---- Settings ------------------------------------------------
            // Open the small in-memory settings menu (Feature 4). Returns
            // when the player picks "Back"; the master volume is already
            // pushed to the renderer live during navigation.
            runSettingsMenu();

        } else if (choice == MENU_LABEL_QUIT) {
            // ---- Quit ----------------------------------------------------
            // Clean exit — fall out of the loop.
            return;
        }
        // Any unrecognised label is ignored and the menu simply redisplays.
    }
}

} // namespace dga
