// =============================================================================
// render/RaylibRenderer.h
//
// Purpose:
//   RaylibRenderer is the OPTIONAL graphical implementation of IRenderer that
//   uses the raylib library (R28.4, R4.5). It satisfies the same interface the
//   ConsoleRenderer does, but instead of writing ASCII to a terminal it opens a
//   1280 x 720 window and draws coloured rectangles, glyphs, HP / charge bars,
//   and a HUD panel (R29.1). The GAME CORE never knows raylib exists: it only
//   ever talks to an `IRenderer&`, so simply linking against this implementation
//   instead of ConsoleRenderer flips the renderer with zero changes elsewhere
//   (R8.2).
//
//   This file is compiled in only when the build is configured with raylib
//   available (the Makefile / CMake passes `-DDGA_WITH_RAYLIB`). When that flag
//   is absent the project still builds and runs perfectly with ConsoleRenderer
//   alone, so the renderer requirement is met whether or not raylib is present
//   on the build machine (R28.5).
//
// The "blocking pollInput" puzzle:
//   IRenderer's contract is that pollInput() BLOCKS until the player presses a
//   meaningful key (R28.6). Raylib, however, is a frame-driven library: its
//   IsKeyPressed / IsKeyDown queries only update if BeginDrawing / EndDrawing is
//   called every frame (which is what swaps the back buffer and pumps the OS
//   message queue). To bridge the two models without redefining the interface,
//   RaylibRenderer caches every piece of "what is currently on screen" in
//   member fields:
//
//     * the game state pointers from the last drawFrame call,
//     * the option list and selected index from the last drawMenu call,
//     * the accumulated message strings from one or more drawMessage calls,
//
//   and pollInput's inner loop simply RE-RENDERS that cached composition each
//   frame while it polls IsKeyPressed. The cache also makes "stacked" UI work:
//   in the main menu Game.cpp issues drawMessage(title), drawMenu(opts),
//   drawMessage(footer) in sequence and then pollInput - we want all three
//   pieces visible at once, which is exactly what the additive cache produces.
//   A `pendingReset_` flag is set when pollInput returns, so the FIRST draw
//   call after a poll clears the cache and the next composition starts fresh.
//
// Layer:
//   render. Like ConsoleRenderer, this file is only ever pulled in by main.cpp
//   (and only when DGA_WITH_RAYLIB is defined). No logic-layer header includes
//   it (R8.2).
// =============================================================================
#pragma once

#include <string> // std::string - cached message overlay lines.
#include <vector> // std::vector - cached menu options and overlay buffer.

// Raylib audio types (Sound, AudioStream) cross the public interface of this
// renderer, so this header — which is itself only pulled into the build when
// DGA_WITH_RAYLIB is defined (see main.cpp) — includes raylib.h directly. That
// keeps the audio fields strongly typed without resorting to a void* PIMPL.
#include "raylib.h"

#include "core/Enums.h"       // AbilityKind - parameter to showAbilitySound override.
#include "core/Vec2.h"        // Vec2 - cached fire-effect cells.
#include "render/IRenderer.h" // IRenderer + the parameter types it forwards.

namespace dga {

// =============================================================================
// RaylibRenderer - raylib graphical renderer (R28.4, R4.5)
// =============================================================================

/// Renders the game in a 1280 x 720 raylib window and reads the keyboard via
/// raylib's IsKeyPressed / IsKeyDown polling (R28.4, R28.6).
///
/// Derives from IRenderer and overrides all four pure virtual methods. Construc-
/// ting an instance opens the window and sets the target frame rate; destroying
/// it closes the window (RAII). Only ONE RaylibRenderer should exist at a time
/// because raylib uses a single global graphics device.
///
/// All raylib API calls are confined to RaylibRenderer.cpp; this header only
/// pulls in IRenderer.h and the standard library, so other translation units do
/// not accidentally depend on raylib.
class RaylibRenderer : public IRenderer {
public:
    // ---- Construction / destruction ---------------------------------------

    /// Open the raylib window and prepare the renderer for use.
    ///
    /// Calls InitWindow(1280, 720, "Dungeon Grid Arena") and SetTargetFPS(60).
    /// All cache fields start empty so the very first drawFrame / drawMenu /
    /// drawMessage call composes a fresh screen rather than overlaying onto
    /// stale state.
    RaylibRenderer();

    /// Close the raylib window and release its OpenGL resources.
    ///
    /// Calls CloseWindow(). Marked override so deleting through an `IRenderer*`
    /// (e.g. a `std::unique_ptr<IRenderer>` in main.cpp) runs THIS destructor
    /// and properly tears down the window (R4.7).
    ~RaylibRenderer() override;

    // Non-copyable: a renderer owns the (single) raylib graphics device, and
    // copying it would mean two objects fighting over CloseWindow on dtor.
    RaylibRenderer(const RaylibRenderer&)            = delete;
    RaylibRenderer& operator=(const RaylibRenderer&) = delete;

    // ---- IRenderer interface overrides ------------------------------------

    /// Render one complete game frame into the window (R28.4, R29.1).
    ///
    /// Layout, all measured in pixels with origin at the top-left of the window:
    ///   * Map area:        x in [0, 960),  y in [0, 576)  - 40 cols x 24 rows
    ///                      at 24 px/cell. Each cell is drawn as a coloured
    ///                      Wall/Floor rectangle, then any item / enemy / the
    ///                      player on it is drawn on top as a single-character
    ///                      glyph in that entity's signature colour.
    ///   * HUD panel:       x in [960, 1280), y in [0, 720) - 320 px wide,
    ///                      full window height. Shows wave / score / turn,
    ///                      an HP bar (green / yellow / red by ratio), ammo,
    ///                      shield status, the charge bar (purple), the list
    ///                      of abilities with cooldown / RDY tags, and the
    ///                      most recent EventLog messages under a header.
    ///   * Controls strip:  x in [0, 960),  y in [576, 720) - sits below the
    ///                      map and prints the keybinding reminder.
    ///
    /// Calling drawFrame DOES output the frame to the screen immediately, AND
    /// caches the parameters so pollInput's wait loop can re-render the same
    /// frame on every subsequent tick until the player presses a key.
    ///
    /// @param state  the authoritative run state (read-only).
    /// @param config the balancing configuration (read-only); supplies the
    ///               event-log display capacity and charge meter max.
    /// @param log    the event log whose recent messages are listed in the HUD
    ///               (read-only).
    void drawFrame(const GameState& state,
                   const Config&    config,
                   const EventLog&  log) override;

    /// Block until the player presses a recognized key, then return the
    /// corresponding InputCommand (R28.4, R28.6).
    ///
    /// Each iteration of the wait loop:
    ///   1. Calls renderCurrentScreen() so the cached game frame / menu /
    ///      message overlays stay visible while we wait.
    ///   2. Checks IsKeyPressed for every key in the table below.
    ///   3. Returns immediately on the first match. WindowShouldClose() (the
    ///      user clicking the close box or pressing Esc) returns Quit.
    ///
    /// Key table (matches ConsoleRenderer for parity):
    ///   W / Up Arrow     -> Move {0,-1}     A / Left Arrow   -> Move {-1, 0}
    ///   S / Down Arrow   -> Move {0,+1}     D / Right Arrow  -> Move {+1, 0}
    ///   F                -> Fire (a follow-up WASD/arrow chooses direction)
    ///   1 / 2 / 3 / 4    -> UseAbility 0 / 1 / 2 / 3
    ///   Space / Enter    -> Wait
    ///   Q                -> Quit
    ///   Ctrl + S         -> Save (checked BEFORE plain S so Save wins)
    ///
    /// On return, sets pendingReset_ = true so the next draw call clears the
    /// cached overlays and the next composition starts fresh.
    ///
    /// @return the InputCommand corresponding to the player's keypress.
    InputCommand pollInput() override;

    /// Render a vertical selection menu as an overlay (R28.4).
    ///
    /// The menu is drawn centred horizontally near the middle of the window in
    /// a dark-background panel. The entry at `selectedIndex` is prefixed with
    /// "> " and drawn in a slightly brighter colour; other entries are prefixed
    /// with "  ". drawMenu does NOT clear the message overlay, so a sequence
    /// of drawMessage(title) - drawMenu(opts) - drawMessage(footer) issued by
    /// Game.cpp's showMainMenu produces all three pieces stacked on screen.
    ///
    /// @param options       the menu items to display (rendered in order).
    /// @param selectedIndex the 0-based index of the highlighted item; values
    ///                       outside [0, options.size()) simply highlight
    ///                       nothing (no crash).
    void drawMenu(const std::vector<std::string>& options,
                  int selectedIndex) override;

    /// Display a message overlay on top of the current screen (R28.4).
    ///
    /// drawMessage APPENDS to the cached message list: calling it twice in a
    /// row stacks both messages on screen. Each '\n' inside `message` is
    /// honoured and split onto its own visual line. The message panel is drawn
    /// centred at the top of the window so that menus drawn beneath remain
    /// fully visible (used by showMainMenu's title + menu + footer pattern).
    ///
    /// @param message the text to add to the overlay; embedded newlines are
    ///                rendered as line breaks.
    void drawMessage(const std::string& message) override;

    /// Stash the cells visited by a Fire projectile this turn so the next few
    /// rendered frames draw a transient yellow/orange tracer line through
    /// `trailCells` and a brighter flash on `impactCell` (R16). The effect is
    /// purely visual: it does not affect game state.
    ///
    /// Called by Game.cpp once per Fire turn after the CombatSystem fills the
    /// cells in. The renderer copies the data and starts a frame counter; each
    /// renderGameFrame call draws the trail + impact and decrements the counter
    /// until it expires (~30 frames at 60 fps, roughly half a second).
    ///
    /// @param trailCells the cells the projectile passed through, in order;
    ///                   the first element is the player's own cell so the
    ///                   draw code can skip it to avoid overdrawing the glyph.
    /// @param impactCell the cell the projectile stopped on (last element of
    ///                   trailCells); duplicated for the impact-flash draw.
    /// @param hit        true when the projectile struck an enemy; false for a
    ///                   wall / out-of-bounds / blocked-LOS / range miss. Used
    ///                   to colour the impact flash differently for the two.
    void showFireEffect(const std::vector<Vec2>& trailCells,
                        const Vec2& impactCell,
                        bool hit) override;

    /// Cache one enemy attack on the player so the next few rendered frames
    /// draw a visual cue for it (R14): a coloured beam from `enemyPos` to
    /// `playerPos` for a ranged attacker, or a red slash flash on `playerPos`
    /// for a melee attacker. The effect is purely visual and does not touch
    /// game state.
    ///
    /// Called by Game.cpp once per enemy attack after a turn resolves. Because
    /// several enemies can attack in the same turn, this APPENDS to a small
    /// list of active effects (rather than overwriting a single slot) so every
    /// simultaneous attack is shown; each entry carries its own frame counter
    /// and is dropped from the list once it expires.
    ///
    /// @param enemyPos  the tile the attacking enemy struck from (beam origin).
    /// @param playerPos the tile the player occupies (beam target / flash cell).
    /// @param ranged    true for a distance attack (beam), false for melee
    ///                  (slash flash on the player's tile).
    /// @param hit       true when the attack dealt damage; false for a ranged
    ///                  MISS (Feature 3). A hit draws a bright red beam, a miss
    ///                  a dim orange beam, so the player can read the outcome.
    void showEnemyAttackEffect(const Vec2& enemyPos,
                               const Vec2& playerPos,
                               bool ranged,
                               bool hit) override;

    /// Stash a Nova ultimate blast so the next ~half-second of rendered frames
    /// draw a dramatic expanding shockwave centred on `center` covering
    /// `radius` tiles (R22.2). The effect is purely visual and does not touch
    /// game state.
    ///
    /// Called by Game.cpp once when the player fires Nova. The renderer records
    /// the centre + radius and starts a frame countdown; renderGameFrame draws
    /// the growing concentric rings, a translucent blast disc, and electric arc
    /// lines while the countdown is positive, decrementing it each frame until
    /// it expires.
    ///
    /// @param center the tile the blast is centred on (the player's position).
    /// @param radius the Chebyshev radius the blast covers (in tiles).
    void showNovaEffect(const Vec2& center, int radius) override;

    /// Cache a player melee-attack target so the next ~20 rendered frames draw a
    /// bright red "X" slash over the target cell, giving unmistakable feedback
    /// that the melee hit landed (Fix 4). Purely visual; no game state is
    /// touched.
    ///
    /// @param targetCell the tile the player's melee attack landed on.
    void showPlayerMeleeEffect(const Vec2& targetCell) override;

    // ---- Audio cue overrides (procedural SFX, no asset files) ------------
    //
    // Every sound effect is synthesised at construction from sine / square /
    // noise samples written into Wave structs and loaded via LoadSoundFromWave.
    // The ambient music drone is generated by an AudioStreamCallback that fills
    // each requested buffer with two slow detuned sines plus a slow LFO. There
    // are no .wav files on disk; the project remains dependency-free beyond the
    // raylib audio module already linked in.

    /// Play the pickup chime (two ascending sine notes, ~0.2 s).
    void showPickupSound() override;

    /// Play the wave-cleared stinger (ascending C-E-G major chord, ~0.5 s).
    void showWaveClearedSound() override;

    /// Play the game-over sting (descending G-Eb-C minor chord, ~0.8 s).
    void showGameOverSound() override;

    /// Play the activation sound for `kind` (Dash whoosh / Nova boom / Shield
    /// shimmer / Blink zap). Falls through to silence on unrecognised values.
    void showAbilitySound(AbilityKind kind) override;

    /// Start or stop the boss-wave streaming music track loaded from
    /// assets/boss_theme.ogg. Toggling this also fades the procedural ambient
    /// drone in / out so the two layers do not fight one another. If the OGG
    /// failed to load at construction, the call silently does nothing.
    /// @param active true to start (or resume) the boss theme; false to stop.
    void setBossMusicActive(bool active) override;

    /// Start or stop the normal-wave streaming track loaded from
    /// assets/normal_theme.ogg. Mirrors setBossMusicActive but operates on
    /// the non-boss action track. Game.cpp calls exactly one of the two per
    /// wave; the renderer ducks the procedural ambient pad whenever either
    /// streamed track is playing.
    /// @param active true to start (or resume) the normal theme; false to
    ///               stop it.
    void setNormalMusicActive(bool active) override;

    /// Set the master music volume scalar (Feature 4). Cached in
    /// masterMusicVolume_ and immediately re-applied to every playing track
    /// (boss / normal / ambient) so the Settings menu's slider changes are
    /// audible without any restart. Input is clamped to [0.0, 1.0].
    /// @param volume the master volume in [0.0, 1.0].
    void setMasterMusicVolume(float volume) override;

    /// Block until the player presses Space / Enter / Q / ESC. Used by the
    /// Game Over and "load failed" screens — narrowly listens for an "ok,
    /// continue" key without touching the F-fire / 1-4 ability prompt
    /// machinery that pollInput would otherwise trigger when the player
    /// hits a stray game key on the dead-screen overlay.
    void waitForAnyKey() override;

private:
    // ---- Cached composition state ----------------------------------------
    //
    // Set by the public draw* methods, read by renderCurrentScreen() so the
    // pollInput wait loop can keep re-rendering the same scene every frame
    // (see the file header for why this is necessary).

    /// True once pollInput returns, signalling that the FIRST draw call after
    /// a successful poll should clear the cached menu / message overlays
    /// before adding new content. This is what lets a fresh iteration of
    /// showMainMenu start from a blank slate without erasing mid-frame state.
    bool pendingReset_;

    /// True between a drawFrame call and the next reset; tells
    /// renderCurrentScreen() to draw the cached game world (map, HUD, log).
    bool gameFrameActive_;

    /// Non-owning pointer to the GameState that drawFrame was last given.
    /// Only valid (non-null) while gameFrameActive_ is true. Pointer rather
    /// than reference because the renderer must be default-constructible
    /// before a state exists, and the IRenderer contract treats the parameter
    /// as borrowed for the duration of the next pollInput call.
    const GameState* cachedState_;

    /// Non-owning pointer to the Config that drawFrame was last given.
    /// Same lifetime caveat as cachedState_.
    const Config* cachedConfig_;

    /// Non-owning pointer to the EventLog that drawFrame was last given.
    /// Same lifetime caveat as cachedState_.
    const EventLog* cachedLog_;

    /// True between a drawMenu call and the next reset; tells
    /// renderCurrentScreen() to draw the cached menu options on top.
    bool menuActive_;

    /// Snapshot of the option strings passed to the most recent drawMenu call.
    /// We deep-copy because the caller's vector may be a temporary that goes
    /// out of scope before pollInput renders.
    std::vector<std::string> menuOptions_;

    /// 0-based index of the highlighted entry in menuOptions_.
    int menuSelected_;

    /// Accumulated message lines from one or more drawMessage calls since the
    /// last reset. Each call's string is split on '\n' and each piece is
    /// pushed individually so that line breaks render correctly.
    std::vector<std::string> messageLines_;

    /// True only inside pollInput while we are waiting for the SECOND key of
    /// a Fire command (the direction). When set, renderCurrentScreen() draws
    /// a small "Fire direction? (WASD)" prompt over the rest of the frame so
    /// the player knows what input is expected.
    bool firePromptActive_;

    /// True only inside pollInput while we are waiting for the SECOND key of
    /// a Dash ability activation (the direction). Mirrors firePromptActive_:
    /// when set, renderCurrentScreen() draws a small "Dash direction? (WASD)"
    /// overlay so the player understands that ability 1 needs a direction
    /// (the other three abilities activate immediately on their own key).
    bool dashPromptActive_;

    // ---- Transient fire effect state -------------------------------------
    //
    // showFireEffect snapshots the projectile's path here and starts a frame
    // counter; renderGameFrame draws the trail + impact while the counter is
    // positive and decrements it each render. The fields are mutable across
    // const renderGameFrame so the countdown can advance with each rendered
    // frame without forcing renderGameFrame to drop its const qualifier.

    /// Cells the most recent player projectile traveled across, including the
    /// player's starting cell at index 0 and the impact cell as the last
    /// element. Cleared once fireTrailFramesRemaining_ hits zero.
    std::vector<Vec2> fireTrailCells_;

    /// Cell where the most recent projectile stopped (mirrors trail.back()).
    /// Drawn with a brighter flash than the trail itself.
    Vec2 fireImpactCell_;

    /// Frames left to draw the current fire effect. While > 0, renderGameFrame
    /// renders the trail / flash and decrements this counter; at 0 the cached
    /// trail is logically expired and not drawn.
    mutable int fireTrailFramesRemaining_;

    /// True when the most recent shot landed on an enemy (changes the impact
    /// flash colour to red); false for a wall / range / LOS miss (uses orange).
    bool fireTrailHit_;

    // ---- Transient enemy-attack effect state -----------------------------
    //
    // showEnemyAttackEffect appends one ActiveEnemyAttack per attacking enemy;
    // renderGameFrame draws each (beam for ranged, slash flash for melee) and
    // decrements its per-effect counter, dropping expired entries. Stored as a
    // small vector so multiple simultaneous attacks in one turn are all shown.
    // mutable so the const renderGameFrame can age and prune the list each
    // rendered frame without dropping its const qualifier (mirrors the fire
    // effect countdown).

    /// One in-progress enemy-attack visual cue.
    struct ActiveEnemyAttack {
        Vec2 enemyPos;        ///< Beam origin (the attacker's tile).
        Vec2 playerPos;       ///< Beam target / melee flash cell (the hero).
        bool ranged;          ///< true → draw a beam; false → draw a slash flash.
        bool hit;             ///< true → attack connected (bright beam); false →
                              ///< ranged MISS (dim orange beam) (Feature 3).
        int  framesRemaining; ///< Frames left to draw this effect; 0 == expired.
    };

    /// Active enemy-attack effects awaiting (or mid-) display. Appended to by
    /// showEnemyAttackEffect and pruned by renderGameFrame as each ages out.
    mutable std::vector<ActiveEnemyAttack> enemyAttacks_;

    // ---- Transient Nova ultimate effect state (Fix 2) --------------------
    //
    // showNovaEffect records the blast centre + radius here and starts a frame
    // countdown; renderGameFrame draws an expanding shockwave (concentric
    // rings + a translucent blast disc + electric arcs) while the countdown is
    // positive, decrementing it each frame. mutable so the const renderGameFrame
    // can age the countdown each rendered frame without dropping its const.

    /// Tile the most recent Nova blast was centred on (the player's position).
    Vec2 novaCenter_;

    /// Chebyshev radius (in tiles) the most recent Nova blast covered; used to
    /// size the shockwave so it matches the real area of effect.
    int novaRadius_;

    /// Frames left to draw the current Nova effect. While > 0, renderGameFrame
    /// renders the expanding shockwave and decrements this; at 0 it is expired.
    mutable int novaFramesRemaining_;

    // ---- Transient player-melee effect state (Fix 4) ---------------------
    //
    // showPlayerMeleeEffect records the target cell; the next
    // MELEE_EFFECT_FRAMES rendered frames draw a bright red "X" (two crossing
    // diagonal lines) centred on that cell so the player gets visual feedback
    // that their melee attack connected. mutable so the const renderGameFrame
    // can age the countdown each rendered frame.

    /// The cell the most recent player melee attack landed on.
    Vec2 meleeEffectCell_;

    /// Frames left to draw the current melee effect. While > 0, renderGameFrame
    /// renders the red slash and decrements this; at 0 it is expired.
    mutable int meleeEffectFramesRemaining_;

    // ---- Transient on-screen notice state (Fix 1) ------------------------
    //
    // showTransientNotice posts a short message (e.g. "FIRE ON COOLDOWN") that
    // renderGameFrame draws near the bottom-centre of the window for a brief
    // period so the player gets immediate feedback for an action the UI refused
    // (such as pressing F while the Fire cooldown is still ticking). mutable so
    // the const renderGameFrame can age the countdown each rendered frame.

    /// The text of the current transient notice; empty when none is showing.
    std::string transientNotice_;

    /// Frames left to draw transientNotice_. While > 0 the notice is rendered
    /// near the bottom-centre and this is decremented each frame; at 0 the
    /// notice is cleared.
    mutable int transientNoticeFrames_;

    // ---- Hell mode (per-wave dramatic overlay) ---------------------------
    //
    // After the player has spent ~8 seconds inside a wave (real time, not
    // turns) the renderer flips into "Hell On Earth" mode: a pulsing red
    // vignette darkens the borders of the map, a layer of upward-drifting
    // ember particles is drawn over the scene, and a brief "HELL ON EARTH"
    // banner pulses in. The map and gameplay are unchanged; only the visual
    // mood escalates. State resets every time the wave number increases.

    /// Wave number the renderer last saw via drawFrame. Used to detect a wave
    /// transition and reset the hell-mode timer + ember particle pool. -1
    /// flags "no wave seen yet" so the first drawFrame initialises everything.
    mutable int hellModeLastWaveNumber_;

    /// Wall-clock time (seconds, from GetTime()) at which the current wave
    /// started — i.e. the moment renderGameFrame first observed the new wave
    /// number. Used to compute "elapsed seconds in wave" for the hell-mode
    /// trigger and the banner fade-out.
    mutable double hellModeWaveStartTime_;

    /// Cached "is hell mode active right now?" flag. Recomputed every frame
    /// from elapsed time vs HELL_MODE_TRIGGER_SECONDS so it stays in sync if
    /// frames are dropped or the game is paused on a menu.
    mutable bool hellModeActive_;

    /// True once the villain-laugh sting has been played for the current
    /// wave's mode shift. Latched so the laugh fires exactly once at the
    /// trigger boundary; reset to false on every wave change.
    mutable bool hellModeLaughTriggered_;

    /// Single ember particle for the hell-mode overlay: a small upward-
    /// drifting glowing dot rendered over the map. Position is in PIXEL
    /// coordinates relative to the window (not grid coordinates) so the
    /// embers can rise smoothly between cells.
    struct HellEmber {
        float x;       ///< Pixel x position (within the map area).
        float y;       ///< Pixel y position (within the map area).
        float vx;      ///< Horizontal velocity (pixels/frame, ±a tiny drift).
        float vy;      ///< Vertical velocity (pixels/frame, negative = up).
        float life;    ///< Frames left before the ember despawns and respawns.
        float maxLife; ///< Initial life value (used to fade alpha over time).
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };

    /// Pool of active ember particles. Sized once at construction (see the
    /// HELL_EMBER_COUNT constant in the .cpp), then individual particles are
    /// recycled in place when their life hits zero.
    mutable std::vector<HellEmber> hellEmbers_;

    /// True once hellEmbers_ has been initialised. Lets us populate the pool
    /// lazily on the first hell-mode frame instead of paying the allocation
    /// cost on every renderer construction (most main-menu frames never need
    /// it).
    mutable bool hellEmbersInitialised_;

    // ---- Audio state (procedural SFX + ambient music) --------------------
    //
    // Every Sound below is a procedurally-generated effect built at construction
    // by the makeXxxSound helpers in the .cpp file (sine / square / noise plus a
    // tiny envelope) and loaded with LoadSoundFromWave. The ambient music plays
    // via an AudioStream whose callback synthesises two slow detuned sines on
    // the fly, so there are zero audio files in the project.

    /// True only when InitAudioDevice() succeeded at construction. Every audio
    /// call (PlaySound, UnloadSound, ...) is gated on this flag so a system
    /// without a working audio device still runs the game silently rather than
    /// crashing.
    bool audioReady_;

    Sound fireSound_;        ///< Player Fire — short noise burst with descending tone.
    Sound hitSound_;         ///< Player Fire impact on enemy — low thump.
    Sound meleeSound_;       ///< Player melee strike — short noise + low tone.
    Sound enemyHitSound_;    ///< Enemy attack lands on player — deeper thump.
    Sound novaSound_;        ///< Nova ultimate detonation — long dramatic boom.
    Sound waveClearSound_;   ///< Wave cleared — ascending major chord.
    Sound gameOverSound_;    ///< Player died — descending minor chord.
    Sound pickupSound_;      ///< Item pickup — short ascending two-note chime.
    Sound dashSound_;        ///< Dash ability — short swept-noise whoosh.
    Sound shieldSound_;      ///< Shield ability — sustained shimmering chord.
    Sound blinkSound_;       ///< Blink ability — fast freq-sweep teleport zap.
    Sound villainLaughSound_; ///< Player death — deep cackling villain laugh
                              ///< (procedural growl pulsations + lowpass noise).

    /// Ambient music stream: a slow detuned drone fed by audioStreamCallback.
    AudioStream musicStream_;

    /// Boss-wave background track streamed from assets/boss_theme.ogg using
    /// raylib's Music type (which decodes the OGG on its own thread). It is
    /// loaded once at construction and reused across every boss wave; the
    /// per-frame UpdateMusicStream pump keeps the decoder fed. The track loops
    /// automatically because Music.looping is set right after loading.
    Music bossMusic_;

    /// True only when LoadMusicStream succeeded — every PlayMusicStream /
    /// UpdateMusicStream / StopMusicStream call is gated on this flag so a
    /// missing assets/boss_theme.ogg degrades to silence instead of a crash.
    bool bossMusicLoaded_;

    /// True while the boss track is actively playing. Tracked separately from
    /// the Music struct so we can avoid restarting it every frame and so the
    /// per-frame update knows whether to pump UpdateMusicStream at all.
    bool bossMusicPlaying_;

    /// Normal-wave background track (Doom-Eternal-style action theme),
    /// streamed from assets/normal_theme.ogg. Plays during every non-boss
    /// wave; stops on boss waves so bossMusic_ can take over. Same lifetime,
    /// same Music API as bossMusic_.
    Music normalMusic_;

    /// True only when assets/normal_theme.ogg loaded successfully — every
    /// PlayMusicStream / UpdateMusicStream / StopMusicStream call on
    /// normalMusic_ is gated on this flag so a missing file degrades to
    /// silence instead of a crash.
    bool normalMusicLoaded_;

    /// True while the normal-wave track is actively playing. Mirrors
    /// bossMusicPlaying_ for the normal track.
    bool normalMusicPlaying_;

    /// Master music volume scalar (Feature 4) in [0, 1]. Multiplied into
    /// every audio-stream / music volume call so the Settings menu slider
    /// scales the FINAL audible level of all music tracks (boss + normal +
    /// procedural ambient pad). Defaults to 1.0 = full volume so the
    /// renderer behaves identically to before until the player adjusts it.
    float masterMusicVolume_;

    // ---- Private helpers --------------------------------------------------

    /// Clear the cached menu / message overlay if pendingReset_ is set, and
    /// drop the pendingReset_ flag. Called at the top of every public draw
    /// method so the very first draw after a successful poll starts fresh.
    /// gameFrameActive_ and the cached game pointers are NOT cleared here -
    /// drawFrame replaces them explicitly, and the Save -> drawMessage path
    /// in Game.cpp deliberately overlays a message on top of the still-cached
    /// game frame, which is the desired behaviour.
    void resetCompositionIfNeeded();

    /// Render one complete frame of the cached composition (game + overlays).
    ///
    /// Wraps the whole pass in a single BeginDrawing / EndDrawing pair so the
    /// caller never has to interleave raylib state. Order: clear, then game
    /// frame (if active), then menu overlay, then message overlay, then the
    /// fire-direction prompt. Called both directly by the public draw methods
    /// (so output is immediate) and by the pollInput wait loop (so the same
    /// scene stays on screen while we poll keys).
    void renderCurrentScreen();

    /// Draw the game world block: map cells, items, enemies, the player, the
    /// HUD panel on the right, and the controls reminder strip at the bottom.
    /// Assumes BeginDrawing has already been called by renderCurrentScreen().
    /// Reads the cachedState_ / cachedConfig_ / cachedLog_ pointers; if any is
    /// null this call does nothing (defensive - should never happen because
    /// gameFrameActive_ is only set when drawFrame supplies all three).
    void renderGameFrame() const;

    /// Draw the right-side HUD panel: wave / score header, HP bar, ammo,
    /// shield status, charge bar, abilities list, recent-events block.
    /// Assumes BeginDrawing has already been called and that all three cached
    /// pointers are valid.
    void renderHud() const;

    /// Post a short transient notice that renderGameFrame draws near the
    /// bottom-centre of the window for a brief period (Fix 1). Used to give the
    /// player immediate on-screen feedback when the UI refuses an action — most
    /// notably pressing F while the Fire cooldown is still ticking down. Resets
    /// the notice frame counter so the message is shown for its full duration.
    /// @param text the message to display (e.g. "FIRE ON COOLDOWN - 2 turn(s)").
    void showTransientNotice(const std::string& text);

    /// Draw the active transient notice (if any) near the bottom-centre of the
    /// window on a dark panel, then age its frame counter by one. Assumes
    /// BeginDrawing has already been called. A no-op when no notice is active.
    void renderTransientNotice() const;

    /// Draw the active Nova ultimate shockwave (if any) centred on novaCenter_:
    /// several expanding concentric rings (cyan / electric blue / white), a
    /// translucent filled blast disc sized to novaRadius_, and a handful of
    /// jagged electric-arc lines from the centre to the blast edge for a flashy
    /// electric look (Fix 2). Ages the frame counter by one each call. Assumes
    /// BeginDrawing has already been called; a no-op when no effect is active.
    void renderNovaEffect() const;

    /// Update wave-tracking and render the "HELL ON EARTH" overlay on top of
    /// the map area. Called from renderGameFrame after the map / entities are
    /// drawn so the overlay sits on top. Behaviour:
    ///   * Detects a wave change by comparing cachedState_->waveNumber()
    ///     against hellModeLastWaveNumber_ — on change, resets the wave start
    ///     time and clears hellModeActive_.
    ///   * Once HELL_MODE_TRIGGER_SECONDS of real time has elapsed in the
    ///     current wave, sets hellModeActive_ to true and renders:
    ///       - a pulsing red vignette darkening the map borders.
    ///       - an upward drift of glowing ember particles (hellEmbers_).
    ///       - a brief pulsing "HELL ON EARTH" banner across the map.
    /// Lazily initialises hellEmbers_ on first activation. Assumes
    /// BeginDrawing has already been called and gameFrameActive_ is true.
    void renderHellMode() const;

    /// Draw the bottom controls-reminder strip below the map.
    /// Assumes BeginDrawing has already been called.
    void renderControlsStrip() const;

    /// Draw the cached menu (options and selection arrow) centred near the
    /// middle of the window on a dark panel. Assumes BeginDrawing has already
    /// been called.
    void renderMenuOverlay() const;

    /// Draw the cached message lines stacked at the top of the window on a
    /// dark panel. Assumes BeginDrawing has already been called.
    void renderMessageOverlay() const;

    /// Draw the small "Fire direction? (WASD)" prompt at the centre of the
    /// window. Assumes BeginDrawing has already been called and is only ever
    /// invoked when firePromptActive_ is true.
    void renderFirePromptOverlay() const;

    /// Compute the cells a fired projectile would cover travelling from
    /// `origin` one step at a time along `dir`, up to `range` cells, stopping
    /// at the first wall or out-of-bounds cell (Feature 2). The returned list
    /// does NOT include the origin itself: it is the lane the shot would sweep,
    /// in order from nearest to farthest. Used by the fire-aim preview to draw
    /// the four firing lanes and highlight reachable targets. Reads the cached
    /// map (cachedState_->map()); returns an empty vector when no state is
    /// cached or the first step is already blocked.
    /// @param origin the player's tile (the shot's start; excluded from output).
    /// @param dir    the unit firing direction (e.g. {0,-1} for up).
    /// @param range  the maximum number of cells the shot may travel.
    /// @return the lane cells the shot would cover, stopping at a wall/oob.
    std::vector<Vec2> computeFireLane(const Vec2& origin, const Vec2& dir,
                                      int range) const;

    /// Draw the fire-aim preview over the map while waiting for a fire
    /// direction (Feature 2): for each of the four directions trace the lane
    /// with computeFireLane and paint its cells as semi-transparent highlights,
    /// marking the first enemy in each lane in red (it would be hit). Assumes
    /// BeginDrawing has already been called and cachedState_ is non-null.
    void renderFirePreview() const;

    /// Draw the small "Dash direction? (WASD)" prompt at the centre of the
    /// window, modelled on renderFirePromptOverlay. Assumes BeginDrawing has
    /// already been called and is only ever invoked when dashPromptActive_
    /// is true. Distinct from the fire prompt so the player can tell which
    /// command the game is waiting on.
    void renderDashPromptOverlay() const;

    /// Wait inside an inner frame loop until the player presses a WASD or
    /// arrow key, then return the matching Fire InputCommand. Sets
    /// firePromptActive_ for the duration so the prompt overlay is drawn.
    /// If the player presses R the `cancelled` flag is set and the caller
    /// should loop back without consuming a turn. If the player closes the
    /// window while the prompt is up the function returns a Quit InputCommand.
    /// @param cancelled output flag set to true when the player aborts with R.
    /// @return a Fire InputCommand carrying the chosen direction, or Quit if
    ///         the window was closed while waiting.
    InputCommand waitForFireDirection(bool& cancelled);

    /// Wait inside an inner frame loop until the player presses a WASD or
    /// arrow key after the Dash key (1) was pressed, then return the matching
    /// UseAbility InputCommand at index 0 (Dash) carrying the chosen
    /// direction. Mirrors waitForFireDirection (same overall structure, same
    /// "close-window returns Quit" guarantee, same R cancel). On cancel the
    /// `cancelled` output flag is set so the caller can loop back without
    /// consuming a turn.
    /// @param cancelled output flag set to true when the player aborts with R.
    /// @return a UseAbility InputCommand for Dash with the chosen direction,
    ///         or Quit if the window was closed while waiting.
    InputCommand waitForDashDirection(bool& cancelled);
};

} // namespace dga
