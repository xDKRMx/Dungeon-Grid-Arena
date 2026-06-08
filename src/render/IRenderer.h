// =============================================================================
// render/IRenderer.h
//
// Purpose:
//   IRenderer is the pure abstract interface that decouples the game core from
//   any concrete drawing or input technology (R28.1, R4.5, R8.2).
//
//   The game orchestrator (Game) calls exactly these four virtual methods:
//
//     * drawFrame()   — render one complete screen: the dungeon map, the HUD
//                       bar, and the event log (R28.2).
//     * pollInput()   — block until the player presses a key and convert the
//                       raw input into a renderer-independent InputCommand so
//                       the turn loop never sees raw keycodes (R28.2).
//     * drawMenu()    — render a vertically stacked selection menu with the
//                       highlighted entry marked (R28.2).
//     * drawMessage() — show a full-screen or overlay message to the player
//                       (R28.2); used for "wave cleared", "game over", etc.
//
//   The game core never includes any rendering header directly; it only depends
//   on this interface and on the types of the parameters it passes:
//     * GameState  (systems/GameState.h)   — the authoritative run state.
//     * Config     (core/Config.h)         — the balancing configuration.
//     * EventLog   (systems/EventLog.h)    — the linked list of recent messages.
//     * InputCommand (systems/TurnManager.h)— the abstract player command.
//
// Architecture note (R8.2):
//   `core`, `world`, `entities`, `combat`, `items`, `abilities`, `systems`, and
//   `io` never include this header (or any render header). Only `render` and
//   `main` know that a renderer exists.
//
// Why header-only:
//   IRenderer is a pure interface with no data members or implemented methods;
//   there is nothing to put in a .cpp.  Every concrete method lives in a
//   subclass .cpp (ConsoleRenderer.cpp, etc.).
//
// Requirements: 28.1, 28.2, 4.5
//
// Layer: render (depends on systems, core; NOT depended upon by logic layers).
// =============================================================================
#pragma once

#include <string>  // std::string  — parameter type for drawMessage / drawMenu.
#include <vector>  // std::vector  — parameter type for drawMenu options.

// Pull in the types whose values cross the interface boundary.
// IRenderer.h is the ONE place in the render layer that may include systems and
// core headers; concrete renderer headers only need to include IRenderer.h.
#include "core/Config.h"           // Config  — passed to drawFrame for HUD values.
#include "core/Enums.h"            // AbilityKind — used by showAbilitySound().
#include "systems/EventLog.h"      // EventLog — passed to drawFrame for log display.
#include "systems/GameState.h"     // GameState — the full run state to render.
#include "systems/TurnManager.h"   // InputCommand — the abstract player command.

namespace dga {

// =============================================================================
// IRenderer — pure abstract renderer interface (R28.1)
// =============================================================================

/// Pure abstract base class for all renderer implementations (R28.1, R4.5).
///
/// Any class that draws game content and reads player input must derive from
/// IRenderer and override all four pure virtual methods. The game core stores
/// an IRenderer& and calls through it, so the correct concrete behaviour
/// executes via virtual dispatch without the core ever knowing which renderer
/// is active (R8.2, R4.5).
///
/// Virtual destructor (R4.7): IRenderer objects are deleted through base
/// pointers (e.g. the unique_ptr in main.cpp holds an IRenderer*), so the
/// destructor must be virtual to guarantee that the concrete subclass destructor
/// runs correctly and all its resources are freed.
class IRenderer {
public:
    // ---- Construction / destruction ---------------------------------------

    /// Virtual destructor (R4.7).
    ///
    /// Required because IRenderer objects are owned and destroyed through a
    /// base pointer (e.g. std::unique_ptr<IRenderer>). Without a virtual
    /// destructor, deleting through a base pointer calls only the base dtor,
    /// skipping the concrete subclass's dtor and leaking its resources.
    virtual ~IRenderer() = default;

    // ---- Core rendering operations ----------------------------------------

    /// Draw the complete game screen for one frame (R28.2).
    ///
    /// Implementations must:
    ///   1. Clear the previous frame.
    ///   2. Render the GridMap as a 2-D field of characters (floor '.', wall
    ///      '#', player '@', enemies by their glyph(), items by their glyph()).
    ///   3. Render the HUD bar below (or beside) the map showing the player's
    ///      HP, ammo, the current wave number, the accumulated score, the turn
    ///      count, shield status, and ability cooldowns.
    ///   4. Render the event log (the most recent messages from `log`).
    ///
    /// Called once per completed turn by the game loop after all phases resolve.
    ///
    /// @param state  the authoritative run state (map, player, enemies, items,
    ///               counters) — read-only; the renderer MUST NOT mutate it.
    /// @param config the balancing configuration (HUD capacity, file paths,
    ///               etc.) — read-only.
    /// @param log    the event log whose most recent messages are displayed in
    ///               the HUD area (R29.3) — read-only.
    virtual void drawFrame(const GameState& state,
                           const Config&    config,
                           const EventLog&  log) = 0;

    /// Block until the player presses a meaningful key and return the command
    /// (R28.2).
    ///
    /// Implementations convert raw keystrokes to abstract InputCommand values
    /// so the turn loop is entirely decoupled from the physical key codes:
    ///   - W / Up Arrow   → Move North ({0,-1})
    ///   - S / Down Arrow → Move South ({0,+1})  [NOTE: 'S' is overloaded:
    ///                       if the game is Playing it means Move South; if the
    ///                       game is Paused the menu uses it as Save — the Game
    ///                       caller distinguishes these contexts and asks for
    ///                       the right poll method at the right time].
    ///   - A / Left Arrow → Move West  ({-1,0})
    ///   - D / Right Arrow→ Move East  ({+1,0})
    ///   - F              → Fire (direction must be asked separately or
    ///                       a follow-up keypress selects direction).
    ///   - 1/2/3/4        → UseAbility at index 0/1/2/3.
    ///   - Q              → Quit.
    ///   - Space / Enter  → Wait.
    ///
    /// The function blocks until a recognized key is pressed; unrecognized keys
    /// are discarded and the call loops until a valid key arrives.
    ///
    /// @return the InputCommand corresponding to the player's keypress.
    virtual InputCommand pollInput() = 0;

    /// Draw a vertical selection menu (R28.2).
    ///
    /// Implementations render each option string on its own line; the entry at
    /// `selectedIndex` is prefixed with "> " (or otherwise highlighted) to show
    /// the current cursor position.  All other entries are prefixed with "  ".
    ///
    /// Callers drive the navigation loop: they call drawMenu() every time the
    /// selection changes, then call pollInput() (or a dedicated menu-input
    /// method) to move the cursor or confirm the selection.
    ///
    /// @param options       the list of option strings to display.
    /// @param selectedIndex the 0-based index of the currently selected option;
    ///                       must be in [0, options.size()); if out of range the
    ///                       implementation may clamp or skip the highlight.
    virtual void drawMenu(const std::vector<std::string>& options,
                          int selectedIndex) = 0;

    /// Display a full-screen or overlay message to the player (R28.2).
    ///
    /// Used for one-shot announcements: "Wave cleared!", "You died.", "Saved.",
    /// "Loading failed — no save file found.", etc. The implementation decides
    /// the exact formatting; it must at minimum show the `message` string in a
    /// readable way and then return to the caller (it does NOT wait for input).
    ///
    /// @param message the text to display.  May contain embedded newline
    ///                characters; implementations should honour them.
    virtual void drawMessage(const std::string& message) = 0;

    /// Show a transient projectile effect for one Fire action.
    ///
    /// Called by Game.cpp immediately after a Fire turn resolves, so a graphical
    /// renderer can draw a tracer line through `trailCells` and a brief flash on
    /// `impactCell` (`hit` colours the flash differently for enemy hits versus
    /// wall / out-of-range misses). The call is purely advisory: this method
    /// has a default empty body so renderers without a need for a visual fire
    /// effect (the ConsoleRenderer in particular) can simply ignore it without
    /// having to override.
    ///
    /// @param trailCells cells the projectile traveled across, in order; the
    ///                   player's own cell is the first element. May be empty
    ///                   when the shot was rejected (e.g. out of ammo).
    /// @param impactCell the cell where the projectile stopped (last element of
    ///                   trailCells if non-empty); duplicated as a convenience
    ///                   so renderers do not have to read trailCells.back().
    /// @param hit        true when the projectile struck an enemy; false for a
    ///                   wall / out-of-bounds / blocked-LOS / range miss.
    virtual void showFireEffect(const std::vector<Vec2>& trailCells,
                                const Vec2& impactCell,
                                bool hit) {
        // Default no-op: console renderers and tests can ignore fire effects;
        // the parameters are silenced so the empty body never warns.
        (void)trailCells;
        (void)impactCell;
        (void)hit;
    }

    /// Show a transient effect for ONE enemy attack on the player.
    ///
    /// Called by Game.cpp once per enemy attack recorded during a turn so a
    /// graphical renderer can make incoming attacks legible: a ranged attacker
    /// (Rook / Bishop / Queen / Boss) draws a coloured beam from `enemyPos` to
    /// `playerPos`, while a melee attacker draws a red slash / flash on the
    /// player's own tile. Like showFireEffect this is purely advisory and has a
    /// default empty body so renderers that do not need it (the ConsoleRenderer)
    /// can ignore the call without overriding.
    ///
    /// May be called several times for a single turn (one per attacking enemy);
    /// implementations are encouraged to accumulate the effects so multiple
    /// simultaneous attacks are all shown rather than only the last.
    ///
    /// @param enemyPos   the tile the attacking enemy struck from (beam origin).
    /// @param playerPos  the tile the player occupies (beam target / flash cell).
    /// @param ranged     true for a distance attack (draw a beam), false for an
    ///                   adjacent melee hit (draw a slash flash on the player).
    /// @param hit        true when the attack dealt damage; false for a ranged
    ///                   MISS (Feature 3). A graphical renderer can draw a
    ///                   bright beam on a hit and a dim/orange beam on a miss so
    ///                   the player can read whether the shot connected. Melee
    ///                   attacks always pass hit == true.
    virtual void showEnemyAttackEffect(const Vec2& enemyPos,
                                       const Vec2& playerPos,
                                       bool ranged,
                                       bool hit) {
        // Default no-op: text renderers and tests ignore enemy-attack effects;
        // parameters are silenced so the empty body never warns.
        (void)enemyPos;
        (void)playerPos;
        (void)ranged;
        (void)hit;
    }

    /// Show a transient Nova ultimate effect (R22.2).
    ///
    /// Called by Game.cpp immediately after the player fires Nova, so a
    /// graphical renderer can draw a dramatic expanding shockwave / ring centred
    /// on `center` covering `radius` tiles. Like the other effect hooks this is
    /// purely advisory and has a default empty body, so renderers that do not
    /// need it (the ConsoleRenderer) ignore the call without overriding.
    ///
    /// @param center the tile the blast is centred on (the player's position).
    /// @param radius the Chebyshev radius the blast covers, so the renderer can
    ///               size the shockwave to match the real area of effect.
    virtual void showNovaEffect(const Vec2& center, int radius) {
        // Default no-op: text renderers and tests ignore the Nova effect;
        // parameters are silenced so the empty body never warns.
        (void)center;
        (void)radius;
    }

    /// Show a transient melee-attack visual effect on `targetCell` (Fix 4).
    ///
    /// Called by Game.cpp when the player walks into an enemy (melee attack) so
    /// a graphical renderer can draw a brief red slash/X over the target cell as
    /// feedback. Like the other effect hooks this is purely advisory and has a
    /// default empty body, so renderers that do not need it (the
    /// ConsoleRenderer) ignore the call without overriding.
    ///
    /// @param targetCell the tile the melee attack lands on (the enemy's cell).
    virtual void showPlayerMeleeEffect(const Vec2& targetCell) {
        // Default no-op: text renderers and tests ignore the melee effect;
        // parameter is silenced so the empty body never warns.
        (void)targetCell;
    }

    // ---- Audio hooks (procedurally synthesised SFX, raylib-only) ---------
    //
    // The graphical RaylibRenderer synthesises every sound at startup using
    // raylib's Wave/Sound API and an AudioStreamCallback (no .wav files on
    // disk). Game.cpp signals each "moment that should make a sound" through
    // the methods below. The default implementations are empty no-ops so the
    // ConsoleRenderer (and unit tests) compile and run with zero audio code.

    /// Play the "item picked up" chime. Called by Game.cpp once per turn in
    /// which the player walked onto an item tile and collected it.
    /// Default: no-op.
    virtual void showPickupSound() {}

    /// Play the "wave cleared" stinger (ascending major chord). Called by
    /// Game.cpp the moment a turn resolves with every enemy defeated.
    /// Default: no-op.
    virtual void showWaveClearedSound() {}

    /// Play the "game over" sting (descending minor chord). Called by Game.cpp
    /// when the player's HP hits zero, before the Game Over screen draws.
    /// Default: no-op.
    virtual void showGameOverSound() {}

    /// Play the activation sound for a successful ability cast (Dash whoosh,
    /// Nova boom, Shield shimmer, Blink zap). Called by Game.cpp once per turn
    /// in which an ability fired. ConsoleRenderer's default no-op ignores it.
    /// Note: Nova's sound is also covered by the Nova-specific path; calling
    /// both is harmless (they are different timbres playing simultaneously).
    /// @param kind which ability fired this turn.
    virtual void showAbilitySound(AbilityKind kind) {
        (void)kind; // Silence the unused parameter warning for the default.
    }

    /// Toggle the boss-fight background track. Called by Game.cpp every time a
    /// new wave begins (true on boss waves where waveNumber % 5 == 0, false on
    /// every other wave) and again when the run ends. Concrete renderers that
    /// support streaming music (RaylibRenderer) load an OGG track from
    /// assets/boss_theme.ogg the first time they are constructed and start /
    /// stop / resume it from this hook; ConsoleRenderer's default no-op
    /// ignores the call.
    /// @param active true to start (or resume) the boss theme; false to stop.
    virtual void setBossMusicActive(bool active) {
        (void)active; // Silence the unused parameter warning for the default.
    }

    /// Toggle the normal-wave background track (the high-energy "action"
    /// theme that plays during every NON-boss wave). Mirrors
    /// setBossMusicActive — Game.cpp calls one or the other depending on
    /// whether the active wave is a boss wave or a normal wave, never both.
    /// Concrete renderers that support streaming music (RaylibRenderer) load
    /// the track from assets/normal_theme.ogg at construction; the default
    /// no-op silently ignores the call.
    /// @param active true to start (or resume) the normal-wave theme; false
    ///               to stop it.
    virtual void setNormalMusicActive(bool active) {
        (void)active; // Silence the unused parameter warning for the default.
    }

    /// Set the MASTER music volume scalar applied to every playing track.
    ///
    /// The Settings menu (Feature 4) pushes the player-chosen volume here as
    /// a value in [0.0, 1.0]. Concrete renderers that play music (currently
    /// RaylibRenderer) cache the multiplier and re-apply it to every track
    /// (boss / normal / ambient) so a change takes effect immediately.
    /// ConsoleRenderer and unit-test stubs ignore the call via this default
    /// no-op so they need not link against any audio API.
    ///
    /// @param volume the master volume in [0.0, 1.0]; values outside that
    ///               range are clamped by the implementation.
    virtual void setMasterMusicVolume(float volume) {
        (void)volume; // Silence the unused parameter warning for the default.
    }

    /// Block until the player presses ANY recognisable key, then return.
    /// Used by the game-over and "loading failed" screens where the only
    /// thing the player needs to do is acknowledge the message and continue —
    /// pollInput is unsuitable for those screens because it interprets keys
    /// like F (fire prompt) or 1-4 (ability prompts) as game commands and
    /// would trap the user inside an unrelated direction-prompt loop.
    /// The default implementation forwards to pollInput so the simpler
    /// renderers (ConsoleRenderer) keep working unchanged; RaylibRenderer
    /// overrides this with a tighter loop that only listens for SPACE /
    /// ENTER / Q / ESC and ignores everything else.
    virtual void waitForAnyKey() {
        (void)pollInput();
    }
};

} // namespace dga
